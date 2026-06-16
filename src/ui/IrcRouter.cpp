#include "ui/IrcRouter.h"

#include <algorithm>

#include <QApplication>
#include <QLatin1Char>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStatusBar>
#include <QString>
#include <QStringList>

#include "irc/IrcConnection.h"
#include "ui/BanListDialog.h"
#include "ui/DccManager.h"
#include "ui/MainWindow.h"
#include "ui/ScriptBridge.h"

namespace maxchat::ui {

namespace {

// Pure helpers the IRC handlers use. Duplicated verbatim from MainWindow.cpp's
// anonymous namespace (internal linkage, so no ODR/link clash) to keep this a
// zero-touch relocation of the wiring — `nickWithoutPrefix` in particular has
// 10+ callers still in MainWindow.cpp. TODO: lift the shared cluster into one
// `IrcRoutingHelpers.h` and dedupe.
QString nickWithoutPrefix(QString nick) {
    static const QString prefixes = QStringLiteral("~&@%+");
    while (!nick.isEmpty() && prefixes.contains(nick.front())) {
        nick.remove(0, 1);
    }
    return nick;
}

bool memberMatchesNick(const QString& memberText, const QString& nick) {
    return nickWithoutPrefix(memberText).compare(nick, Qt::CaseInsensitive) == 0;
}

bool removeMember(QListWidget* memberList, const QString& nick) {
    if (memberList == nullptr || nick.trimmed().isEmpty()) {
        return false;
    }
    bool removed = false;
    for (int row = memberList->count() - 1; row >= 0; --row) {
        QListWidgetItem* item = memberList->item(row);
        if (item != nullptr && memberMatchesNick(item->text(), nick)) {
            delete memberList->takeItem(row);
            removed = true;
        }
    }
    return removed;
}

void addMember(QListWidget* memberList, const QString& nick) {
    const QString trimmed = nick.trimmed();
    if (memberList == nullptr || trimmed.isEmpty()) {
        return;
    }
    for (int row = 0; row < memberList->count(); ++row) {
        const QListWidgetItem* item = memberList->item(row);
        if (item != nullptr && memberMatchesNick(item->text(), trimmed)) {
            return;
        }
    }
    memberList->addItem(trimmed);
    memberList->sortItems(Qt::AscendingOrder);
}

bool isChannelTarget(const QString& target) {
    return target.startsWith(QLatin1Char('#')) || target.startsWith(QLatin1Char('&'));
}

bool isLikelyServerNotice(const QString& sender, const QString& target, const QString& ownNick) {
    const QString cleanTarget = target.trimmed();
    if (isChannelTarget(cleanTarget)) {
        return false;
    }
    if (cleanTarget.isEmpty() || cleanTarget == QStringLiteral("*")) {
        return true;
    }
    if (sender.trimmed().isEmpty()) {
        return true;
    }
    if (sender.contains(QLatin1Char('.'))) {
        return true;
    }
    Q_UNUSED(ownNick);
    return false;
}

QString normalizeIgnoreMask(QString mask) {
    mask = mask.trimmed();
    if (mask.isEmpty()) {
        return {};
    }
    return mask.contains(QLatin1Char('!')) || mask.contains(QLatin1Char('@'))
               ? mask
               : QStringLiteral("%1!*@*").arg(mask);
}

bool containsCaseInsensitive(const QStringList& values, const QString& needle) {
    return std::any_of(values.cbegin(), values.cend(), [&needle](const QString& value) {
        return value.compare(needle, Qt::CaseInsensitive) == 0;
    });
}

} // namespace

void IrcRouter::wire(const QString& network, maxchat::irc::IrcConnection* irc) {
    if (irc == nullptr) {
        return;
    }
    const QString signalNetwork = network.trimmed();
    const auto runInContext = [this, signalNetwork](const auto& body) {
        if (signalNetwork.isEmpty()) {
            body();
        } else {
            m_window.withNetworkContext(signalNetwork, body);
        }
    };

    connect(irc, &maxchat::irc::IrcConnection::connected, this, [this, runInContext]() {
        runInContext([&]() {
            m_window.appendSystemLineToTarget(
                QStringLiteral("server"),
                QStringLiteral("! Socket connected. Registering with IRC server..."));
            m_window.showConnectionStatus(tr("Registering with IRC server..."));
        });
    });
    connect(irc, &maxchat::irc::IrcConnection::registered, this, [this, runInContext]() {
        runInContext([&]() {
            m_window.m_registered = true;
            m_window.m_registeredByNetwork.insert(m_window.activeNetworkName(), true);
            m_window.m_connectionUptime.start();
            m_window.m_connectionUptimeRunning = true;
            m_window.m_initialConnectAttempts = 0;
            m_window.m_initialConnectAttemptsByNetwork.insert(m_window.activeNetworkName(), 0);
            m_window.m_connectionUptimeStartMsByNetwork.insert(m_window.activeNetworkName(),
                                                      QDateTime::currentMSecsSinceEpoch());
            if (m_window.m_currentTarget.isEmpty() && !m_window.m_connectionPlan.autojoin.isEmpty()) {
                m_window.m_currentTarget = m_window.m_connectionPlan.autojoin.first();
            }
            const QString nick =
                m_window.connection().nick().isEmpty() ? m_window.m_connectionPlan.nick : m_window.connection().nick();
            m_window.appendSystemLineToTarget(
                QStringLiteral("server"),
                QStringLiteral("! Connected to %1 as %2.").arg(m_window.m_connectionPlan.networkName, nick));
            m_window.statusBar()->showMessage(
                QStringLiteral("Connected to %1 as %2").arg(m_window.m_connectionPlan.networkName, nick));
            m_window.updateNickLabel(); // your nick is known now — show it by the input box
            // First connect only: widen the nick column to fit your nick (never
            // shrinks a column you've already widened; runs once). Python parity.
            if (m_window.m_alignNicks && !nick.isEmpty() &&
                !m_window.m_settings.loadWithDefaults()
                     .value(QStringLiteral("nick_width_autoset"), false)
                     .toBool()) {
                static_cast<void>(
                    m_window.m_settings.setValue(QStringLiteral("nick_width_autoset"), true));
                const int want = static_cast<int>(nick.size()) + 2; // < > around it
                if (want > m_window.m_nickColumnWidth) {
                    m_window.setNickColumnWidth(want, true);
                }
            }
            for (const QString& performLine : m_window.m_connectionPlan.perform) {
                const QString trimmedPerform = performLine.trimmed();
                if (trimmedPerform.isEmpty()) {
                    continue;
                }
                if (trimmedPerform.startsWith(QLatin1Char('/'))) {
                    m_window.sendCommandOrMessage(trimmedPerform);
                } else {
                    m_window.connection().sendRaw(trimmedPerform);
                }
            }
            m_window.showConnectionStatus(m_window.m_currentTarget.isEmpty()
                                   ? QStringLiteral("Connected to %1 as %2")
                                         .arg(m_window.m_connectionPlan.networkName, nick)
                                   : QStringLiteral("%1 - %2 as %3")
                                         .arg(m_window.m_connectionPlan.networkName, m_window.m_currentTarget, nick));
            m_window.rebuildNetworkTree();
            m_window.updateChannelModeButton();
            m_window.m_haveFriendSnapshot = false;
            m_window.pollFriends();
            if (!m_window.m_friendNicks.isEmpty()) {
                m_window.m_friendPollTimer.start();
            }
        });
    });
    connect(irc, &maxchat::irc::IrcConnection::disconnected, this,
            [this, signalNetwork](const QString& reason) {
                if (signalNetwork.isEmpty()) {
                    m_window.handleDisconnected(reason);
                } else {
                    m_window.handleDisconnected(signalNetwork, reason);
                }
            });
    connect(irc, &maxchat::irc::IrcConnection::errorOccurred, this,
            [this, runInContext](const QString& message) {
                runInContext([&]() {
                    m_window.appendSystemLineToTarget(QStringLiteral("server"),
                                             QStringLiteral("! Error: %1").arg(message));
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::systemText, this,
            [this, runInContext](const QString& line) {
                runInContext([&]() {
                    m_window.appendSystemLineToTarget(QStringLiteral("server"),
                                             QStringLiteral("! %1").arg(line));
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::rawLine, this,
            [this, signalNetwork](const QString& direction, const QString& line) {
                const QString rawLine =
                    signalNetwork.isEmpty()
                        ? QStringLiteral("%1 %2").arg(direction, line)
                        : QStringLiteral("[%1] %2 %3").arg(signalNetwork, direction, line);
                m_window.appendRawLogLine(rawLine);
            });
    connect(irc, &maxchat::irc::IrcConnection::replyText, this,
            [this, signalNetwork](const QString& line) {
                if (signalNetwork.isEmpty()) {
                    m_window.appendReplyLine(line);
                } else {
                    m_window.appendReplyLineForNetwork(signalNetwork, line);
                }
            });
    connect(
        irc, &maxchat::irc::IrcConnection::lagMeasured, this, [this, runInContext](double seconds) {
            runInContext([&]() {
                m_window.appendSystemLine(tr("* Lag: %1 ms").arg(qRound64(seconds * 1000.0)));
            });
        });
    connect(irc, &maxchat::irc::IrcConnection::listReply, this,
            [this, runInContext](const QString& channel, int users, const QString& topic) {
                if (m_window.m_channelListDialog != nullptr) {
                    // Buffer directly, WITHOUT runInContext: its epilogue
                    // re-renders the chat view and rebuilds the tree per call,
                    // which at 20k /list replies freezes the app for minutes.
                    // The pending buffer is dialog-global, not per-network.
                    m_window.m_pendingChannels.append({channel, users, topic});
                    if (!m_window.m_channelDrainTimer.isActive()) {
                        m_window.m_channelDrainTimer.start();
                    }
                    return;
                }
                runInContext([&]() {
                    m_window.appendSystemLineToTarget(
                        QStringLiteral("server"),
                        topic.trimmed().isEmpty()
                            ? QStringLiteral("[list] %1 (%2 users)").arg(channel).arg(users)
                            : QStringLiteral("[list] %1 (%2 users): %3")
                                  .arg(channel)
                                  .arg(users)
                                  .arg(topic));
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::listEnd, this, [this, runInContext]() {
        runInContext([&]() {
            m_window.m_channelDrainTimer.stop();
            if (m_window.m_channelListDialog != nullptr) {
                // Flush any remaining buffered entries, then mark complete.
                if (!m_window.m_pendingChannels.isEmpty()) {
                    m_window.m_channelListDialog->addChannelsBulk(m_window.m_pendingChannels);
                    m_window.m_pendingChannels.clear();
                }
                m_window.m_channelListDialog->setComplete(true);
                return;
            }
            m_window.m_pendingChannels.clear();
            m_window.appendSystemLineToTarget(QStringLiteral("server"),
                                     QStringLiteral("[list] End of /LIST."));
        });
    });
    connect(irc, &maxchat::irc::IrcConnection::messageReceived, this,
            [this, runInContext, signalNetwork](const QString& sender, const QString& target,
                                                const QString& text, bool notice, bool action) {
                runInContext([&]() {
                    const QString nick =
                        m_window.connection().nick().isEmpty() ? m_window.m_connectionPlan.nick : m_window.connection().nick();
                    if (m_window.shouldDropForFlood(sender, nick)) {
                        return;
                    }
                    const bool serverNotice = notice && isLikelyServerNotice(sender, target, nick);
                    const bool privateToMe =
                        !serverNotice && target.compare(nick, Qt::CaseInsensitive) == 0;
                    const QString conversation =
                        serverNotice ? QStringLiteral("server") : (privateToMe ? sender : target);
                    const bool activeConversation =
                        conversation.compare(m_window.m_currentTarget, Qt::CaseInsensitive) == 0;
                    if (privateToMe) {
                        m_window.rememberTarget(conversation);
                    }
                    if (privateToMe && !activeConversation) {
                        const maxchat::core::ChatBufferId queryBuffer =
                            m_window.bufferIdForTarget(conversation);
                        Q_UNUSED(queryBuffer);
                        m_window.rebuildNetworkTree();
                    } else if (activeConversation) {
                        m_window.renderActiveBufferMetadata();
                    }

                    const bool highlight = sender.compare(nick, Qt::CaseInsensitive) != 0 &&
                                           m_window.textHighlightsMe(text, nick) &&
                                           !m_window.isMutedChannel(conversation);
                    if (action) {
                        m_window.appendSystemLineToTarget(conversation,
                                                 QStringLiteral("* %1 %2").arg(sender, text), true,
                                                 false, highlight, false);
                    } else if (notice) {
                        m_window.appendSystemLineToTarget(conversation,
                                                 QStringLiteral("-%1- %2").arg(sender, text), true,
                                                 false, highlight, false);
                    } else {
                        m_window.appendSystemLineToTarget(conversation,
                                                 QStringLiteral("<%1> %2").arg(sender, text), true,
                                                 false, highlight);
                    }

                    // Notify on PMs and highlights
                    if (!m_window.isActiveWindow()) {
                        if (privateToMe && m_window.m_notifyPm) {
                            const QString stripped = sender;  // stripFormatting TBD
                            QString title = QStringLiteral("Private message \u00b7 %1").arg(stripped);
                            m_window.notify(title, text, m_window.activeNetworkName(), conversation);
                        } else if (highlight && m_window.m_notifyHighlight) {
                            QString title = QStringLiteral("%1 mentioned you").arg(sender);
                            m_window.notify(title, text, m_window.activeNetworkName(), conversation);
                        }
                        if ((privateToMe || highlight) && m_window.m_beepHighlight &&
                            !m_window.m_settings.loadWithDefaults()
                                 .value(QStringLiteral("dnd"), false)
                                 .toBool()) {
                            QApplication::beep(); // DND promises to silence the beep too
                        }
                    }

                    if (!action) {
                        m_window.m_scripts->dispatch(notice ? QStringLiteral("on_notice")
                                               : QStringLiteral("on_message"),
                                        signalNetwork, {signalNetwork, target, sender, text});
                    }
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::nickChanged, this,
            [this, runInContext, signalNetwork](const QString& oldNick, const QString& newNick) {
                runInContext([&]() {
                    m_window.m_scripts->dispatch(QStringLiteral("on_nick"), signalNetwork,
                                    {signalNetwork, oldNick, newNick});
                    m_window.updateNickLabel(); // refresh in case it was your own nick that changed
                    const QStringList affectedChannels = m_window.channelTargetsContainingMember(oldNick);
                    m_window.renameMemberInChannelBuffers(oldNick, newNick);
                    if (m_window.m_memberList != nullptr) {
                        if (removeMember(m_window.m_memberList, oldNick)) {
                            addMember(m_window.m_memberList, newNick);
                            m_window.memberListChanged();
                        }
                    }
                    const QString line =
                        QStringLiteral("* %1 is now known as %2").arg(oldNick, newNick);
                    if (affectedChannels.isEmpty()) {
                        m_window.appendSystemLineToTarget(QStringLiteral("server"), line);
                        return;
                    }
                    for (const QString& channel : affectedChannels) {
                        m_window.appendSystemLineToTarget(channel, line);
                    }
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::userJoined, this,
            [this, runInContext, signalNetwork](const QString& channel, const QString& nick) {
                runInContext([&]() {
                    m_window.m_scripts->dispatch(QStringLiteral("on_join"), signalNetwork,
                                    {signalNetwork, channel, nick});
                    const QString currentNick =
                        m_window.connection().nick().isEmpty() ? m_window.m_connectionPlan.nick : m_window.connection().nick();
                    const bool ownJoin = nick.compare(currentNick, Qt::CaseInsensitive) == 0;
                    const maxchat::core::ChatBufferId channelBuffer = m_window.bufferIdForTarget(channel);
                    const bool memberAdded = m_window.m_chatBuffers.addMember(channelBuffer, nick);
                    Q_UNUSED(memberAdded);
                    if (ownJoin || m_window.m_currentTarget.isEmpty()) {
                        m_window.activateBufferTarget(channel);
                        const bool joinedSet = m_window.m_chatBuffers.setJoined(channelBuffer, true);
                        Q_UNUSED(joinedSet);
                        m_window.showConnectionStatus(
                            QStringLiteral("%1 - %2").arg(m_window.m_connectionPlan.networkName, channel));
                        m_window.rebuildNetworkTree();
                        m_window.updateChannelModeButton();
                        if (m_window.connection().isConnected()) {
                            m_window.connection().sendRaw(QStringLiteral("MODE %1").arg(channel));
                        }
                    }
                    if (channel.compare(m_window.m_currentTarget, Qt::CaseInsensitive) == 0) {
                        addMember(m_window.m_memberList, nick);
                        m_window.memberListChanged();
                    }
                    if (!m_window.m_hideJoinPart || ownJoin) {
                        m_window.appendSystemLineToTarget(
                            channel, QStringLiteral("* %1 joined %2").arg(nick, channel));
                    }
                });
            });
    connect(
        irc, &maxchat::irc::IrcConnection::userParted, this,
        [this, runInContext, signalNetwork](const QString& channel, const QString& nick,
                                            const QString& reason) {
            runInContext([&]() {
                m_window.m_scripts->dispatch(QStringLiteral("on_part"), signalNetwork,
                                {signalNetwork, channel, nick});
                const QString currentNick =
                    m_window.connection().nick().isEmpty() ? m_window.m_connectionPlan.nick : m_window.connection().nick();
                const bool ownPart = nick.compare(currentNick, Qt::CaseInsensitive) == 0;
                const maxchat::core::ChatBufferId channelBuffer = m_window.bufferIdForTarget(channel);
                const bool memberRemoved = m_window.m_chatBuffers.removeMember(channelBuffer, nick);
                Q_UNUSED(memberRemoved);
                if (channel.compare(m_window.m_currentTarget, Qt::CaseInsensitive) == 0) {
                    removeMember(m_window.m_memberList, nick);
                    m_window.memberListChanged();
                }
                if (!m_window.m_hideJoinPart || ownPart) {
                    m_window.appendSystemLineToTarget(
                        channel,
                        reason.isEmpty()
                            ? QStringLiteral("* %1 left %2").arg(nick, channel)
                            : QStringLiteral("* %1 left %2 (%3)").arg(nick, channel, reason));
                }
                if (ownPart && channel.compare(m_window.m_currentTarget, Qt::CaseInsensitive) == 0) {
                    const bool joinedSet = m_window.m_chatBuffers.setJoined(channelBuffer, false);
                    Q_UNUSED(joinedSet);
                    m_window.forgetTarget(channel);
                    m_window.activateBufferTarget({});
                    m_window.m_pendingNamesByChannel.remove(QStringLiteral("%1/%2").arg(
                        m_window.activeNetworkName().toCaseFolded(), channel.toCaseFolded()));
                    if (m_window.m_memberList != nullptr) {
                        m_window.m_memberList->clear();
                    }
                    m_window.showConnectionStatus(
                        QStringLiteral("%1 - Connected").arg(m_window.m_connectionPlan.networkName));
                    m_window.rebuildNetworkTree();
                    m_window.updateChannelModeButton();
                }
            });
        });
    connect(irc, &maxchat::irc::IrcConnection::userQuit, this,
            [this, runInContext, signalNetwork](const QString& nick, const QString& reason) {
                runInContext([&]() {
                    m_window.m_scripts->dispatch(QStringLiteral("on_quit"), signalNetwork, {signalNetwork, nick});
                    const QStringList affectedChannels = m_window.channelTargetsContainingMember(nick);
                    m_window.removeMemberFromChannelBuffers(nick);
                    removeMember(m_window.m_memberList, nick);
                    m_window.memberListChanged();
                    if (!m_window.m_hideJoinPart) {
                        const QString line =
                            reason.isEmpty() ? QStringLiteral("* %1 quit").arg(nick)
                                             : QStringLiteral("* %1 quit (%2)").arg(nick, reason);
                        if (affectedChannels.isEmpty()) {
                            m_window.appendSystemLineToTarget(QStringLiteral("server"), line);
                            return;
                        }
                        for (const QString& channel : affectedChannels) {
                            m_window.appendSystemLineToTarget(channel, line);
                        }
                    }
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::topicChanged, this,
            [this, runInContext](const QString& channel, const QString& topic) {
                runInContext([&]() {
                    const maxchat::core::ChatBufferId channelBuffer = m_window.bufferIdForTarget(channel);
                    const QString previousTopic = m_window.m_chatBuffers.snapshot(channelBuffer).topic;
                    const bool topicSet = m_window.m_chatBuffers.setTopic(channelBuffer, topic);
                    Q_UNUSED(topicSet);
                    const bool active = channel.compare(m_window.m_currentTarget, Qt::CaseInsensitive) == 0;
                    if (active) {
                        m_window.renderActiveBufferMetadata();
                    }
                    if (previousTopic == topic) {
                        return;
                    }
                    m_window.appendSystemLineToTarget(
                        channel, topic.isEmpty()
                                     ? QStringLiteral("! %1 has no topic.").arg(channel)
                                     : QStringLiteral("! Topic for %1: %2").arg(channel, topic));
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::namesReceived, this,
            [this, runInContext](const QString& channel, const QStringList& names) {
                runInContext([&]() {
                    const QString key = QStringLiteral("%1/%2").arg(
                        m_window.activeNetworkName().toCaseFolded(), channel.toCaseFolded());
                    QStringList& pendingNames = m_window.m_pendingNamesByChannel[key];
                    pendingNames.append(names);
                    const bool membersSet =
                        m_window.m_chatBuffers.setMembers(m_window.bufferIdForTarget(channel), pendingNames);
                    Q_UNUSED(membersSet);
                    if (channel.compare(m_window.m_currentTarget, Qt::CaseInsensitive) != 0) {
                        return;
                    }
                    m_window.renderActiveBufferMetadata();
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::namesEnd, this,
            [this, runInContext](const QString& channel) {
                runInContext([&]() {
                    m_window.m_pendingNamesByChannel.remove(QStringLiteral("%1/%2").arg(
                        m_window.activeNetworkName().toCaseFolded(), channel.toCaseFolded()));
                    if (channel.compare(m_window.m_currentTarget, Qt::CaseInsensitive) == 0) {
                        m_window.renderActiveBufferMetadata();
                    }
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::dccRequest, this,
            [this, runInContext](const QString& sender, const QString& args) {
                runInContext([&]() {
                    if (!m_window.m_dccEnabled) {
                        return;
                    }
                    m_window.m_dccManager->handleIncoming(sender, args);
                });
            });
    connect(irc, &maxchat::irc::IrcConnection::ctcpSound, this,
            [this, signalNetwork](const QString& sender, const QString& target, const QString& file,
                                  const QString& text) {
                m_window.handleCtcpSound(signalNetwork, sender, target, file, text);
            });
    connect(irc, &maxchat::irc::IrcConnection::mcDataReceived, this,
            [this, signalNetwork](const QString& sender, const QString& target,
                                  const QString& service, const QString& verb,
                                  const QString& payload, const bool notice) {
                if (ScriptBridge::scriptingAvailable()) {
                    m_window.m_scripts->dispatch(QStringLiteral("on_mc_data"), signalNetwork,
                                    {signalNetwork, target, sender, service, verb, payload, notice});
                }
            });
    connect(irc, &maxchat::irc::IrcConnection::invited, this,
            [this, runInContext](const QString& sender, const QString& channel,
                                 const QString& mask) {
                runInContext([&]() {
                    if (m_window.m_inviteProtect && !containsCaseInsensitive(m_window.m_friendNicks, sender)) {
                        // Treat invite spam from non-friends as an auto-ignore.
                        m_window.addIgnoreMask(normalizeIgnoreMask(mask.isEmpty() ? sender : mask));
                    }
                    if (m_window.m_ignoreInvites) {
                        return; // silently drop
                    }
                    m_window.appendSystemLineToTarget(
                        QStringLiteral("server"),
                        QStringLiteral("[invite] %1 invited you to %2").arg(sender, channel));
                });
            });
    connect(
        irc, &maxchat::irc::IrcConnection::kicked, this,
        [this, runInContext](const QString& channel, const QString& nick, const QString& reason) {
            runInContext([&]() {
                m_window.appendSystemLineToTarget(
                    channel, reason.isEmpty()
                                 ? QStringLiteral("* %1 was kicked from %2").arg(nick, channel)
                                 : QStringLiteral("* %1 was kicked from %2 (%3)")
                                       .arg(nick, channel, reason));
                const QString ownNick =
                    m_window.connection().nick().isEmpty() ? m_window.m_connectionPlan.nick : m_window.connection().nick();
                if (m_window.m_autoRejoin && nick.compare(ownNick, Qt::CaseInsensitive) == 0) {
                    const QString net = m_window.activeNetworkName();
                    QTimer::singleShot(std::max(0, m_window.m_rejoinDelay) * 1000, this, [this, net, channel]() {
                        m_window.withNetworkContext(net, [&]() {
                            if (m_window.connection().isConnected()) {
                                m_window.connection().sendRaw(QStringLiteral("JOIN %1").arg(channel));
                            }
                        });
                    });
                }
                if (channel.compare(m_window.m_currentTarget, Qt::CaseInsensitive) == 0) {
                    const QString currentNick =
                        m_window.connection().nick().isEmpty() ? m_window.m_connectionPlan.nick : m_window.connection().nick();
                    if (nick.compare(currentNick, Qt::CaseInsensitive) == 0) {
                        const bool joinedSet =
                            m_window.m_chatBuffers.setJoined(m_window.bufferIdForTarget(channel), false);
                        Q_UNUSED(joinedSet);
                        m_window.forgetTarget(channel);
                        m_window.activateBufferTarget({});
                        m_window.m_pendingNamesByChannel.remove(QStringLiteral("%1/%2").arg(
                            m_window.activeNetworkName().toCaseFolded(), channel.toCaseFolded()));
                        if (m_window.m_memberList != nullptr) {
                            m_window.m_memberList->clear();
                        }
                        m_window.showConnectionStatus(
                            QStringLiteral("%1 - Connected").arg(m_window.m_connectionPlan.networkName));
                        m_window.rebuildNetworkTree();
                        m_window.updateChannelModeButton();
                    } else {
                        const bool memberRemoved =
                            m_window.m_chatBuffers.removeMember(m_window.bufferIdForTarget(channel), nick);
                        Q_UNUSED(memberRemoved);
                        removeMember(m_window.m_memberList, nick);
                        m_window.memberListChanged();
                    }
                }
            });
        });
    connect(irc, &maxchat::irc::IrcConnection::channelModeIs, this,
            [this, runInContext](const QString& channel, const QString& modeLine) {
                runInContext([&]() {
                    const QString key = QStringLiteral("%1/%2").arg(
                        m_window.activeNetworkName().toCaseFolded(), channel.toCaseFolded());
                    const QString previousModeLine = m_window.m_channelModeLines.value(key);
                    m_window.m_channelModeLines.insert(key, modeLine);
                    if (previousModeLine == modeLine) {
                        return;
                    }
                    m_window.appendSystemLineToTarget(
                        channel, modeLine.isEmpty()
                                     ? QStringLiteral("! Modes for %1 are not set.").arg(channel)
                                     : QStringLiteral("! Modes for %1: %2").arg(channel, modeLine));
                });
            });
    connect(
        irc, &maxchat::irc::IrcConnection::modeChanged, this,
        [this, runInContext](const QString& target, const QString& by, const QString& modeLine) {
            runInContext([&]() {
                if (!m_window.m_showMode) {
                    return;
                }
                const QString actor = by.isEmpty() ? QStringLiteral("server") : by;
                m_window.appendSystemLineToTarget(
                    isChannelTarget(target) ? target : QStringLiteral("server"),
                    modeLine.isEmpty()
                        ? QStringLiteral("* %1 changed modes for %2").arg(actor, target)
                        : QStringLiteral("* %1 sets mode %2 on %3").arg(actor, modeLine, target));
            });
        });
    connect(irc, &maxchat::irc::IrcConnection::awayChanged, this,
            [this, signalNetwork](const QString& nick, const bool away) {
                const QString network =
                    signalNetwork.isEmpty() ? m_window.activeNetworkName() : signalNetwork;
                const QString key = nick.trimmed().toLower();
                if (key.isEmpty()) {
                    return;
                }
                QSet<QString>& awayNicks = m_window.m_awayNicksByNetwork[network];
                if (away) {
                    awayNicks.insert(key);
                } else {
                    awayNicks.remove(key);
                }
                if (network.compare(m_window.activeNetworkName(), Qt::CaseInsensitive) == 0) {
                    m_window.recolorMemberList();
                }
            });
    connect(irc, &maxchat::irc::IrcConnection::isonReply, this,
            [this, signalNetwork](const QStringList& onlineNicks) {
                if (signalNetwork.isEmpty()) {
                    m_window.handleIsonReply(onlineNicks);
                } else {
                    m_window.handleIsonReplyForNetwork(signalNetwork, onlineNicks);
                }
            });
    connect(
        irc, &maxchat::irc::IrcConnection::banList, this,
        [this, runInContext](const QString& channel, const QString& mask, const QString& setter) {
            runInContext([&]() {
                if (m_window.m_banListDialog != nullptr &&
                    m_window.m_banListDialog->channel().compare(channel, Qt::CaseInsensitive) == 0) {
                    m_window.m_banListDialog->addBan(mask, setter);
                    return;
                }
                m_window.appendSystemLineToTarget(
                    QStringLiteral("server"),
                    setter.trimmed().isEmpty()
                        ? QStringLiteral("[ban] %1 %2").arg(channel, mask)
                        : QStringLiteral("[ban] %1 %2 set by %3").arg(channel, mask, setter));
            });
        });
    connect(irc, &maxchat::irc::IrcConnection::banListEnd, this,
            [this, runInContext](const QString& channel) {
                runInContext([&]() {
                    if (m_window.m_banListDialog != nullptr &&
                        m_window.m_banListDialog->channel().compare(channel, Qt::CaseInsensitive) == 0) {
                        m_window.m_banListDialog->setStatusText(
                            QStringLiteral("Ban list loaded for %1.").arg(channel));
                        return;
                    }
                    m_window.appendSystemLineToTarget(
                        QStringLiteral("server"),
                        QStringLiteral("[ban] End of ban list for %1.").arg(channel));
                });
            });
}

} // namespace maxchat::ui
