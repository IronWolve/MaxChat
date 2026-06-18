#include "ui/MainWindow.h"

#include "app/AppInfo.h"
#include "core/CommandAlias.h"
#include "core/ConnectionPlan.h"
#include "core/DefaultNetworks.h"
#include "core/SettingsStore.h"
#include "core/UrlDetector.h"
#include "irc/CommandParser.h"
#include "irc/IrcRedaction.h"
#include "irc/ReconnectPlanner.h"
#include "services/LinkPreviewClassifier.h"
#include "services/LinkPreviewPolicy.h"
#include "services/LinkPreviewRenderer.h"
#include "spell/HunspellSpellchecker.h"
#include "spell/SpellcheckDictionaryCatalog.h"
#include "ui/AliasEditorDialog.h"
#include "ui/BanListDialog.h"
#include "ui/ChannelListDialog.h"
#include "ui/ChannelModesDialog.h"
#include "irc/IrcFormat.h"
#include "ui/AudioPlayerBar.h"
#include "ui/ChatFindDialog.h"
#include "ui/ColorPickerDialog.h"
#include "comic/ComicArt.h"
#include "comic/ComicCharacter.h"
#include "comic/ComicEmotion.h"
#include "comic/ComicRenderer.h"
#include "ui/ComicSettingsDialog.h"
#include "ui/ComicView.h"
#include "ui/DccManager.h"
#include "ui/DccTransfersDialog.h"
#include "ui/ImageViewerDialog.h"
#include "ui/MediaPlayerDialog.h"
#include "ui/ShortcutEditorDialog.h"
#include "ui/FriendsNotifyDialog.h"
#include "ui/GeometryPersist.h"
#include "ui/IgnoreListDialog.h"
#include "ui/PreferencesDialog.h"
#include "ui/QuickConnectDialog.h"
#include "ui/RawLogDialog.h"
#include "ui/ServerListDialog.h"
#include "ui/AppearanceController.h"
#include "ui/ChatRenderTheme.h"
#include "ui/MediaController.h"
#include "ui/ComicController.h"
#include "ui/IrcRouter.h"
#include "ui/IrcRoutingHelpers.h"
#include "ui/NotificationController.h"
#include "ui/ScriptBridge.h"
#include "ui/ScriptTerminalManager.h"
#include "ui/SpellTextEdit.h"
#include "ui/SpellcheckHighlighter.h"
#include "ui/TerminalProfile.h"
#include "ui/SystemInfo.h"
#include "ui/ThemeCatalog.h"
#include "ui/UrlListDialog.h"
#include "ui/AppIcon.h"
#include "ui/Notifier.h"
#include <QSystemTrayIcon>
#include <QMenu>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QStyle>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDesktopServices>
#include <QColor>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QTextStream>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QShowEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMetaType>
#include <QMouseEvent>
#include <QPainter>
#include <QPair>
#include <QPalette>
#include <QPushButton>
#include <QProcess>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSaveFile>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QSysInfo>
#include <QTabBar>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVariantList>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace maxchat::ui {

namespace {

// Per-buffer key used for comic view suppression and marker tracking.
QString comicKey(const QString& network, const QString& target) {
    return network + QChar(0x1f) + target;
}

QString cleanedChannelToken(QString token) {
    token = token.trimmed();
    while (!token.isEmpty() && QStringLiteral(":,.;)]}").contains(token.back())) {
        token.chop(1);
    }
    return isChannelTarget(token) ? token : QString();
}

QString firstChannelToken(const QString& text) {
    static const QRegularExpression channelToken(QStringLiteral(R"((^|\s)([#&][^\s,.;:\)\]\}]+))"));
    const QRegularExpressionMatch match = channelToken.match(text);
    return match.hasMatch() ? cleanedChannelToken(match.captured(2)) : QString();
}

QString replyTextTarget(const QString& line) {
    static const QRegularExpression labelPattern(QStringLiteral(R"(^\[([^\]]+)\]\s*(.*)$)"));
    const QRegularExpressionMatch match = labelPattern.match(line.trimmed());
    if (!match.hasMatch()) {
        return QStringLiteral("server");
    }

    const QString label = match.captured(1).toLower();
    const QString body = match.captured(2).trimmed();
    if (label == QStringLiteral("topic") || label == QStringLiteral("channel")) {
        return firstChannelToken(body);
    }
    if (label != QStringLiteral("error")) {
        return QStringLiteral("server");
    }

    if (body.startsWith(QStringLiteral("Cannot send to ")) ||
        body.startsWith(QStringLiteral("Cannot join ")) ||
        body.startsWith(QStringLiteral("No such channel: ")) ||
        body.startsWith(QStringLiteral("You are not on "))) {
        return firstChannelToken(body);
    }

    const QString target = firstChannelToken(body);
    if (!target.isEmpty() && (body.contains(QStringLiteral(" is not on ")) ||
                              body.contains(QStringLiteral(" is already on ")))) {
        return target;
    }
    return body.startsWith(target) ? target : QStringLiteral("server");
}

QString replyTextLabel(const QString& line) {
    static const QRegularExpression labelPattern(QStringLiteral(R"(^\[([^\]]+)\])"));
    const QRegularExpressionMatch match = labelPattern.match(line.trimmed());
    return match.hasMatch() ? match.captured(1).toLower() : QString();
}

QStringList variantStringList(const QVariant& value) {
    if (value.metaType().id() == QMetaType::QStringList) {
        return value.toStringList();
    }

    QStringList strings;
    const QVariantList values = value.toList();
    for (const QVariant& item : values) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty()) {
            strings.append(text);
        }
    }
    return strings;
}

QString formatDurationMs(const qint64 milliseconds) {
    qint64 seconds = std::max<qint64>(0, milliseconds / 1000);
    const qint64 days = seconds / 86400;
    seconds %= 86400;
    const qint64 hours = seconds / 3600;
    seconds %= 3600;
    const qint64 minutes = seconds / 60;
    seconds %= 60;

    QStringList parts;
    if (days > 0) {
        parts.append(QStringLiteral("%1d").arg(days));
    }
    if (hours > 0 || !parts.isEmpty()) {
        parts.append(QStringLiteral("%1h").arg(hours));
    }
    if (minutes > 0 || !parts.isEmpty()) {
        parts.append(QStringLiteral("%1m").arg(minutes));
    }
    parts.append(QStringLiteral("%1s").arg(seconds));
    return parts.join(QLatin1Char(' '));
}

QString qtDateTimeFormat(const QString& strftimeFormat) {
    const QString fallback = QStringLiteral("hh:mm AP");
    const QString source = strftimeFormat.trimmed();
    if (source.isEmpty()) {
        return fallback;
    }

    QString out;
    out.reserve(source.size() * 2);
    for (qsizetype i = 0; i < source.size(); ++i) {
        const QChar ch = source.at(i);
        if (ch != QLatin1Char('%') || i + 1 >= source.size()) {
            out.append(ch);
            continue;
        }

        const QChar code = source.at(++i);
        if (code == QLatin1Char('%')) {
            out.append(QLatin1Char('%'));
        } else if (code == QLatin1Char('I')) {
            out.append(QStringLiteral("hh"));
        } else if (code == QLatin1Char('H')) {
            out.append(QStringLiteral("HH"));
        } else if (code == QLatin1Char('M')) {
            out.append(QStringLiteral("mm"));
        } else if (code == QLatin1Char('S')) {
            out.append(QStringLiteral("ss"));
        } else if (code == QLatin1Char('p')) {
            out.append(QStringLiteral("AP"));
        } else if (code == QLatin1Char('Y')) {
            out.append(QStringLiteral("yyyy"));
        } else if (code == QLatin1Char('y')) {
            out.append(QStringLiteral("yy"));
        } else if (code == QLatin1Char('m')) {
            out.append(QStringLiteral("MM"));
        } else if (code == QLatin1Char('d')) {
            out.append(QStringLiteral("dd"));
        } else if (code == QLatin1Char('b')) {
            out.append(QStringLiteral("MMM"));
        } else if (code == QLatin1Char('B')) {
            out.append(QStringLiteral("MMMM"));
        } else if (code == QLatin1Char('a')) {
            out.append(QStringLiteral("ddd"));
        } else if (code == QLatin1Char('A')) {
            out.append(QStringLiteral("dddd"));
        } else {
            out.append(QLatin1Char('%'));
            out.append(code);
        }
    }
    return out.isEmpty() ? fallback : out;
}

QStringList removeCaseInsensitive(QStringList values, const QString& needle) {
    for (int index = values.size() - 1; index >= 0; --index) {
        if (values.at(index).compare(needle, Qt::CaseInsensitive) == 0) {
            values.removeAt(index);
        }
    }
    return values;
}

QString inputText(const QTextEdit* input) {
    return input == nullptr ? QString() : input->toPlainText();
}

struct CommandServerSpec {
    bool valid = false;
    QString errorText;
    QString host;
    int port = 6697;
    bool tls = true;
    QString serverPassword;
};

bool tokenIsPort(QString token) {
    token = token.trimmed();
    if (token.startsWith(QLatin1Char('+'))) {
        token.remove(0, 1);
    }
    bool ok = false;
    const int port = token.toInt(&ok);
    return ok && port > 0 && port <= 65535;
}

CommandServerSpec parseCommandServerSpec(const QString& firstToken, const QString& rest) {
    QStringList tokens = QStringLiteral("%1 %2")
                             .arg(firstToken, rest)
                             .trimmed()
                             .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);

    CommandServerSpec spec;
    if (tokens.isEmpty()) {
        spec.errorText = QStringLiteral("Usage: /server server[:port] "
                                        "[server-password]");
        return spec;
    }

    bool tls = true;
    bool tlsOptionSeen = false;
    int index = 0;
    while (index < tokens.size()) {
        const QString option = tokens.at(index).toLower();
        if (option == QStringLiteral("-ssl") || option == QStringLiteral("--ssl") ||
            option == QStringLiteral("-tls") || option == QStringLiteral("--tls")) {
            tls = true;
            tlsOptionSeen = true;
            ++index;
            continue;
        }
        if (option == QStringLiteral("-nossl") || option == QStringLiteral("--nossl") ||
            option == QStringLiteral("-notls") || option == QStringLiteral("--notls")) {
            tls = false;
            tlsOptionSeen = true;
            ++index;
            continue;
        }
        break;
    }

    if (index >= tokens.size()) {
        spec.errorText = QStringLiteral("Usage: /server server[:port] "
                                        "[server-password]");
        return spec;
    }

    maxchat::irc::ServerEndpoint endpoint =
        maxchat::core::parseServerSpec(tokens.at(index), 6697, tls);
    if (tlsOptionSeen) {
        endpoint.tls = tls;
    }
    ++index;

    if (index < tokens.size() && tokenIsPort(tokens.at(index))) {
        QString portText = tokens.at(index);
        if (portText.startsWith(QLatin1Char('+'))) {
            endpoint.tls = true;
            portText.remove(0, 1);
        } else if (!tlsOptionSeen) {
            endpoint.tls = false;
        }
        endpoint.port = portText.toInt();
        ++index;
    }

    endpoint.host = endpoint.host.trimmed();
    if (endpoint.host.isEmpty()) {
        spec.errorText = QStringLiteral("Usage: /server server[:port] "
                                        "[server-password]");
        return spec;
    }
    if (endpoint.port <= 0 || endpoint.port > 65535) {
        spec.errorText = QStringLiteral("! Invalid server port.");
        return spec;
    }

    spec.valid = true;
    spec.host = endpoint.host;
    spec.port = endpoint.port;
    spec.tls = endpoint.tls;
    if (index < tokens.size()) {
        spec.serverPassword = tokens.mid(index).join(QLatin1Char(' '));
    }
    return spec;
}

bool networkMatchesCommandTarget(const maxchat::core::NetworkConfig& network,
                                 const QString& target) {
    const QString cleanTarget = target.trimmed();
    if (cleanTarget.isEmpty()) {
        return false;
    }

    if (network.value(QStringLiteral("name"))
                .toString()
                .compare(cleanTarget, Qt::CaseInsensitive) == 0 ||
        network.value(QStringLiteral("host"))
                .toString()
                .compare(cleanTarget, Qt::CaseInsensitive) == 0) {
        return true;
    }

    const QStringList servers = variantStringList(network.value(QStringLiteral("servers")));
    for (const QString& server : servers) {
        if (server.compare(cleanTarget, Qt::CaseInsensitive) == 0) {
            return true;
        }
        const maxchat::irc::ServerEndpoint endpoint = maxchat::core::parseServerSpec(
            server, network.value(QStringLiteral("port"), 6667).toInt(),
            network.value(QStringLiteral("tls")).toBool());
        if (endpoint.host.compare(cleanTarget, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    return false;
}

maxchat::core::NetworkConfig findCommandNetwork(const maxchat::core::NetworkConfigList& networks,
                                                const QString& target, const QString& rest) {
    const QString fullTarget = QStringLiteral("%1 %2").arg(target, rest).trimmed();
    if (!fullTarget.isEmpty()) {
        for (const maxchat::core::NetworkConfig& network : networks) {
            if (networkMatchesCommandTarget(network, fullTarget)) {
                return network;
            }
        }
    }

    for (const maxchat::core::NetworkConfig& network : networks) {
        if (networkMatchesCommandTarget(network, target)) {
            return network;
        }
    }
    return {};
}

struct ReplayLogLine {
    QDateTime timestamp;
    QString body;
};

bool looksLikeReplayTimestampPrefix(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }

    static const QRegularExpression timestampLike(QStringLiteral(R"(^[0-9A-Za-z ,:/.\-+]+$)"));
    return timestampLike.match(trimmed).hasMatch() &&
           trimmed.contains(QRegularExpression(QStringLiteral(R"(\d)")));
}

QString unwrapAlignedReplayBody(QString body) {
    body = body.trimmed();
    const int separator = body.indexOf(QStringLiteral(" | "));
    if (separator < 0) {
        return body;
    }

    const QString left = body.left(separator).trimmed();
    const QString right = body.mid(separator + 3).trimmed();
    if (right.isEmpty()) {
        return body;
    }

    const int nickStart = left.lastIndexOf(QLatin1Char('<'));
    if (nickStart >= 0 && left.endsWith(QLatin1Char('>'))) {
        return QStringLiteral("%1 %2").arg(left.mid(nickStart), right);
    }

    static const QRegularExpression noticeLabel(QStringLiteral(R"((-[^\s-][^-]*-)$)"));
    const QRegularExpressionMatch noticeMatch = noticeLabel.match(left);
    if (noticeMatch.hasMatch()) {
        return QStringLiteral("%1 %2").arg(noticeMatch.captured(1), right);
    }

    const int actionStart = left.lastIndexOf(QStringLiteral("* "));
    if (actionStart >= 0) {
        const QString actionLabel = left.mid(actionStart).trimmed();
        static const QRegularExpression actionLabelPattern(QStringLiteral(R"(^\* \S+$)"));
        if (actionLabelPattern.match(actionLabel).hasMatch()) {
            return QStringLiteral("%1 %2").arg(actionLabel, right);
        }
    }

    if (left.isEmpty() || looksLikeReplayTimestampPrefix(left)) {
        return right;
    }
    return body;
}

ReplayLogLine parseReplayLogLine(const QString& line) {
    static const QRegularExpression timestampPrefix(
        QStringLiteral(R"(^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s+(.*)$)"));
    const QRegularExpressionMatch match = timestampPrefix.match(line);
    if (!match.hasMatch()) {
        return {{}, unwrapAlignedReplayBody(line)};
    }

    return {QDateTime::fromString(match.captured(1), QStringLiteral("yyyy-MM-dd HH:mm:ss")),
            unwrapAlignedReplayBody(match.captured(2))};
}

void setInputText(QTextEdit* input, const QString& text) {
    if (input == nullptr) {
        return;
    }
    input->setPlainText(text);
    QTextCursor cursor = input->textCursor();
    cursor.movePosition(QTextCursor::End);
    input->setTextCursor(cursor);
}

int inputCursorPosition(const QTextEdit* input) {
    return input == nullptr ? 0 : input->textCursor().position();
}

void setInputCursorPosition(QTextEdit* input, const int position) {
    if (input == nullptr) {
        return;
    }
    QTextCursor cursor = input->textCursor();
    cursor.setPosition(std::clamp(position, 0, static_cast<int>(input->toPlainText().size())));
    input->setTextCursor(cursor);
}

constexpr int MaxRawLogLines = 5000;
constexpr int MaxUrlListItems = 1000;
constexpr int FriendPollIntervalMs = 60000;
constexpr int TreeTargetRole = Qt::UserRole;
constexpr int TreeNetworkRole = Qt::UserRole + 1;
// Set on "Term N" nodes; holds the manager's scoped terminal id. Terminal nodes
// are launchers (pop the window), not chat buffers, so they carry no target role.
constexpr int TreeTerminalRole = Qt::UserRole + 2;

QTreeWidgetItem* newTreeItem(const QString& label, const QString& target = {},
                             const QString& network = {}) {
    auto* item = new QTreeWidgetItem(QStringList{label});
    if (!target.trimmed().isEmpty()) {
        item->setData(0, TreeTargetRole, target.trimmed());
    }
    if (!network.trimmed().isEmpty()) {
        item->setData(0, TreeNetworkRole, network.trimmed());
    }
    return item;
}

QString treeItemTarget(const QTreeWidgetItem* item) {
    if (item == nullptr) {
        return {};
    }

    const QString storedTarget = item->data(0, TreeTargetRole).toString().trimmed();
    return storedTarget.isEmpty() ? item->text(0).trimmed() : storedTarget;
}

QString treeItemNetwork(const QTreeWidgetItem* item) {
    if (item == nullptr) {
        return {};
    }

    const QString storedNetwork = item->data(0, TreeNetworkRole).toString().trimmed();
    if (!storedNetwork.isEmpty()) {
        return storedNetwork;
    }

    const QTreeWidgetItem* root = item;
    while (root->parent() != nullptr) {
        root = root->parent();
    }
    return root->data(0, TreeNetworkRole).toString().trimmed();
}

bool isTreeStatusTarget(const QString& target) {
    return target.compare(QStringLiteral("server"), Qt::CaseInsensitive) == 0 ||
           target == QStringLiteral("Connected") || target == QStringLiteral("Connecting") ||
           target == QStringLiteral("Disconnected");
}

QString labelWithTreeCounts(const QString& label, const int unreadCount, const int highlightCount) {
    if (unreadCount <= 0) {
        return label;
    }

    if (highlightCount > 0) {
        if (highlightCount == unreadCount) {
            return QStringLiteral("%1 [!%2]").arg(label).arg(highlightCount);
        }
        return QStringLiteral("%1 [!%2/%3]").arg(label).arg(highlightCount).arg(unreadCount);
    }
    return QStringLiteral("%1 [%2]").arg(label).arg(unreadCount);
}


} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_chatLogStore(QDir(m_settings.paths().configDir).filePath(QStringLiteral("logs"))),
      m_imageFetcher(&m_previewNetworkManager),
      m_osNotifyAvailable(false) {
    m_appUptime.start();
    // Link-preview card fetcher (decomp Phase 2b). Constructed first so any early
    // appendSystemLine that queues a preview has it available.
    m_previewFetcher = new PreviewFetcher(*this, m_previewNetworkManager, this);
    // IRC signal router (decomp Phase 7) — wires connection signals to handlers.
    m_ircRouter = new IrcRouter(*this, this);
    // Comic Mode backend (decomp Phase 5).
    m_comicController = new ComicController(*this, this);
    // Notification + tray subsystem (decomp Phase 4).
    m_notifyController = new NotificationController(*this, this);
    // Inline media + image upload controller. Constructed before buildLayout()
    // because the chat view's anchor-click handler and the audio bar wire into it.
    m_media = new MediaController(*this, this);
    m_appearance = new AppearanceController(*this, this);
    m_appearance->registerBundledFonts();
    buildMenus();
    buildLayout();
    setupConnectionSignals();
    m_notifyController->setupTrayIcon();
    m_notifier = new Notifier(this);
    // OS native notifications post through the VISIBLE tray icon (showMessage on
    // a never-shown tray is a silent no-op), so they require m_tray to exist.
    m_osNotifyAvailable = (m_tray != nullptr) && ::QSystemTrayIcon::supportsMessages();
#ifdef Q_OS_LINUX
    // Wayland/WSLg reports supportsMessages()=true but often has no notification
    // daemon; confirm one actually owns the D-Bus name, else fall back to toast.
    if (m_osNotifyAvailable) {
        // Async: the old waitForFinished(3000) stalled first paint by up to
        // 3 s when no session bus answered. Assume unavailable (toast
        // fallback) until the probe confirms a daemon.
        m_osNotifyAvailable = false;
        auto* probe = new QProcess(this);
        connect(probe, &QProcess::finished, this,
                [this, probe](int exitCode, QProcess::ExitStatus exitStatus) {
                    m_osNotifyAvailable =
                        exitStatus == QProcess::NormalExit && exitCode == 0 &&
                        m_tray != nullptr;
                    probe->deleteLater();
                });
        connect(probe, &QProcess::errorOccurred, this,
                [probe](QProcess::ProcessError) { probe->deleteLater(); });
        probe->start(QStringLiteral("dbus-send"),
                     {QStringLiteral("--session"),
                      QStringLiteral("--dest=org.freedesktop.DBus"),
                      QStringLiteral("--type=method_call"),
                      QStringLiteral("--print-reply"),
                      QStringLiteral("/org/freedesktop/DBus"),
                      QStringLiteral("org.freedesktop.DBus.GetNameOwner"),
                      QStringLiteral("string:org.freedesktop.Notifications")});
    }
#endif
    // OpenGraph card fetch lives in PreviewFetcher now (decomp Phase 2b); it wires
    // its own fetcher signals. The image fetcher stays here, feeding ChatPane (R3).
    connect(&m_imageFetcher, &maxchat::services::ImageFetcher::imageFetched, this,
            &MainWindow::handlePreviewImageFetched);
    connect(&m_imageFetcher, &maxchat::services::ImageFetcher::imageFetchFailed, this,
            &MainWindow::handlePreviewImageFailed);
    m_friendPollTimer.setInterval(FriendPollIntervalMs);
    connect(&m_friendPollTimer, &QTimer::timeout, this, &MainWindow::pollFriends);
    m_channelDrainTimer.setInterval(100);
    connect(&m_channelDrainTimer, &QTimer::timeout, this, [this]() {
        if (m_channelListDialog == nullptr || m_pendingChannels.isEmpty()) {
            m_channelDrainTimer.stop();
            m_pendingChannels.clear();  // drop buffered entries if dialog was closed mid-fetch
            return;
        }
        constexpr int kBatchSize = 500;
        const int take = qMin(kBatchSize, m_pendingChannels.size());
        m_channelListDialog->addChannelsBulk(m_pendingChannels.mid(0, take));
        m_pendingChannels.remove(0, take);
        if (m_pendingChannels.isEmpty()) {
            m_channelDrainTimer.stop();
        }
    });
    m_autoAwayTimer.setSingleShot(true);
    connect(&m_autoAwayTimer, &QTimer::timeout, this, &MainWindow::triggerAutoAway);

    m_dccManager = new DccManager(this);
    connect(m_dccManager, &DccManager::ctcpToSend, this,
            [this](const QString& peer, const QString& ctcpArgs) {
                connection().ctcp(peer, QStringLiteral("DCC"), ctcpArgs);
                appendSystemLineToTarget(peer, QStringLiteral("-> [dcc] %1").arg(ctcpArgs), true,
                                         true, false, false);
            });
    connect(m_dccManager, &DccManager::status, this,
            [this](const QString& message) { appendSystemLine(tr("! %1").arg(message)); });
    connect(m_dccManager, &DccManager::chatLineReceived, this,
            [this](const QString& peer, const QString& line) {
                appendSystemLineToTarget(QStringLiteral("=%1").arg(peer),
                                         QStringLiteral("<%1> %2").arg(peer, line), true, false,
                                         false, false);
            });
    connect(m_dccManager, &DccManager::chatStateChanged, this,
            [this](const QString& peer, int state) {
                const QString target = QStringLiteral("=%1").arg(peer);
                const QString note = state == 0   ? QStringLiteral("connecting...")
                                     : state == 1 ? QStringLiteral("connected.")
                                                  : QStringLiteral("closed.");
                rememberTarget(target);
                appendSystemLineToTarget(
                    target, QStringLiteral("* DCC chat with %1 %2").arg(peer, note), true, false,
                    false);
                rebuildNetworkTree();
            });

    setupNavShortcuts();
    // Ordering matters here (review 2026-06-12):
    //  - fallback icon/title BEFORE applyCurrentSettings, or the static .ico
    //    would overwrite the user's themed tray/window icon;
    //  - geometry restore BEFORE applyCurrentSettings, or the saved splitter
    //    sizes get applied to the default 1100x720 window and then drift when
    //    restoreGeometry resizes it.
    setWindowTitle(QStringLiteral("%1 %2").arg(app::displayName(), app::version()));
    setWindowIcon(QIcon(QStringLiteral(":/icons/maxchat.ico")));
    const QVariantMap startupSettings = m_settings.loadWithDefaults();
    const QString savedGeom =
        startupSettings.value(QStringLiteral("window_geometry")).toString();
    if (savedGeom.isEmpty() || !restoreGeometry(QByteArray::fromBase64(savedGeom.toLatin1()))) {
        resize(1100, 720);
    }
    applyCurrentSettings();
    if (startupSettings.value(QStringLiteral("connect_on_start"), false).toBool()) {
        QTimer::singleShot(0, this, [this]() { startConfiguredStartupConnection(); });
    }

    if (startupSettings.value(QStringLiteral("update_check"), false).toBool()) {
        QTimer::singleShot(3500, this, [this]() { checkForUpdates(/*manual=*/false); });
    }

    // Scripting: the whole subsystem (LuaEngine + terminal manager + ScriptHost
    // callbacks) lives in ScriptBridge now; MainWindow just owns it and forwards.
    const QString scriptsDir =
        QDir(m_settings.paths().configDir).filePath(QStringLiteral("scripts"));
    m_scripts = new ScriptBridge(*this, scriptsDir, this);
    if (ScriptBridge::scriptingAvailable()) {
        // Deferred so the window paints before any script's top-level code
        // runs (a slow or dialog-popping script must not block first paint).
        QTimer::singleShot(0, this, [this]() { m_scripts->seedAndLoadAll(); });
    }
}

bool MainWindow::selfTest() const {
    return m_networkTree != nullptr && m_chatView != nullptr && m_bufferTabBar != nullptr &&
           m_memberList != nullptr && m_channelModesButton != nullptr && m_topicLabel != nullptr &&
           m_input != nullptr && !windowTitle().isEmpty();
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange && isMinimized() && m_minimizeToTray && m_tray) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (m_media->isConfigured() && event->mimeData() != nullptr &&
        (event->mimeData()->hasImage() || event->mimeData()->hasUrls())) {
        event->acceptProposedAction();
    } else {
        QMainWindow::dragEnterEvent(event);
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (!m_media->isConfigured() || event->mimeData() == nullptr) {
        QMainWindow::dropEvent(event);
        return;
    }
    // Image data (e.g. copied from a browser / screenshot tool)
    if (event->mimeData()->hasImage()) {
        const QImage img = qvariant_cast<QImage>(event->mimeData()->imageData());
        if (!img.isNull()) {
            event->acceptProposedAction();
            m_media->uploadImage(img);
            return;
        }
    }
    // File(s) dragged from a file manager
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) continue;
        const QImage img(url.toLocalFile());
        if (!img.isNull()) {
            event->acceptProposedAction();
            m_media->uploadImage(img);
            return; // upload the first valid image
        }
    }
    QMainWindow::dropEvent(event);
}

// True if w is a writable text field — typing should stay there, not jump to
// the message box. The chat view is a read-only QTextBrowser, so it returns
// false and keystrokes typed while the chat is clicked redirect to m_input.
static bool isTextEntry(QWidget* w) {
    if (w == nullptr) { return false; }
    if (qobject_cast<QAbstractSpinBox*>(w) != nullptr) { return true; }
    if (auto* cb = qobject_cast<QComboBox*>(w)) { return cb->isEditable(); }
    if (qobject_cast<QLineEdit*>(w) != nullptr) { return true; }
    if (auto* te = qobject_cast<QTextEdit*>(w)) { return !te->isReadOnly(); }
    if (auto* pe = qobject_cast<QPlainTextEdit*>(w)) { return !pe->isReadOnly(); }
    return false;
}

// HexChat-style key redirect.
//
// Invariants that must hold forever:
//  1. If a QMenu, combo popup, or modal dialog is active → do nothing.
//  2. If the focused widget is a writable text field → do nothing.
//  3. If the focused widget is interactive (button, list, slider, tab bar)
//     → do nothing (let it keep its own key handling).
//  4. If the event has Ctrl/Alt/Meta → do nothing (let shortcuts through).
//  5. Only printable text characters trigger the redirect.
//  6. Install via qApp->installEventFilter(this), NOT per-widget — so that
//     clicks on the chat view, nick list, topic bar, etc. all funnel here
//     without needing per-widget installs.
//
// Why this breaks if you change it:
//  - Removing the qApp install means widgets not explicitly filtered are
//    missed (the original bug: clicking chat → typing did nothing).
//  - Removing the activePopupWidget/activeModalWidget guard makes typing in
//    a right-click spell-check menu jump to the input field mid-selection.
//  - Removing the isTextEntry guard would steal keystrokes from alias/prefs
//    text fields that live in child widgets of this window (non-modal).
//  - Removing the interactive-widget guard (buttons, list, slider, tabbar)
//    breaks keyboard navigation in dialogs and the member list.
bool MainWindow::redirectKeyToInput(QKeyEvent* e) {
    // Guard 1: menu, popup, or modal dialog active — leave completely alone.
    if (QApplication::activePopupWidget() != nullptr ||
        QApplication::activeModalWidget() != nullptr) {
        return false;
    }
    // Guard: only when this window is the active top-level window.
    if (QApplication::activeWindow() != this) {
        return false;
    }
    // Escape: return focus to the message box (no find bar in C++ yet, just focus).
    if (e->key() == Qt::Key_Escape) {
        if (m_input != nullptr && QApplication::focusWidget() != m_input) {
            m_input->setFocus();
            return true;
        }
        return false;
    }
    // Guard 2: focus is already in a writable text field — let it handle keys.
    if (isTextEntry(QApplication::focusWidget())) {
        return false;
    }
    // Guard 3: interactive navigation widget — don't steal activation/nav keys.
    // QAbstractItemView (tree, member list) is intentionally NOT here: arrow
    // keys reach Guard 5 (non-printable → return false) so tree navigation is
    // preserved, while printable characters still redirect to the input box.
    QWidget* focus = QApplication::focusWidget();
    if (qobject_cast<QAbstractButton*>(focus) != nullptr ||
        qobject_cast<QAbstractSlider*>(focus) != nullptr ||
        qobject_cast<QTabBar*>(focus) != nullptr) {
        return false;
    }
    // Guard 4: Ctrl/Alt/Meta shortcuts pass through unchanged — EXCEPT AltGr:
    // on Windows AltGr arrives as Ctrl+Alt, and international layouts type
    // printable chars with it (é @ € [ ]). A printable Ctrl+Alt key is AltGr,
    // not a shortcut.
    const QString text = e->text();
    const bool printable = !text.isEmpty() && text.at(0).isPrint();
    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool altGr = (mods & Qt::ControlModifier) && (mods & Qt::AltModifier) && printable;
    if (!altGr && (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        return false;
    }
    // Guard 5: only printable characters (not arrows, Tab, F-keys, etc.).
    if (!printable) {
        return false;
    }
    // Redirect: focus the message box and append the typed character at the
    // END (insert at the saved mid-string cursor would garble a draft the
    // user can't see happening).
    if (m_input != nullptr) {
        m_input->setFocus();
        QTextCursor c = m_input->textCursor();
        c.movePosition(QTextCursor::End);
        m_input->setTextCursor(c);
        m_input->insertPlainText(text);
        return true;
    }
    return false;
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    // Set focus to the message box on first show so the user can type immediately.
    if (!m_focusedOnce && m_input != nullptr) {
        m_focusedOnce = true;
        m_input->setFocus();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // Key redirect must run before per-widget handling so it can consume
    // events from any widget in the window hierarchy.
    if (event->type() == QEvent::KeyPress) {
        if (redirectKeyToInput(static_cast<QKeyEvent*>(event))) {
            return true;
        }
    }
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                return false; // Shift+Enter = newline (multiline compose)
            }
            handleInputSubmitted();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Up) {
            return showHistoryEntry(-1);
        }
        if (keyEvent->key() == Qt::Key_Down) {
            return showHistoryEntry(1);
        }
        if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
            return completeInput(keyEvent->key() == Qt::Key_Tab);
        }
        // Autocorrect: a Space may replace the word just typed; an immediate
        // Backspace undoes that. Any other key cancels the pending undo window.
        if (keyEvent->key() == Qt::Key_Space && keyEvent->modifiers() == Qt::NoModifier) {
            if (tryAutocorrectAtSpace()) {
                return true;
            }
        } else if (keyEvent->key() == Qt::Key_Backspace) {
            if (undoAutocorrect()) {
                return true;
            }
        } else {
            m_autocorrect.active = false;
        }
    }
    if ((watched == m_input || (m_input != nullptr && watched == m_input->viewport())) &&
        event->type() == QEvent::ContextMenu) {
        auto* contextEvent = static_cast<QContextMenuEvent*>(event);
        showInputContextMenu(contextEvent->pos(), contextEvent->globalPos());
        return true;
    }
    // After a menu-bar dropdown closes, restore focus to the input box.
    // Guard: only direct children of QMenuBar (not context menus or sub-menus).
    // Deferred so Qt finishes teardown; activePopupWidget guard handles the
    // case where a sub-menu closed but the parent menu is still open.
    if (event->type() == QEvent::Hide && qobject_cast<QMenu*>(watched) != nullptr) {
        if (qobject_cast<QMenuBar*>(static_cast<QMenu*>(watched)->parentWidget()) == nullptr) {
            return QMainWindow::eventFilter(watched, event);
        }
        QTimer::singleShot(0, this, [this]() {
            if (m_input == nullptr) return;
            if (QApplication::activeWindow() != this) return;
            if (QApplication::activeModalWidget() != nullptr) return;
            if (QApplication::activePopupWidget() != nullptr) return;
            if (isTextEntry(QApplication::focusWidget())) return;
            m_input->setFocus();
        });
    }
    return QMainWindow::eventFilter(watched, event);
}

void maxchat::ui::MainWindow::showInputContextMenu(const QPoint& localPos,
                                                   const QPoint& globalPos) {
    if (m_input == nullptr) {
        return;
    }
    QMenu* menu = m_input->createStandardContextMenu();
    if (menu == nullptr) {
        return;
    }
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // Prepend spelling suggestions for the (misspelled) word under the cursor.
    if (m_spellchecker != nullptr && m_spellchecker->isLoaded()) {
        QTextCursor wordCursor = m_input->cursorForPosition(localPos);
        wordCursor.select(QTextCursor::WordUnderCursor);
        const QString word = wordCursor.selectedText();
        if (!word.isEmpty() && !m_spellchecker->isCorrect(word)) {
            QAction* anchor = menu->actions().isEmpty() ? nullptr : menu->actions().first();
            const QStringList suggestions = m_spellchecker->suggestions(word);
            if (suggestions.isEmpty()) {
                auto* none = new QAction(QStringLiteral("(no suggestions)"), menu);
                none->setEnabled(false);
                menu->insertAction(anchor, none);
            } else {
                for (const QString& suggestion : suggestions) {
                    auto* action = new QAction(suggestion, menu);
                    connect(action, &QAction::triggered, this,
                            [wordCursor, suggestion]() mutable {
                                wordCursor.insertText(suggestion);
                            });
                    menu->insertAction(anchor, action);
                }
            }
            auto* addAction =
                new QAction(QStringLiteral("Add to Dictionary"), menu);
            const QString plainWord = word;
            connect(addAction, &QAction::triggered, this,
                    [this, plainWord]() { addWordToPersonalDictionary(plainWord); });
            menu->insertAction(anchor, addAction);
            menu->insertSeparator(anchor);
        }
    }
    menu->popup(globalPos);
}

namespace {
bool isAutocorrectWordChar(const QChar ch) {
    return ch.isLetter() || ch == QLatin1Char('\'') || ch == QLatin1Char('-');
}

// Case-insensitive Levenshtein edit distance, capped — used to reject
// autocorrections that are too far from what the user typed (so "chatgpt" never
// becomes "chatting"). Returns a value > cap as soon as it's clearly too far.
int boundedEditDistance(const QString& a, const QString& b, const int cap) {
    const QString lhs = a.toLower();
    const QString rhs = b.toLower();
    const int n = static_cast<int>(lhs.size());
    const int m = static_cast<int>(rhs.size());
    if (std::abs(n - m) > cap) {
        return cap + 1;
    }
    QVector<int> prev(m + 1);
    QVector<int> cur(m + 1);
    for (int j = 0; j <= m; ++j) {
        prev[j] = j;
    }
    for (int i = 1; i <= n; ++i) {
        cur[0] = i;
        int rowBest = cur[0];
        for (int j = 1; j <= m; ++j) {
            const int cost = (lhs[i - 1] == rhs[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
            rowBest = std::min(rowBest, cur[j]);
        }
        if (rowBest > cap) {
            return cap + 1; // whole row already past the cap — bail
        }
        prev.swap(cur);
    }
    return prev[m];
}
} // namespace

bool maxchat::ui::MainWindow::tryAutocorrectAtSpace() {
    m_autocorrect.active = false;
    if (!m_autocorrectEnabled || m_input == nullptr || m_spellchecker == nullptr ||
        !m_spellchecker->isLoaded()) {
        return false;
    }

    const QTextCursor cursor = m_input->textCursor();
    if (cursor.hasSelection()) {
        return false;
    }
    const int pos = cursor.position();
    const QString text = m_input->toPlainText();
    int start = pos;
    while (start > 0 && isAutocorrectWordChar(text.at(start - 1))) {
        --start;
    }
    const QString word = text.mid(start, pos - start);

    // Skip the word the user just deliberately restored (one-shot), then resume.
    if (!m_autocorrectSuppress.isEmpty() && word == m_autocorrectSuppress) {
        m_autocorrectSuppress.clear();
        return false;
    }
    // Conservative: leave short words, capitalised words (likely names), and
    // anything with digits alone — autocorrect should never surprise-mangle.
    if (word.size() < 3 || word.front().isUpper()) {
        return false;
    }
    for (const QChar ch : word) {
        if (ch.isDigit()) {
            return false;
        }
    }
    if (m_spellchecker->isCorrect(word)) {
        return false;
    }
    const QStringList suggestions = m_spellchecker->suggestions(word, 1);
    if (suggestions.isEmpty()) {
        return false;
    }
    const QString corrected = suggestions.first();
    if (corrected.compare(word, Qt::CaseInsensitive) == 0 || corrected.contains(QLatin1Char(' '))) {
        return false;
    }
    // Only replace when the suggestion is close to what was typed (a real typo),
    // not a wildly different word — this is the "aggressiveness" control.
    if (boundedEditDistance(word, corrected, m_autocorrectMaxDistance) > m_autocorrectMaxDistance) {
        return false;
    }

    QTextCursor edit = cursor;
    edit.beginEditBlock();
    edit.setPosition(start);
    edit.setPosition(pos, QTextCursor::KeepAnchor);
    edit.insertText(corrected + QLatin1Char(' '));
    edit.endEditBlock();
    m_input->setTextCursor(edit);

    m_autocorrect = {true, start, word, corrected};
    m_autocorrectSuppress.clear();
    return true;
}

bool maxchat::ui::MainWindow::undoAutocorrect() {
    if (!m_autocorrect.active || m_input == nullptr) {
        return false;
    }
    QTextCursor cursor = m_input->textCursor();
    const int pos = cursor.position();
    // Only undo if the caret is still right after "<corrected> " untouched.
    const int expectedEnd = m_autocorrect.wordStart +
                            static_cast<int>(m_autocorrect.corrected.size()) + 1;
    if (cursor.hasSelection() || pos != expectedEnd) {
        m_autocorrect.active = false;
        return false;
    }

    cursor.beginEditBlock();
    cursor.setPosition(m_autocorrect.wordStart);
    cursor.setPosition(pos, QTextCursor::KeepAnchor);
    cursor.insertText(m_autocorrect.original);
    cursor.endEditBlock();
    m_input->setTextCursor(cursor);

    m_autocorrectSuppress = m_autocorrect.original;
    m_autocorrect.active = false;
    return true;
}

void maxchat::ui::MainWindow::buildMenus() {
    const QVariantMap initialSettings = m_settings.loadWithDefaults();

    auto* serverMenu = menuBar()->addMenu(tr("&Server"));
    QAction* serverListAction =
        serverMenu->addAction(tr("Server List..."), this, &MainWindow::openServerList);
    serverListAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    serverMenu->addAction(tr("Quick Connect..."), this, &MainWindow::openQuickConnect);
    serverMenu->addAction(tr("Disconnect"), this,
                          &MainWindow::disconnectFromCurrentServer);
    serverMenu->addAction(tr("Disconnect"), this,
                          &MainWindow::disconnectFromCurrentServer);
    serverMenu->addAction(tr("Reconnect"), this,
                          &MainWindow::reconnectCurrentServer);
    serverMenu->addSeparator();
    QAction* joinAction =
        serverMenu->addAction(tr("Join..."), this, &MainWindow::openJoinDialog);
    joinAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
    serverMenu->addAction(tr("Leave Channel"), this, &MainWindow::leaveCurrentChannel);
    serverMenu->addAction(tr("Leave All Channels"), this,
                          [this]() { leaveAllChannels(QString()); });
    serverMenu->addSeparator();
    QAction* channelListAction = serverMenu->addAction(tr("Channels..."), this,
                                                       [this]() { openChannelList(false); });
    serverMenu->addSeparator();
    serverMenu->addAction(tr("Quit"), this, &MainWindow::quitApplication);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    m_buttonBarAction = viewMenu->addAction(tr("Button Bar"));
    m_buttonBarAction->setCheckable(true);
    m_buttonBarAction->setChecked(true);
    connect(m_buttonBarAction, &QAction::toggled, this,
            [this](const bool visible) { setButtonBarVisible(visible, true); });
    viewMenu->addSeparator();
    m_serverListVisibleAction = viewMenu->addAction(tr("Server List"));
    m_serverListVisibleAction->setCheckable(true);
    m_serverListVisibleAction->setChecked(true);
    connect(m_serverListVisibleAction, &QAction::toggled, this,
            [this](const bool visible) { setServerListVisible(visible, true); });
    m_membersVisibleAction = viewMenu->addAction(tr("Member List"));
    m_membersVisibleAction->setCheckable(true);
    m_membersVisibleAction->setChecked(true);
    connect(m_membersVisibleAction, &QAction::toggled, this,
            [this](const bool visible) { setMembersVisible(visible, true); });
    m_buttonsAsTabsAction = viewMenu->addAction(tr("Buttons as Tabs"));
    m_buttonsAsTabsAction->setCheckable(true);
    m_buttonsAsTabsAction->setChecked(
        initialSettings.value(QStringLiteral("buffer_tabs"), false).toBool());
    connect(m_buttonsAsTabsAction, &QAction::toggled, this,
            [this](const bool visible) { setBufferTabsVisible(visible, true); });
    m_chatSeparatorAction = viewMenu->addAction(tr("Chat Separator"));
    m_chatSeparatorAction->setCheckable(true);
    m_chatSeparatorAction->setChecked(true);
    connect(m_chatSeparatorAction, &QAction::toggled, this,
            [this](const bool visible) { setChatSeparatorVisible(visible, true); });
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Clear Current Chat"), this, &MainWindow::clearCurrentChat);
    viewMenu->addAction(tr("Mark All Read"), this, &MainWindow::markAllRead);

    auto* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    auto* findAction =
        toolsMenu->addAction(tr("Find in Chat..."), this, &MainWindow::openChatFind);
    findAction->setShortcut(QKeySequence::Find);
    toolsMenu->addAction(tr("Open Log Folder"), this, [this]() {
        const QString logDir =
            QDir(m_settings.paths().configDir).filePath(QStringLiteral("logs"));
        QDir().mkpath(logDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(logDir));
    });
    toolsMenu->addSeparator();
    QAction* urlListAction =
        toolsMenu->addAction(tr("URL List..."), this, &MainWindow::openUrlList);
    toolsMenu->addAction(tr("Raw Log..."), this, &MainWindow::openRawLog);
    toolsMenu->addSeparator();
    m_doNotDisturbAction = toolsMenu->addAction(tr("Do Not Disturb"));
    m_doNotDisturbAction->setCheckable(true);
    m_doNotDisturbAction->setChecked(initialSettings.value(QStringLiteral("dnd"), false).toBool());
    m_doNotDisturbAction->setToolTip(
        QStringLiteral("Suppress toast, flash, tray, beep, and sound notifications."));
    connect(m_doNotDisturbAction, &QAction::toggled, this, [this](const bool enabled) {
        QVariantMap settings = m_settings.loadWithDefaults();
        settings.insert(QStringLiteral("dnd"), enabled);
        if (!m_settings.saveRaw(settings)) {
            appendSystemLine(tr("! Could not save Do Not Disturb."));
            return;
        }
        appendSystemLine(enabled ? QStringLiteral("! Do Not Disturb enabled.")
                                 : QStringLiteral("! Do Not Disturb disabled."));
    });

    auto* settingsMenu = menuBar()->addMenu(tr("&Settings"));
    QAction* prefsAction = settingsMenu->addAction(tr("Preferences..."), this,
                                                   &MainWindow::openPreferences);
    prefsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    settingsMenu->addAction(tr("Ignore List..."), this, &MainWindow::openIgnoreList);
    settingsMenu->addAction(tr("Aliases..."), this, &MainWindow::openAliases);
    settingsMenu->addSeparator();
    QMenu* themesMenu = settingsMenu->addMenu(tr("Themes"));
    QMenu* themeMenu = themesMenu->addMenu(tr("Theme"));
    auto* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    for (const AppThemeDefinition& theme : appThemes()) {
        QAction* action = themeMenu->addAction(theme.label);
        action->setCheckable(true);
        action->setData(theme.id);
        themeGroup->addAction(action);
        m_appearance->registerThemeAction(action);
        connect(action, &QAction::triggered, this, [this, id = theme.id]() { m_appearance->setTheme(id, true); });
    }
    QMenu* chatThemeMenu = themesMenu->addMenu(tr("Chat Theme"));
    auto* chatThemeGroup = new QActionGroup(this);
    chatThemeGroup->setExclusive(true);
    for (const ChatThemeDefinition& theme : chatThemes()) {
        QAction* action = chatThemeMenu->addAction(theme.label);
        action->setCheckable(true);
        action->setData(theme.id);
        chatThemeGroup->addAction(action);
        m_appearance->registerChatThemeAction(action);
        connect(action, &QAction::triggered, this,
                [this, id = theme.id]() { m_appearance->setChatTheme(id, true); });
    }
    QMenu* wallpaperMenu = themesMenu->addMenu(tr("Wallpaper"));
    auto* wallpaperGroup = new QActionGroup(this);
    wallpaperGroup->setExclusive(true);
    for (const WallpaperDefinition& wallpaper : wallpaperChoices()) {
        QAction* action = wallpaperMenu->addAction(wallpaper.label);
        action->setCheckable(true);
        action->setData(wallpaper.value);
        wallpaperGroup->addAction(action);
        m_appearance->registerWallpaperAction(action);
        connect(action, &QAction::triggered, this,
                [this, value = wallpaper.value]() { m_appearance->setWallpaper(value, true); });
    }
    wallpaperMenu->addSeparator();
    wallpaperMenu->addAction(tr("Load Image..."), this, [this]() {
        const QString wallPath =
            QFileDialog::getOpenFileName(this, QStringLiteral("Choose Wallpaper"), QString(),
                                         QStringLiteral("Images (*.png *.jpg *.jpeg *.webp)"));
        if (!wallPath.isEmpty()) {
            m_appearance->setWallpaper(wallPath, true);
        }
    });
    m_looksMenu = themesMenu->addMenu(tr("Saved Looks"));
    rebuildLooksMenu();
    settingsMenu->addSeparator();
    settingsMenu->addAction(tr("Keyboard Shortcuts..."), this,
                            &MainWindow::openShortcutEditor);
    settingsMenu->addAction(tr("Friends / Notify..."), this,
                            &MainWindow::openFriendsNotify);
    settingsMenu->addAction(tr("Scripts..."), this, &MainWindow::openScriptsManager);
    QAction* transfersAction =
        settingsMenu->addAction(tr("File Transfers..."), this,
                                &MainWindow::openDccTransfers);
    settingsMenu->addSeparator();
    settingsMenu->addAction(tr("Import Settings..."), this,
                            &MainWindow::importSettings);
    settingsMenu->addAction(tr("Export Settings..."), this,
                            &MainWindow::exportSettings);
    settingsMenu->addAction(tr("Reset Server List"), this,
                            &MainWindow::resetServerList);

    auto* comicMenu = menuBar()->addMenu(tr("&Comic"));
    m_comicModeAction = comicMenu->addAction(tr("Comic Mode"));
    m_comicModeAction->setCheckable(true);
    m_comicModeAction->setEnabled(false); // per channel — enabled on buffer switch
    connect(m_comicModeAction, &QAction::toggled, m_comicController, &ComicController::setComicMode);
    QAction* comicModeAction = m_comicModeAction;
    comicModeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    QAction* emotionAction =
        comicMenu->addAction(tr("Emotion..."), this, &MainWindow::openEmotionPicker);
    comicMenu->addSeparator();
    comicMenu->addAction(tr("Comic Settings..."), m_comicController, &ComicController::openComicSettings);
    comicMenu->addAction(tr("Browse Characters..."), this,
                         &MainWindow::openCharacterGallery);
    comicMenu->addAction(tr("Save Comic..."), m_comicController, &ComicController::saveComic);
    comicMenu->addSeparator();
    m_comicCaptionsAction = comicMenu->addAction(tr("Character Names"));
    m_comicCaptionsAction->setCheckable(true);
    m_comicCaptionsAction->setChecked(
        initialSettings.value(QStringLiteral("comic_captions"), true).toBool());
    connect(m_comicCaptionsAction, &QAction::toggled, this, [this](const bool enabled) {
        QVariantMap settings = m_settings.loadWithDefaults();
        settings.insert(QStringLiteral("comic_captions"), enabled);
        if (!m_settings.saveRaw(settings)) {
            appendSystemLine(tr("! Could not save comic caption setting."));
            return;
        }
        statusBar()->showMessage(enabled ? QStringLiteral("Character names on.")
                                         : QStringLiteral("Character names off."));
        m_comicController->refreshComic();
    });

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* helpAction = helpMenu->addAction(tr("Commands && Shortcuts..."), this,
                                              &MainWindow::openCommandHelp);
    helpMenu->addAction(tr("Comic Chat Guide..."), m_comicController,
                        &ComicController::openComicHelp);
    helpMenu->addAction(tr("Theme Builder..."), this, &MainWindow::openThemeBuilder);
    helpAction->setShortcut(QKeySequence(Qt::Key_F1));
    helpMenu->addSeparator();
    helpMenu->addAction(tr("Check for Updates..."), this,
                        [this]() { checkForUpdates(/*manual=*/true); });
    helpMenu->addAction(tr("About"), this, &MainWindow::openAbout);

    m_buttonBar = addToolBar(QStringLiteral("Button Bar"));
    m_buttonBar->setObjectName(QStringLiteral("mainToolbar"));
    m_buttonBar->setMovable(false);
    m_buttonBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_buttonBar->addAction(m_serverListVisibleAction);
    m_buttonBar->addAction(m_membersVisibleAction);
    m_buttonBar->addSeparator();
    m_buttonBar->addAction(channelListAction);
    m_buttonBar->addAction(joinAction);
    m_buttonBar->addSeparator();
    m_buttonBar->addAction(comicModeAction);
    m_buttonBar->addAction(emotionAction);
    m_buttonBar->addSeparator();
    m_buttonBar->addAction(urlListAction);
    m_buttonBar->addAction(transfersAction);
    m_buttonBar->addAction(prefsAction);
    const auto setToolbarLabel = [this](QAction* action, const QString& label) {
        if (auto* button = m_buttonBar->widgetForAction(action); button != nullptr) {
            button->setProperty("text", label);
            if (auto* toolButton = qobject_cast<QToolButton*>(button); toolButton != nullptr) {
                toolButton->setText(label);
            }
        }
    };
    const auto reapplyToolbarLabels = [this, setToolbarLabel, channelListAction, joinAction,
                                       comicModeAction, emotionAction, urlListAction,
                                       transfersAction, prefsAction]() {
        setToolbarLabel(m_serverListVisibleAction, QStringLiteral("Servers"));
        setToolbarLabel(m_membersVisibleAction, QStringLiteral("Members"));
        setToolbarLabel(channelListAction, QStringLiteral("Channels"));
        setToolbarLabel(joinAction, QStringLiteral("Join"));
        setToolbarLabel(comicModeAction, QStringLiteral("Comic"));
        setToolbarLabel(emotionAction, QStringLiteral("Emotion"));
        setToolbarLabel(urlListAction, QStringLiteral("URLs"));
        setToolbarLabel(transfersAction, QStringLiteral("Transfers"));
        setToolbarLabel(prefsAction, QStringLiteral("Prefs"));
    };
    reapplyToolbarLabels();
    connect(m_serverListVisibleAction, &QAction::changed, this, reapplyToolbarLabels);
    connect(m_membersVisibleAction, &QAction::changed, this, reapplyToolbarLabels);
}

void maxchat::ui::MainWindow::buildLayout() {
    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("centralRoot"));
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(4);

    m_topicLabel = new QLabel(QString(), root); // topic bar starts empty (no channel yet)
    m_topicLabel->setObjectName(QStringLiteral("topicLabel"));
    m_topicLabel->setMinimumHeight(30);
    m_topicLabel->setAlignment(Qt::AlignCenter); // centred in the bar, Python parity
    // The topic is attacker-controlled (anyone can set a channel TOPIC); force
    // plain text so a topic like "<img src=...>" can't render as rich text.
    m_topicLabel->setTextFormat(Qt::PlainText);
    // A long topic must NEVER drive the window width: Ignored horizontal policy
    // means the label takes whatever width the window has; the text is elided
    // to fit (updateTopicElide) and the full topic lives in the tooltip.
    m_topicLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_bufferTabBar = new QTabBar(root);
    m_bufferTabBar->setObjectName(QStringLiteral("bufferTabBar"));
    m_bufferTabBar->setDrawBase(false);
    m_bufferTabBar->setExpanding(false);
    m_bufferTabBar->setUsesScrollButtons(true);
    m_bufferTabBar->setTabsClosable(true); // ✕ on channel/query tabs (removed on roots in syncBufferTabs)
    m_bufferTabBar->setFocusPolicy(Qt::NoFocus);
    m_bufferTabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    m_bufferTabBar->addTab(QStringLiteral("Server"));
    m_bufferTabBar->setVisible(false);
    connect(m_bufferTabBar, &QTabBar::tabBarClicked, this, [this](const int index) {
        QTreeWidgetItem* item = treeItemForTabIndex(index);
        if (item != nullptr && m_networkTree != nullptr) {
            m_networkTree->setCurrentItem(item);
        }
    });
    connect(m_bufferTabBar, &QTabBar::tabCloseRequested, this, [this](const int index) {
        closeBufferTab(index);
    });
    connect(m_bufferTabBar, &QTabBar::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                const int index = m_bufferTabBar->tabAt(pos);
                QTreeWidgetItem* item = treeItemForTabIndex(index);
                if (item == nullptr) {
                    return;
                }
                const QString target = item->data(0, Qt::UserRole).toString();
                const QString network = treeItemNetwork(item).trimmed().isEmpty()
                    ? activeNetworkName() : treeItemNetwork(item).trimmed();
                QMenu menu(this);
                if (isTreeStatusTarget(target)) {
                    menu.addAction(QStringLiteral("Disconnect"), this,
                                   [this, network]() { disconnectNetwork(network); });
                    menu.addAction(QStringLiteral("Reconnect Now"), this,
                                   [this, network]() { reconnectNetwork(network); });
                    menu.addSeparator();
                    menu.addAction(QStringLiteral("Server List..."), this,
                                   &MainWindow::openServerList);
                    menu.addAction(QStringLiteral("Quick Connect..."), this,
                                   &MainWindow::openQuickConnect);
                } else if (!target.isEmpty()) {
                    const QString closeTarget = target;
                    const QString closeNetwork = network;
                    menu.addAction(QStringLiteral("Close"), this,
                                   [this, closeTarget, closeNetwork]() {
                                       // Re-derive index at close time: tab bar may have
                                       // shifted during QMenu::exec() event loop.
                                       for (int i = 0; i < m_bufferTabBar->count(); ++i) {
                                           QTreeWidgetItem* ti = treeItemForTabIndex(i);
                                           if (ti == nullptr) continue;
                                           const QString tiNet = treeItemNetwork(ti).trimmed().isEmpty()
                                               ? activeNetworkName() : treeItemNetwork(ti).trimmed();
                                           if (treeItemTarget(ti) == closeTarget && tiNet == closeNetwork) {
                                               closeBufferTab(i);
                                               return;
                                           }
                                       }
                                   });
                }
                if (!menu.isEmpty()) {
                    menu.exec(m_bufferTabBar->mapToGlobal(pos));
                }
            });

    m_networkTree = new QTreeWidget(root);
    m_networkTree->setObjectName(QStringLiteral("networkTree"));
    m_networkTree->setMinimumWidth(0);
    m_networkTree->setHeaderHidden(true);
    m_networkTree->setContextMenuPolicy(Qt::CustomContextMenu);
    auto* rootItem = newTreeItem(QStringLiteral("Server"), QStringLiteral("server"));
    m_networkTree->addTopLevelItem(rootItem);
    m_networkTree->expandAll();
    m_networkTree->setCurrentItem(rootItem);
    connect(m_networkTree, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::showNetworkTreeContextMenu);
    connect(m_networkTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                // Roots are server-buffer rows; children are channels/queries.
                if (current == nullptr || !m_hasConnectionPlan) {
                    return;
                }

                // Terminal launcher nodes pop their window instead of switching
                // the chat buffer; the session stays alive when hidden.
                const QString terminalId = current->data(0, TreeTerminalRole).toString();
                if (!terminalId.isEmpty()) {
                    const QString termNetwork = treeItemNetwork(current);
                    if (!termNetwork.isEmpty() &&
                        termNetwork.compare(activeNetworkName(), Qt::CaseInsensitive) != 0) {
                        setActiveNetwork(termNetwork);
                    }
                    if (m_scripts != nullptr) {
                        m_scripts->showTerminal(terminalId);
                    }
                    return;
                }

                const QString network = treeItemNetwork(current);
                const QString target = treeItemTarget(current);
                if (target.isEmpty()) {
                    return;
                }

                if (!network.isEmpty() &&
                    network.compare(activeNetworkName(), Qt::CaseInsensitive) != 0) {
                    setActiveNetwork(network);
                }

                if (isTreeStatusTarget(target)) {
                    activateBufferTarget({});
                    updateChannelModeButton();
                    return;
                }

                activateBufferTarget(target);
                showConnectionStatus(
                    QStringLiteral("%1 - %2").arg(m_connectionPlan.networkName, target));
                updateChannelModeButton();
            });

    // Re-clicking an already-selected Term node re-pops a hidden terminal
    // (currentItemChanged won't fire when the selection doesn't change).
    connect(m_networkTree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (item == nullptr || m_scripts == nullptr) {
                    return;
                }
                const QString terminalId = item->data(0, TreeTerminalRole).toString();
                if (!terminalId.isEmpty()) {
                    m_scripts->showTerminal(terminalId);
                }
            });

    auto* chatColumn = new QWidget(root);
    chatColumn->setObjectName(QStringLiteral("chatColumn"));
    auto* chatLayout = new QVBoxLayout(chatColumn);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(4);

    // The chat view + its append primitives live in ChatPane now (render-
    // pipeline R1). MainWindow keeps the buffer model + render orchestration and
    // borrows m_chatView for the find/scroll code not yet moved. Anchor-click +
    // separator-drag route back through ChatPaneDelegate (this). The view's
    // parent is chatColumn (passed to the ChatPane ctor), so it drops into the
    // existing splitter exactly where the raw view used to sit.
    m_chatPane = new ChatPane(chatColumn);
    m_chatPane->setDelegate(this);
    m_chatView = m_chatPane->view();
    m_chatView->setPlainText(
        QStringLiteral("Not connected. Open Server > Server List... or Quick Connect... to get started."));

    m_memberPanel = new QWidget(root);
    m_memberPanel->setObjectName(QStringLiteral("memberPanel"));
    m_memberPanel->setMinimumWidth(0);
    auto* memberPanelLayout = new QVBoxLayout(m_memberPanel);
    memberPanelLayout->setContentsMargins(0, 0, 0, 0);
    memberPanelLayout->setSpacing(4);

    m_channelModesButton = new QPushButton(QStringLiteral("Channel Modes..."), m_memberPanel);
    m_channelModesButton->setObjectName(QStringLiteral("channelModesButton"));
    m_channelModesButton->setEnabled(false);
    connect(m_channelModesButton, &QPushButton::clicked, this, &MainWindow::openChannelModes);

    m_membersHeader = new QLabel(QStringLiteral("Members"), m_memberPanel);
    m_membersHeader->setObjectName(QStringLiteral("membersHeader"));
    m_memberList = new QListWidget(m_memberPanel);
    m_memberList->setObjectName(QStringLiteral("memberList"));
    m_memberList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_memberList, &QListWidget::customContextMenuRequested, this,
            &MainWindow::showMemberContextMenu);
    connect(m_memberList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item == nullptr || item->data(Qt::UserRole + 1).toBool()) {
            return;
        }
        openQueryForNick(nickWithoutPrefix(item->text()));
    });
    memberPanelLayout->addWidget(m_channelModesButton);
    memberPanelLayout->addWidget(m_membersHeader);
    memberPanelLayout->addWidget(m_memberList, 1);

    m_input = new SpellTextEdit(root);
    m_input->setObjectName(QStringLiteral("messageInput"));
    m_input->setPlaceholderText(QStringLiteral("Message"));
    m_input->setAcceptRichText(false);
    m_input->setLineWrapMode(QTextEdit::NoWrap);
    m_input->setTabChangesFocus(false);
    m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_input->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_input->document()->setDocumentMargin(4);
    resizeMessageInput();
    connect(m_input->document(), &QTextDocument::blockCountChanged, this,
            [this](int) { resizeMessageInput(); });
    m_input->installEventFilter(this);
    // QTextEdit is a scroll area: mouse-driven context-menu events go to the
    // viewport, not the widget, so the viewport must be filtered too or
    // right-click spelling suggestions never fire.
    m_input->viewport()->installEventFilter(this);
    // Global key redirect: typing anywhere in the main window jumps to the
    // message box. Installed on QApplication so every widget inside this
    // window's hierarchy is covered (see redirectKeyToInput for the guards
    // that keep menus, dialogs, and interactive widgets unaffected).
    qApp->installEventFilter(this);
    connect(m_input, &SpellTextEdit::imageReceived, this,
            [this](const QImage& image) { m_media->uploadImage(image); });

    // mIRC formatting: Ctrl+B/I/U insert the control codes, Ctrl+K opens the
    // colour picker. Widget-scoped so they only fire while typing.
    const auto addFormattingShortcut = [this](const QString& key, const ushort code) {
        auto* shortcut = new QShortcut(QKeySequence(key), m_input);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, code]() {
            QTextCursor cursor = m_input->textCursor();
            if (cursor.hasSelection()) {
                // Wrap the selection — inserting over it would DELETE the text.
                const QString selected = cursor.selectedText();
                cursor.insertText(QString(QChar(code)) + selected + QString(QChar(code)));
            } else {
                cursor.insertText(QString(QChar(code)));
            }
        });
    };
    addFormattingShortcut(QStringLiteral("Ctrl+B"), 0x02);
    addFormattingShortcut(QStringLiteral("Ctrl+I"), 0x1D);
    addFormattingShortcut(QStringLiteral("Ctrl+U"), 0x1F);
    addFormattingShortcut(QStringLiteral("Ctrl+R"), 0x16); // reverse
    addFormattingShortcut(QStringLiteral("Ctrl+O"), 0x0F); // reset/plain
    auto* colorShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), m_input);
    colorShortcut->setContext(Qt::WidgetShortcut);
    connect(colorShortcut, &QShortcut::activated, this, [this]() {
        ColorPickerDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedCode().isEmpty()) {
            QTextCursor cursor = m_input->textCursor();
            if (cursor.hasSelection()) {
                const QString selected = cursor.selectedText();
                cursor.insertText(QString(QChar(0x03)) + dialog.selectedCode() + selected +
                                  QString(QChar(0x03)));
            } else {
                cursor.insertText(QString(QChar(0x03)) + dialog.selectedCode());
            }
            m_input->setFocus();
        }
    });

    // Comic panels sit ABOVE the chat in a vertical splitter so the chat stays
    // visible beneath them when Comic Mode is on (MS Comic Chat style). ChatPane
    // owns the splitter now (render-pipeline R4); MainWindow still owns the
    // ComicView + its art pipeline (refreshComic/saveComic) and injects it.
    m_comicView = new ComicView(chatColumn);
    connect(m_comicView, &ComicView::saveRequested, m_comicController, &ComicController::saveComic);
    m_chatPane->setComicWidget(m_comicView);
    chatLayout->addWidget(m_chatPane, 1);
    m_audioBar = new AudioPlayerBar(chatColumn);
    chatLayout->addWidget(m_audioBar);
    m_media->setAudioBar(m_audioBar);
    auto* inputRow = new QWidget(chatColumn);
    auto* inputRowLayout = new QHBoxLayout(inputRow);
    inputRowLayout->setContentsMargins(0, 0, 0, 0);
    inputRowLayout->setSpacing(4);
    m_nickLabel = new QLabel(inputRow);
    m_nickLabel->setObjectName(QStringLiteral("nickLabel"));
    m_nickLabel->setVisible(false);
    inputRowLayout->addWidget(m_nickLabel);
    inputRowLayout->addWidget(m_input, 1);
    chatLayout->addWidget(inputRow);

    m_mainSplitter = new QSplitter(Qt::Horizontal, root);
    m_mainSplitter->setObjectName(QStringLiteral("mainSplitter"));
    m_mainSplitter->addWidget(m_networkTree);
    m_mainSplitter->addWidget(chatColumn);
    m_mainSplitter->addWidget(m_memberPanel);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setChildrenCollapsible(true);
    m_mainSplitter->setCollapsible(0, true);
    m_mainSplitter->setCollapsible(2, true);
    m_mainSplitter->setSizes({180, 720, 190});
    connect(m_mainSplitter, &QSplitter::splitterMoved, this,
            [this]() { syncPanelActionsFromSplitter(true); });

    rootLayout->addWidget(m_topicLabel);
    rootLayout->addWidget(m_bufferTabBar);
    rootLayout->addWidget(m_mainSplitter, 1);

    setCentralWidget(root);
    statusBar()->showMessage(
        QStringLiteral("Not connected - Server > Server List... or Quick Connect..."));
}

void maxchat::ui::MainWindow::openServerList() {
    const bool mergedDefaults = m_settings.mergeDefaultNetworks();
    QVariantMap settings = m_settings.loadWithDefaults();
    auto networks =
        maxchat::core::networkConfigListFromVariant(settings.value(QStringLiteral("networks")));

    ServerListDialog dialog(networks, settings.value(QStringLiteral("connect_on_start")).toBool(),
                            this);
    attachGeometryPersist(&dialog, m_settings, QStringLiteral("geom_server_list"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    settings.insert(QStringLiteral("networks"),
                    maxchat::core::networkConfigListToVariantList(dialog.networks()));
    settings.insert(QStringLiteral("connect_on_start"), dialog.connectOnStart());
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save server list."));
        return;
    }

    appendSystemLine(
        QStringLiteral("! Server list saved (%1 networks).").arg(dialog.networks().size()));
    // Networks deleted from the list must not keep zombie IrcConnection
    // objects (and possibly live sockets) around until app exit.
    {
        QSet<QString> keep;
        for (const auto& net : dialog.networks()) {
            keep.insert(net.value(QStringLiteral("name")).toString().trimmed().toLower());
        }
        for (auto it = m_connectionsByNetwork.begin(); it != m_connectionsByNetwork.end();) {
            // Map keys are trimmed but case-preserving; compare folded.
            if (!keep.contains(it.key().toLower()) && it.value() != &m_connection) {
                it.value()->disconnectFromServer();
                it.value()->deleteLater();
                it = m_connectionsByNetwork.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (dialog.connectWasRequested()) {
        if (mergedDefaults) {
            appendSystemLine(tr("! Server list was updated with bundled defaults."));
        }
        const auto network = dialog.selectedNetwork();
        startConnection(network);
    }
}

void maxchat::ui::MainWindow::openQuickConnect() {
    QuickConnectDialog dialog(this);
    attachGeometryPersist(&dialog, m_settings, QStringLiteral("geom_quick_connect"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    startConnection(dialog.network());
}

void maxchat::ui::MainWindow::openJoinDialog() {
    bool accepted = false;
    const QString channel =
        QInputDialog::getText(this, QStringLiteral("Join Channel"), QStringLiteral("Channel"),
                              QLineEdit::Normal, QString(), &accepted);
    if (!accepted || channel.trimmed().isEmpty()) {
        return;
    }
    sendCommandOrMessage(QStringLiteral("/join %1").arg(channel.trimmed()));
}

void maxchat::ui::MainWindow::openPreferences() {
    const QString scriptsDir =
        QDir(m_settings.paths().configDir).filePath(QStringLiteral("scripts"));
    const QStringList loadedScripts = m_scripts->loadedScripts();
    const QString soundsDir =
        QDir(m_settings.paths().configDir).filePath(QStringLiteral("sounds"));
    PreferencesDialog dialog(m_settings.loadWithDefaults(), loadedScripts, scriptsDir, soundsDir, this);
    attachGeometryPersist(&dialog, m_settings, QStringLiteral("geom_preferences"));
    connect(&dialog, &PreferencesDialog::exportSettingsRequested, this,
            &MainWindow::exportSettings);
    connect(&dialog, &PreferencesDialog::importSettingsRequested, this, [this, &dialog]() {
        dialog.reject();
        importSettings();
    });
    connect(&dialog, &PreferencesDialog::resetServerListRequested, this, [this, &dialog]() {
        dialog.reject();
        resetServerList();
    });
    connect(&dialog, &PreferencesDialog::resetAllSettingsRequested, this, [this, &dialog]() {
        dialog.reject();
        resetAllSettings();
    });
    connect(&dialog, &PreferencesDialog::openComicSettingsRequested, m_comicController,
            &ComicController::openComicSettings);
    connect(&dialog, &PreferencesDialog::browseCharactersRequested, this,
            &MainWindow::openCharacterGallery);
    connect(&dialog, &PreferencesDialog::loadScriptRequested, this,
            [this, &dialog](const QString& name) {
                handleScriptsCommand(QStringLiteral("load"), name);
                dialog.refreshScriptList(m_scripts->loadedScripts());
            });
    connect(&dialog, &PreferencesDialog::unloadScriptRequested, this,
            [this, &dialog](const QString& name) {
                handleScriptsCommand(QStringLiteral("unload"), name);
                dialog.refreshScriptList(m_scripts->loadedScripts());
            });
    connect(&dialog, &PreferencesDialog::reloadScriptRequested, this,
            [this, &dialog](const QString& name) {
                handleScriptsCommand(QStringLiteral("reload"), name);
                dialog.refreshScriptList(m_scripts->loadedScripts());
            });
    connect(&dialog, &PreferencesDialog::editScriptRequested, this,
            [](const QString& path) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            });
    connect(&dialog, &PreferencesDialog::scriptPermissionChanged, this,
            [this, &dialog](const QString& name, const QVariantMap& newPerms) {
                QVariantMap allPerms =
                    m_settings.loadRaw().value(QStringLiteral("scriptPerms")).toMap();
                allPerms.insert(name, newPerms);
                (void)m_settings.setValue(QStringLiteral("scriptPerms"), allPerms);
                // A loaded script picks up the new permissions immediately.
                m_scripts->reapplyPermissions(name);
            });
    connect(&dialog, &PreferencesDialog::testNotificationRequested, this, [this, &dialog]() {
        // Use the dialog's current (unsaved) settings, not the cached m_notify*
        QVariantMap testSettings = dialog.settings();
        const QString style = testSettings.value(QStringLiteral("notify_popup"), QStringLiteral("custom")).toString();
        const bool flash = testSettings.value(QStringLiteral("notify_flash"), true).toBool();
        const bool sound = testSettings.value(QStringLiteral("notify_sound"), false).toBool();
        const int durationMs = testSettings.value(QStringLiteral("notify_duration"), 6).toInt() * 1000;
        const QString corner = testSettings.value(QStringLiteral("notify_corner"), QStringLiteral("br")).toString();
        const QString theme = testSettings.value(QStringLiteral("notify_theme"), QStringLiteral("follow")).toString();

        // Don't notify if window is active or DND
        if (isActiveWindow()) return;
        if (testSettings.value(QStringLiteral("dnd"), false).toBool()) return;

        if (flash) QApplication::alert(this, 0);
        if (sound) {
            const QString soundsDir =
                QDir(m_settings.paths().configDir).filePath(QStringLiteral("sounds"));
            const QString bundled = QDir(QCoreApplication::applicationDirPath())
                                        .filePath(QStringLiteral("assets/sounds"));
            const QString selectedSound = testSettings.value(QStringLiteral("notify_sound_file"), QStringLiteral("notify.wav")).toString();
            if (!m_soundPlayer.play(notifySoundPath(soundsDir, bundled, selectedSound))) {
                QApplication::beep();
            }
        }

        if (style == QLatin1String("off")) return;

        // OS native - post through the visible tray icon.
        if (style == QLatin1String("system") && m_osNotifyAvailable && m_tray != nullptr) {
            m_tray->showMessage(
                QStringLiteral("Test \u00b7 MaxChat"),
                QStringLiteral("This is what a notification looks like \u2014 click to open the chat."),
                m_tray->icon(), 5000);
            return;
        }

        // Custom toast
        const AppThemeDefinition& themeDef = m_appearance->appTheme();
        QColor followBg = themeDef.panel.isValid() ? themeDef.panel : QColor(QStringLiteral("#2b2b2b"));
        QColor followFg = themeDef.text.isValid() ? themeDef.text : QColor(QStringLiteral("#e8e8e8"));
        QColor followAccent = themeDef.accent.isValid() ? themeDef.accent : QColor(QStringLiteral("#4a9eff"));

        QColor bg = Notifier::paletteBg(theme, followBg);
        QColor fg = Notifier::paletteFg(theme, followFg);
        QColor accent = Notifier::paletteAccent(theme, followAccent);

        QIcon icon = ui::AppIcon::makeIcon(
            testSettings.value(QStringLiteral("tray_icon"), QStringLiteral("bubble")).toString(), accent);

        m_notifier->show(
            QStringLiteral("Test \u00b7 MaxChat"),
            QStringLiteral("This is what a notification looks like \u2014 click to open the chat."),
            bg, fg, accent, corner, durationMs, icon);
    });
    // Live theme preview: combo changes in the Themes tab restyle the app
    // immediately (not saved); Cancel below restores the saved look.
    connect(&dialog, &PreferencesDialog::themePreviewRequested, this,
            [this](const QString& app, const QString& chat, const QString& wallpaper,
                   int chatOpacity) {
                m_appearance->setChatOpacity(chatOpacity);
                m_appearance->setTheme(app, false);
                m_appearance->setChatTheme(chat, false);
                m_appearance->setWallpaper(wallpaper, false);
            });
    if (dialog.exec() != QDialog::Accepted) {
        const QVariantMap saved = m_settings.loadWithDefaults();
        m_appearance->setChatOpacity(saved.value(QStringLiteral("chat_opacity"), 100).toInt());
        m_appearance->setTheme(saved.value(QStringLiteral("theme"), QStringLiteral("synthwave")).toString(),
                 false);
        m_appearance->setChatTheme(
            saved.value(QStringLiteral("chat_theme"), QStringLiteral("follow")).toString(),
            false);
        m_appearance->setWallpaper(saved.value(QStringLiteral("wallpaper")).toString(), false);
        return;
    }

    // Merge only the keys the user actually CHANGED over a fresh load. Saving
    // the dialog's open-time snapshot wholesale reverted everything written
    // while it was open: window geometry (geom_* save on finished, which fires
    // before exec() returns), Comic Settings opened from inside Preferences,
    // and IRC-driven keys like nick_width_autoset.
    QVariantMap merged = m_settings.loadWithDefaults();
    const QVariantMap changed = dialog.changedSettings();
    for (auto it = changed.constBegin(); it != changed.constEnd(); ++it) {
        merged.insert(it.key(), it.value());
    }
    if (!m_settings.saveRaw(merged)) {
        appendSystemLine(tr("! Could not save preferences."));
        return;
    }
    applyCurrentSettings();
    appendSystemLine(tr("! Preferences saved."));
}

void maxchat::ui::MainWindow::openAliases() {
    AliasEditorDialog dialog(m_commandAliases, {}, this);
    attachGeometryPersist(&dialog, m_settings, QStringLiteral("geom_aliases"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_commandAliases = dialog.aliases();
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("command_aliases"), m_commandAliases);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save command aliases."));
        return;
    }
    appendSystemLine(
        QStringLiteral("! Command aliases saved (%1 aliases).").arg(m_commandAliases.size()));
}

void maxchat::ui::MainWindow::openIgnoreList() {
    IgnoreListDialog dialog(
        m_ignoreMasks,
        [this](const QStringList& masks) {
            QStringList normalized;
            for (const QString& mask : masks) {
                const QString value = normalizeIgnoreMask(mask);
                if (!value.isEmpty() && !containsCaseInsensitive(normalized, value)) {
                    normalized.append(value);
                }
            }
            m_ignoreMasks = normalized;
            QVariantMap settings = m_settings.loadWithDefaults();
            settings.insert(QStringLiteral("ignores"), m_ignoreMasks);
            if (!m_settings.saveRaw(settings)) {
                appendSystemLine(tr("! Could not save ignore list."));
                return;
            }
            m_connection.setIgnoreMasks(m_ignoreMasks);
            for (auto* irc : std::as_const(m_connectionsByNetwork)) {
                if (irc != nullptr) {
                    irc->setIgnoreMasks(m_ignoreMasks);
                }
            }
            appendSystemLine(
                QStringLiteral("! Ignore list saved (%1 masks).").arg(m_ignoreMasks.size()));
        },
        this);
    attachGeometryPersist(&dialog, m_settings, QStringLiteral("geom_ignore_list"));
    dialog.exec();
}

void maxchat::ui::MainWindow::openFriendsNotify() {
    FriendsNotifyDialog dialog(
        m_friendNicks,
        [this](const QStringList& friends) {
            QStringList cleaned;
            for (const QString& nick : friends) {
                const QString value = nickWithoutPrefix(nick).trimmed();
                if (!value.isEmpty() && !containsCaseInsensitive(cleaned, value)) {
                    cleaned.append(value);
                }
            }
            m_friendNicks = cleaned;
            QVariantMap settings = m_settings.loadWithDefaults();
            settings.insert(QStringLiteral("friends"), m_friendNicks);
            if (!m_settings.saveRaw(settings)) {
                appendSystemLine(tr("! Could not save notify list."));
                return;
            }
            m_onlineFriends.clear();
            m_haveFriendSnapshot = false;
            if (m_friendNicks.isEmpty()) {
                m_friendPollTimer.stop();
            } else {
                pollFriends();
                if (anyNetworkConnectionIsConnected()) {
                    m_friendPollTimer.start();
                }
            }
            appendSystemLine(
                QStringLiteral("! Notify list saved (%1 nicks).").arg(m_friendNicks.size()));
        },
        this);
    attachGeometryPersist(&dialog, m_settings, QStringLiteral("geom_friends_notify"));
    dialog.exec();
}

void maxchat::ui::MainWindow::openChannelModes() {
    const QString channel = m_currentTarget.trimmed();
    if (!connection().isConnected() || !isChannelTarget(channel)) {
        appendSystemLine(tr("! Select a channel before opening channel modes."));
        return;
    }

    connection().sendRaw(QStringLiteral("MODE %1").arg(channel));
    ChannelModesDialog dialog(
        channel, m_connectionPlan.networkName,
        m_channelModeLines.value(QStringLiteral("%1/%2").arg(activeNetworkName().toCaseFolded(),
                                                             channel.toCaseFolded())),
        [this, channel](const QString& change) { sendModeChange(channel, change); }, this);
    attachGeometryPersist(&dialog, m_settings, QStringLiteral("geom_channel_modes"));
    dialog.exec();
}

void maxchat::ui::MainWindow::openBanList(const QString& channel) {
    const QString target =
        channel.trimmed().isEmpty() ? m_currentTarget.trimmed() : channel.trimmed();
    if (!connection().isConnected() || !isChannelTarget(target)) {
        appendSystemLine(tr("! Select a channel before opening the ban list."));
        return;
    }

    if (m_banListDialog != nullptr) {
        if (m_banListDialog->channel().compare(target, Qt::CaseInsensitive) == 0) {
            m_banListDialog->raise();
            m_banListDialog->activateWindow();
            return;
        }
        m_banListDialog->close();
    }

    m_banListDialog = new BanListDialog(
        target, m_connectionPlan.networkName,
        [this, target](const QString& mask) {
            connection().sendRaw(QStringLiteral("MODE %1 +b %2").arg(target, mask.trimmed()));
        },
        [this, target](const QString& mask) {
            connection().sendRaw(QStringLiteral("MODE %1 -b %2").arg(target, mask.trimmed()));
        },
        this);
    m_banListDialog->setAttribute(Qt::WA_DeleteOnClose);
    attachGeometryPersist(m_banListDialog, m_settings, QStringLiteral("geom_ban_list"));
    m_banListDialog->show();
    m_banListDialog->clearBans();
    m_banListDialog->setStatusText(QStringLiteral("Requesting ban list..."));
    connection().sendRaw(QStringLiteral("MODE %1 +b").arg(target));
}

void maxchat::ui::MainWindow::openChannelList(bool reset) {
    if (m_channelListDialog == nullptr) {
        m_channelListDialog = new ChannelListDialog(this);
        attachGeometryPersist(m_channelListDialog, m_settings, QStringLiteral("geom_channel_list"));
        connect(m_channelListDialog, &ChannelListDialog::joinRequested, this,
                [this](const QString& channel) {
                    sendCommandOrMessage(QStringLiteral("/join %1").arg(channel));
                });
        connect(m_channelListDialog, &ChannelListDialog::listRequested, this, [this]() {
            if (m_channelListDialog == nullptr) { return; }
            m_channelDrainTimer.stop();
            m_pendingChannels.clear();
            m_channelListDialog->clearChannels();
            if (!connection().sendRaw(QStringLiteral("LIST"))) {
                appendSystemLine(tr("! Could not send LIST — not connected."));
                m_channelListDialog->setComplete(true);
            }
        });
    }

    if (reset) {
        m_channelDrainTimer.stop();
        m_pendingChannels.clear();
        m_channelListDialog->clearChannels();
    }
    m_channelListDialog->show();
    m_channelListDialog->raise();
    m_channelListDialog->activateWindow();
}

void maxchat::ui::MainWindow::openChatFind() {
    if (m_chatFindDialog != nullptr) {
        m_chatFindDialog->raise();
        m_chatFindDialog->activateWindow();
        return;
    }

    m_chatFindDialog = new ChatFindDialog(this);
    m_chatFindDialog->setAttribute(Qt::WA_DeleteOnClose);
    attachGeometryPersist(m_chatFindDialog, m_settings, QStringLiteral("geom_chat_find"));
    if (m_chatView != nullptr) {
        const QString selectedText = m_chatView->textCursor().selectedText();
        if (!selectedText.trimmed().isEmpty()) {
            m_chatFindDialog->setSearchText(selectedText);
        }
    }
    connect(m_chatFindDialog, &ChatFindDialog::findNextRequested, this,
            [this](const QString& text, bool caseSensitive, bool wrapSearch) {
                findInChat(text, false, caseSensitive, wrapSearch);
            });
    connect(m_chatFindDialog, &ChatFindDialog::findPreviousRequested, this,
            [this](const QString& text, bool caseSensitive, bool wrapSearch) {
                findInChat(text, true, caseSensitive, wrapSearch);
            });
    m_chatFindDialog->show();
}

void maxchat::ui::MainWindow::openRawLog() {
    if (m_rawLogDialog != nullptr) {
        m_rawLogDialog->raise();
        m_rawLogDialog->activateWindow();
        return;
    }

    m_rawLogDialog = new RawLogDialog(this);
    m_rawLogDialog->setAttribute(Qt::WA_DeleteOnClose);
    attachGeometryPersist(m_rawLogDialog, m_settings, QStringLiteral("geom_raw_log"));
    m_rawLogDialog->setLines(m_rawLogLines);
    connect(m_rawLogDialog, &RawLogDialog::clearRequested, this, [this]() {
        m_rawLogLines.clear();
        appendSystemLine(tr("! Raw log cleared."));
    });
    m_rawLogDialog->show();
}

void maxchat::ui::MainWindow::openUrlList() {
    if (m_urlListDialog != nullptr) {
        m_urlListDialog->raise();
        m_urlListDialog->activateWindow();
        return;
    }

    m_urlListDialog = new UrlListDialog(this);
    m_urlListDialog->setAttribute(Qt::WA_DeleteOnClose);
    attachGeometryPersist(m_urlListDialog, m_settings, QStringLiteral("geom_url_list"));
    m_urlListDialog->setUrls(m_urlList);
    connect(m_urlListDialog, &UrlListDialog::clearRequested, this, [this]() {
        m_urlList.clear();
        appendSystemLine(tr("! URL list cleared."));
    });
    m_urlListDialog->show();
}

void maxchat::ui::MainWindow::openCommandHelp() {
    QMessageBox::information(
        this, tr("Commands & Shortcuts"),
        QStringList{
            tr("Core: /join, /part, /cycle, /msg, /query, /notice, /me, /nick, /topic, "
                           "/names, /list"),
            tr("Info: /whois, /who, /whowas, /lag, /uptime, /netinfo, /ctcp"),
            tr("Local: /alias, /unalias, /ignore, /unignore, /notify, /unnotify, "
                           "/mute, /unmute, /clear, /clearall, /close, /sysinfo"),
            tr("Connection: /connect, /server, /reconnect, /disconnect, /quit, /raw, "
                           "/quote, /away, /back"),
            tr("Shortcuts: Ctrl+S server list, Ctrl+J join, Ctrl+P preferences, Ctrl+F "
                           "find, F1 help"),
        }
            .join(QStringLiteral("\n\n")));
}

void maxchat::ui::MainWindow::openThemeBuilder() {
    // The builder is a standalone HTML page shipped in the themes/ gallery folder
    // (sibling of the binary in a release; repo root in the dev tree). Open it in
    // the user's browser — it's our own bundled asset, not a chat-supplied URL.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("themes/theme-builder.html")),
        QDir(appDir).filePath(QStringLiteral("../themes/theme-builder.html")),
        QDir::current().filePath(QStringLiteral("themes/theme-builder.html")),
    };
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
            return;
        }
    }
    QMessageBox::information(
        this, tr("Theme Builder"),
        tr("The theme builder (themes/theme-builder.html) wasn't found next to the "
           "application. You can still create and customise themes in "
           "Preferences ▸ Themes."));
}

void maxchat::ui::MainWindow::openAbout() {
    QMessageBox::about(
        this, tr("About %1").arg(app::displayName()),
        tr("<b>%1 %2</b><br><br>"
                       "Native C++/Qt port of MaxChat.<br><br>"
                       "A full IRC client: multi-network, server list, link previews, "
                       "inline media, spellcheck, logging, DCC, scripting, and Comic Chat "
                       "mode with themeable balloons.")
            .arg(app::displayName().toHtmlEscaped(), app::version().toHtmlEscaped()));
}

namespace {
// Compare two version strings by their integer components (Python parity:
// pull out every digit-run and compare them in order). Missing trailing
// components count as 0, so "1.0" and "1" are equal. Returns true if
// `remote` is strictly newer than `local`.
bool versionIsNewer(const QString& remote, const QString& local) {
    const QRegularExpression digits(QStringLiteral("\\d+"));
    const auto parts = [&](const QString& s) {
        QList<int> out;
        auto it = digits.globalMatch(s);
        while (it.hasNext()) {
            out.append(it.next().captured(0).toInt());
        }
        return out;
    };
    const QList<int> r = parts(remote);
    const QList<int> l = parts(local);
    const int n = std::max(r.size(), l.size());
    for (int i = 0; i < n; ++i) {
        const int rv = i < r.size() ? r.at(i) : 0;
        const int lv = i < l.size() ? l.at(i) : 0;
        if (rv != lv) {
            return rv > lv;
        }
    }
    return false;
}
} // namespace

void maxchat::ui::MainWindow::checkForUpdates(bool manual) {
    QNetworkRequest req(QUrl(
        QStringLiteral("https://api.github.com/repos/IronWolve/MaxChat/releases/latest")));
    req.setRawHeader("User-Agent", "MaxChat-update-check");
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(15000); // don't hang on a stalled connection
    QNetworkReply* reply = m_updateNetworkManager.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual]() {
        reply->deleteLater();
        const bool ok = reply->error() == QNetworkReply::NoError;
        QString latest;
        QString url = QStringLiteral("https://github.com/IronWolve/MaxChat/releases");
        bool parsed = ok;
        if (ok) {
            // Cap the read — a release body can be arbitrarily large, and we only
            // need tag_name/html_url out of the top of it.
            const QByteArray body = reply->read(256 * 1024);
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                latest = obj.value(QStringLiteral("tag_name")).toString();
                while (latest.startsWith(QLatin1Char('v')) || latest.startsWith(QLatin1Char('V'))) {
                    latest.remove(0, 1);
                }
                // Only follow html_url if it is an https URL — never hand an
                // unexpected scheme to QDesktopServices (matches the allow-list
                // every other openUrl path uses). Otherwise keep the safe default.
                const QUrl html(obj.value(QStringLiteral("html_url")).toString());
                if (html.isValid() && html.scheme().compare(QLatin1String("https"),
                                                            Qt::CaseInsensitive) == 0) {
                    url = html.toString();
                }
            } else {
                parsed = false;
            }
        }
        if (!latest.isEmpty() && versionIsNewer(latest, app::version())) {
            const auto choice = QMessageBox::question(
                this, QStringLiteral("Update available"),
                QStringLiteral("%1 v%2 is available — you have v%3.\n\nOpen the releases page?")
                    .arg(app::displayName(), latest, app::version()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (choice == QMessageBox::Yes) {
                QDesktopServices::openUrl(QUrl(url));
            }
        } else if (manual) {
            const QString msg =
                parsed ? tr("You're on the latest version (v%1).").arg(app::version())
                       : tr("Couldn't check for updates (no releases yet, or no connection).");
            QMessageBox::information(this, tr("Check for Updates"), msg);
        }
    });
}

void maxchat::ui::MainWindow::handleCtcpSound(const QString& network, const QString& sender,
                                              const QString& target, const QString& file,
                                              const QString& text) {
    // Do Not Disturb silences remote-triggered sounds like everything else.
    const bool suppressSound =
        m_settings.loadWithDefaults().value(QStringLiteral("dnd"), false).toBool();
    // A PM SOUND lands under the sender's buffer; a channel SOUND under the
    // channel (Python parity).
    const bool isPm = !isChannelTarget(target);
    const QString buffer = isPm ? sender : (target.isEmpty() ? QStringLiteral("server") : target);

    // Always show the action line — even with playback off or the file missing.
    const QString action =
        !text.isEmpty()
            ? text
            : (file.isEmpty() ? QStringLiteral("plays a sound") : QStringLiteral("plays %1").arg(file));
    appendSystemLineToNetworkTarget(network, buffer,
                                    QStringLiteral("* %1 %2").arg(sender, action));

    // Play only when the feature is on AND the named .wav already exists locally.
    const QVariantMap settings = m_settings.loadWithDefaults();
    if (suppressSound || file.isEmpty() ||
        !settings.value(QStringLiteral("ctcp_sound"), false).toBool()) {
        return;
    }
    const QString soundsDir = QDir(m_settings.paths().configDir).filePath(QStringLiteral("sounds"));
    const QString path = resolveSoundPath(soundsDir, file);
    if (!path.isEmpty()) {
        m_soundPlayer.play(path);
    }
}

void maxchat::ui::MainWindow::insertInput(const QString& text) {
    if (m_input != nullptr) {
        m_input->textCursor().insertText(text);
    }
}

QStringList maxchat::ui::MainWindow::channelsFor(const QString& network) {
    QStringList channels;
    for (const maxchat::core::ChatBufferId& id : m_chatBuffers.buffers()) {
        if (id.kind == maxchat::core::ChatBufferKind::Channel &&
            id.network.compare(network, Qt::CaseInsensitive) == 0) {
            channels.append(id.target);
        }
    }
    return channels;
}

void maxchat::ui::MainWindow::handleScriptsCommand(const QString& command, const QString& arg) {
    if (!ScriptBridge::scriptingAvailable()) {
        appendSystemLine(tr("! This build has no scripting support."));
        return;
    }
    const QString scriptsDir = m_scripts->scriptsDirectory();

    if (command == QStringLiteral("scripts")) {
        const QStringList names = m_scripts->loadedScripts();
        appendSystemLine(names.isEmpty()
                             ? QStringLiteral("* No scripts loaded.")
                             : QStringLiteral("* Loaded scripts: %1").arg(names.join(QStringLiteral(", "))));
        appendSystemLine(tr("* Scripts folder: %1").arg(scriptsDir));
        return;
    }

    if (arg.isEmpty()) {
        appendSystemLine(tr("! Usage: /%1 <script>").arg(command));
        return;
    }
    const QString name = QFileInfo(arg).completeBaseName(); // tolerate "foo" or "foo.lua"
    if (command == QStringLiteral("load")) {
        appendSystemLine(m_scripts->loadByName(name)
                             ? QStringLiteral("* Loaded %1.").arg(name)
                             : QStringLiteral("! Could not load %1.").arg(name));
    } else if (command == QStringLiteral("unload")) {
        appendSystemLine(m_scripts->unloadByName(name) ? QStringLiteral("* Unloaded %1.").arg(name)
                                                       : QStringLiteral("! %1 is not loaded.").arg(name));
    } else if (command == QStringLiteral("reload")) {
        appendSystemLine(m_scripts->reloadByName(name) ? QStringLiteral("* Reloaded %1.").arg(name)
                                                       : QStringLiteral("! Could not reload %1.").arg(name));
    }
}

void maxchat::ui::MainWindow::openScriptsManager() {
    if (!ScriptBridge::scriptingAvailable()) {
        appendSystemLine(tr(
            "! Scripts are unavailable: this build was compiled without scripting support."));
        return;
    }
    const QString scriptsDir = m_scripts->scriptsDirectory();

    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Scripts"));
    dialog->resize(460, 380);
    attachGeometryPersist(dialog, m_settings, QStringLiteral("geom_scripts_manager"));
    auto* root = new QVBoxLayout(dialog);
    root->addWidget(new QLabel(QStringLiteral("Lua scripts in %1").arg(scriptsDir), dialog));
    auto* list = new QListWidget(dialog);
    root->addWidget(list, 1);

    const auto refresh = [this, list, scriptsDir]() {
        list->clear();
        const QStringList loaded = m_scripts->loadedScripts();
        const QFileInfoList files =
            QDir(scriptsDir).entryInfoList({QStringLiteral("*.lua")}, QDir::Files, QDir::Name);
        for (const QFileInfo& fi : files) {
            const QString name = fi.completeBaseName();
            // Show the full filename (foo.lua) so it reads as a Lua script; the
            // base name (used by load/unload) lives in UserRole; the full path
            // lives in UserRole+1 for Edit/Settings.
            const QString display = fi.fileName();
            auto* item = new QListWidgetItem(
                loaded.contains(name) ? QStringLiteral("%1   [loaded]").arg(display) : display);
            item->setData(Qt::UserRole, name);
            item->setData(Qt::UserRole + 1, fi.absoluteFilePath());
            list->addItem(item);
        }
    };
    refresh();

    const auto selectedName = [list]() -> QString {
        const QListWidgetItem* item = list->currentItem();
        return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
    };
    const auto selectedPath = [list]() -> QString {
        const QListWidgetItem* item = list->currentItem();
        return item != nullptr ? item->data(Qt::UserRole + 1).toString() : QString();
    };

    auto* buttons = new QHBoxLayout();
    const auto addButton = [&](const QString& label, std::function<void()> handler) {
        auto* button = new QPushButton(label, dialog);
        connect(button, &QPushButton::clicked, dialog, std::move(handler));
        buttons->addWidget(button);
    };
    addButton(QStringLiteral("Load"), [this, selectedName, refresh]() {
        const QString name = selectedName();
        if (!name.isEmpty()) {
            handleScriptsCommand(QStringLiteral("load"), name);
            refresh();
        }
    });
    addButton(QStringLiteral("Unload"), [this, selectedName, refresh]() {
        const QString name = selectedName();
        if (!name.isEmpty()) {
            handleScriptsCommand(QStringLiteral("unload"), name);
            refresh();
        }
    });
    addButton(QStringLiteral("Reload"), [this, selectedName, refresh]() {
        const QString name = selectedName();
        if (!name.isEmpty()) {
            handleScriptsCommand(QStringLiteral("reload"), name);
            refresh();
        }
    });
    addButton(QStringLiteral("Edit"), [selectedPath]() {
        const QString path = selectedPath();
        if (!path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    addButton(QStringLiteral("Settings"), [this, dialog, selectedName, selectedPath]() {
        const QString name = selectedName();
        const QString path = selectedPath();
        if (name.isEmpty())
            return;

        // Read header comments (lines starting with "--") for script metadata.
        QStringList headerLines;
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            for (int i = 0; i < 30 && !in.atEnd(); ++i) {
                const QString line = in.readLine();
                if (!line.startsWith(QStringLiteral("--")))
                    break;
                const QString text = line.mid(2).trimmed();
                if (!text.isEmpty())
                    headerLines << text;
            }
        }

        auto* info = new QDialog(dialog);
        info->setAttribute(Qt::WA_DeleteOnClose);
        info->setWindowTitle(QStringLiteral("Script: %1").arg(name));
        info->setMinimumWidth(400);
        auto* root = new QVBoxLayout(info);

        auto* form = new QFormLayout();
        form->addRow(QStringLiteral("Name:"), new QLabel(name, info));
        auto* pathLabel = new QLabel(path, info);
        pathLabel->setWordWrap(true);
        form->addRow(QStringLiteral("File:"), pathLabel);
        const bool isLoaded = m_scripts->loadedScripts().contains(name);
        form->addRow(QStringLiteral("Status:"),
                     new QLabel(isLoaded ? QStringLiteral("Loaded") : QStringLiteral("Not loaded"),
                                info));
        root->addLayout(form);

        if (!headerLines.isEmpty()) {
            auto* hdrGroup = new QGroupBox(QStringLiteral("Script header"), info);
            auto* hdrLayout = new QVBoxLayout(hdrGroup);
            for (const QString& line : std::as_const(headerLines))
                hdrLayout->addWidget(new QLabel(line, hdrGroup));
            root->addWidget(hdrGroup);
        }

        auto* closeBtn = new QPushButton(QStringLiteral("Close"), info);
        connect(closeBtn, &QPushButton::clicked, info, &QDialog::accept);
        auto* closeRow = new QHBoxLayout();
        closeRow->addStretch(1);
        closeRow->addWidget(closeBtn);
        root->addLayout(closeRow);
        info->exec();
    });
    addButton(QStringLiteral("Open folder"),
              [scriptsDir]() { QDesktopServices::openUrl(QUrl::fromLocalFile(scriptsDir)); });
    buttons->addStretch(1);
    addButton(QStringLiteral("Close"), [dialog]() { dialog->accept(); });
    root->addLayout(buttons);
    dialog->show();
}

void maxchat::ui::MainWindow::leaveCurrentChannel() {
    const QString channel = m_currentTarget.trimmed();
    if (!connection().isConnected()) {
        appendSystemLine(tr("! Not connected."));
        return;
    }
    if (!isChannelTarget(channel)) {
        appendSystemLine(tr("! Select a channel before leaving."));
        return;
    }
    sendCommandOrMessage(QStringLiteral("/part %1").arg(channel));
}

void maxchat::ui::MainWindow::leaveAllChannels(const QString& network) {
    const QString net = network.trimmed().isEmpty() ? activeNetworkName() : network.trimmed();
    maxchat::irc::IrcConnection* irc = connectionForNetwork(net);
    if (irc == nullptr) {
        irc = &m_connection;
    }
    if (!irc->isConnected()) {
        appendSystemLine(tr("! Not connected."));
        return;
    }
    QStringList channels;
    for (const maxchat::core::ChatBufferId& id : m_chatBuffers.buffersForNetwork(net)) {
        if (id.kind != maxchat::core::ChatBufferKind::Channel || !isChannelTarget(id.target)) {
            continue;
        }
        if (m_chatBuffers.snapshot(id).joined) {
            channels.append(id.target);
        }
    }
    if (channels.isEmpty()) {
        appendSystemLine(tr("! No joined channels on %1.").arg(net));
        return;
    }
    for (const QString& channel : channels) {
        irc->sendRaw(QStringLiteral("PART %1").arg(channel));
    }
    appendSystemLine(
        QStringLiteral("! Leaving %1 channel(s) on %2.").arg(channels.size()).arg(net));
}

bool maxchat::ui::MainWindow::seedReplayForBuffer(const QString& network, const QString& target) {
    if (!m_replayLogEnabled) {
        return false;
    }
    const QString trimmedTarget = target.trimmed();
    if (trimmedTarget.isEmpty() || isTreeStatusTarget(trimmedTarget)) {
        return false; // no resume for the server buffer
    }

    const QString key = network + QChar(0x1f) + trimmedTarget;
    if (m_replayedBuffers.contains(key)) {
        return false;
    }

    const maxchat::core::ChatBufferId id = bufferIdForNetworkTarget(network, trimmedTarget);
    // Seed history only into an empty buffer so it prepends cleanly; never inject
    // it between live messages that already arrived.
    if (!m_chatBuffers.snapshot(id).lines.isEmpty()) {
        m_replayedBuffers.insert(key);
        return false;
    }
    m_replayedBuffers.insert(key);

    // replay_lines == 0 means "a sensible default" (Python uses 50), not
    // "everything" — dumping a whole multi-thousand-line log is slow/unhelpful.
    constexpr int DefaultReplayLines = 50;
    const QStringList lines = m_chatLogStore.recentLines(
        network, trimmedTarget, m_replayLines > 0 ? m_replayLines : DefaultReplayLines);
    if (lines.isEmpty()) {
        return false;
    }

    QDateTime lastWhen;
    int added = 0;
    for (const QString& line : lines) {
        const ReplayLogLine replayLine = parseReplayLogLine(line);
        if (replayLine.body.trimmed().isEmpty()) {
            continue;
        }
        maxchat::core::ChatBufferLine stored;
        stored.timestamp = replayLine.timestamp;
        stored.sourceText = replayLine.body;
        stored.dimmed = true; // renderActiveBuffer formats dimmed history lines
        if (replayLine.timestamp.isValid()) {
            lastWhen = replayLine.timestamp;
        }
        if (m_chatBuffers.appendLine(id, stored)) {
            ++added;
        }
    }
    if (added == 0) {
        return false;
    }

    // A dimmed divider names when the previous session left off (date + time) in
    // the text, so the resume point is unmistakable.
    const QString when =
        lastWhen.isValid()
            ? lastWhen.toString(QStringLiteral("ddd MMM d ")) +
                  lastWhen.toString(qtDateTimeFormat(m_timestampFormat))
            : QString();
    maxchat::core::ChatBufferLine divider;
    divider.sourceText = when.isEmpty()
                           ? QStringLiteral("──────────  Chat ended  ──────────")
                           : QStringLiteral("──────  Chat ended %1  ──────").arg(when);
    divider.dimmed = true;
    divider.systemLine = true; // time is in the text, not the gutter
    (void)m_chatBuffers.appendLine(id, divider);
    return true;
}

void maxchat::ui::MainWindow::markAllRead() {
    const int changed = m_chatBuffers.markAllReadForNetwork(currentLogNetwork());
    updateNetworkTreeLabels();
    statusBar()->showMessage(changed == 1 ? QStringLiteral("Marked 1 chat read.")
                                          : QStringLiteral("Marked %1 chats read.").arg(changed));
}

void maxchat::ui::MainWindow::clearCurrentChat() {
    if (m_chatView != nullptr) {
        m_chatView->clear();
    }
    const bool cleared = m_chatBuffers.clearLines(bufferIdForTarget(m_currentTarget));
    Q_UNUSED(cleared);
    updateNetworkTreeLabels();
    statusBar()->showMessage(tr("Cleared current chat view."));
}

void maxchat::ui::MainWindow::clearAllChats() {
    for (const maxchat::core::ChatBufferId& id :
         m_chatBuffers.buffersForNetwork(currentLogNetwork())) {
        const bool cleared = m_chatBuffers.clearLines(id);
        Q_UNUSED(cleared);
    }
    renderActiveBuffer();
    updateNetworkTreeLabels();
    appendSystemLine(tr("* All chats cleared."));
}

void maxchat::ui::MainWindow::showUptime() {
    QString line =
        QStringLiteral("* MaxChat uptime %1").arg(formatDurationMs(m_appUptime.elapsed()));
    const qint64 startedAt = m_connectionUptimeStartMsByNetwork.value(activeNetworkName(), 0);
    if (startedAt > 0) {
        const qint64 elapsed = std::max<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAt);
        line += QStringLiteral(" - connected to %1 for %2")
                    .arg(m_connectionPlan.networkName, formatDurationMs(elapsed));
    } else if (m_connectionUptimeRunning) {
        line +=
            QStringLiteral(" - connected to %1 for %2")
                .arg(m_connectionPlan.networkName, formatDurationMs(m_connectionUptime.elapsed()));
    }
    appendSystemLine(line);
}

void maxchat::ui::MainWindow::showNetInfo() {
    if (!m_hasConnectionPlan) {
        appendSystemLine(tr("* No network selected."));
        return;
    }

    const QHash<QString, QString> info = connection().isupport();
    const maxchat::irc::ServerEndpoint server =
        maxchat::irc::currentServer(m_connectionPlan.reconnect);
    const QString serverLabel = server.host.trimmed().isEmpty()
                                    ? m_connectionPlan.networkName
                                    : QStringLiteral("%1:%2").arg(server.host).arg(server.port);

    if (info.isEmpty()) {
        appendSystemLine(tr("* %1 (%2): no ISUPPORT received yet")
                             .arg(m_connectionPlan.networkName, serverLabel));
        return;
    }

    const QStringList preferredKeys = {
        QStringLiteral("NETWORK"),    QStringLiteral("CHANTYPES"),   QStringLiteral("PREFIX"),
        QStringLiteral("CHANMODES"),  QStringLiteral("CASEMAPPING"), QStringLiteral("NICKLEN"),
        QStringLiteral("CHANNELLEN"), QStringLiteral("TOPICLEN"),    QStringLiteral("MODES"),
    };

    QStringList preferred;
    for (const QString& key : preferredKeys) {
        if (!info.contains(key)) {
            continue;
        }
        const QString value = info.value(key);
        preferred.append(value.isEmpty() ? key : QStringLiteral("%1=%2").arg(key, value));
    }

    QStringList extraKeys = info.keys();
    extraKeys.sort(Qt::CaseInsensitive);
    for (const QString& key : extraKeys) {
        if (containsCaseInsensitive(preferredKeys, key)) {
            continue;
        }
        const QString value = info.value(key);
        preferred.append(value.isEmpty() ? key : QStringLiteral("%1=%2").arg(key, value));
    }

    appendSystemLine(
        QStringLiteral("* %1 (%2): %3")
            .arg(m_connectionPlan.networkName, serverLabel, preferred.join(QStringLiteral(" - "))));
}

QString MainWindow::systemInfoText() const {
    return maxchat::ui::systemInfoLine();
}

void maxchat::ui::MainWindow::showCommandHelp(const QString& topic) {
    const QString cleanTopic = topic.trimmed().toLower();
    if (cleanTopic == QStringLiteral("services")) {
        appendSystemLine(
            QStringLiteral("! Services: /nickserv, /ns, /identify, /ghost, /chanserv, /cs, "
                           "/memoserv, /ms, /operserv, /os, /hostserv, /hs"));
        return;
    }
    if (cleanTopic == QStringLiteral("ops") || cleanTopic == QStringLiteral("operators")) {
        appendSystemLine(
            QStringLiteral("! Operator/channel tools: /op, /deop, /voice, /devoice, /halfop, "
                           "/dehalfop, /mode, /kick, /ban, /kickban, /onotice"));
        return;
    }
    if (cleanTopic == QStringLiteral("local")) {
        appendSystemLine(
            QStringLiteral("! Local tools: /alias, /unalias, /ignore, /unignore, /notify, "
                           "/unnotify, /mute, /unmute, /clear, /clearall, /close, /sysinfo"));
        return;
    }

    appendSystemLine(
        QStringLiteral("! Core: /join, /part, /cycle, /msg, /query, /notice, /me, /nick, "
                       "/topic, /names, /list"));
    appendSystemLine(
        QStringLiteral("! Info: /whois, /who, /whowas, /lag, /uptime, /netinfo, /ctcp"));
    appendSystemLine(
        QStringLiteral("! Local: /help local, /help services, /help ops, /alias, /ignore, "
                       "/notify, /mute"));
    appendSystemLine(
        QStringLiteral("! Connection/raw: /connect, /server, /reconnect, /disconnect, /quit, "
                       "/raw, /quote, /away, /back"));
}

void maxchat::ui::MainWindow::appendReplyLine(const QString& line) {
    appendReplyLineForNetwork(currentLogNetwork(), line);
}

void maxchat::ui::MainWindow::appendReplyLineForNetwork(const QString& network, const QString& line) {
    const QString label = replyTextLabel(line);
    if (label == QStringLiteral("topic") || label == QStringLiteral("channel")) {
        return;
    }

    QString target = replyTextTarget(line);
    // CTCP replies (VERSION / TIME / PING / CLIENTINFO …) surface in the window
    // you're looking at — like other clients, and matching Python's
    // _on_ctcp_reply(_active_target). Otherwise they vanish into the server tab.
    if (label == QStringLiteral("ctcp")) {
        const QString active = currentTargetForNetwork(network).trimmed();
        if (!active.isEmpty() && !isTreeStatusTarget(active)) {
            target = active;
        }
    }
    if (target.trimmed().isEmpty()) {
        target = QStringLiteral("server");
    }
    appendSystemLineToNetworkTarget(network, target, line);
}

void maxchat::ui::MainWindow::closeTarget(const QString& target) {
    const QString cleanTarget = target.trimmed();
    if (cleanTarget.isEmpty() || isTreeStatusTarget(cleanTarget)) {
        appendSystemLine(tr("! No channel or query is selected to close."));
        return;
    }

    forgetTarget(cleanTarget);
    // Free the buffer's stored lines and every per-buffer key — without this a
    // long session leaks one full scrollback per channel/PM ever opened, and a
    // REJOINED channel would inherit a stale unread-marker position.
    (void)m_chatBuffers.removeBuffer(bufferIdForTarget(cleanTarget));
    {
        const QString key = activeNetworkName() + QChar(0x1f) + cleanTarget;
        m_bufferMarkerCount.remove(key);
        m_replayedBuffers.remove(key);
        m_comicEnabledBuffers.remove(key);
    }
    if (cleanTarget.compare(m_currentTarget, Qt::CaseInsensitive) == 0) {
        activateBufferTarget({});
        updateChannelModeButton();
        showConnectionStatus(m_hasConnectionPlan
                               ? QStringLiteral("%1 - Connected").arg(m_connectionPlan.networkName)
                               : QStringLiteral("Not connected"));
    }
    rebuildNetworkTree();
    statusBar()->showMessage(tr("Closed %1.").arg(cleanTarget));
}

void maxchat::ui::MainWindow::setServerListVisible(const bool visible, const bool save) {
    // Buttons-as-tabs replaces the server-list tree — never show both
    // navigators at once. While tabs are on, only persist the preference; the
    // tree is restored from it when tabs go off (setBufferTabsVisible).
    const bool tabsOn = m_bufferTabBar != nullptr && m_bufferTabBar->isVisible();
    if (visible && tabsOn) {
        if (save) {
            saveViewVisibilitySetting(QStringLiteral("server_list_visible"), true);
            statusBar()->showMessage(tr(
                "Server list saved — it will show when Buttons as Tabs is turned off."));
        }
        if (m_serverListVisibleAction != nullptr && m_serverListVisibleAction->isChecked()) {
            const QSignalBlocker blocker(m_serverListVisibleAction);
            m_serverListVisibleAction->setChecked(false);
        }
        return;
    }
    setSplitterPanelVisible(0, visible, save);
    if (save) {
        statusBar()->showMessage(visible ? QStringLiteral("Server list shown.")
                                         : QStringLiteral("Server list hidden."));
    }
}

void maxchat::ui::MainWindow::setMembersVisible(const bool visible, const bool save) {
    setSplitterPanelVisible(2, visible, save);
    if (save) {
        statusBar()->showMessage(visible ? QStringLiteral("Members list shown.")
                                         : QStringLiteral("Members list hidden."));
    }
}

void maxchat::ui::MainWindow::setButtonBarVisible(const bool visible, const bool save) {
    if (m_buttonBar != nullptr) {
        m_buttonBar->setVisible(visible);
    }
    if (m_buttonBarAction != nullptr && m_buttonBarAction->isChecked() != visible) {
        const QSignalBlocker blocker(m_buttonBarAction);
        m_buttonBarAction->setChecked(visible);
    }
    if (save) {
        saveViewVisibilitySetting(QStringLiteral("show_button_bar"), visible);
        statusBar()->showMessage(visible ? QStringLiteral("Button bar shown.")
                                         : QStringLiteral("Button bar hidden."));
    }
}

void maxchat::ui::MainWindow::setBufferTabsVisible(const bool visible, const bool save) {
    if (m_bufferTabBar != nullptr) {
        if (visible) {
            syncBufferTabs();
        }
        m_bufferTabBar->setVisible(visible);
    }
    // Buttons-as-tabs replaces the server-list tree. When tabs go on, hide the
    // tree regardless of current visibility (the user may have it open). When
    // tabs go off, restore per the saved server_list_visible preference.
    // setSplitterPanelVisible (called by setServerListVisible) now explicitly
    // calls widget->setVisible(), so restoration is reliable.
    if (m_networkTree != nullptr) {
        if (visible) {
            m_networkTree->setVisible(false);
        } else {
            const bool serverListPref = m_settings.loadWithDefaults()
                                            .value(QStringLiteral("server_list_visible"), true)
                                            .toBool();
            setServerListVisible(serverListPref, false);
        }
    }
    if (m_buttonsAsTabsAction != nullptr && m_buttonsAsTabsAction->isChecked() != visible) {
        const QSignalBlocker blocker(m_buttonsAsTabsAction);
        m_buttonsAsTabsAction->setChecked(visible);
    }
    if (save) {
        saveViewVisibilitySetting(QStringLiteral("buffer_tabs"), visible);
        statusBar()->showMessage(visible ? QStringLiteral("Buttons as tabs shown.")
                                         : QStringLiteral("Buttons as tabs hidden."));
    }
}

void maxchat::ui::MainWindow::setChatSeparatorVisible(const bool visible, const bool save) {
    if (m_separatorLine == visible) {
        if (m_chatSeparatorAction != nullptr && m_chatSeparatorAction->isChecked() != visible) {
            const QSignalBlocker blocker(m_chatSeparatorAction);
            m_chatSeparatorAction->setChecked(visible);
        }
        updateChatSeparatorGuide();
        return;
    }

    m_separatorLine = visible;
    if (m_chatSeparatorAction != nullptr && m_chatSeparatorAction->isChecked() != visible) {
        const QSignalBlocker blocker(m_chatSeparatorAction);
        m_chatSeparatorAction->setChecked(visible);
    }
    if (save) {
        saveViewVisibilitySetting(QStringLiteral("separator_line"), visible);
    }
    renderActiveBuffer();
    updateChatSeparatorGuide();
    statusBar()->showMessage(visible ? QStringLiteral("Chat separator shown.")
                                     : QStringLiteral("Chat separator hidden."));
}

void maxchat::ui::MainWindow::setNickColumnWidth(const int nickWidth, const bool save) {
    const int cleanWidth = std::clamp(nickWidth, 4, 40);
    if (cleanWidth == m_nickColumnWidth) {
        updateChatSeparatorGuide();
        return;
    }

    m_nickColumnWidth = cleanWidth;
    if (save) {
        QVariantMap settings = m_settings.loadWithDefaults();
        settings.insert(QStringLiteral("nick_width"), m_nickColumnWidth);
        if (!m_settings.saveRaw(settings)) {
            statusBar()->showMessage(tr("Could not save nick column width."));
        }
    }
    renderActiveBuffer();
    updateChatSeparatorGuide();
    statusBar()->showMessage(tr("Nick column width: %1").arg(m_nickColumnWidth));
}

void maxchat::ui::MainWindow::setSplitterPanelVisible(const int index, const bool visible, const bool save) {
    QAction* action = nullptr;
    const QString prefKey = index == 0   ? QStringLiteral("server_list_visible")
                            : index == 2 ? QStringLiteral("member_list_visible")
                                         : QString();
    const int fallbackWidth = index == 2 ? 190 : 180;
    if (index == 0) {
        action = m_serverListVisibleAction;
    } else if (index == 2) {
        action = m_membersVisibleAction;
    }

    if (m_mainSplitter != nullptr && index >= 0 && index < m_mainSplitter->count()) {
        QWidget* panel = m_mainSplitter->widget(index);
        // setVisible is explicit: splitter setSizes alone does not re-show a
        // widget that was hidden via setVisible(false) (e.g. by setBufferTabsVisible).
        if (panel != nullptr && panel->isVisible() != visible) {
            panel->setVisible(visible);
        }
        QList<int> sizes = m_mainSplitter->sizes();
        if (visible && sizes.at(index) <= 0) {
            const int restored = fallbackWidth;
            sizes[index] = restored;
            if (sizes.size() > 1) {
                sizes[1] = std::max(160, sizes.at(1) - restored);
            }
            m_mainSplitter->setSizes(sizes);
        } else if (!visible && sizes.at(index) > 0) {
            if (sizes.size() > 1) {
                sizes[1] += sizes.at(index);
            }
            sizes[index] = 0;
            m_mainSplitter->setSizes(sizes);
        }
    }

    if (action != nullptr && action->isChecked() != visible) {
        const QSignalBlocker blocker(action);
        action->setChecked(visible);
    }
    if (save && !prefKey.isEmpty()) {
        saveViewVisibilitySetting(prefKey, visible);
        saveSplitterSizes();
    }
}

void maxchat::ui::MainWindow::syncBufferTabs() {
    if (m_bufferTabBar == nullptr || m_networkTree == nullptr) {
        return;
    }

    // One tab per server-tree row (network roots + their channels/queries),
    // mirroring the tree's order and labels. Clicking a tab selects the
    // matching tree item, which drives all the buffer-switch logic.
    const QSignalBlocker blocker(m_bufferTabBar);
    while (m_bufferTabBar->count() > 0) {
        m_bufferTabBar->removeTab(0);
    }
    int currentIndex = -1;
    const QTreeWidgetItem* currentItem = m_networkTree->currentItem();
    for (int top = 0; top < m_networkTree->topLevelItemCount(); ++top) {
        QTreeWidgetItem* root = m_networkTree->topLevelItem(top);
        for (int child = -1; child < root->childCount(); ++child) {
            QTreeWidgetItem* item = child < 0 ? root : root->child(child);
            const QString label = item->text(0);
            const int tabIndex = m_bufferTabBar->addTab(label);
            if (child < 0) {
                // Network / server roots aren't closable from the tab bar.
                m_bufferTabBar->setTabButton(tabIndex, QTabBar::RightSide, nullptr);
                m_bufferTabBar->setTabButton(tabIndex, QTabBar::LeftSide, nullptr);
            }
            // Mirror the tree: offline networks grey; otherwise tint by activity
            // (highlight vs plain unread) so tabs signal it like the tree rows.
            if (item->foreground(0).style() != Qt::NoBrush) {
                m_bufferTabBar->setTabTextColor(tabIndex, item->foreground(0).color());
            } else if (label.contains(QStringLiteral("[!"))) {
                m_bufferTabBar->setTabTextColor(tabIndex, QColor(0xE2, 0x4B, 0x4A));
            } else if (label.contains(QStringLiteral(" ["))) {
                m_bufferTabBar->setTabTextColor(tabIndex, QColor(0x6F, 0x8C, 0xFF));
            }
            if (item == currentItem) {
                currentIndex = tabIndex;
            }
        }
    }
    if (currentIndex >= 0) {
        m_bufferTabBar->setCurrentIndex(currentIndex);
    }
}

void maxchat::ui::MainWindow::closeBufferTab(const int index) {
    QTreeWidgetItem* item = treeItemForTabIndex(index);
    if (item == nullptr) {
        return;
    }
    const QString target = item->data(0, Qt::UserRole).toString();
    if (target.trimmed().isEmpty() || isTreeStatusTarget(target)) {
        return; // network/server roots aren't closable from a tab
    }
    if (m_networkTree != nullptr) {
        m_networkTree->setCurrentItem(item); // switch context to it, then close
    }
    closeTarget(target);
}

QTreeWidgetItem* MainWindow::treeItemForTabIndex(const int index) const {
    if (m_networkTree == nullptr || index < 0) {
        return nullptr;
    }
    int seen = 0;
    for (int top = 0; top < m_networkTree->topLevelItemCount(); ++top) {
        QTreeWidgetItem* root = m_networkTree->topLevelItem(top);
        for (int child = -1; child < root->childCount(); ++child) {
            QTreeWidgetItem* item = child < 0 ? root : root->child(child);
            if (seen++ == index) {
                return item;
            }
        }
    }
    return nullptr;
}

void maxchat::ui::MainWindow::syncPanelActionsFromSplitter(const bool save) {
    if (m_mainSplitter == nullptr) {
        return;
    }

    const QList<int> sizes = m_mainSplitter->sizes();
    const bool serverVisible = sizes.size() > 0 && sizes.at(0) > 0;
    const bool membersVisible = sizes.size() > 2 && sizes.at(2) > 0;
    if (m_serverListVisibleAction != nullptr &&
        m_serverListVisibleAction->isChecked() != serverVisible) {
        const QSignalBlocker blocker(m_serverListVisibleAction);
        m_serverListVisibleAction->setChecked(serverVisible);
    }
    if (m_membersVisibleAction != nullptr &&
        m_membersVisibleAction->isChecked() != membersVisible) {
        const QSignalBlocker blocker(m_membersVisibleAction);
        m_membersVisibleAction->setChecked(membersVisible);
    }

    if (save) {
        QVariantMap settings = m_settings.loadWithDefaults();
        settings.insert(QStringLiteral("server_list_visible"), serverVisible);
        settings.insert(QStringLiteral("member_list_visible"), membersVisible);
        QVariantList savedSizes;
        savedSizes.reserve(sizes.size());
        for (const int size : sizes) {
            savedSizes.append(size);
        }
        settings.insert(QStringLiteral("splitter_sizes"), savedSizes);
        if (!m_settings.saveRaw(settings)) {
            statusBar()->showMessage(tr("Could not save panel layout."));
        }
    }
}

void maxchat::ui::MainWindow::updateChatSeparatorGuide() {
    if (m_chatPane == nullptr) {
        return;
    }

    const QString timestamp = m_showTimestamps ? timestampText() : QString();
    const double timestampColumns =
        timestamp.isEmpty() ? 0.0 : static_cast<double>(timestamp.size() + 1);
    QColor color = m_chatView->palette().color(QPalette::ColorRole::Text);
    // Measure the actual rendered prefix so the guide stays glued to the nick
    // column regardless of font (the column estimate assumed monospace).
    const maxchat::core::FormattedChatLine column =
        maxchat::core::formatChatLine(QString(), chatLineFormatOptions());
    const QFontMetricsF metrics(m_chatView->font());
    const double pixelX = column.prefixPlain.isEmpty()
                              ? -1.0
                              : metrics.horizontalAdvance(column.prefixPlain) -
                                    metrics.horizontalAdvance(QLatin1Char(' ')) / 2.0;
    m_chatPane->setSeparatorGuide(timestampColumns, m_nickColumnWidth,
                                  m_separatorLine && m_alignNicks, color, pixelX);
}

void maxchat::ui::MainWindow::saveViewVisibilitySetting(const QString& key, const bool visible) {
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(key, visible);
    if (!m_settings.saveRaw(settings)) {
        statusBar()->showMessage(tr("Could not save view setting."));
    }
}

void maxchat::ui::MainWindow::saveSplitterSizes() {
    if (m_mainSplitter == nullptr) {
        return;
    }

    const QList<int> sizes = m_mainSplitter->sizes();
    QVariantList savedSizes;
    savedSizes.reserve(sizes.size());
    for (const int size : sizes) {
        savedSizes.append(size);
    }
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("splitter_sizes"), savedSizes);
    if (!m_settings.saveRaw(settings)) {
        statusBar()->showMessage(tr("Could not save panel layout."));
    }
}

void maxchat::ui::MainWindow::exportSettings() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Settings"), QStringLiteral("maxchat-settings.json"),
        QStringLiteral("JSON files (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(
        QStringLiteral("networks"),
        maxchat::core::networkConfigListToVariantList(maxchat::core::networkConfigListFromVariant(
            settings.value(QStringLiteral("networks")))));

    // Carry the personal dictionary in the same file so a profile export is
    // self-contained (the words live in a separate file on disk normally).
    QStringList personalWords;
    QFile personalFile(personalDictionaryPath());
    if (personalFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&personalFile);
        while (!in.atEnd()) {
            const QString word = in.readLine().trimmed();
            if (!word.isEmpty()) {
                personalWords.append(word);
            }
        }
    }
    if (!personalWords.isEmpty()) {
        settings.insert(QStringLiteral("personal_dictionary"), personalWords);
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        appendSystemLine(tr("! Could not open settings export file."));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromVariant(settings);
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        appendSystemLine(tr("! Could not write settings export file."));
        return;
    }
    appendSystemLine(tr("! Settings exported to %1.").arg(path));
}

void maxchat::ui::MainWindow::importSettings() {
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("Import Settings"), QString(),
                                     QStringLiteral("JSON files (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        appendSystemLine(tr("! Could not open settings import file."));
        return;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        appendSystemLine(tr("! Settings import file is not valid JSON."));
        return;
    }

    QVariantMap rawImported = document.object().toVariantMap();
    // Merge any bundled personal-dictionary words into the on-disk file (union
    // with what's already there), then drop the key from the settings map.
    const QVariantList importedWords = rawImported.take(QStringLiteral("personal_dictionary")).toList();
    if (!importedWords.isEmpty()) {
        QStringList merged;
        QSet<QString> seen;
        const auto addWord = [&](const QString& w) {
            const QString c = w.trimmed();
            if (!c.isEmpty() && !seen.contains(c)) {
                seen.insert(c);
                merged.append(c);
            }
        };
        QFile existingFile(personalDictionaryPath());
        if (existingFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&existingFile);
            while (!in.atEnd()) {
                addWord(in.readLine());
            }
            existingFile.close();
        }
        for (const QVariant& w : importedWords) {
            addWord(w.toString());
        }
        QDir().mkpath(m_settings.paths().configDir);
        QSaveFile out(personalDictionaryPath());
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream stream(&out);
            for (const QString& w : merged) {
                stream << w << '\n';
            }
            out.commit();
        }
    }

    const QVariantMap prepared = m_settings.prepareImportedSettings(rawImported);
    if (!m_settings.saveRaw(prepared)) {
        appendSystemLine(tr("! Could not save imported settings."));
        return;
    }
    applyCurrentSettings();
    appendSystemLine(tr("! Settings imported from %1.").arg(path));
}

void maxchat::ui::MainWindow::resetServerList() {
    if (!m_settings.resetServerList()) {
        appendSystemLine(tr("! Could not reset server list."));
        return;
    }
    appendSystemLine(tr("! Server list reset to bundled defaults."));
}

void maxchat::ui::MainWindow::saveActiveNetworkState() {
    if (!m_hasConnectionPlan) {
        return;
    }
    const QString network = activeNetworkName();
    rememberNetwork(network);
    m_connectionPlansByNetwork.insert(network, m_connectionPlan);
    m_currentTargetByNetwork.insert(network, m_currentTarget);
    m_openTargetsByNetwork.insert(network, m_openTargets);
    m_registeredByNetwork.insert(network, m_registered);
    m_initialConnectAttemptsByNetwork.insert(network, m_initialConnectAttempts);
    m_manualDisconnectByNetwork.insert(network, m_manualDisconnect);
    m_reconnectRequestedByNetwork.insert(network, m_reconnectRequested);
}

maxchat::irc::IrcConnection* MainWindow::connectionForNetwork(const QString& network) const {
    const QString normalized = network.trimmed();
    if (normalized.isEmpty()) {
        return nullptr;
    }
    return m_connectionsByNetwork.value(normalized, nullptr);
}

maxchat::irc::IrcConnection* MainWindow::ensureConnectionForNetwork(const QString& network) {
    const QString normalized = network.trimmed();
    if (normalized.isEmpty()) {
        return &m_connection;
    }

    if (auto* existing = connectionForNetwork(normalized); existing != nullptr) {
        return existing;
    }

    auto* created = new maxchat::irc::IrcConnection(this);
    created->setIgnoreMasks(m_ignoreMasks);
    const QVariantMap settings = m_settings.loadWithDefaults();
    created->setCtcpVersion(settings.value(QStringLiteral("hide_version"), false).toBool(),
                            settings.value(QStringLiteral("ctcp_version")).toString());
    created->setCtcpOptions(
        settings.value(QStringLiteral("ctcp_respond_ping"), true).toBool(),
        settings.value(QStringLiteral("ctcp_respond_time"), true).toBool(),
        settings.value(QStringLiteral("ctcp_respond_clientinfo"), true).toBool());
    m_connectionsByNetwork.insert(normalized, created);
    setupConnectionSignals(normalized, created);
    return created;
}

maxchat::irc::IrcConnection& MainWindow::connection() {
    if (auto* existing = connectionForNetwork(activeNetworkName()); existing != nullptr) {
        return *existing;
    }
    return m_connection;
}

const maxchat::irc::IrcConnection& MainWindow::connection() const {
    if (auto* existing = connectionForNetwork(activeNetworkName()); existing != nullptr) {
        return *existing;
    }
    return m_connection;
}

bool MainWindow::anyNetworkConnectionIsConnected() const {
    if (m_connection.isConnected()) {
        return true;
    }
    for (auto it = m_connectionsByNetwork.cbegin(); it != m_connectionsByNetwork.cend(); ++it) {
        if (it.value() != nullptr && it.value()->isConnected()) {
            return true;
        }
    }
    return false;
}

void maxchat::ui::MainWindow::withNetworkContext(const QString& network, const std::function<void()>& body) {
    const QString normalized = network.trimmed();
    if (normalized.isEmpty() || normalized.compare(activeNetworkName(), Qt::CaseInsensitive) == 0) {
        body();
        return;
    }

    const bool hadConnectionPlan = m_hasConnectionPlan;
    const maxchat::core::NetworkConnectionPlan previousPlan = m_connectionPlan;
    const QString previousTarget = m_currentTarget;
    const QStringList previousOpenTargets = m_openTargets;
    const bool previousRegistered = m_registered;
    const bool previousBackgroundContext = m_backgroundNetworkContext;

    saveActiveNetworkState();
    rememberNetwork(normalized);
    m_hasConnectionPlan = true;
    if (m_connectionPlansByNetwork.contains(normalized)) {
        m_connectionPlan = m_connectionPlansByNetwork.value(normalized);
    } else {
        m_connectionPlan = {};
        m_connectionPlan.networkName = normalized;
    }
    m_currentTarget = m_currentTargetByNetwork.value(normalized);
    m_openTargets = m_openTargetsByNetwork.value(normalized);
    m_registered = m_registeredByNetwork.value(normalized, false);

    m_backgroundNetworkContext = true;
    body();
    m_backgroundNetworkContext = previousBackgroundContext;

    saveActiveNetworkState();
    m_hasConnectionPlan = hadConnectionPlan;
    m_connectionPlan = previousPlan;
    m_currentTarget = previousTarget;
    m_openTargets = previousOpenTargets;
    m_registered = previousRegistered;
    if (hadConnectionPlan) {
        const bool activeSet = m_chatBuffers.setActiveBuffer(
            bufferIdForNetworkTarget(activeNetworkName(), m_currentTarget));
        Q_UNUSED(activeSet);
    }
    renderActiveBuffer();
    renderActiveBufferMetadata();
    rebuildNetworkTree();
    updateChannelModeButton();
}

void maxchat::ui::MainWindow::startConfiguredStartupConnection() {
    if (m_hasConnectionPlan || anyNetworkConnectionIsConnected()) {
        return;
    }

    const bool mergedDefaults = m_settings.mergeDefaultNetworks();
    const QVariantMap settings = m_settings.loadWithDefaults();
    const auto networks =
        maxchat::core::networkConfigListFromVariant(settings.value(QStringLiteral("networks")));
    if (mergedDefaults) {
        appendSystemLine(tr("! Server list was updated with bundled defaults."));
    }

    // Networks flagged "Connect on startup" all come up (staggered, in Server
    // List order). With none flagged, fall back to the first connectable one.
    QList<maxchat::core::NetworkConfig> autoconnects;
    for (const maxchat::core::NetworkConfig& network : networks) {
        if (network.value(QStringLiteral("autoconnect")).toBool() &&
            maxchat::core::hasConnectableServer(
                maxchat::core::connectionPlanFromNetwork(network))) {
            autoconnects.append(network);
        }
    }
    if (!autoconnects.isEmpty()) {
        int delayMs = 0;
        for (const maxchat::core::NetworkConfig& network : autoconnects) {
            QTimer::singleShot(delayMs, this, [this, network]() { startConnection(network); });
            delayMs += 1500;
        }
        return;
    }

    for (const maxchat::core::NetworkConfig& network : networks) {
        if (maxchat::core::hasConnectableServer(
                maxchat::core::connectionPlanFromNetwork(network))) {
            startConnection(network);
            return;
        }
    }

    appendSystemLine(tr("! Auto-connect is enabled, but no server is available."));
}

void maxchat::ui::MainWindow::setupConnectionSignals() {
    setupConnectionSignals({}, &m_connection);
}

void maxchat::ui::MainWindow::setupConnectionSignals(const QString& network, maxchat::irc::IrcConnection* irc) {
    m_ircRouter->wire(network, irc);
}

void maxchat::ui::MainWindow::startConnection(const maxchat::core::NetworkConfig& network) {
    saveActiveNetworkState();

    const maxchat::core::NetworkConnectionPlan plan =
        maxchat::core::connectionPlanFromNetwork(network);
    if (!maxchat::core::hasConnectableServer(plan)) {
        appendSystemLine(tr("! Saved network has no usable server."));
        return;
    }

    const QVariantMap settings = m_settings.loadWithDefaults();
    m_autoReconnect = settings.value(QStringLiteral("auto_reconnect")).toBool();
    m_hasConnectionPlan = true;
    m_connectionPlan = plan;
    m_connectionPlansByNetwork.insert(m_connectionPlan.networkName, m_connectionPlan);
    m_registered = false;
    m_connectionUptimeRunning = false;
    m_manualDisconnect = false;
    m_initialConnectAttempts = 0;
    m_initialConnectAttemptsByNetwork.insert(m_connectionPlan.networkName, 0);
    m_manualDisconnectByNetwork.insert(m_connectionPlan.networkName, false);
    m_reconnectRequestedByNetwork.insert(m_connectionPlan.networkName, false);
    m_connectionUptimeStartMsByNetwork.remove(m_connectionPlan.networkName);
    const QString networkKeyPrefix =
        QStringLiteral("%1/").arg(m_connectionPlan.networkName.toCaseFolded());
    for (const QString& key : m_pendingNamesByChannel.keys()) {
        if (key.startsWith(networkKeyPrefix)) {
            m_pendingNamesByChannel.remove(key);
        }
    }
    for (const QString& key : m_channelModeLines.keys()) {
        if (key.startsWith(networkKeyPrefix)) {
            m_channelModeLines.remove(key);
        }
    }
    rememberNetwork(m_connectionPlan.networkName);
    maxchat::irc::IrcConnection* irc = ensureConnectionForNetwork(m_connectionPlan.networkName);
    Q_UNUSED(irc);
    const maxchat::core::ChatBufferId serverBuffer =
        m_chatBuffers.ensureServerBuffer(m_connectionPlan.networkName);
    Q_UNUSED(serverBuffer);
    m_floodGuard.clear();
    m_openTargets = m_connectionPlan.autojoin;
    for (const QString& target : m_openTargets) {
        const maxchat::core::ChatBufferId targetBuffer = bufferIdForTarget(target);
        Q_UNUSED(targetBuffer);
    }
    m_currentTarget =
        m_connectionPlan.autojoin.isEmpty() ? QString() : m_connectionPlan.autojoin.first();
    m_currentTargetByNetwork.insert(m_connectionPlan.networkName, m_currentTarget);
    m_openTargetsByNetwork.insert(m_connectionPlan.networkName, m_openTargets);
    m_registeredByNetwork.insert(m_connectionPlan.networkName, false);
    activateBufferTarget(m_currentTarget);

    rebuildNetworkTree();
    // rebuildNetworkTree uses QSignalBlocker so currentItemChanged doesn't fire;
    // sync the tab bar explicitly so the new server's tab is visually selected.
    syncBufferTabs();
    updateChannelModeButton();
    // Replay is seeded per-buffer on first open (activateBufferTarget above), so
    // it survives buffer switches; no separate connect-time replay needed.
    connectNextServer(m_connectionPlan.networkName);
}

void maxchat::ui::MainWindow::connectNextServer(const QString& network, const bool forceNext) {
    withNetworkContext(network, [this, network, forceNext]() {
        if (!m_hasConnectionPlan || !maxchat::core::hasConnectableServer(m_connectionPlan)) {
            appendSystemLine(tr("! No server is available to connect."));
            return;
        }

        const QString signalNetwork = activeNetworkName();
        const int attempts = m_initialConnectAttemptsByNetwork.value(signalNetwork, 0);
        if (!m_registered && attempts >= maxInitialConnectAttempts(signalNetwork)) {
            appendSystemLine(tr("! Connection attempts exhausted for %1.")
                                 .arg(m_connectionPlan.networkName));
            showConnectionStatus(tr("Not connected"));
            return;
        }

        maxchat::core::NetworkConnectionPlan plan = m_connectionPlan;
        const maxchat::irc::ServerEndpoint server =
            maxchat::irc::chooseReconnectServer(plan.reconnect, forceNext);
        if (server.host.trimmed().isEmpty()) {
            appendSystemLine(tr("! No server is available to connect."));
            return;
        }
        m_connectionPlan = plan;
        m_connectionPlansByNetwork.insert(signalNetwork, m_connectionPlan);

        const int nextAttempts = attempts + 1;
        m_initialConnectAttempts = nextAttempts;
        m_initialConnectAttemptsByNetwork.insert(signalNetwork, nextAttempts);
        const QString tlsText = server.tls ? QStringLiteral(" SSL/TLS") : QString();
        appendSystemLine(tr("! Connecting to %1 (%2:%3%4), attempt %5 of %6.")
                             .arg(m_connectionPlan.networkName, server.host)
                             .arg(server.port)
                             .arg(tlsText)
                             .arg(nextAttempts)
                             .arg(maxInitialConnectAttempts(signalNetwork)));
        showConnectionStatus(
            QStringLiteral("Connecting to %1:%2...").arg(server.host).arg(server.port));

        maxchat::irc::IrcConnection* irc = ensureConnectionForNetwork(signalNetwork);
        if (irc == nullptr) {
            appendSystemLine(tr("! Could not create network connection."));
            return;
        }
        irc->connectTo(connectConfigFor(server, m_connectionPlan));
    });
}

void maxchat::ui::MainWindow::connectNextServer(bool forceNext) {
    connectNextServer(activeNetworkName(), forceNext);
}

void maxchat::ui::MainWindow::reconnectNetwork(const QString& network) {
    withNetworkContext(network, [this]() {
        if (!m_hasConnectionPlan || !maxchat::core::hasConnectableServer(m_connectionPlan)) {
            appendSystemLine(tr("! No saved connection is available to reconnect."));
            return;
        }

        const QString signalNetwork = activeNetworkName();
        m_initialConnectAttempts = 0;
        m_initialConnectAttemptsByNetwork.insert(signalNetwork, 0);
        m_reconnectRequested = true;
        m_reconnectRequestedByNetwork.insert(signalNetwork, true);
        maxchat::irc::IrcConnection* irc = ensureConnectionForNetwork(signalNetwork);
        if (irc != nullptr && irc->isConnected()) {
            appendSystemLine(
                QStringLiteral("! Reconnecting to %1.").arg(m_connectionPlan.networkName));
            m_manualDisconnect = true;
            m_manualDisconnectByNetwork.insert(signalNetwork, true);
            irc->disconnectFromServer();
            return;
        }

        m_reconnectRequested = false;
        m_reconnectRequestedByNetwork.insert(signalNetwork, false);
        m_manualDisconnect = false;
        m_manualDisconnectByNetwork.insert(signalNetwork, false);
        // Manual reconnect retries the SAME server (the user is fixing lag, not
        // asking to move); automatic failover advances on its own.
        connectNextServer(signalNetwork, false);
    });
}

void maxchat::ui::MainWindow::reconnectCurrentServer() {
    reconnectNetwork(activeNetworkName());
}

void maxchat::ui::MainWindow::disconnectNetwork(const QString& network) {
    withNetworkContext(network, [this]() {
        const QString signalNetwork = activeNetworkName();
        m_manualDisconnect = true;
        m_reconnectRequested = false;
        m_manualDisconnectByNetwork.insert(signalNetwork, true);
        m_reconnectRequestedByNetwork.insert(signalNetwork, false);
        if (m_hasConnectionPlan) {
            appendSystemLine(
                QStringLiteral("! Disconnecting from %1.").arg(m_connectionPlan.networkName));
            if (auto* irc = connectionForNetwork(signalNetwork); irc != nullptr) {
                irc->disconnectFromServer();
            } else {
                connection().disconnectFromServer();
            }
        } else {
            appendSystemLine(tr("! Not connected."));
            showConnectionStatus(tr("Not connected"));
        }
    });
}

void maxchat::ui::MainWindow::disconnectFromCurrentServer() {
    disconnectNetwork(activeNetworkName());
}

void maxchat::ui::MainWindow::closeNetwork(const QString& network) {
    const QString net = network.trimmed();
    if (net.isEmpty()) {
        return;
    }
    maxchat::irc::IrcConnection* irc = connectionForNetwork(net);
    const bool isActive = net.compare(activeNetworkName(), Qt::CaseInsensitive) == 0;
    maxchat::irc::IrcConnection* effective =
        irc != nullptr ? irc : (isActive ? &m_connection : nullptr);
    if (effective != nullptr && effective->isConnected()) {
        if (QMessageBox::question(
                this, QStringLiteral("Close Server"),
                QStringLiteral("Disconnect from %1 and close all of its chats?").arg(net)) !=
            QMessageBox::Yes) {
            return;
        }
    }

    // Kill any reconnect machinery FIRST, then drop the link. abort() emits
    // disconnected synchronously, so all "Disconnected" fallout (which may
    // touch buffers/tree) lands before the cleanup below removes them.
    m_manualDisconnectByNetwork.insert(net, true);
    m_reconnectRequestedByNetwork.insert(net, false);
    if (isActive) {
        m_manualDisconnect = true;
        m_reconnectRequested = false;
    }
    if (effective != nullptr) {
        effective->disconnectFromServer();
        if (effective != &m_connection) {
            m_connectionsByNetwork.remove(net);
            effective->deleteLater();
        }
    }

    // Free every buffer (and per-buffer keys) belonging to the network.
    for (const maxchat::core::ChatBufferId& id : m_chatBuffers.buffersForNetwork(net)) {
        const QString key = id.network + QChar(0x1f) + id.target;
        m_bufferMarkerCount.remove(key);
        m_replayedBuffers.remove(key);
        m_comicEnabledBuffers.remove(key);
        (void)m_chatBuffers.removeBuffer(id);
    }

    // Drop the per-network bookkeeping (keep the manual-disconnect tombstone:
    // a late disconnected signal must not look like a crash and auto-reconnect).
    const auto removeKeyCI = [&net](auto& map) {
        for (auto it = map.begin(); it != map.end();) {
            if (it.key().compare(net, Qt::CaseInsensitive) == 0) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    };
    removeKeyCI(m_connectionPlansByNetwork);
    removeKeyCI(m_currentTargetByNetwork);
    removeKeyCI(m_openTargetsByNetwork);
    removeKeyCI(m_registeredByNetwork);
    removeKeyCI(m_initialConnectAttemptsByNetwork);
    removeKeyCI(m_connectionUptimeStartMsByNetwork);
    removeKeyCI(m_onlineFriendsByNetwork);
    removeKeyCI(m_haveFriendSnapshotByNetwork);
    removeKeyCI(m_awayNicksByNetwork);
    for (int i = m_knownNetworks.size() - 1; i >= 0; --i) {
        if (m_knownNetworks.at(i).compare(net, Qt::CaseInsensitive) == 0) {
            m_knownNetworks.removeAt(i);
        }
    }

    // If the active network was closed, hop to another known one (or the bare
    // not-connected view). Done by hand: setActiveNetwork would re-remember
    // the just-closed name while saving "current" state.
    if (isActive) {
        const QString fallback =
            m_knownNetworks.isEmpty() ? QString() : m_knownNetworks.first();
        if (fallback.isEmpty()) {
            m_hasConnectionPlan = false;
            m_connectionPlan = {};
            m_currentTarget.clear();
            m_openTargets.clear();
            m_registered = false;
        } else {
            m_hasConnectionPlan = true;
            m_connectionPlan = m_connectionPlansByNetwork.value(fallback);
            if (m_connectionPlan.networkName.trimmed().isEmpty()) {
                m_connectionPlan.networkName = fallback;
            }
            m_currentTarget = m_currentTargetByNetwork.value(fallback);
            m_openTargets = m_openTargetsByNetwork.value(fallback);
            m_registered = m_registeredByNetwork.value(fallback, false);
            m_initialConnectAttempts = m_initialConnectAttemptsByNetwork.value(fallback, 0);
            m_manualDisconnect = m_manualDisconnectByNetwork.value(fallback, false);
            m_reconnectRequested = m_reconnectRequestedByNetwork.value(fallback, false);
        }
        activateBufferTarget(m_currentTarget);
        updateChannelModeButton();
        updateWindowTitle();
        updateNickLabel();
    }
    rebuildNetworkTree();
    statusBar()->showMessage(tr("Closed %1.").arg(net));
}

void maxchat::ui::MainWindow::handleDisconnected(const QString& network, const QString& reason) {
    withNetworkContext(network, [this, reason]() { handleDisconnected(reason); });
}

void maxchat::ui::MainWindow::handleDisconnected(const QString& reason) {
    appendSystemLine(tr("! Disconnected: %1").arg(reason));
    showConnectionStatus(tr("Not connected"));
    const QString networkKeyPrefix = QStringLiteral("%1/").arg(activeNetworkName().toCaseFolded());
    for (const QString& key : m_pendingNamesByChannel.keys()) {
        if (key.startsWith(networkKeyPrefix)) {
            m_pendingNamesByChannel.remove(key);
        }
    }
    if (!anyNetworkConnectionIsConnected()) {
        m_friendPollTimer.stop();
    }
    m_onlineFriendsByNetwork.remove(activeNetworkName());
    m_haveFriendSnapshotByNetwork.insert(activeNetworkName(), false);
    m_onlineFriends.clear();
    m_haveFriendSnapshot = false;
    updateChannelModeButton();
    const bool wasRegistered = m_registered;
    m_registered = false;
    m_registeredByNetwork.insert(activeNetworkName(), false);
    rebuildNetworkTree(); // reflect the offline state in the tree right away
    m_connectionUptimeRunning = false;
    m_connectionUptimeStartMsByNetwork.remove(activeNetworkName());
    m_connectionPlansByNetwork.insert(activeNetworkName(), m_connectionPlan);

    const QString network = activeNetworkName();
    const bool reconnectRequested =
        m_reconnectRequestedByNetwork.value(network, m_reconnectRequested);
    const bool manualDisconnect = m_manualDisconnectByNetwork.value(network, m_manualDisconnect);

    if (reconnectRequested) {
        m_reconnectRequested = false;
        m_manualDisconnect = false;
        m_reconnectRequestedByNetwork.insert(network, false);
        m_manualDisconnectByNetwork.insert(network, false);
        connectNextServer(network, false); // manual reconnect = same server
        return;
    }

    if (manualDisconnect || !m_hasConnectionPlan) {
        return;
    }
    if (wasRegistered && !m_autoReconnect) {
        appendSystemLine(tr("! Auto reconnect is disabled."));
        return;
    }
    if (wasRegistered) {
        m_initialConnectAttempts = 0;
        m_initialConnectAttemptsByNetwork.insert(network, 0);
        m_connectionPlan.reconnect.serverAttempt = 0;
        m_connectionPlansByNetwork.insert(network, m_connectionPlan);
        appendSystemLine(tr("! Reconnecting to %1.").arg(m_connectionPlan.networkName));
    }
    connectNextServer(network);
}

void maxchat::ui::MainWindow::connectFromCommand(const QString& command, const QStringList& targets,
                                    const QString& text) {
    if (targets.isEmpty() || targets.first().trimmed().isEmpty()) {
        appendSystemLine(command == QStringLiteral("connect")
                             ? QStringLiteral("! Usage: /connect network-or-server "
                                              "[port] [server-password]")
                             : QStringLiteral("! Usage: /server server[:port] "
                                              "[server-password]"));
        return;
    }

    const QString firstTarget = targets.first().trimmed();
    const bool mergedDefaults = m_settings.mergeDefaultNetworks();
    Q_UNUSED(mergedDefaults);
    const QVariantMap settings = m_settings.loadWithDefaults();
    const maxchat::core::NetworkConfigList networks =
        maxchat::core::networkConfigListFromVariant(settings.value(QStringLiteral("networks")));

    const maxchat::core::NetworkConfig savedNetwork =
        findCommandNetwork(networks, firstTarget, text);
    if (!savedNetwork.isEmpty()) {
        startConnection(savedNetwork);
        return;
    }

    const CommandServerSpec serverSpec = parseCommandServerSpec(firstTarget, text);
    if (!serverSpec.valid) {
        appendSystemLine(serverSpec.errorText.startsWith(QLatin1Char('!'))
                             ? serverSpec.errorText
                             : QStringLiteral("! %1").arg(serverSpec.errorText));
        return;
    }

    const QString currentNick = !connection().nick().isEmpty() ? connection().nick()
                                : !m_connectionPlan.nick.trimmed().isEmpty()
                                    ? m_connectionPlan.nick.trimmed()
                                    : QStringLiteral("comicfan");

    maxchat::core::NetworkConfig network;
    network.insert(QStringLiteral("name"), serverSpec.host);
    network.insert(QStringLiteral("host"), serverSpec.host);
    network.insert(QStringLiteral("port"), serverSpec.port);
    network.insert(QStringLiteral("tls"), serverSpec.tls);
    network.insert(QStringLiteral("nick"), currentNick);
    network.insert(QStringLiteral("username"), m_connectionPlan.username);
    network.insert(QStringLiteral("realname"), m_connectionPlan.realname);
    network.insert(QStringLiteral("channels"), QString());
    network.insert(QStringLiteral("server_pass"), serverSpec.serverPassword);
    network.insert(QStringLiteral("servers"), QStringList{});
    startConnection(network);
}

void maxchat::ui::MainWindow::handleInputSubmitted() {
    noteUserActivity();
    const QString raw = inputText(m_input);
    const QStringList lines = raw.split(QLatin1Char('\n'));
    int lineCount = 0;
    for (const QString& line : lines) {
        if (!line.trimmed().isEmpty()) {
            ++lineCount;
        }
    }
    if (lineCount == 0) {
        return;
    }

    // Paste guard: confirm before firing a big multi-line block at the network.
    if (m_pasteGuard && lineCount >= m_pasteLines) {
        const QString where =
            m_currentTarget.trimmed().isEmpty() ? QStringLiteral("the server") : m_currentTarget;
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Paste guard"),
            QStringLiteral("Send %1 lines to %2?").arg(lineCount).arg(where));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    addInputHistory(raw.trimmed());
    m_input->clear();
    if (lines.size() <= 1) {
        sendCommandOrMessage(raw.trimmed());
    } else {
        for (const QString& line : lines) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty()) {
                sendCommandOrMessage(trimmed);
            }
        }
    }
}

void maxchat::ui::MainWindow::sendCommandOrMessage(const QString& text) {
    const QString aliasChannel = m_currentTarget.startsWith(QLatin1Char('#')) ||
                                         m_currentTarget.startsWith(QLatin1Char('&'))
                                     ? m_currentTarget
                                     : QString();
    const auto aliasExpansion = maxchat::core::expandCommandAliases(
        text, m_commandAliases, currentNickForNetwork(activeNetworkName()), aliasChannel);

    // /dcc is handled by the DCC manager, not the IRC command parser.
    if (aliasExpansion.commandLine.startsWith(QStringLiteral("/dcc"), Qt::CaseInsensitive)) {
        const QStringList parts =
            aliasExpansion.commandLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        handleDccCommand(parts.mid(1));
        return;
    }

    // Plain text in a "=peer" buffer is a DCC CHAT line, not an IRC message.
    if (m_currentTarget.startsWith(QLatin1Char('=')) &&
        !aliasExpansion.commandLine.startsWith(QLatin1Char('/'))) {
        if (!m_dccEnabled) { return; }
        const QString peer = m_currentTarget.mid(1);
        m_dccManager->sendChatLine(peer, text);
        appendSystemLineToTarget(m_currentTarget,
                                 QStringLiteral("<%1> %2").arg(currentNickForNetwork(activeNetworkName()), text),
                                 false, true, false, false);
        return;
    }

    // Give scripts first crack at any /command or !trigger (after alias expansion).
    // A script's on_command returning true consumes the input — it is not sent.
    {
        const bool isSlash = aliasExpansion.commandLine.startsWith(QLatin1Char('/'));
        const bool isBang  = !isSlash && aliasExpansion.commandLine.startsWith(QLatin1Char('!'));
        if (isSlash || isBang) {
            const QString rest = aliasExpansion.commandLine.mid(1);
            const int space = rest.indexOf(QLatin1Char(' '));
            const QString cmd  = space >= 0 ? rest.left(space) : rest;
            const QString args = space >= 0 ? rest.mid(space + 1) : QString();
            if (!cmd.isEmpty() &&
                m_scripts->dispatch(QStringLiteral("on_command"), activeNetworkName(), {cmd, args})) {
                return;
            }
        }
    }

    const auto parsed = maxchat::irc::parseUserCommand(aliasExpansion.commandLine, m_currentTarget);

    if (parsed.type == maxchat::irc::UserCommandType::Clear) {
        clearCurrentChat();
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::ClearAll) {
        clearAllChats();
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Close) {
        closeTarget(m_currentTarget);
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Reconnect) {
        reconnectCurrentServer();
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Disconnect) {
        if (!connection().isConnected()) {
            appendSystemLine(tr("! Not connected."));
            return;
        }
        disconnectFromCurrentServer();
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Connect) {
        connectFromCommand(parsed.command, parsed.targets, parsed.text);
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Uptime) {
        showUptime();
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::NetInfo) {
        showNetInfo();
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Alias) {
        if (parsed.targets.isEmpty()) {
            showAliasList();
            return;
        }

        const QString name = parsed.targets.first().toLower();
        if (parsed.text.trimmed().isEmpty()) {
            const QVariant value = m_commandAliases.value(name);
            appendSystemLine(value.isValid()
                                 ? QStringLiteral("! /%1 = %2").arg(name, value.toString())
                                 : QStringLiteral("! /%1 is not defined.").arg(name));
            return;
        }
        setAliasCommand(name, parsed.text);
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Unalias) {
        removeAliasCommand(parsed.targets.first());
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Mute) {
        addMutedChannel(parsed.targets.first());
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Unmute) {
        removeMutedChannel(parsed.targets.first());
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::SysInfo) {
        const QString info = systemInfoText();
        const QStringList args =
            parsed.text.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // "/sysinfo send [#chan|nick]" broadcasts; a bare "/sysinfo" shows it to you.
        if (!args.isEmpty() && args.first().compare(QStringLiteral("send"), Qt::CaseInsensitive) == 0) {
            const QString target =
                args.size() > 1 ? args.at(1).trimmed() : m_currentTarget.trimmed();
            if (!connection().isConnected() || target.isEmpty() || isTreeStatusTarget(target)) {
                appendSystemLine(tr("! Usage: /sysinfo send [#channel|nick] "
                                                "(needs a connection and a target)."));
                return;
            }
            if (connection().privmsg(target, info)) {
                const QString nick =
                    connection().nick().isEmpty() ? m_connectionPlan.nick : connection().nick();
                appendSystemLineToTarget(target, QStringLiteral("<%1> %2").arg(nick, info), true,
                                         true);
            } else {
                appendSystemLine(tr("! Could not send sysinfo."));
            }
            return;
        }
        appendSystemLine(tr("* %1").arg(info));
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Scripts) {
        handleScriptsCommand(parsed.command, parsed.text.trimmed());
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Help) {
        showCommandHelp(parsed.text);
        return;
    }
    if (parsed.type == maxchat::irc::UserCommandType::Error) {
        appendSystemLine(tr("! %1").arg(parsed.errorText));
        return;
    }
    if (!connection().isConnected()) {
        appendSystemLine(tr("! Not connected."));
        return;
    }

    const QString nick =
        connection().nick().isEmpty() ? m_connectionPlan.nick : connection().nick();

    switch (parsed.type) {
    case maxchat::irc::UserCommandType::Empty:
        return;
    case maxchat::irc::UserCommandType::Error:
        appendSystemLine(tr("! %1").arg(parsed.errorText));
        return;
    case maxchat::irc::UserCommandType::Clear:
        clearCurrentChat();
        return;
    case maxchat::irc::UserCommandType::ClearAll:
        clearAllChats();
        return;
    case maxchat::irc::UserCommandType::Close:
        closeTarget(m_currentTarget);
        return;
    case maxchat::irc::UserCommandType::Disconnect:
        disconnectFromCurrentServer();
        return;
    case maxchat::irc::UserCommandType::Reconnect:
        reconnectCurrentServer();
        return;
    case maxchat::irc::UserCommandType::Connect:
        connectFromCommand(parsed.command, parsed.targets, parsed.text);
        return;
    case maxchat::irc::UserCommandType::Lag:
        if (connection().measureLag()) {
            appendSystemLine(tr("* Measuring lag..."));
        } else {
            appendSystemLine(tr("! Could not send lag probe."));
        }
        return;
    case maxchat::irc::UserCommandType::Uptime:
        showUptime();
        return;
    case maxchat::irc::UserCommandType::NetInfo:
        showNetInfo();
        return;
    case maxchat::irc::UserCommandType::Alias:
    case maxchat::irc::UserCommandType::Unalias:
    case maxchat::irc::UserCommandType::Mute:
    case maxchat::irc::UserCommandType::Unmute:
    case maxchat::irc::UserCommandType::SysInfo:
    case maxchat::irc::UserCommandType::Scripts:
    case maxchat::irc::UserCommandType::Help:
        return;
    case maxchat::irc::UserCommandType::Text:
        if (parsed.targets.isEmpty()) {
            appendSystemLine(tr("! Join a channel or use /msg before sending text."));
            return;
        }
        if (connection().privmsg(parsed.targets.first(), parsed.text)) {
            appendSystemLineToTarget(parsed.targets.first(),
                                     QStringLiteral("<%1> %2").arg(nick, parsed.text), true, true);
        }
        return;
    case maxchat::irc::UserCommandType::Join:
        activateBufferTarget(parsed.targets.first());
        for (const QString& target : parsed.targets) {
            rememberTarget(target);
        }
        rebuildNetworkTree();
        if (!connection().sendRaw(
                QStringLiteral("JOIN %1").arg(parsed.targets.join(QLatin1Char(','))))) {
            appendSystemLine(tr("! Could not send JOIN."));
        }
        return;
    case maxchat::irc::UserCommandType::Part: {
        const QString target = parsed.targets.first();
        QString raw = QStringLiteral("PART %1").arg(target);
        if (!parsed.text.isEmpty()) {
            raw += QStringLiteral(" :%1").arg(parsed.text);
        }
        if (connection().sendRaw(raw)) {
            forgetTarget(target);
            if (target.compare(m_currentTarget, Qt::CaseInsensitive) == 0) {
                activateBufferTarget({});
                showConnectionStatus(
                    QStringLiteral("%1 - Connected").arg(m_connectionPlan.networkName));
            }
            rebuildNetworkTree();
        }
        return;
    }
    case maxchat::irc::UserCommandType::Cycle: {
        const QString target = parsed.targets.first();
        QString raw = QStringLiteral("PART %1").arg(target);
        if (!parsed.text.isEmpty()) {
            raw += QStringLiteral(" :%1").arg(parsed.text);
        }
        if (!connection().sendRaw(raw)) {
            appendSystemLine(tr("! Could not send PART."));
            return;
        }
        if (!connection().sendRaw(QStringLiteral("JOIN %1").arg(target))) {
            appendSystemLine(tr("! Could not send JOIN."));
            return;
        }
        rememberTarget(target);
        activateBufferTarget(target);
        rebuildNetworkTree();
        appendSystemLineToTarget(target, QStringLiteral("! Cycling %1.").arg(target), true, true);
        return;
    }
    case maxchat::irc::UserCommandType::PrivateMessage:
        if (connection().privmsg(parsed.targets.first(), parsed.text)) {
            rememberTarget(parsed.targets.first());
            if (m_pmEcho || isChannelTarget(parsed.targets.first())) {
                appendSystemLineToTarget(parsed.targets.first(),
                                         QStringLiteral("<%1> %2").arg(nick, parsed.text), true,
                                         true);
            }
            rebuildNetworkTree();
        }
        return;
    case maxchat::irc::UserCommandType::Query:
        activateBufferTarget(parsed.targets.first());
        showConnectionStatus(tr("%1 - %2 as %3")
                               .arg(m_connectionPlan.networkName, m_currentTarget, nick));
        rebuildNetworkTree();
        if (!parsed.text.isEmpty() && connection().privmsg(parsed.targets.first(), parsed.text) &&
            m_pmEcho) {
            appendSystemLineToTarget(parsed.targets.first(),
                                     QStringLiteral("<%1> %2").arg(nick, parsed.text), true, true);
        }
        return;
    case maxchat::irc::UserCommandType::Notice:
        if (connection().sendRaw(
                QStringLiteral("NOTICE %1 :%2").arg(parsed.targets.first(), parsed.text))) {
            appendSystemLineToTarget(
                parsed.targets.first(),
                QStringLiteral("-%1:%2- %3").arg(nick, parsed.targets.first(), parsed.text), true,
                true);
        } else {
            appendSystemLine(tr("! Could not send NOTICE."));
        }
        return;
    case maxchat::irc::UserCommandType::BroadcastMessage: {
        const QStringList channels = joinedChannelTargets();
        if (channels.isEmpty()) {
            appendSystemLine(tr("! No joined channels to message."));
            return;
        }
        for (const QString& channel : channels) {
            if (connection().privmsg(channel, parsed.text)) {
                appendSystemLineToTarget(
                    channel, QStringLiteral("<%1:%2> %3").arg(nick, channel, parsed.text), true,
                    true);
            } else {
                appendSystemLineToTarget(
                    channel, QStringLiteral("! Could not send message to %1.").arg(channel), true,
                    true);
            }
        }
        return;
    }
    case maxchat::irc::UserCommandType::BroadcastAction: {
        const QStringList channels = joinedChannelTargets();
        if (channels.isEmpty()) {
            appendSystemLine(tr("! No joined channels for action."));
            return;
        }
        for (const QString& channel : channels) {
            if (connection().action(channel, parsed.text)) {
                appendSystemLineToTarget(channel, QStringLiteral("* %1 %2").arg(nick, parsed.text),
                                         true, true, false, false);
            } else {
                appendSystemLineToTarget(
                    channel, QStringLiteral("! Could not send action to %1.").arg(channel), true,
                    true);
            }
        }
        return;
    }
    case maxchat::irc::UserCommandType::OpNotice: {
        const QString channel = parsed.targets.first();
        const QString target = QStringLiteral("@%1").arg(channel);
        if (connection().sendRaw(QStringLiteral("NOTICE %1 :%2").arg(target, parsed.text))) {
            appendSystemLineToTarget(channel, QStringLiteral("-> -%1- %2").arg(target, parsed.text),
                                     true, true, false, false);
        } else {
            appendSystemLine(tr("! Could not send op notice."));
        }
        return;
    }
    case maxchat::irc::UserCommandType::Action:
        if (connection().action(parsed.targets.first(), parsed.text)) {
            appendSystemLineToTarget(parsed.targets.first(),
                                     QStringLiteral("* %1 %2").arg(nick, parsed.text), true, true,
                                     false, false);
        }
        return;
    case maxchat::irc::UserCommandType::Ctcp:
        if (connection().ctcp(parsed.targets.first(), parsed.rawLine, parsed.text)) {
            appendSystemLineToTarget(
                parsed.targets.first(),
                parsed.text.isEmpty()
                    ? QStringLiteral("* CTCP %1 sent to %2")
                          .arg(parsed.rawLine, parsed.targets.first())
                    : QStringLiteral("* CTCP %1 %2 sent to %3")
                          .arg(parsed.rawLine, parsed.text, parsed.targets.first()),
                true, true);
        } else {
            appendSystemLine(tr("! Could not send CTCP."));
        }
        return;
    case maxchat::irc::UserCommandType::Sound: {
        const QString soundTarget = parsed.targets.first();
        if (isTreeStatusTarget(soundTarget)) {
            appendSystemLine(tr("! /sound needs a channel or query."));
            return;
        }
        // Send the CTCP SOUND to the room (wire parity with Python). Local
        // playback (self + received sounds) arrives with the sound subsystem
        // (audit Phase 7 / S3).
        const QString soundArgs =
            QStringLiteral("%1 %2").arg(parsed.rawLine, parsed.text).trimmed();
        if (connection().ctcp(soundTarget, QStringLiteral("SOUND"), soundArgs)) {
            appendSystemLineToTarget(
                soundTarget,
                QStringLiteral("* [sound] %1").arg(soundArgs), true, true, false,
                false);
        } else {
            appendSystemLine(tr("! Could not send sound."));
        }
        return;
    }
    case maxchat::irc::UserCommandType::ServiceMessage:
        if (connection().privmsg(parsed.targets.first(), parsed.text)) {
            const QString redactedLine = maxchat::irc::redactLine(
                QStringLiteral("PRIVMSG %1 :%2").arg(parsed.targets.first(), parsed.text));
            const int textStart = redactedLine.indexOf(QStringLiteral(" :"));
            const QString displayText =
                textStart < 0 ? parsed.text : redactedLine.mid(textStart + 2);
            appendSystemLine(tr("-> %1: %2").arg(parsed.targets.first(), displayText));
        } else {
            appendSystemLine(tr("! Could not message %1.").arg(parsed.targets.first()));
        }
        return;
    case maxchat::irc::UserCommandType::Nick:
        if (!connection().sendRaw(QStringLiteral("NICK %1").arg(parsed.text))) {
            appendSystemLine(tr("! Could not change nick."));
        }
        return;
    case maxchat::irc::UserCommandType::Whois:
        if (!connection().sendRaw(QStringLiteral("WHOIS %1").arg(parsed.targets.first()))) {
            appendSystemLine(tr("! Could not send WHOIS."));
        }
        return;
    case maxchat::irc::UserCommandType::Who:
        if (!connection().sendRaw(QStringLiteral("WHO %1").arg(parsed.targets.first()))) {
            appendSystemLine(tr("! Could not send WHO."));
        }
        return;
    case maxchat::irc::UserCommandType::Whowas:
        if (!connection().sendRaw(QStringLiteral("WHOWAS %1").arg(parsed.targets.first()))) {
            appendSystemLine(tr("! Could not send WHOWAS."));
        }
        return;
    case maxchat::irc::UserCommandType::Names:
        if (!connection().sendRaw(QStringLiteral("NAMES %1").arg(parsed.targets.first()))) {
            appendSystemLine(tr("! Could not send NAMES."));
        }
        return;
    case maxchat::irc::UserCommandType::ChannelList:
        openChannelList(true);
        if (!connection().sendRaw(parsed.text.trimmed().isEmpty()
                                      ? QStringLiteral("LIST")
                                      : QStringLiteral("LIST %1").arg(parsed.text.trimmed()))) {
            appendSystemLine(tr("! Could not send LIST."));
            if (m_channelListDialog != nullptr) {
                m_channelListDialog->setComplete(true);
            }
        }
        return;
    case maxchat::irc::UserCommandType::Topic: {
        const QString target = parsed.targets.first();
        QString raw = QStringLiteral("TOPIC %1").arg(target);
        if (!parsed.text.isEmpty()) {
            raw += QStringLiteral(" :%1").arg(parsed.text);
        }
        if (!connection().sendRaw(raw)) {
            appendSystemLine(tr("! Could not send TOPIC."));
        }
        return;
    }
    case maxchat::irc::UserCommandType::Mode:
        if (!connection().sendRaw(parsed.rawLine)) {
            appendSystemLine(tr("! Could not send MODE."));
        }
        return;
    case maxchat::irc::UserCommandType::Invite:
        if (!connection().sendRaw(
                QStringLiteral("INVITE %1 %2").arg(parsed.targets.at(0), parsed.targets.at(1)))) {
            appendSystemLine(tr("! Could not send INVITE."));
        }
        return;
    case maxchat::irc::UserCommandType::Kick: {
        QString raw = QStringLiteral("KICK %1 %2").arg(parsed.targets.at(0), parsed.targets.at(1));
        if (!parsed.text.isEmpty()) {
            raw += QStringLiteral(" :%1").arg(parsed.text);
        }
        if (!connection().sendRaw(raw)) {
            appendSystemLine(tr("! Could not send KICK."));
        }
        return;
    }
    case maxchat::irc::UserCommandType::Ban:
        if (!connection().sendRaw(
                QStringLiteral("MODE %1 +b %2").arg(parsed.targets.at(0), parsed.targets.at(2)))) {
            appendSystemLine(tr("! Could not send ban."));
        }
        return;
    case maxchat::irc::UserCommandType::KickBan: {
        const QString channel = parsed.targets.at(0);
        const QString nickOrMask = parsed.targets.at(1);
        const QString mask = parsed.targets.at(2);
        if (!connection().sendRaw(QStringLiteral("MODE %1 +b %2").arg(channel, mask))) {
            appendSystemLine(tr("! Could not send ban."));
            return;
        }
        const QString reason = parsed.text.isEmpty() ? nickOrMask : parsed.text;
        if (!connection().sendRaw(
                QStringLiteral("KICK %1 %2 :%3").arg(channel, nickOrMask, reason))) {
            appendSystemLine(tr("! Could not send KICK."));
        }
        return;
    }
    case maxchat::irc::UserCommandType::Away:
        if (!connection().sendRaw(parsed.text.isEmpty()
                                      ? QStringLiteral("AWAY")
                                      : QStringLiteral("AWAY :%1").arg(parsed.text))) {
            appendSystemLine(tr("! Could not send AWAY."));
        }
        return;
    case maxchat::irc::UserCommandType::Ignore:
        if (parsed.text.trimmed().isEmpty()) {
            appendSystemLine(m_ignoreMasks.isEmpty()
                                 ? QStringLiteral("! Ignore list is empty.")
                                 : QStringLiteral("! Ignoring: %1")
                                       .arg(m_ignoreMasks.join(QStringLiteral(", "))));
        } else {
            addIgnoreMask(parsed.text);
        }
        return;
    case maxchat::irc::UserCommandType::Unignore:
        removeIgnoreMask(parsed.text);
        return;
    case maxchat::irc::UserCommandType::Notify:
        if (parsed.text.trimmed().isEmpty()) {
            appendSystemLine(m_friendNicks.isEmpty()
                                 ? QStringLiteral("! Notify list is empty.")
                                 : QStringLiteral("! Notify list: %1")
                                       .arg(m_friendNicks.join(QStringLiteral(", "))));
        } else {
            addFriendNick(parsed.text);
        }
        return;
    case maxchat::irc::UserCommandType::Unnotify:
        removeFriendNick(parsed.text);
        return;
    case maxchat::irc::UserCommandType::Raw:
        if (!connection().sendRaw(parsed.rawLine)) {
            appendSystemLine(tr("! Could not send raw command."));
        }
        return;
    case maxchat::irc::UserCommandType::Quit:
        if (!parsed.text.isEmpty()) {
            connection().sendRaw(QStringLiteral("QUIT :%1").arg(parsed.text));
        } else {
            connection().sendRaw(QStringLiteral("QUIT"));
        }
        disconnectFromCurrentServer();
        return;
    }
}

void maxchat::ui::MainWindow::addInputHistory(const QString& text) {
    if (text.trimmed().isEmpty()) {
        return;
    }
    if (m_inputHistory.isEmpty() || m_inputHistory.last() != text) {
        m_inputHistory.append(text);
    }

    constexpr int MaxInputHistory = 200;
    while (m_inputHistory.size() > MaxInputHistory) {
        m_inputHistory.removeFirst();
    }
    m_inputHistoryIndex = m_inputHistory.size();
}

bool MainWindow::showHistoryEntry(int delta) {
    if (m_input == nullptr || m_inputHistory.isEmpty()) {
        return false;
    }

    if (delta < 0) {
        m_inputHistoryIndex = std::max(0, m_inputHistoryIndex - 1);
        setInputText(m_input, m_inputHistory.at(m_inputHistoryIndex));
    } else {
        if (m_inputHistoryIndex >= m_inputHistory.size()) {
            // Not browsing history — Down must NOT clear an in-progress draft.
            return false;
        }
        if (m_inputHistoryIndex >= m_inputHistory.size() - 1) {
            m_inputHistoryIndex = m_inputHistory.size();
            m_input->clear();
        } else {
            ++m_inputHistoryIndex;
            setInputText(m_input, m_inputHistory.at(m_inputHistoryIndex));
        }
    }
    setInputCursorPosition(m_input, inputText(m_input).size());
    return true;
}

bool MainWindow::completeInput(const bool forward) {
    if (m_input == nullptr) {
        return false;
    }

    const QString text = inputText(m_input);
    const int cursor = inputCursorPosition(m_input);

    // Continuing a cycle: the input hasn't changed since our last insertion, so
    // Tab/Backtab steps to the next/previous match instead of restarting.
    if (m_completion.active && !m_completion.matches.isEmpty() &&
        text == m_completion.lastText && cursor == m_completion.lastCursor) {
        const int n = m_completion.matches.size();
        m_completion.index = ((m_completion.index + (forward ? 1 : -1)) % n + n) % n;
        applyCompletionCandidate();
        return true;
    }

    // Fresh completion: collect every candidate matching the typed prefix.
    const QString beforeCursor = text.left(cursor);
    const int lastSpace = std::max(beforeCursor.lastIndexOf(QLatin1Char(' ')),
                                   beforeCursor.lastIndexOf(QLatin1Char('\t')));
    const int tokenStart = lastSpace < 0 ? 0 : lastSpace + 1;
    const QString prefix = beforeCursor.mid(tokenStart);
    if (prefix.isEmpty()) {
        m_completion.active = false;
        return true;
    }

    const bool commandCompletion = prefix.startsWith(QLatin1Char('/')) && tokenStart == 0;
    const QStringList candidates = completionCandidates(commandCompletion);
    QStringList matches;
    for (const QString& candidate : candidates) {
        if (candidate.startsWith(prefix, Qt::CaseInsensitive)) {
            // An exact match stays in: Tab on a fully-typed nick should still
            // append the ": " suffix (cycle-state handles re-Tab correctly).
            matches.append(candidate);
        }
    }
    if (matches.isEmpty()) {
        m_completion.active = false;
        return true;
    }

    m_completion.active = true;
    m_completion.commandCompletion = commandCompletion;
    m_completion.tokenStart = tokenStart;
    m_completion.head = text.left(tokenStart);
    m_completion.tail = text.mid(cursor);
    m_completion.matches = matches;
    m_completion.index = forward ? 0 : matches.size() - 1;
    applyCompletionCandidate();
    return true;
}

void maxchat::ui::MainWindow::applyCompletionCandidate() {
    if (m_input == nullptr || m_completion.matches.isEmpty()) {
        return;
    }
    const QString candidate = m_completion.matches.at(m_completion.index);
    const bool nickStyleSuffix = !m_completion.commandCompletion && m_completion.tokenStart == 0 &&
                                 !candidate.startsWith(QLatin1Char('#')) &&
                                 !candidate.startsWith(QLatin1Char('&'));
    const QString suffix = nickStyleSuffix ? QStringLiteral(": ") : QStringLiteral(" ");
    const QString base = m_completion.head + candidate + suffix;
    const QString full = base + m_completion.tail;
    setInputText(m_input, full);
    setInputCursorPosition(m_input, base.size());
    m_completion.lastText = full;
    m_completion.lastCursor = base.size();
}

void maxchat::ui::MainWindow::showNetworkTreeContextMenu(const QPoint& pos) {
    if (m_networkTree == nullptr) {
        return;
    }

    QTreeWidgetItem* item = m_networkTree->itemAt(pos);
    if (item == nullptr) {
        return;
    }

    // Terminal launcher nodes: open/raise or master-kill the session.
    const QString terminalId = item->data(0, TreeTerminalRole).toString();
    if (!terminalId.isEmpty()) {
        QMenu termMenu(this);
        termMenu.addAction(QStringLiteral("Open Terminal"), this, [this, terminalId]() {
            if (m_scripts != nullptr) {
                m_scripts->showTerminal(terminalId);
            }
        });
        termMenu.addSeparator();
        termMenu.addAction(QStringLiteral("Kill Terminal"), this, [this, terminalId]() {
            if (m_scripts != nullptr) {
                m_scripts->killTerminal(terminalId);
            }
        });
        termMenu.exec(m_networkTree->viewport()->mapToGlobal(pos));
        return;
    }

    const QString target = treeItemTarget(item);
    if (target.isEmpty()) {
        return;
    }
    const QString itemNetwork = treeItemNetwork(item).trimmed().isEmpty()
                                    ? activeNetworkName()
                                    : treeItemNetwork(item).trimmed();

    QMenu menu(this);
    if (item->parent() == nullptr || !m_hasConnectionPlan || isTreeStatusTarget(target)) {
        menu.addAction(QStringLiteral("Leave All Channels"), this,
                       [this, itemNetwork]() { leaveAllChannels(itemNetwork); });
        menu.addAction(QStringLiteral("Disconnect"), this,
                       [this, itemNetwork]() { disconnectNetwork(itemNetwork); });
        menu.addAction(QStringLiteral("Reconnect Now"), this,
                       [this, itemNetwork]() { reconnectNetwork(itemNetwork); });
        menu.addAction(QStringLiteral("Close Server"), this,
                       [this, itemNetwork]() { closeNetwork(itemNetwork); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Server List..."), this, &MainWindow::openServerList);
        menu.addAction(QStringLiteral("Quick Connect..."), this, &MainWindow::openQuickConnect);
    } else {
        menu.addAction(QStringLiteral("Select"), this, [this, item]() {
            if (m_networkTree != nullptr) {
                m_networkTree->setCurrentItem(item);
            }
        });
        menu.addAction(QStringLiteral("Copy Name"), this,
                       [target]() { QApplication::clipboard()->setText(target); });

        if (isChannelTarget(target)) {
            menu.addSeparator();
            menu.addAction(QStringLiteral("Refresh Topic / Names / Modes"), this,
                           [this, itemNetwork, target]() {
                               setActiveNetwork(itemNetwork);
                               connection().sendRaw(QStringLiteral("TOPIC %1").arg(target));
                               connection().sendRaw(QStringLiteral("NAMES %1").arg(target));
                               connection().sendRaw(QStringLiteral("MODE %1").arg(target));
                           });
            menu.addAction(QStringLiteral("Set Topic..."), this, [this, itemNetwork, target]() {
                setActiveNetwork(itemNetwork);
                const QString currentTopic =
                    m_chatBuffers.snapshot(bufferIdForNetworkTarget(itemNetwork, target)).topic;
                bool ok = false;
                const QString topic = QInputDialog::getText(this, QStringLiteral("Set Topic"),
                                                            QStringLiteral("Topic:"),
                                                            QLineEdit::Normal, currentTopic, &ok);
                if (ok) {
                    QString raw = QStringLiteral("TOPIC %1").arg(target);
                    if (!topic.trimmed().isEmpty()) {
                        raw += QStringLiteral(" :%1").arg(topic.trimmed());
                    }
                    connection().sendRaw(raw);
                }
            });
            menu.addAction(QStringLiteral("Channel Modes..."), this, [this, itemNetwork, target]() {
                setActiveNetwork(itemNetwork);
                activateBufferTarget(target);
                rebuildNetworkTree();
                updateChannelModeButton();
                openChannelModes();
            });
            menu.addAction(QStringLiteral("Ban List..."), this, [this, itemNetwork, target]() {
                setActiveNetwork(itemNetwork);
                openBanList(target);
            });
            menu.addAction(QStringLiteral("Leave Channel"), this, [this, itemNetwork, target]() {
                setActiveNetwork(itemNetwork);
                sendCommandOrMessage(QStringLiteral("/part %1").arg(target));
            });
        }

        menu.addSeparator();
        menu.addAction(QStringLiteral("Close"), this, [this, itemNetwork, target]() {
            setActiveNetwork(itemNetwork);
            closeTarget(target);
        });
    }

    menu.exec(m_networkTree->viewport()->mapToGlobal(pos));
}

void maxchat::ui::MainWindow::showMemberContextMenu(const QPoint& pos) {
    if (m_memberList == nullptr || !connection().isConnected()) {
        return;
    }

    QListWidgetItem* item = m_memberList->itemAt(pos);
    if (item == nullptr || item->data(Qt::UserRole + 1).toBool()) {
        return;
    }

    const QString nick = nickWithoutPrefix(item->text()).trimmed();
    if (nick.isEmpty() || nick == QStringLiteral("Members")) {
        return;
    }

    QMenu menu(this);
    menu.addAction(QStringLiteral("WhoIs %1").arg(nick), this,
                   [this, nick]() { sendCommandOrMessage(QStringLiteral("/whois %1").arg(nick)); });
    menu.addAction(QStringLiteral("Message %1").arg(nick), this,
                   [this, nick]() { openQueryForNick(nick); });
    menu.addAction(QStringLiteral("Copy Nick"), this,
                   [nick]() { QApplication::clipboard()->setText(nick); });
    menu.addAction(QStringLiteral("Send File to %1...").arg(nick), this, [this, nick]() {
        configureDcc();
        const QString path =
            QFileDialog::getOpenFileName(this, QStringLiteral("Send file to %1").arg(nick));
        if (!path.isEmpty()) {
            m_dccManager->offerSend(nick, path);
        }
    });
    menu.addAction(QStringLiteral("DCC Chat with %1").arg(nick), this, [this, nick]() {
        configureDcc();
        m_dccManager->offerChat(nick);
    });
    menu.addAction(QStringLiteral("Set Color..."), this,
                   [this, nick]() { setNickColorOverride(nick); });
    if (m_nickColorOverrides.contains(nick.toLower())) {
        menu.addAction(QStringLiteral("Reset Color"), this,
                       [this, nick]() { clearNickColorOverride(nick); });
    }
    if (!m_comicCharacterPaths.isEmpty()) {
        menu.addAction(QStringLiteral("Assign Comic Character..."), this, [this, nick]() {
            QStringList stems = m_comicCharacterPaths.keys();
            std::sort(stems.begin(), stems.end());
            QStringList options;
            options << QStringLiteral("(default / random)") << stems;
            QVariantMap settings = m_settings.loadWithDefaults();
            QVariantMap chars = settings.value(QStringLiteral("comic_chars")).toMap();
            const QString current = chars.value(nick.toLower()).toString();
            const int idx =
                current.isEmpty() ? 0 : std::max(0, static_cast<int>(stems.indexOf(current)) + 1);
            bool ok = false;
            const QString choice = QInputDialog::getItem(
                this, QStringLiteral("Assign comic character"),
                QStringLiteral("Comic character for %1:").arg(nick), options, idx, false, &ok);
            if (!ok) {
                return;
            }
            if (choice == options.first()) {
                chars.remove(nick.toLower());
            } else {
                chars.insert(nick.toLower(), choice);
            }
            settings.insert(QStringLiteral("comic_chars"), chars);
            if (!m_settings.saveRaw(settings)) {
                appendSystemLine(tr("! Could not save comic character."));
                return;
            }
            appendSystemLine(choice == options.first()
                                 ? QStringLiteral("! %1: comic character reset to default.").arg(nick)
                                 : QStringLiteral("! %1: comic character set to %2.").arg(nick, choice));
            m_comicController->refreshComic();
        });
    }
    const QString ignoreMask = normalizeIgnoreMask(nick);
    if (containsCaseInsensitive(m_ignoreMasks, ignoreMask)) {
        menu.addAction(QStringLiteral("Unignore %1").arg(nick), this,
                       [this, ignoreMask]() { removeIgnoreMask(ignoreMask); });
    } else {
        menu.addAction(QStringLiteral("Ignore %1").arg(nick), this,
                       [this, ignoreMask]() { addIgnoreMask(ignoreMask); });
    }
    if (containsCaseInsensitive(m_friendNicks, nick)) {
        menu.addAction(QStringLiteral("Remove %1 from Notify").arg(nick), this,
                       [this, nick]() { removeFriendNick(nick); });
    } else {
        menu.addAction(QStringLiteral("Add %1 to Notify").arg(nick), this,
                       [this, nick]() { addFriendNick(nick); });
    }

    const QString channel = m_currentTarget.trimmed();
    if (isChannelTarget(channel)) {
        menu.addSeparator();
        QMenu* operatorMenu = menu.addMenu(QStringLiteral("Operator"));
        const QList<QPair<QString, QString>> modeActions = {
            {QStringLiteral("Owner"), QStringLiteral("q")},
            {QStringLiteral("Admin"), QStringLiteral("a")},
            {QStringLiteral("Op"), QStringLiteral("o")},
            {QStringLiteral("Half-Op"), QStringLiteral("h")},
            {QStringLiteral("Voice"), QStringLiteral("v")},
        };
        bool first = true;
        for (const auto& modeAction : modeActions) {
            if (!first) {
                operatorMenu->addSeparator();
            }
            first = false;
            const QString label = modeAction.first;
            const QString mode = modeAction.second;
            operatorMenu->addAction(QStringLiteral("Give %1 (+%2)").arg(label, mode), this,
                                    [this, channel, mode, nick]() {
                                        sendModeChange(channel,
                                                       QStringLiteral("+%1 %2").arg(mode, nick));
                                    });
            operatorMenu->addAction(QStringLiteral("Take %1 (-%2)").arg(label, mode), this,
                                    [this, channel, mode, nick]() {
                                        sendModeChange(channel,
                                                       QStringLiteral("-%1 %2").arg(mode, nick));
                                    });
        }

        QMenu* kickBanMenu = menu.addMenu(QStringLiteral("Kick / Ban"));
        kickBanMenu->addAction(QStringLiteral("Kick"), this,
                               [this, channel, nick]() { kickNick(channel, nick); });
        kickBanMenu->addAction(
            QStringLiteral("Kick With Reason..."), this, [this, channel, nick]() {
                bool ok = false;
                const QString reason = QInputDialog::getText(
                    this, QStringLiteral("Kick %1").arg(nick), QStringLiteral("Reason:"),
                    QLineEdit::Normal, QString(), &ok);
                if (ok) {
                    kickNick(channel, nick, reason);
                }
            });
        kickBanMenu->addAction(QStringLiteral("Ban"), this,
                               [this, channel, nick]() { banNick(channel, nick); });
        kickBanMenu->addAction(QStringLiteral("Kick + Ban"), this,
                               [this, channel, nick]() { kickBanNick(channel, nick); });
    }

    menu.addSeparator();
    QMenu* ctcpMenu = menu.addMenu(QStringLiteral("CTCP"));
    for (const QString& command : {QStringLiteral("PING"), QStringLiteral("VERSION"),
                                   QStringLiteral("TIME"), QStringLiteral("CLIENTINFO")}) {
        ctcpMenu->addAction(command, this,
                            [this, nick, command]() { connection().ctcp(nick, command); });
    }

    menu.exec(m_memberList->viewport()->mapToGlobal(pos));
}

void maxchat::ui::MainWindow::openQueryForNick(const QString& nick) {
    const QString cleanNick = nickWithoutPrefix(nick).trimmed();
    if (cleanNick.isEmpty() || cleanNick == QStringLiteral("Members")) {
        return;
    }

    activateBufferTarget(cleanNick);
    rebuildNetworkTree();
    updateChannelModeButton();
    showConnectionStatus(
        QStringLiteral("%1 - private chat with %2").arg(m_connectionPlan.networkName, cleanNick));
}

void maxchat::ui::MainWindow::sendModeChange(const QString& channel, const QString& change) {
    if (channel.trimmed().isEmpty() || change.trimmed().isEmpty()) {
        return;
    }
    if (!connection().sendRaw(QStringLiteral("MODE %1 %2").arg(channel, change.trimmed()))) {
        appendSystemLine(tr("! Could not send MODE."));
    }
}

void maxchat::ui::MainWindow::kickNick(const QString& channel, const QString& nick, const QString& reason) {
    const QString cleanNick = nickWithoutPrefix(nick).trimmed();
    if (channel.trimmed().isEmpty() || cleanNick.isEmpty()) {
        return;
    }
    const QString kickReason = reason.trimmed().isEmpty() ? cleanNick : reason.trimmed();
    if (!connection().sendRaw(
            QStringLiteral("KICK %1 %2 :%3").arg(channel, cleanNick, kickReason))) {
        appendSystemLine(tr("! Could not send KICK."));
    }
}

void maxchat::ui::MainWindow::banNick(const QString& channel, const QString& nick) {
    const QString cleanNick = nickWithoutPrefix(nick).trimmed();
    if (channel.trimmed().isEmpty() || cleanNick.isEmpty()) {
        return;
    }
    sendModeChange(channel, QStringLiteral("+b %1!*@*").arg(cleanNick));
}

void maxchat::ui::MainWindow::kickBanNick(const QString& channel, const QString& nick) {
    banNick(channel, nick);
    kickNick(channel, nick);
}

void maxchat::ui::MainWindow::addIgnoreMask(const QString& mask) {
    const QString normalized = normalizeIgnoreMask(mask);
    if (normalized.isEmpty()) {
        return;
    }
    if (containsCaseInsensitive(m_ignoreMasks, normalized)) {
        appendSystemLine(tr("! Already ignoring %1.").arg(normalized));
        return;
    }

    m_ignoreMasks.append(normalized);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("ignores"), m_ignoreMasks);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save ignore list."));
        return;
    }
    m_connection.setIgnoreMasks(m_ignoreMasks);
    for (auto* irc : std::as_const(m_connectionsByNetwork)) {
        if (irc != nullptr) {
            irc->setIgnoreMasks(m_ignoreMasks);
        }
    }
    appendSystemLine(tr("! Ignoring %1.").arg(normalized));
}

void maxchat::ui::MainWindow::removeIgnoreMask(const QString& mask) {
    const QString normalized = normalizeIgnoreMask(mask);
    if (normalized.isEmpty()) {
        return;
    }
    if (!containsCaseInsensitive(m_ignoreMasks, normalized)) {
        appendSystemLine(tr("! %1 is not in the ignore list.").arg(normalized));
        return;
    }

    m_ignoreMasks = removeCaseInsensitive(m_ignoreMasks, normalized);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("ignores"), m_ignoreMasks);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save ignore list."));
        return;
    }
    m_connection.setIgnoreMasks(m_ignoreMasks);
    for (auto* irc : std::as_const(m_connectionsByNetwork)) {
        if (irc != nullptr) {
            irc->setIgnoreMasks(m_ignoreMasks);
        }
    }
    appendSystemLine(tr("! No longer ignoring %1.").arg(normalized));
}

void maxchat::ui::MainWindow::addMutedChannel(const QString& channel) {
    const QString key = mutedChannelKey(channel);
    if (key.isEmpty()) {
        appendSystemLine(tr("! Usage: /mute [#channel]"));
        return;
    }
    if (containsCaseInsensitive(m_mutedChannelKeys, key)) {
        appendSystemLine(
            QStringLiteral("! Highlights are already muted for %1.").arg(channel.trimmed()));
        return;
    }

    QStringList next = m_mutedChannelKeys;
    next.append(key);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("muted_channels"), next);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save muted channels."));
        return;
    }

    m_mutedChannelKeys = next;
    appendSystemLine(tr("! Muted highlights for %1 on %2.")
                         .arg(channel.trimmed(), activeNetworkName()));
}

void maxchat::ui::MainWindow::removeMutedChannel(const QString& channel) {
    const QString key = mutedChannelKey(channel);
    if (key.isEmpty()) {
        appendSystemLine(tr("! Usage: /unmute [#channel]"));
        return;
    }
    if (!containsCaseInsensitive(m_mutedChannelKeys, key)) {
        appendSystemLine(
            QStringLiteral("! Highlights are not muted for %1.").arg(channel.trimmed()));
        return;
    }

    const QStringList next = removeCaseInsensitive(m_mutedChannelKeys, key);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("muted_channels"), next);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save muted channels."));
        return;
    }

    m_mutedChannelKeys = next;
    appendSystemLine(tr("! Unmuted highlights for %1 on %2.")
                         .arg(channel.trimmed(), activeNetworkName()));
}

bool MainWindow::confirmQuitIfConnected() {
    if (m_confirmQuit && anyNetworkConnectionIsConnected()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Quit MaxChat"),
            QStringLiteral("You are still connected. Quit anyway?"));
        if (answer != QMessageBox::Yes) {
            return false;
        }
    }
    return true;
}

void MainWindow::saveWindowGeometry() {
    QVariantMap s = m_settings.loadRaw();
    s.insert(QStringLiteral("window_geometry"), QString::fromLatin1(saveGeometry().toBase64()));
    (void)m_settings.saveRaw(s);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmQuitIfConnected()) {
        event->ignore();
        return;
    }
    saveWindowGeometry();
    QMainWindow::closeEvent(event);
}

void MainWindow::quitApplication() {
    // The tray "Quit" runs while the window is hidden in the tray; close() on a
    // hidden window does not trigger quitOnLastWindowClosed, so quit explicitly.
    if (!confirmQuitIfConnected()) {
        return;
    }
    saveWindowGeometry();
    qApp->quit();
}

void MainWindow::noteUserActivity() {
    // Any typed input resets the idle clock and clears an auto-set away.
    if (m_autoAwayMins > 0) {
        m_autoAwayTimer.start(m_autoAwayMins * 60 * 1000);
    }
    if (m_autoAwayActive) {
        m_autoAwayActive = false;
        for (auto* irc : std::as_const(m_connectionsByNetwork)) {
            if (irc != nullptr && irc->isConnected()) {
                irc->sendRaw(QStringLiteral("AWAY"));
            }
        }
    }
}

void MainWindow::triggerAutoAway() {
    if (m_autoAwayMins <= 0 || m_autoAwayActive || !anyNetworkConnectionIsConnected()) {
        return;
    }
    m_autoAwayActive = true;
    for (auto* irc : std::as_const(m_connectionsByNetwork)) {
        if (irc != nullptr && irc->isConnected()) {
            irc->sendRaw(QStringLiteral("AWAY :Auto-away (idle)"));
        }
    }
}

void MainWindow::applyCtcpVersion(const QVariantMap& settings) {
    const bool hide = settings.value(QStringLiteral("hide_version"), false).toBool();
    const QString custom = settings.value(QStringLiteral("ctcp_version")).toString();
    const bool ping = settings.value(QStringLiteral("ctcp_respond_ping"), true).toBool();
    const bool time = settings.value(QStringLiteral("ctcp_respond_time"), true).toBool();
    const bool clientInfo = settings.value(QStringLiteral("ctcp_respond_clientinfo"), true).toBool();
    for (auto* irc : std::as_const(m_connectionsByNetwork)) {
        if (irc != nullptr) {
            irc->setCtcpVersion(hide, custom);
            irc->setCtcpOptions(ping, time, clientInfo);
        }
    }
    m_connection.setCtcpVersion(hide, custom);
    m_connection.setCtcpOptions(ping, time, clientInfo);
}

bool MainWindow::textHighlightsMe(const QString& text, const QString& nick) const {
    // Match against the plain text (Python strips mIRC codes first) so a colour
    // or bold code adjacent to / inside your nick can't hide a highlight.
    const QString plain = maxchat::irc::stripFormatting(text);
    // Whole-word match: bare contains() made nick "art" highlight on "start"
    // and word "hi" on "this". \b treats IRC nick punctuation ([]{}\|^`-)
    // as word chars poorly, so use explicit boundary lookarounds on letters
    // and digits instead.
    const auto wordHit = [&plain](const QString& needle) {
        const QRegularExpression re(
            QStringLiteral("(?<![A-Za-z0-9_])%1(?![A-Za-z0-9_])")
                .arg(QRegularExpression::escape(needle)),
            QRegularExpression::CaseInsensitiveOption);
        return re.match(plain).hasMatch();
    };
    if (!nick.isEmpty() && wordHit(nick)) {
        return true;
    }
    for (const QString& word : m_highlightWords) {
        const QString trimmed = word.trimmed();
        if (!trimmed.isEmpty() && wordHit(trimmed)) {
            return true;
        }
    }
    return false;
}

bool MainWindow::isMutedChannel(const QString& channel) const {
    const QString key = mutedChannelKey(channel);
    return !key.isEmpty() && containsCaseInsensitive(m_mutedChannelKeys, key);
}

QString MainWindow::mutedChannelKey(const QString& channel) const {
    const QString cleanChannel = channel.trimmed();
    if (!isChannelTarget(cleanChannel)) {
        return {};
    }
    return QStringLiteral("%1/%2").arg(activeNetworkName().toCaseFolded(),
                                       cleanChannel.toCaseFolded());
}

void maxchat::ui::MainWindow::showAliasList() {
    if (m_commandAliases.isEmpty()) {
        appendSystemLine(tr("! No command aliases are defined."));
        return;
    }

    QStringList keys = m_commandAliases.keys();
    keys.sort(Qt::CaseInsensitive);
    appendSystemLine(tr("! Command aliases:"));
    for (const QString& key : keys) {
        appendSystemLine(
            QStringLiteral("! /%1 = %2").arg(key, m_commandAliases.value(key).toString()));
    }
}

void maxchat::ui::MainWindow::setAliasCommand(const QString& name, const QString& expansion) {
    QString cleanName = name.trimmed().toLower();
    if (cleanName.startsWith(QLatin1Char('/'))) {
        cleanName.remove(0, 1);
    }
    const QString cleanExpansion = expansion.trimmed();
    if (cleanName.isEmpty() || cleanExpansion.isEmpty()) {
        appendSystemLine(tr("! Usage: /alias name command"));
        return;
    }

    m_commandAliases.insert(cleanName, cleanExpansion);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("command_aliases"), m_commandAliases);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save command aliases."));
        return;
    }
    appendSystemLine(tr("! Alias /%1 = %2").arg(cleanName, cleanExpansion));
}

void maxchat::ui::MainWindow::removeAliasCommand(const QString& name) {
    QString cleanName = name.trimmed().toLower();
    if (cleanName.startsWith(QLatin1Char('/'))) {
        cleanName.remove(0, 1);
    }
    if (cleanName.isEmpty()) {
        appendSystemLine(tr("! Usage: /unalias name"));
        return;
    }
    if (!m_commandAliases.contains(cleanName)) {
        appendSystemLine(tr("! Alias /%1 is not defined.").arg(cleanName));
        return;
    }

    m_commandAliases.remove(cleanName);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("command_aliases"), m_commandAliases);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save command aliases."));
        return;
    }
    appendSystemLine(tr("! Removed alias /%1.").arg(cleanName));
}

void maxchat::ui::MainWindow::addFriendNick(const QString& nick) {
    const QString cleanNick = nickWithoutPrefix(nick).trimmed();
    if (cleanNick.isEmpty()) {
        return;
    }
    if (containsCaseInsensitive(m_friendNicks, cleanNick)) {
        appendSystemLine(tr("! %1 is already on the notify list.").arg(cleanNick));
        return;
    }

    m_friendNicks.append(cleanNick);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("friends"), m_friendNicks);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save notify list."));
        return;
    }
    m_haveFriendSnapshot = false;
    m_haveFriendSnapshotByNetwork.clear();
    pollFriends();
    if (anyNetworkConnectionIsConnected()) {
        m_friendPollTimer.start();
    }
    appendSystemLine(tr("! Added %1 to the notify list.").arg(cleanNick));
}

void maxchat::ui::MainWindow::removeFriendNick(const QString& nick) {
    const QString cleanNick = nickWithoutPrefix(nick).trimmed();
    if (cleanNick.isEmpty()) {
        return;
    }
    if (!containsCaseInsensitive(m_friendNicks, cleanNick)) {
        appendSystemLine(tr("! %1 is not on the notify list.").arg(cleanNick));
        return;
    }

    m_friendNicks = removeCaseInsensitive(m_friendNicks, cleanNick);
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("friends"), m_friendNicks);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save notify list."));
        return;
    }
    m_onlineFriends.remove(cleanNick.toCaseFolded());
    for (auto it = m_onlineFriendsByNetwork.begin(); it != m_onlineFriendsByNetwork.end(); ++it) {
        it.value().remove(cleanNick.toCaseFolded());
    }
    m_haveFriendSnapshot = false;
    m_haveFriendSnapshotByNetwork.clear();
    if (m_friendNicks.isEmpty()) {
        m_friendPollTimer.stop();
    } else {
        pollFriends();
    }
    appendSystemLine(tr("! Removed %1 from the notify list.").arg(cleanNick));
}

void maxchat::ui::MainWindow::pollFriends() {
    if (m_friendNicks.isEmpty()) {
        return;
    }
    const QString raw = QStringLiteral("ISON %1").arg(m_friendNicks.join(QLatin1Char(' ')));
    bool sent = false;
    if (m_connection.isConnected()) {
        m_connection.sendRaw(raw);
        sent = true;
    }
    for (auto it = m_connectionsByNetwork.cbegin(); it != m_connectionsByNetwork.cend(); ++it) {
        if (it.value() != nullptr && it.value()->isConnected()) {
            it.value()->sendRaw(raw);
            sent = true;
        }
    }
    if (!sent) {
        m_friendPollTimer.stop();
    }
}

void maxchat::ui::MainWindow::handleIsonReply(const QStringList& onlineNicks) {
    handleIsonReplyForNetwork(currentLogNetwork(), onlineNicks);
}

void maxchat::ui::MainWindow::handleIsonReplyForNetwork(const QString& network, const QStringList& onlineNicks) {
    QSet<QString> nowOnline;
    for (const QString& nick : onlineNicks) {
        if (!nick.trimmed().isEmpty()) {
            nowOnline.insert(nick.toCaseFolded());
        }
    }

    const QString cleanNetwork =
        network.trimmed().isEmpty() ? QStringLiteral("Server") : network.trimmed();
    if (!m_haveFriendSnapshotByNetwork.value(cleanNetwork, false)) {
        m_onlineFriends = nowOnline;
        m_onlineFriendsByNetwork.insert(cleanNetwork, nowOnline);
        m_haveFriendSnapshot = true;
        m_haveFriendSnapshotByNetwork.insert(cleanNetwork, true);
        QStringList onlineDisplay;
        for (const QString& friendNick : m_friendNicks) {
            if (nowOnline.contains(friendNick.toCaseFolded())) {
                onlineDisplay.append(friendNick);
            }
        }
        if (!onlineDisplay.isEmpty()) {
            appendSystemLineToNetworkTarget(cleanNetwork, QStringLiteral("server"),
                                            QStringLiteral("! Notify online: %1")
                                                .arg(onlineDisplay.join(QStringLiteral(", "))));
        }
        return;
    }

    const QSet<QString> previousOnline = m_onlineFriendsByNetwork.value(cleanNetwork);
    for (const QString& friendNick : m_friendNicks) {
        const QString key = friendNick.toCaseFolded();
        const bool online = nowOnline.contains(key);
        const bool wasOnline = previousOnline.contains(key);
        if (online && !wasOnline) {
            appendSystemLineToNetworkTarget(cleanNetwork, QStringLiteral("server"),
                                            QStringLiteral("! %1 is online.").arg(friendNick));
        } else if (!online && wasOnline) {
            appendSystemLineToNetworkTarget(cleanNetwork, QStringLiteral("server"),
                                            QStringLiteral("! %1 went offline.").arg(friendNick));
        }
    }
    m_onlineFriends = nowOnline;
    m_onlineFriendsByNetwork.insert(cleanNetwork, nowOnline);
}

bool MainWindow::shouldDropForFlood(const QString& sender, const QString& ownNick) {
    const QString cleanSender = nickWithoutPrefix(sender).trimmed();
    if (cleanSender.isEmpty() || cleanSender.compare(ownNick, Qt::CaseInsensitive) == 0 ||
        containsCaseInsensitive(m_friendNicks, cleanSender)) {
        return false;
    }

    const QString key =
        QStringLiteral("%1/%2").arg(currentLogNetwork(), cleanSender.toCaseFolded());
    if (!m_floodGuard.recordMessage(key, QDateTime::currentMSecsSinceEpoch())) {
        return false;
    }

    addIgnoreMask(cleanSender);
    appendSystemLine(tr("! Auto-ignored %1 for flooding. Use /unignore %1 to undo.")
                         .arg(cleanSender));
    return true;
}

bool MainWindow::findInChat(const QString& text, bool backwards, bool caseSensitive,
                            bool wrapSearch) {
    const QString needle = text.trimmed();
    if (m_chatView == nullptr || needle.isEmpty()) {
        statusBar()->showMessage(tr("Enter text to find."));
        return false;
    }

    QTextDocument::FindFlags flags;
    if (backwards) {
        flags |= QTextDocument::FindBackward;
    }
    if (caseSensitive) {
        flags |= QTextDocument::FindCaseSensitively;
    }

    const QTextCursor originalCursor = m_chatView->textCursor();
    if (m_chatView->find(needle, flags)) {
        statusBar()->showMessage(tr("Found \"%1\".").arg(needle));
        return true;
    }

    if (wrapSearch) {
        QTextCursor cursor(m_chatView->document());
        if (backwards) {
            cursor.movePosition(QTextCursor::End);
        } else {
            cursor.movePosition(QTextCursor::Start);
        }
        m_chatView->setTextCursor(cursor);
        if (m_chatView->find(needle, flags)) {
            statusBar()->showMessage(tr("Found \"%1\".").arg(needle));
            return true;
        }
    }

    m_chatView->setTextCursor(originalCursor);
    statusBar()->showMessage(tr("No matches for \"%1\".").arg(needle));
    return false;
}

QStringList MainWindow::completionCandidates(bool commandCompletion) const {
    QStringList candidates;
    QSet<QString> seen;
    const auto appendUnique = [&candidates, &seen](const QString& value) {
        const QString trimmed = value.trimmed();
        if (trimmed.isEmpty()) {
            return;
        }
        const QString key = trimmed.toCaseFolded();
        if (seen.contains(key)) {
            return;
        }
        seen.insert(key);
        candidates.append(trimmed);
    };

    if (commandCompletion) {
        for (const QString& command : maxchat::irc::supportedCommandNames()) {
            appendUnique(QStringLiteral("/%1").arg(command));
        }
        for (auto it = m_commandAliases.cbegin(); it != m_commandAliases.cend(); ++it) {
            appendUnique(QStringLiteral("/%1").arg(it.key()));
        }
        return candidates;
    }

    appendUnique(m_currentTarget);
    for (const QString& target : m_openTargets) {
        appendUnique(target);
    }
    for (const QString& channel : m_connectionPlan.autojoin) {
        appendUnique(channel);
    }
    if (m_memberList != nullptr) {
        for (int row = 0; row < m_memberList->count(); ++row) {
            const QListWidgetItem* item = m_memberList->item(row);
            if (item == nullptr || item->text() == QStringLiteral("Members")) {
                continue;
            }
            appendUnique(nickWithoutPrefix(item->text()));
        }
    }
    return candidates;
}

// Theme-derived colour resolution lives in AppearanceController now (decomp
// phase 3 / A1); these stay as thin forwarders so their many callers (member
// list recolor, the QSS, chatLineFormatOptions) don't all change.
QColor MainWindow::resolvedChatBackground() const {
    return m_appearance->resolvedChatBackground();
}

QStringList MainWindow::effectiveNickPalette(bool* monoOut) const {
    return m_appearance->effectiveNickPalette(monoOut);
}

maxchat::core::ChatLineFormatOptions MainWindow::chatLineFormatOptions() const {
    maxchat::core::ChatLineFormatOptions options;
    options.showTimestamp = m_showTimestamps;
    options.alignNicks = m_alignNicks;
    options.separatorLine = m_separatorLine;
    options.renderFormatting = m_showFormatting;
    options.colorNicks = m_coloredNicks;
    options.nickColumnWidth = m_nickColumnWidth;
    options.timestamp = timestampText();

    // All theme-derived colour resolution lives in AppearanceController (which
    // wraps the pure resolveChatRenderTheme — R0). This method keeps only the
    // user-pref toggles above, the per-nick overrides below, and the timestamp.
    const maxchat::ui::ChatRenderTheme rt = m_appearance->buildChatRenderTheme();
    options.timestampColor = rt.timestampColor;
    if (!rt.bracketColor.isEmpty()) {
        options.bracketColor = rt.bracketColor;
    }
    if (!rt.systemColor.isEmpty()) {
        options.systemColor = rt.systemColor;
    }
    options.defaultBackground = rt.defaultBackground;
    options.defaultForeground = rt.defaultForeground;
    options.nickPalette = rt.nickPalette;
    options.monoNicks = rt.monoNicks;
    for (auto it = m_nickColorOverrides.constBegin(); it != m_nickColorOverrides.constEnd();
         ++it) {
        options.nickColorOverrides.insert(it.key(), it.value().toString());
    }
    return options;
}

QString MainWindow::timestampText() const {
    return QDateTime::currentDateTime().toString(qtDateTimeFormat(m_timestampFormat));
}

// These four are thin forwarders to ChatPane, which owns the document-insert
// logic now (render-pipeline R1). MainWindow still computes the per-line format/
// indent and the preview cache registration; ChatPane does the DOM work.
void maxchat::ui::MainWindow::appendPlainChatLine(const QString& line) {
    m_chatPane->appendPlain(line);
}

void maxchat::ui::MainWindow::appendHtmlChatLine(const QString& html) {
    m_chatPane->appendHtml(html);
}

void maxchat::ui::MainWindow::appendFormattedChatLine(const maxchat::core::FormattedChatLine& line) {
    m_chatPane->appendFormatted(line, m_indentWrap);
}

void maxchat::ui::MainWindow::appendPreviewHtmlLine(const QString& html) {
    if (m_chatPane == nullptr) {
        return;
    }
    // The indent prefix is the (empty-line) timestamp+nick column; ChatPane
    // measures its pixel width and indents the card to align with chat text. The
    // image-cache register + missing-image fetch request happen inside ChatPane
    // (R3); MainWindow only services the fetch via the previewImageNeeded hook.
    const maxchat::core::FormattedChatLine column =
        maxchat::core::formatChatLine(QString(), chatLineFormatOptions());
    m_chatPane->appendPreviewHtml(html, column.prefixPlain);
}

// ChatPaneDelegate override: does the active buffer reference this image URL?
// (The buffer model lives here; ChatPane asks before re-rendering on arrival.)
bool maxchat::ui::MainWindow::activeBufferReferencesImage(const QString& url) {
    const maxchat::core::ChatBufferSnapshot snapshot =
        m_chatBuffers.snapshot(bufferIdForTarget(m_currentTarget));
    for (const maxchat::core::ChatBufferLine& line : snapshot.lines) {
        if (!line.htmlText.isEmpty() && line.htmlText.contains(url)) {
            return true;
        }
    }
    return false;
}

void maxchat::ui::MainWindow::handlePreviewImageFetched(const QUrl& url, const QImage& image) {
    // Scale to the OG render bound (QTextDocument ignores CSS max-width/height),
    // then hand to ChatPane's preview cache (R3): it caches + re-renders to swap
    // the broken <img> for the decoded image while preserving scroll.
    const int maxW = m_ogRenderOptions.maxImageWidth;
    const int maxH = m_ogRenderOptions.maxImageHeight;
    const QImage scaled = (!image.isNull() && (image.width() > maxW || image.height() > maxH))
        ? image.scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : image;
    m_chatPane->onPreviewImageReady(url, scaled);
}

void maxchat::ui::MainWindow::handlePreviewImageFailed(const QUrl& url, const QString& reason) {
    Q_UNUSED(reason);
    m_chatPane->onPreviewImageFailed(url);
}

void maxchat::ui::MainWindow::appendPreviewHtml(const QString& html) {
    if (html.isEmpty()) {
        return;
    }
    appendPreviewHtmlToNetworkTarget(currentLogNetwork(), m_currentTarget, html);
}

void maxchat::ui::MainWindow::appendPreviewHtmlToNetworkTarget(const QString& network, const QString& target,
                                                  const QString& html) {
    if (html.isEmpty()) {
        return;
    }

    maxchat::core::ChatBufferLine line;
    line.htmlText = html;
    line.localEcho = true;
    const maxchat::core::ChatBufferId bufferId = bufferIdForNetworkTarget(network, target);
    const int lineCountBefore = static_cast<int>(m_chatBuffers.snapshot(bufferId).lines.size());
    const bool stored = m_chatBuffers.appendLine(bufferId, line);
    Q_UNUSED(stored);
    const bool active = isActiveBufferTarget(network, target);
    noteUnreadBoundary(bufferId, active, lineCountBefore);
    if (active) {
        appendPreviewHtmlLine(html);
    }
}

void maxchat::ui::MainWindow::appendSystemLine(const QString& line, bool logLine) {
    appendSystemLineToTarget(m_currentTarget, line, logLine);
}

void maxchat::ui::MainWindow::appendSystemLineToTarget(const QString& target, const QString& line, bool logLine,
                                          bool localEcho, bool highlight, bool systemStyling) {
    appendSystemLineToNetworkTarget(currentLogNetwork(), target, line, logLine, localEcho,
                                    highlight, systemStyling);
}

void maxchat::ui::MainWindow::appendSystemLineToNetworkTarget(const QString& network, const QString& target,
                                                 const QString& line, bool logLine, bool localEcho,
                                                 bool highlight, bool systemStyling) {
    rememberUrlsFromLine(line);
    maxchat::core::ChatLineFormatOptions formatOptions = chatLineFormatOptions();
    formatOptions.systemLine = systemStyling;
    const maxchat::core::FormattedChatLine display =
        maxchat::core::formatChatLine(line, formatOptions);
    const bool shouldQueuePreviews = logLine && !m_replayingLog;
    if (logLine) {
        appendChatLogLineForNetworkTarget(network, target, line);
    }

    maxchat::core::ChatBufferLine bufferLine;
    bufferLine.plainText = display.plainText;
    bufferLine.htmlText = display.html;
    bufferLine.sourceText = line;
    bufferLine.localEcho = localEcho;
    bufferLine.highlight = highlight;
    bufferLine.systemLine = systemStyling;
    const maxchat::core::ChatBufferId bufferId = bufferIdForNetworkTarget(network, target);
    const int lineCountBefore = static_cast<int>(m_chatBuffers.snapshot(bufferId).lines.size());
    const bool stored = m_chatBuffers.appendLine(bufferId, bufferLine);
    Q_UNUSED(stored);
    if (stored) {
        updateNetworkTreeLabels();
    }

    const bool active = isActiveBufferTarget(network, target);
    noteUnreadBoundary(bufferId, active, lineCountBefore);
    if (active) {
        appendFormattedChatLine(display);
        if (m_comicMode) {
            m_comicController->refreshComic();
        }
    }
    if (active && shouldQueuePreviews) {
        m_previewFetcher->queueFromLine(display.plainText);
    }
    // The status bar is for transient status (connect/find/etc.), not a mirror of
    // chat — echoing every appended line here is what put your own "<nick> msg"
    // in the bottom bar.
}

void maxchat::ui::MainWindow::appendRawLogLine(const QString& line) {
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    m_rawLogLines.append(trimmed);
    while (m_rawLogLines.size() > MaxRawLogLines) {
        m_rawLogLines.removeFirst();
    }

    if (m_rawLogDialog != nullptr) {
        m_rawLogDialog->appendLine(trimmed);
    }
}

void maxchat::ui::MainWindow::appendChatLogLine(const QString& line) {
    appendChatLogLineForTarget(m_currentTarget, line);
}

void maxchat::ui::MainWindow::appendChatLogLineForTarget(const QString& target, const QString& line) {
    appendChatLogLineForNetworkTarget(currentLogNetwork(), target, line);
}

void maxchat::ui::MainWindow::appendChatLogLineForNetworkTarget(const QString& network, const QString& target,
                                                   const QString& line) {
    if (!m_loggingEnabled || m_replayingLog || line.trimmed().isEmpty()) {
        return;
    }
    const bool logged = m_chatLogStore.appendLine(
        network.trimmed().isEmpty() ? QStringLiteral("Server") : network.trimmed(),
        target.trimmed().isEmpty() ? QStringLiteral("server") : target.trimmed(), line);
    Q_UNUSED(logged);
}

void maxchat::ui::MainWindow::rememberUrlsFromLine(const QString& line) {
    const QStringList urls = maxchat::core::extractUrls(line);
    if (urls.isEmpty()) {
        return;
    }

    QStringList newUrls;
    for (const QString& url : urls) {
        bool seen = false;
        for (const QString& existing : m_urlList) {
            if (existing.compare(url, Qt::CaseInsensitive) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            m_urlList.append(url);
            newUrls.append(url);
        }
    }

    if (newUrls.isEmpty()) {
        return;
    }

    bool trimmed = false;
    while (m_urlList.size() > MaxUrlListItems) {
        m_urlList.removeFirst();
        trimmed = true;
    }

    if (m_urlListDialog != nullptr) {
        if (trimmed) {
            m_urlListDialog->setUrls(m_urlList);
        } else {
            m_urlListDialog->appendUrls(newUrls);
        }
    }
}

void maxchat::ui::MainWindow::showConnectionStatus(const QString& line) {
    // Connection lifecycle / context goes to the bottom status bar. The topic bar
    // is reserved for the channel topic (set in renderActiveBufferMetadata).
    statusBar()->showMessage(line);
}

void maxchat::ui::MainWindow::updateWindowTitle() {
    // Active network/channel context lives in the window title (UI_SURFACES.md),
    // not the topic or status bars.
    QString title = QStringLiteral("%1 %2").arg(app::displayName(), app::version());
    if (m_hasConnectionPlan && !m_connectionPlan.networkName.isEmpty()) {
        title += QStringLiteral(" — %1").arg(m_connectionPlan.networkName);
        const QString target = m_currentTarget.trimmed();
        if (!target.isEmpty() && !isTreeStatusTarget(target)) {
            title += QStringLiteral(" / %1").arg(target);
        }
    }
    setWindowTitle(title);
}

void maxchat::ui::MainWindow::updateNickLabel() {
    if (m_nickLabel == nullptr) {
        return;
    }
    const QString nick =
        m_hasConnectionPlan ? currentNickForNetwork(activeNetworkName()) : QString();
    m_nickLabel->setText(nick.isEmpty() ? QString() : QStringLiteral("%1:").arg(nick));
    m_nickLabel->setVisible(!nick.isEmpty());
}

maxchat::core::ChatBufferId MainWindow::bufferIdForTarget(const QString& target) {
    return bufferIdForNetworkTarget(currentLogNetwork(), target);
}

maxchat::core::ChatBufferId MainWindow::bufferIdForNetworkTarget(const QString& network,
                                                                 const QString& target) {
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("server"), Qt::CaseInsensitive) == 0) {
        return m_chatBuffers.ensureServerBuffer(
            network.trimmed().isEmpty() ? QStringLiteral("Server") : network);
    }
    if (isChannelTarget(trimmed)) {
        return m_chatBuffers.ensureChannelBuffer(
            network.trimmed().isEmpty() ? QStringLiteral("Server") : network, trimmed);
    }
    return m_chatBuffers.ensureQueryBuffer(
        network.trimmed().isEmpty() ? QStringLiteral("Server") : network, trimmed);
}

bool MainWindow::isActiveBufferTarget(const QString& target) const {
    return isActiveBufferTarget(currentLogNetwork(), target);
}

bool MainWindow::isActiveBufferTarget(const QString& network, const QString& target) const {
    if (m_backgroundNetworkContext) {
        return false;
    }
    if (network.trimmed().compare(activeNetworkName(), Qt::CaseInsensitive) != 0) {
        return false;
    }
    const QString trimmed = target.trimmed();
    const QString current = m_currentTarget.trimmed();
    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("server"), Qt::CaseInsensitive) == 0) {
        return current.isEmpty();
    }
    return current.compare(trimmed, Qt::CaseInsensitive) == 0;
}

void maxchat::ui::MainWindow::activateBufferTarget(const QString& target) {
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("server"), Qt::CaseInsensitive) == 0) {
        m_currentTarget.clear();
    } else {
        rememberTarget(trimmed);
        m_currentTarget = trimmed;
    }
    rememberNetwork(activeNetworkName());
    m_currentTargetByNetwork.insert(activeNetworkName(), m_currentTarget);
    m_openTargetsByNetwork.insert(activeNetworkName(), m_openTargets);

    // On a buffer's first open, seed its stored history (dimmed) + "Chat ended"
    // divider so resume shows and survives later switches. Replay isn't "unread".
    const bool seeded =
        m_currentTarget.isEmpty()
            ? false
            : seedReplayForBuffer(activeNetworkName(), m_currentTarget);
    if (seeded) {
        (void)m_chatBuffers.markRead(bufferIdForTarget(m_currentTarget));
    }

    const maxchat::core::ChatBufferId incoming = bufferIdForTarget(m_currentTarget);
    const bool activeSet = m_chatBuffers.setActiveBuffer(incoming);
    Q_UNUSED(activeSet);
    renderActiveBuffer();
    renderActiveBufferMetadata();
    updateNetworkTreeLabels();
    syncBufferTabs();

    // Apply per-buffer comic view state: comic is opted in per channel; the
    // toggle is greyed out on the server buffer (panels make no sense there).
    const bool serverBuffer = m_currentTarget.trimmed().isEmpty();
    const QString key = comicKey(activeNetworkName(), m_currentTarget);
    const bool viewVisible = !serverBuffer && m_comicEnabledBuffers.contains(key);
    m_chatPane->setComicVisible(viewVisible);
    if (m_comicModeAction != nullptr) {
        m_comicModeAction->setEnabled(!serverBuffer);
        if (m_comicModeAction->isChecked() != viewVisible) {
            const QSignalBlocker blocker(m_comicModeAction);
            m_comicModeAction->setChecked(viewVisible);
        }
    }
}

void maxchat::ui::MainWindow::renderActiveBuffer() {
    if (m_chatPane == nullptr) {
        return;
    }
    // ChatPane owns the unread-marker / dim-replay / per-line formatting loop now
    // (render-pipeline R2). MainWindow keeps the model + theme decisions and hands
    // down the resolved render inputs; the comic view is still refreshed here
    // until R4 folds it in.
    const maxchat::core::ChatBufferId currentId = bufferIdForTarget(m_currentTarget);
    const maxchat::core::ChatBufferSnapshot snapshot = m_chatBuffers.snapshot(currentId);
    const QString markerKey = currentId.network + QChar(0x1f) + currentId.target;

    ChatPane::BufferRenderOptions options;
    options.baseOptions = chatLineFormatOptions();
    options.timestampFormat = qtDateTimeFormat(m_timestampFormat);
    options.markerLine = m_markerLine;
    options.markerCount = m_bufferMarkerCount.value(markerKey, -1);
    options.indentWrap = m_indentWrap;
    options.alignNicks = m_alignNicks;
    m_chatPane->showBuffer(snapshot, options);

    m_comicController->refreshComic();
}

void maxchat::ui::MainWindow::renderActiveBufferMetadata() {
    const maxchat::core::ChatBufferSnapshot snapshot =
        m_chatBuffers.snapshot(bufferIdForTarget(m_currentTarget));

    if (m_memberList != nullptr) {
        m_memberList->clear();
        if (snapshot.id.kind == maxchat::core::ChatBufferKind::Channel) {
            if (m_membersHeader != nullptr) {
                m_membersHeader->setText(
                    QStringLiteral("%1 users").arg(snapshot.members.size()));
            }
            if (m_coloredNicks) {
                const auto roleRank = [](const QString& member) {
                    const int idx =
                        QStringLiteral("~&@%+").indexOf(member.isEmpty() ? QChar() : member.front());
                    return idx < 0 ? 5 : idx;
                };
                QStringList members = snapshot.members;
                std::sort(members.begin(), members.end(),
                          [&](const QString& a, const QString& b) {
                              if (m_sortByStatus && roleRank(a) != roleRank(b)) {
                                  return roleRank(a) < roleRank(b);
                              }
                              return nickWithoutPrefix(a).compare(nickWithoutPrefix(b),
                                                                  Qt::CaseInsensitive) < 0;
                          });
                for (const QString& member : members) {
                    m_memberList->addItem(member);
                }
            } else {
                // Colours off: IRCCloud-style sections grouped by channel role.
                const QList<QPair<QString, QString>> memberGroups = {
                    {QStringLiteral("~"), QStringLiteral("Owners")},
                    {QStringLiteral("&"), QStringLiteral("Admins")},
                    {QStringLiteral("@"), QStringLiteral("Operators")},
                    {QStringLiteral("%"), QStringLiteral("Half-ops")},
                    {QStringLiteral("+"), QStringLiteral("Voiced")},
                    {QString(), QStringLiteral("Members")},
                };
                QHash<QString, QStringList> grouped;
                for (const QString& member : snapshot.members) {
                    const QString prefix = member.left(1);
                    grouped[QStringLiteral("~&@%+").contains(prefix) ? prefix : QString()]
                        .append(member);
                }
                for (const auto& group : memberGroups) {
                    QStringList names = grouped.value(group.first);
                    if (names.isEmpty()) {
                        continue;
                    }
                    std::sort(names.begin(), names.end(),
                              [](const QString& a, const QString& b) {
                                  return nickWithoutPrefix(a).compare(
                                             nickWithoutPrefix(b), Qt::CaseInsensitive) < 0;
                              });
                    auto* header = new QListWidgetItem(QStringLiteral("%1   %2")
                                                           .arg(group.second.toUpper())
                                                           .arg(names.size()));
                    header->setFlags(Qt::ItemIsEnabled);
                    QFont headerFont = m_memberList->font();
                    headerFont.setBold(true);
                    header->setFont(headerFont);
                    header->setData(Qt::UserRole + 1, true);
                    m_memberList->addItem(header);
                    for (const QString& name : names) {
                        m_memberList->addItem(name);
                    }
                }
            }
            recolorMemberList();
        } else if (m_membersHeader != nullptr) {
            m_membersHeader->setText(QStringLiteral("Members"));
        }
    }

    // The topic bar shows ONLY the active channel's topic — empty on the server
    // tab, in a PM, when not connected, or when the channel has no topic.
    // Connection state and network/channel context belong to the status bar and
    // the window title, not here.
    if (m_topicLabel != nullptr) {
        m_topicFullText =
            (m_hasConnectionPlan && snapshot.id.kind == maxchat::core::ChatBufferKind::Channel)
                ? snapshot.topic.trimmed()
                : QString();
        updateTopicElide();
    }
    updateWindowTitle();
    updateNickLabel();
}

void maxchat::ui::MainWindow::updateTopicElide() {
    if (m_topicLabel == nullptr) {
        return;
    }
    const int avail = std::max(40, m_topicLabel->width() - 12);
    m_topicLabel->setText(
        QFontMetrics(m_topicLabel->font()).elidedText(m_topicFullText, Qt::ElideRight, avail));
    // The bar shows what fits; the full topic is in the tooltip (and via
    // double-click edit / the raw TOPIC line in chat).
    m_topicLabel->setToolTip(m_topicFullText);
}

void maxchat::ui::MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    updateTopicElide(); // re-fit the elided topic to the new width
}

namespace {

const QStringList& savedLookKeys() {
    static const QStringList keys = {
        QStringLiteral("theme"),            QStringLiteral("chat_theme"),
        QStringLiteral("wallpaper"),        QStringLiteral("app_font_family"),
        QStringLiteral("app_font_size"),    QStringLiteral("app_font_bold"),
        QStringLiteral("chat_font_family"), QStringLiteral("chat_font_size"),
        QStringLiteral("chat_font_bold"),   QStringLiteral("list_font_family"),
        QStringLiteral("list_font_size"),   QStringLiteral("list_font_bold"),
        QStringLiteral("nick_font_family"), QStringLiteral("nick_font_size"),
        QStringLiteral("nick_font_bold"),   QStringLiteral("status_font_family"),
        QStringLiteral("status_font_size"), QStringLiteral("status_font_bold"),
        QStringLiteral("topic_font_family"), QStringLiteral("topic_font_size"),
        QStringLiteral("topic_font_bold"),
    };
    return keys;
}

} // namespace

void maxchat::ui::MainWindow::setupNavShortcuts() {
    for (const NavShortcutSpec& spec : navShortcutSpecs()) {
        auto* shortcut = new QShortcut(QKeySequence(spec.defaultKey), this);
        if (spec.id == QStringLiteral("navActivity")) {
            connect(shortcut, &QShortcut::activated, this, &MainWindow::jumpToNextActivity);
        } else {
            const int index = spec.id.mid(3).toInt() - 1;
            connect(shortcut, &QShortcut::activated, this,
                    [this, index]() { jumpToBufferIndex(index); });
        }
        m_navShortcuts.insert(spec.id, shortcut);
    }
}

void maxchat::ui::MainWindow::applyNavShortcutOverrides(const QVariantMap& settings) {
    const QVariantMap overrides = settings.value(QStringLiteral("shortcuts")).toMap();
    for (const NavShortcutSpec& spec : navShortcutSpecs()) {
        QShortcut* shortcut = m_navShortcuts.value(spec.id);
        if (shortcut == nullptr) {
            continue;
        }
        const QString bound = overrides.value(spec.id, spec.defaultKey).toString();
        shortcut->setKey(QKeySequence(bound));
    }
}

void maxchat::ui::MainWindow::jumpToBufferIndex(const int index) {
    if (m_networkTree == nullptr || index < 0) {
        return;
    }
    int seen = 0;
    for (int top = 0; top < m_networkTree->topLevelItemCount(); ++top) {
        QTreeWidgetItem* root = m_networkTree->topLevelItem(top);
        if (seen++ == index) {
            m_networkTree->setCurrentItem(root);
            return;
        }
        for (int child = 0; child < root->childCount(); ++child) {
            if (seen++ == index) {
                m_networkTree->setCurrentItem(root->child(child));
                return;
            }
        }
    }
}

void maxchat::ui::MainWindow::jumpToNextActivity() {
    if (m_networkTree == nullptr) {
        return;
    }
    for (int top = 0; top < m_networkTree->topLevelItemCount(); ++top) {
        QTreeWidgetItem* root = m_networkTree->topLevelItem(top);
        for (int child = -1; child < root->childCount(); ++child) {
            QTreeWidgetItem* item = child < 0 ? root : root->child(child);
            const QString network = treeItemNetwork(item);
            QString target = treeItemTarget(item);
            if (isTreeStatusTarget(target)) {
                target.clear(); // the root row = the network's server buffer
            } else if (target.isEmpty() && child >= 0) {
                continue;
            }
            if (m_chatBuffers.snapshot(bufferIdForNetworkTarget(network, target)).unreadCount >
                0) {
                m_networkTree->setCurrentItem(item);
                return;
            }
        }
    }
    statusBar()->showMessage(tr("No unread activity."));
}

void maxchat::ui::MainWindow::openShortcutEditor() {
    ShortcutEditorDialog dialog(
        m_settings.loadWithDefaults().value(QStringLiteral("shortcuts")).toMap(), this);
    attachGeometryPersist(&dialog, m_settings, QStringLiteral("geom_shortcut_editor"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("shortcuts"), dialog.overrides());
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save shortcuts."));
        return;
    }
    applyNavShortcutOverrides(settings);
    appendSystemLine(tr("! Shortcuts saved."));
}

void maxchat::ui::MainWindow::rebuildLooksMenu() {
    if (m_looksMenu == nullptr) {
        return;
    }

    m_looksMenu->clear();
    m_looksMenu->addAction(QStringLiteral("Save Current Look..."), this,
                           &MainWindow::saveCurrentLook);
    const QVariantMap looks =
        m_settings.loadWithDefaults().value(QStringLiteral("looks")).toMap();
    if (looks.isEmpty()) {
        QAction* none = m_looksMenu->addAction(QStringLiteral("No saved looks"));
        none->setEnabled(false);
        return;
    }

    m_looksMenu->addSeparator();
    for (auto it = looks.constBegin(); it != looks.constEnd(); ++it) {
        const QString name = it.key();
        m_looksMenu->addAction(name, this, [this, name]() { applyLook(name); });
    }
    m_looksMenu->addSeparator();
    QMenu* deleteMenu = m_looksMenu->addMenu(QStringLiteral("Delete"));
    for (auto it = looks.constBegin(); it != looks.constEnd(); ++it) {
        const QString name = it.key();
        deleteMenu->addAction(name, this, [this, name]() { deleteLook(name); });
    }
}

void maxchat::ui::MainWindow::saveCurrentLook() {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Save Look"),
                                               QStringLiteral("Name for this look:"),
                                               QLineEdit::Normal, QString(), &accepted)
                             .trimmed();
    if (!accepted || name.isEmpty()) {
        return;
    }

    QVariantMap settings = m_settings.loadWithDefaults();
    QVariantMap look;
    for (const QString& key : savedLookKeys()) {
        look.insert(key, settings.value(key));
    }
    QVariantMap looks = settings.value(QStringLiteral("looks")).toMap();
    looks.insert(name, look);
    settings.insert(QStringLiteral("looks"), looks);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save look."));
        return;
    }
    rebuildLooksMenu();
    appendSystemLine(tr("! Look \"%1\" saved.").arg(name));
}

void maxchat::ui::MainWindow::applyLook(const QString& name) {
    QVariantMap settings = m_settings.loadWithDefaults();
    const QVariantMap look =
        settings.value(QStringLiteral("looks")).toMap().value(name).toMap();
    if (look.isEmpty()) {
        appendSystemLine(tr("! Look \"%1\" was not found.").arg(name));
        rebuildLooksMenu();
        return;
    }

    for (auto it = look.constBegin(); it != look.constEnd(); ++it) {
        settings.insert(it.key(), it.value());
    }
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not apply look."));
        return;
    }
    applyCurrentSettings();
    appendSystemLine(tr("! Look \"%1\" applied.").arg(name));
}

void maxchat::ui::MainWindow::deleteLook(const QString& name) {
    QVariantMap settings = m_settings.loadWithDefaults();
    QVariantMap looks = settings.value(QStringLiteral("looks")).toMap();
    if (looks.remove(name) == 0) {
        rebuildLooksMenu();
        return;
    }
    settings.insert(QStringLiteral("looks"), looks);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not delete look."));
        return;
    }
    rebuildLooksMenu();
    appendSystemLine(tr("! Look \"%1\" deleted.").arg(name));
}


void maxchat::ui::MainWindow::noteUnreadBoundary(const maxchat::core::ChatBufferId& id,
                                                 const bool active, const int lineCountBeforeAppend) {
    const QString key = id.network + QChar(0x1f) + id.target;
    if (active) {
        m_bufferMarkerCount.remove(key); // read live → caught up, no boundary
    } else if (!m_bufferMarkerCount.contains(key)) {
        m_bufferMarkerCount.insert(key, lineCountBeforeAppend); // first unread sits here
    }
}

void maxchat::ui::MainWindow::configureDcc() {
    const QVariantMap settings = m_settings.loadWithDefaults();
    QString dir = settings.value(QStringLiteral("dcc_dir")).toString().trimmed();
    if (dir.isEmpty()) {
        dir = QDir(m_settings.paths().configDir).filePath(QStringLiteral("downloads"));
    }
    m_dccManager->setEnabled(settings.value(QStringLiteral("dcc_enabled"), false).toBool());
    m_dccManager->setDownloadDir(dir);
    m_dccManager->setPassive(settings.value(QStringLiteral("dcc_passive"), true).toBool());
    m_dccManager->setPortRange(settings.value(QStringLiteral("dcc_port_first"), 0).toInt(),
                               settings.value(QStringLiteral("dcc_port_last"), 0).toInt());
    m_dccManager->setAcceptPolicy(
        settings.value(QStringLiteral("dcc_accept"), QStringLiteral("ask")).toString(),
        settings.value(QStringLiteral("dcc_trusted")).toStringList());
    // Advertised IP: pref override, else the active connection's local IPv4.
    QString ip = settings.value(QStringLiteral("dcc_ip")).toString().trimmed();
    if (ip.isEmpty()) {
        ip = connection().localAddress();
    }
    m_dccManager->setAdvertisedIp(ip);
}

void maxchat::ui::MainWindow::openDccTransfers() {
    configureDcc();
    DccTransfersDialog dialog(m_dccManager, this);
    attachGeometryPersist(&dialog, m_settings, QStringLiteral("geom_dcc_transfers"));
    dialog.exec();
}

void maxchat::ui::MainWindow::handleDccCommand(const QStringList& args) {
    if (!m_dccEnabled) {
        appendSystemLine(tr(
            "! File transfers are disabled. Enable them in Preferences → Files (DCC)."));
        return;
    }
    configureDcc();

    const QString sub = args.isEmpty() ? QString() : args.first().toLower();
    if (sub == QStringLiteral("send") && args.size() >= 3) {
        const QString peer = args.at(1);
        const QString path = args.mid(2).join(QLatin1Char(' '));
        m_dccManager->offerSend(peer, path);
    } else if (sub == QStringLiteral("send") && args.size() == 2) {
        const QString peer = args.at(1);
        const QString path =
            QFileDialog::getOpenFileName(this, QStringLiteral("Send file to %1").arg(peer));
        if (!path.isEmpty()) {
            m_dccManager->offerSend(peer, path);
        }
    } else if (sub == QStringLiteral("chat") && args.size() >= 2) {
        m_dccManager->offerChat(args.at(1));
    } else if (sub == QStringLiteral("list")) {
        const auto transfers = m_dccManager->transfers();
        appendSystemLine(tr("! %1 DCC transfer(s).").arg(transfers.size()));
        openDccTransfers();
    } else if (sub == QStringLiteral("close") || sub == QStringLiteral("cancel")) {
        const QString target = m_currentTarget.trimmed();
        if (target.startsWith(QLatin1Char('='))) {
            m_dccManager->closeChat(target.mid(1));
        } else {
            for (const auto& t : m_dccManager->transfers()) {
                m_dccManager->cancelTransfer(t.id);
            }
            appendSystemLine(tr("! Cancelled active DCC transfers."));
        }
    } else {
        appendSystemLine(tr(
            "! Usage: /dcc send <nick> [file] | /dcc chat <nick> | /dcc list | /dcc close"));
    }
}


void maxchat::ui::MainWindow::openCharacterGallery() {
    m_comicController->ensureComicArt();
    if (m_comicCharacterPaths.isEmpty()) {
        appendSystemLine(tr("! No comic art found - set your comic art folder in "
                                        "Comic > Comic Settings first."));
        return;
    }
    QStringList stems = m_comicCharacterPaths.keys();
    std::sort(stems.begin(), stems.end());
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Comic Characters"));
    dialog->resize(560, 480);
    attachGeometryPersist(dialog, m_settings, QStringLiteral("geom_char_gallery"));
    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(
        QStringLiteral("%1 characters - assign them to nicks in Comic Settings.").arg(stems.size()),
        dialog));
    auto* list = new QListWidget(dialog);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(96, 110));
    list->setResizeMode(QListView::Adjust);
    list->setSpacing(8);
    for (const QString& stem : stems) {
        maxchat::comic::Character* ch = maxchat::comic::loadCharacter(m_comicCharacterPaths.value(stem));
        auto* item = new QListWidgetItem(stem, list);
        if (ch != nullptr) {
            const QImage im = ch->imageTrimmed(QStringLiteral("neutral"), QStringLiteral("right"), 0);
            if (!im.isNull()) {
                item->setIcon(QIcon(QPixmap::fromImage(
                    im.scaledToHeight(110, Qt::SmoothTransformation))));
            }
        }
    }
    layout->addWidget(list, 1);
    auto* close = new QPushButton(QStringLiteral("Close"), dialog);
    connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
    layout->addWidget(close, 0, Qt::AlignRight);
    dialog->show();
}

void maxchat::ui::MainWindow::openEmotionPicker() {
    // Set the expression used for YOUR comic panels (overrides the text guess);
    // "Auto" returns to guessing from your message text. Shows your character's
    // actual face per emotion when comic art is loaded (the emotion wheel look);
    // falls back to a plain list without art.
    const QStringList emotions = {QStringLiteral("neutral"),  QStringLiteral("happy"),
                                  QStringLiteral("laughing"), QStringLiteral("coy"),
                                  QStringLiteral("scared"),   QStringLiteral("bored"),
                                  QStringLiteral("angry"),    QStringLiteral("shouting"),
                                  QStringLiteral("sad")};
    const auto applyChoice = [this](const QString& choice) {
        m_comicSelfEmotion = choice.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0
                                 ? QStringLiteral("auto")
                                 : choice.toLower();
        appendSystemLine(m_comicSelfEmotion == QStringLiteral("auto")
                             ? QStringLiteral("! Comic emotion: auto (guess from text).")
                             : QStringLiteral("! Comic emotion set to %1.").arg(m_comicSelfEmotion));
        m_comicController->refreshComic();
    };

    m_comicController->ensureComicArt();
    maxchat::comic::Character* character =
        m_comicController->comicCharacterForNick(currentNickForNetwork(activeNetworkName()));

    if (character == nullptr) {
        // No art loaded — plain text list.
        QStringList options = emotions;
        options.prepend(QStringLiteral("Auto"));
        const QString currentLabel = m_comicSelfEmotion == QStringLiteral("auto")
                                         ? QStringLiteral("Auto")
                                         : m_comicSelfEmotion;
        const int current = std::max(0, static_cast<int>(options.indexOf(currentLabel)));
        bool ok = false;
        const QString choice = QInputDialog::getItem(
            this, QStringLiteral("Comic emotion"),
            QStringLiteral("Expression for your comic panels (Auto = guess from your text):"),
            options, current, false, &ok);
        if (ok) {
            applyChoice(choice);
        }
        return;
    }

    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Comic emotion"));
    dialog->resize(420, 340);
    attachGeometryPersist(dialog, m_settings, QStringLiteral("geom_emotion_picker"));
    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(
        QStringLiteral("Expression for your comic panels (Auto = guess from your text):"),
        dialog));
    auto* list = new QListWidget(dialog);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(72, 84));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setSpacing(8);
    list->setWordWrap(true);

    auto* autoItem = new QListWidgetItem(QStringLiteral("Auto"), list);
    {
        const QImage neutral = character->faceCell(QStringLiteral("neutral"));
        if (!neutral.isNull()) {
            autoItem->setIcon(QIcon(QPixmap::fromImage(neutral)));
        }
    }
    autoItem->setToolTip(QStringLiteral("Guess the expression from your message text"));
    for (const QString& emotion : emotions) {
        auto* item = new QListWidgetItem(emotion, list);
        const QImage face = character->faceCell(emotion);
        if (!face.isNull()) {
            item->setIcon(QIcon(QPixmap::fromImage(face)));
        }
    }
    const QString currentLabel =
        m_comicSelfEmotion == QStringLiteral("auto") ? QStringLiteral("Auto") : m_comicSelfEmotion;
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->text().compare(currentLabel, Qt::CaseInsensitive) == 0) {
            list->setCurrentRow(row);
            break;
        }
    }
    layout->addWidget(list, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
    connect(list, &QListWidget::itemDoubleClicked, dialog,
            [dialog](QListWidgetItem*) { dialog->accept(); });
    connect(dialog, &QDialog::accepted, this, [list, applyChoice]() {
        if (const QListWidgetItem* item = list->currentItem()) {
            applyChoice(item->text());
        }
    });
    dialog->show();
}



void maxchat::ui::MainWindow::recolorMemberList() {
    if (m_memberList == nullptr) {
        return;
    }

    // Identical source as the chat view (effectiveNickPalette) — a nick is the
    // same colour everywhere, including the contrast-guard adjustments.
    bool monoNicks = false;
    const QStringList palette = effectiveNickPalette(&monoNicks);
    const bool plain = !m_coloredNicks || monoNicks;
    const QSet<QString> awayNicks = m_awayNicksByNetwork.value(activeNetworkName());

    for (int row = 0; row < m_memberList->count(); ++row) {
        QListWidgetItem* item = m_memberList->item(row);
        if (item == nullptr || item->data(Qt::UserRole + 1).toBool()) {
            continue; // group header rows keep their own styling
        }
        const QString nick = nickWithoutPrefix(item->text());
        const QString key = nick.toLower();
        const QString overrideColor = m_nickColorOverrides.value(key).toString();
        if (awayNicks.contains(key)) {
            item->setForeground(QColor(0x80, 0x80, 0x80));
        } else if (!overrideColor.isEmpty()) {
            // An explicit per-user colour wins, even in mono/colours-off modes.
            item->setForeground(QColor(overrideColor));
        } else if (plain || nick.isEmpty()) {
            item->setData(Qt::ForegroundRole, QVariant());
        } else {
            item->setForeground(QColor(maxchat::irc::nickColor(nick, palette)));
        }
    }
}

void maxchat::ui::MainWindow::resetAllSettings() {
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Reset Settings"),
        QStringLiteral("Reset ALL settings to defaults?\nNetworks, themes, colors and options "
                       "return to their first-run state."));
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (!m_settings.saveRaw(maxchat::core::SettingsStore::defaultSettings(), false)) {
        appendSystemLine(tr("! Could not reset settings."));
        return;
    }
    applyCurrentSettings();
    rebuildLooksMenu();
    appendSystemLine(tr("! Settings were reset to defaults."));
}

void maxchat::ui::MainWindow::memberListChanged() {
    if (!m_coloredNicks) {
        renderActiveBufferMetadata(); // grouped view: rebuild groups + counts
    } else {
        recolorMemberList();
    }
}

void maxchat::ui::MainWindow::setNickColorOverride(const QString& nick) {
    const QString key = nick.trimmed().toLower();
    if (key.isEmpty()) {
        return;
    }
    const QColor current(m_nickColorOverrides.value(key).toString());
    const QColor picked =
        QColorDialog::getColor(current.isValid() ? current : QColor(Qt::white), this,
                               QStringLiteral("Color for %1").arg(nick));
    if (!picked.isValid()) {
        return;
    }
    m_nickColorOverrides.insert(key, picked.name());
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("nick_colors"), m_nickColorOverrides);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save nick colors."));
    }
    recolorMemberList();
    renderActiveBuffer();
}

void maxchat::ui::MainWindow::clearNickColorOverride(const QString& nick) {
    const QString key = nick.trimmed().toLower();
    if (m_nickColorOverrides.remove(key) == 0) {
        return;
    }
    QVariantMap settings = m_settings.loadWithDefaults();
    settings.insert(QStringLiteral("nick_colors"), m_nickColorOverrides);
    if (!m_settings.saveRaw(settings)) {
        appendSystemLine(tr("! Could not save nick colors."));
    }
    recolorMemberList();
    renderActiveBuffer();
}

void maxchat::ui::MainWindow::removeMemberFromChannelBuffers(const QString& nick) {
    removeMemberFromChannelBuffers(currentLogNetwork(), nick);
}

void maxchat::ui::MainWindow::removeMemberFromChannelBuffers(const QString& network, const QString& nick) {
    for (const maxchat::core::ChatBufferId& id : m_chatBuffers.buffersForNetwork(network)) {
        if (id.kind == maxchat::core::ChatBufferKind::Channel) {
            const bool removed = m_chatBuffers.removeMember(id, nick);
            Q_UNUSED(removed);
        }
    }
}

void maxchat::ui::MainWindow::renameMemberInChannelBuffers(const QString& oldNick, const QString& newNick) {
    renameMemberInChannelBuffers(currentLogNetwork(), oldNick, newNick);
}

void maxchat::ui::MainWindow::renameMemberInChannelBuffers(const QString& network, const QString& oldNick,
                                              const QString& newNick) {
    for (const maxchat::core::ChatBufferId& id : m_chatBuffers.buffersForNetwork(network)) {
        if (id.kind == maxchat::core::ChatBufferKind::Channel) {
            const bool renamed = m_chatBuffers.renameMember(id, oldNick, newNick);
            Q_UNUSED(renamed);
        }
    }
}

QStringList MainWindow::channelTargetsContainingMember(const QString& nick) const {
    return channelTargetsContainingMember(currentLogNetwork(), nick);
}

QStringList MainWindow::channelTargetsContainingMember(const QString& network,
                                                       const QString& nick) const {
    QStringList channels;
    const QString cleanNick = nickWithoutPrefix(nick).trimmed();
    if (cleanNick.isEmpty()) {
        return channels;
    }

    for (const maxchat::core::ChatBufferId& id : m_chatBuffers.buffersForNetwork(network)) {
        if (id.kind != maxchat::core::ChatBufferKind::Channel) {
            continue;
        }
        const maxchat::core::ChatBufferSnapshot snapshot = m_chatBuffers.snapshot(id);
        const auto memberIt = std::find_if(
            snapshot.members.cbegin(), snapshot.members.cend(),
            [&cleanNick](const QString& member) { return memberMatchesNick(member, cleanNick); });
        if (memberIt != snapshot.members.cend() && !snapshot.id.target.trimmed().isEmpty()) {
            channels.append(snapshot.id.target);
        }
    }
    return channels;
}

QStringList MainWindow::joinedChannelTargets() const {
    return joinedChannelTargets(currentLogNetwork());
}

QStringList MainWindow::joinedChannelTargets(const QString& network) const {
    QStringList channels;
    for (const maxchat::core::ChatBufferId& id : m_chatBuffers.buffersForNetwork(network)) {
        if (id.kind != maxchat::core::ChatBufferKind::Channel) {
            continue;
        }
        const maxchat::core::ChatBufferSnapshot snapshot = m_chatBuffers.snapshot(id);
        if (snapshot.joined && !snapshot.id.target.trimmed().isEmpty()) {
            channels.append(snapshot.id.target);
        }
    }
    return channels;
}

QString MainWindow::activeNetworkName() const {
    const QString active = currentLogNetwork().trimmed();
    return active.isEmpty() ? QStringLiteral("Server") : active;
}

QString MainWindow::currentTargetForNetwork(const QString& network) const {
    const QString normalized = network.trimmed();
    if (normalized.compare(activeNetworkName(), Qt::CaseInsensitive) == 0) {
        return m_currentTarget;
    }
    return m_currentTargetByNetwork.value(normalized);
}

QStringList MainWindow::openTargetsForNetwork(const QString& network) const {
    const QString normalized = network.trimmed();
    if (normalized.compare(activeNetworkName(), Qt::CaseInsensitive) == 0) {
        return m_openTargets;
    }
    return m_openTargetsByNetwork.value(normalized);
}

bool MainWindow::networkRegistered(const QString& network) const {
    const QString normalized = network.trimmed();
    if (normalized.compare(activeNetworkName(), Qt::CaseInsensitive) == 0) {
        return m_registered;
    }
    return m_registeredByNetwork.value(normalized, false);
}

void maxchat::ui::MainWindow::rememberNetwork(const QString& network) {
    const QString normalized = network.trimmed();
    if (normalized.isEmpty()) {
        return;
    }
    if (!containsCaseInsensitive(m_knownNetworks, normalized)) {
        m_knownNetworks.append(normalized);
    }
    if (!m_openTargetsByNetwork.contains(normalized)) {
        m_openTargetsByNetwork.insert(normalized, {});
    }
    if (!m_registeredByNetwork.contains(normalized)) {
        m_registeredByNetwork.insert(normalized, false);
    }
}

void maxchat::ui::MainWindow::setActiveNetwork(const QString& network) {
    const QString normalized = network.trimmed();
    if (normalized.isEmpty() || normalized.compare(activeNetworkName(), Qt::CaseInsensitive) == 0) {
        return;
    }

    rememberNetwork(activeNetworkName());
    m_connectionPlansByNetwork.insert(activeNetworkName(), m_connectionPlan);
    m_currentTargetByNetwork.insert(activeNetworkName(), m_currentTarget);
    m_openTargetsByNetwork.insert(activeNetworkName(), m_openTargets);
    m_registeredByNetwork.insert(activeNetworkName(), m_registered);
    m_initialConnectAttemptsByNetwork.insert(activeNetworkName(), m_initialConnectAttempts);
    m_manualDisconnectByNetwork.insert(activeNetworkName(), m_manualDisconnect);
    m_reconnectRequestedByNetwork.insert(activeNetworkName(), m_reconnectRequested);

    rememberNetwork(normalized);
    if (m_connectionPlansByNetwork.contains(normalized)) {
        m_connectionPlan = m_connectionPlansByNetwork.value(normalized);
    } else {
        m_connectionPlan.networkName = normalized;
    }
    m_currentTarget = m_currentTargetByNetwork.value(normalized);
    m_openTargets = m_openTargetsByNetwork.value(normalized);
    m_registered = m_registeredByNetwork.value(normalized, false);
    m_initialConnectAttempts = m_initialConnectAttemptsByNetwork.value(normalized, 0);
    m_manualDisconnect = m_manualDisconnectByNetwork.value(normalized, false);
    m_reconnectRequested = m_reconnectRequestedByNetwork.value(normalized, false);
    m_connectionUptimeRunning = m_connectionUptimeStartMsByNetwork.contains(normalized);
    activateBufferTarget(m_currentTarget);
    rebuildNetworkTree();
    updateChannelModeButton();
}

QString MainWindow::treeDisplayLabelForNetwork(const QString& network) {
    const QString trimmed = network.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    int unreadCount = 0;
    int highlightCount = 0;
    for (const QString& target : visibleTreeTargets(trimmed)) {
        const maxchat::core::ChatBufferId id = bufferIdForNetworkTarget(trimmed, target);
        const maxchat::core::ChatBufferSnapshot snapshot = m_chatBuffers.snapshot(id);
        unreadCount += snapshot.unreadCount;
        highlightCount += snapshot.highlightCount;
    }

    return labelWithTreeCounts(trimmed, unreadCount, highlightCount);
}

QStringList MainWindow::visibleTreeTargets(const QString& network) const {
    QStringList targets;
    for (const QString& target : openTargetsForNetwork(network)) {
        const QString trimmed = target.trimmed();
        if (!trimmed.isEmpty() && !containsCaseInsensitive(targets, trimmed)) {
            targets.append(trimmed);
        }
    }

    const QString current = currentTargetForNetwork(network).trimmed();
    if (!current.isEmpty() && !containsCaseInsensitive(targets, current)) {
        targets.append(current);
    }
    return targets;
}

QStringList MainWindow::visibleTreeTargets() const {
    return visibleTreeTargets(activeNetworkName());
}

QString MainWindow::treeDisplayLabelForTarget(const QString& network, const QString& target) {
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    if (isTreeStatusTarget(trimmed)) {
        return QStringLiteral("Server");
    }

    const maxchat::core::ChatBufferSnapshot snapshot =
        m_chatBuffers.snapshot(bufferIdForNetworkTarget(network, trimmed));
    return labelWithTreeCounts(trimmed, snapshot.unreadCount, snapshot.highlightCount);
}

QString MainWindow::treeDisplayLabelForTarget(const QString& target) {
    return treeDisplayLabelForTarget(activeNetworkName(), target);
}

void maxchat::ui::MainWindow::updateNetworkTreeLabels() {
    if (m_networkTree == nullptr) {
        return;
    }

    for (int topIndex = 0; topIndex < m_networkTree->topLevelItemCount(); ++topIndex) {
        QTreeWidgetItem* rootItem = m_networkTree->topLevelItem(topIndex);
        if (rootItem == nullptr) {
            continue;
        }
        const QString network = treeItemNetwork(rootItem);
        if (!network.isEmpty()) {
            rootItem->setText(0, treeDisplayLabelForNetwork(network));
        }
        for (int childIndex = 0; childIndex < rootItem->childCount(); ++childIndex) {
            QTreeWidgetItem* item = rootItem->child(childIndex);
            const QString target = treeItemTarget(item);
            if (target.isEmpty()) {
                continue;
            }
            item->setText(0, treeDisplayLabelForTarget(network, target));
        }
    }
    syncBufferTabs();
}

void maxchat::ui::MainWindow::rememberTarget(const QString& target) {
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    for (const QString& openTarget : m_openTargets) {
        if (openTarget.compare(trimmed, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    m_openTargets.append(trimmed);
    m_openTargetsByNetwork.insert(activeNetworkName(), m_openTargets);
}

void maxchat::ui::MainWindow::forgetTarget(const QString& target) {
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    for (int index = m_openTargets.size() - 1; index >= 0; --index) {
        if (m_openTargets.at(index).compare(trimmed, Qt::CaseInsensitive) == 0) {
            m_openTargets.removeAt(index);
        }
    }
    m_openTargetsByNetwork.insert(activeNetworkName(), m_openTargets);
}

void maxchat::ui::MainWindow::rebuildNetworkTree() {
    if (m_networkTree == nullptr) {
        return;
    }
    const QSignalBlocker blocker(m_networkTree);
    m_networkTree->clear();

    if (!m_hasConnectionPlan && m_knownNetworks.isEmpty()) {
        // Network roots ARE the server buffer rows - no separate "Server" child.
        auto* rootItem = newTreeItem(QStringLiteral("Server"), QStringLiteral("server"));
        m_networkTree->addTopLevelItem(rootItem);
        m_networkTree->expandAll();
        m_networkTree->setCurrentItem(rootItem);
        return;
    }

    QStringList networks = m_knownNetworks;
    if (m_hasConnectionPlan && !containsCaseInsensitive(networks, m_connectionPlan.networkName)) {
        networks.append(m_connectionPlan.networkName);
    }

    QTreeWidgetItem* itemToSelect = nullptr;
    for (const QString& network : networks) {
        const QString cleanNetwork = network.trimmed();
        if (cleanNetwork.isEmpty()) {
            continue;
        }
        const bool active = cleanNetwork.compare(activeNetworkName(), Qt::CaseInsensitive) == 0;
        const maxchat::irc::IrcConnection* conn = connectionForNetwork(cleanNetwork);
        const bool online =
            networkRegistered(cleanNetwork) || (conn != nullptr && conn->isConnected());
        const QString status = networkRegistered(cleanNetwork) ? QStringLiteral("Connected")
                               : online                        ? QStringLiteral("Connecting")
                                                               : QStringLiteral("Disconnected");
        // A disconnected network STAYS in the tree (only "Close" removes it), but
        // is marked offline + greyed so the state is visible — Python parity.
        QString label = treeDisplayLabelForNetwork(cleanNetwork);
        if (!online) {
            label += QStringLiteral(" (offline)");
        }
        auto* rootItem = newTreeItem(label, QStringLiteral("server"), cleanNetwork);
        rootItem->setToolTip(0, status);
        if (!online) {
            rootItem->setForeground(0, QColor(0x9a, 0xa0, 0xa6));
        }
        if (active && currentTargetForNetwork(cleanNetwork).trimmed().isEmpty()) {
            itemToSelect = rootItem;
        }
        for (const QString& target : visibleTreeTargets(cleanNetwork)) {
            auto* item =
                newTreeItem(treeDisplayLabelForTarget(cleanNetwork, target), target, cleanNetwork);
            rootItem->addChild(item);
            if (active &&
                target.compare(currentTargetForNetwork(cleanNetwork), Qt::CaseInsensitive) == 0) {
                itemToSelect = item;
            }
        }
        // Script/BBS terminal launchers ("Term N") nest under their network.
        if (m_scripts != nullptr) {
            for (const TerminalInfo& term : m_scripts->terminals()) {
                if (term.network.compare(cleanNetwork, Qt::CaseInsensitive) != 0) {
                    continue;
                }
                QString label = term.label;
                if (!term.scriptName.isEmpty()) {
                    label += QStringLiteral(" - %1").arg(term.scriptName);
                }
                auto* item = newTreeItem(label, {}, cleanNetwork);
                item->setData(0, TreeTerminalRole, term.id);
                item->setToolTip(0, QStringLiteral("Script terminal (%1)").arg(term.scriptName));
                rootItem->addChild(item);
            }
        }
        // addTopLevelItem must come before setCurrentItem — calling setCurrentItem
        // on an item not yet in the tree is a no-op and causes the fallback below
        // to select topLevelItem(0) (the wrong/first server) instead of the active one.
        m_networkTree->addTopLevelItem(rootItem);
    }
    m_networkTree->expandAll();
    if (itemToSelect != nullptr) {
        m_networkTree->setCurrentItem(itemToSelect);
    } else if (m_networkTree->currentItem() == nullptr) {
        QTreeWidgetItem* rootItem = m_networkTree->topLevelItem(0);
        if (rootItem != nullptr) {
            m_networkTree->setCurrentItem(rootItem);
        }
    }
    syncBufferTabs();
}

QString maxchat::ui::MainWindow::personalDictionaryPath() const {
    return QDir(m_settings.paths().configDir).filePath(QStringLiteral("personal_dict.dic"));
}

void maxchat::ui::MainWindow::loadPersonalDictionary() {
    if (m_spellchecker == nullptr) {
        return;
    }
    QFile file(personalDictionaryPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return; // none saved yet
    }
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString word = in.readLine().trimmed();
        if (!word.isEmpty()) {
            m_spellchecker->addWord(word);
        }
    }
}

void maxchat::ui::MainWindow::addWordToPersonalDictionary(const QString& word) {
    const QString cleaned = word.trimmed();
    if (cleaned.isEmpty()) {
        return;
    }

    // Persist (de-duped) to <config>/personal_dict.dic.
    const QString path = personalDictionaryPath();
    QSet<QString> existing;
    QFile readFile(path);
    if (readFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&readFile);
        while (!in.atEnd()) {
            const QString w = in.readLine().trimmed();
            if (!w.isEmpty()) {
                existing.insert(w);
            }
        }
        readFile.close();
    }
    if (!existing.contains(cleaned)) {
        QDir().mkpath(m_settings.paths().configDir);
        QFile writeFile(path);
        if (writeFile.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&writeFile);
            out << cleaned << '\n';
        }
    }

    // Teach the live engine + refresh the underlines immediately.
    if (m_spellchecker != nullptr) {
        m_spellchecker->addWord(cleaned);
    }
    if (m_input != nullptr) {
        m_input->viewport()->update();
    }
    statusBar()->showMessage(tr("Added “%1” to your dictionary.").arg(cleaned));
}

void maxchat::ui::MainWindow::updateChannelModeButton() {
    if (m_channelModesButton != nullptr) {
        m_channelModesButton->setEnabled(connection().isConnected() &&
                                         isChannelTarget(m_currentTarget.trimmed()));
    }
}

QString MainWindow::currentLogNetwork() const {
    const QString network = m_connectionPlan.networkName.trimmed();
    return network.isEmpty() ? QStringLiteral("Server") : network;
}

QString MainWindow::currentLogTarget() const {
    const QString target = m_currentTarget.trimmed();
    return target.isEmpty() ? QStringLiteral("server") : target;
}

QString MainWindow::currentNickForNetwork(const QString& network) const {
    const QString cleanNetwork = network.trimmed();
    const maxchat::core::NetworkConnectionPlan plan =
        m_connectionPlansByNetwork.value(cleanNetwork, m_connectionPlan);
    const maxchat::irc::IrcConnection* irc = connectionForNetwork(cleanNetwork);
    if (irc != nullptr && !irc->nick().isEmpty()) {
        return irc->nick();
    }
    return plan.nick.trimmed().isEmpty() ? QStringLiteral("comicfan") : plan.nick.trimmed();
}

maxchat::irc::IrcConnection::ConnectConfig
MainWindow::connectConfigFor(const maxchat::irc::ServerEndpoint& server) const {
    return connectConfigFor(server, m_connectionPlan);
}

maxchat::irc::IrcConnection::ConnectConfig
MainWindow::connectConfigFor(const maxchat::irc::ServerEndpoint& server,
                             const maxchat::core::NetworkConnectionPlan& plan) const {
    maxchat::irc::IrcConnection::ConnectConfig config;
    config.host = server.host;
    config.port = server.port;
    config.tls = server.tls;
    config.nick = plan.nick;
    config.username = plan.username;
    config.realname = plan.realname;
    config.serverPassword = plan.serverPassword;
    config.saslPassword = plan.saslPassword;
    config.saslAccount = plan.saslAccount;
    config.acceptInvalidCertificate = plan.acceptInvalidCertificate;
    config.allowInsecureAuth = plan.allowInsecureAuth;
    config.autojoin = plan.autojoin;
    config.connectTimeoutMs = plan.connectTimeoutMs;
    config.registrationTimeoutMs = plan.registrationTimeoutMs;
    config.proxyType = plan.proxyType;
    config.proxyHost = plan.proxyHost;
    config.proxyPort = plan.proxyPort;
    config.proxyUsername = plan.proxyUsername;
    config.proxyPassword = plan.proxyPassword;
    return config;
}

int MainWindow::maxInitialConnectAttempts() const {
    return maxInitialConnectAttempts(currentLogNetwork());
}

int MainWindow::maxInitialConnectAttempts(const QString& network) const {
    const maxchat::core::NetworkConnectionPlan plan =
        m_connectionPlansByNetwork.value(network.trimmed(), m_connectionPlan);
    return std::max(1, static_cast<int>(plan.reconnect.servers.size()) *
                           maxchat::irc::ServerRetryLimit);
}

void maxchat::ui::MainWindow::configureSpellcheck(const QVariantMap& settings) {
    if (m_input == nullptr) {
        return;
    }

    // Autocorrect only applies while spellcheck is on; it shares the engine.
    m_autocorrectEnabled =
        settings.value(QStringLiteral("spellcheck_autocorrect"), false).toBool();
    // How far (edit distance) a suggestion may be from the typed word before we
    // auto-replace it. Keeps "chatgpt" from becoming "chatting". Default 2.
    m_autocorrectMaxDistance =
        std::clamp(settings.value(QStringLiteral("autocorrect_max_distance"), 2).toInt(), 1, 6);

    const auto disableSpell = [this]() {
        m_input->setWordChecker({});
        m_input->setSpellcheckEnabled(false);
        m_spellchecker.reset();
        m_autocorrectEnabled = false;
    };

    if (!settings.value(QStringLiteral("spellcheck_enabled"), true).toBool()) {
        disableSpell();
        return;
    }

    const auto wireActiveSpeller = [this]() {
        m_input->setWordChecker([this](const QString& word) {
            return m_spellchecker != nullptr && m_spellchecker->isLoaded() &&
                   m_spellchecker->isCorrect(word);
        });
        loadPersonalDictionary(); // teach the engine the user's saved words
        m_input->setSpellcheckEnabled(true);
    };

    const QString languageCode =
        settings.value(QStringLiteral("spell_language"), QStringLiteral("en")).toString();
    const QString backend =
        settings.value(QStringLiteral("spellcheck_backend"), QStringLiteral("internal")).toString();

    const bool osRequested = (backend == QStringLiteral("os"));
    bool osUnavailable = false;

    // OS engine — independent of the (Linux-only) Hunspell build option.
    if (osRequested) {
        m_spellchecker = maxchat::spell::createOsSpeller(languageCode);
        if (m_spellchecker != nullptr && m_spellchecker->isLoaded()) {
            wireActiveSpeller();
            return;
        }
        osUnavailable = true; // not built in / unsupported — fall through; message below
    }

    // Internal engine: Hunspell + an on-disk dictionary (dictionaries/ folder).
#ifdef MAXCHAT_WITH_HUNSPELL
    const QList<maxchat::spell::SpellcheckLanguage> languages =
        maxchat::spell::spellcheckLanguages();
    auto languageIt =
        std::find_if(languages.cbegin(), languages.cend(),
                     [&languageCode](const maxchat::spell::SpellcheckLanguage& language) {
                         return language.code.compare(languageCode, Qt::CaseInsensitive) == 0;
                     });
    if (languageIt == languages.cend() || !languageIt->dictionaryAvailable()) {
        languageIt = std::find_if(languages.cbegin(), languages.cend(),
                                  [](const maxchat::spell::SpellcheckLanguage& language) {
                                      return language.code == QStringLiteral("en") &&
                                             language.dictionaryAvailable();
                                  });
    }
    if (languageIt == languages.cend() || !languageIt->dictionaryAvailable()) {
        disableSpell();
        statusBar()->showMessage(
            osRequested ? QStringLiteral("OS spell engine isn't in this build (rebuild without "
                                         "the noosspell flag), and no internal dictionary was found.")
                        : QStringLiteral("No spelling dictionary found — add a .aff/.dic to the "
                                         "'dictionaries' folder."));
        return;
    }
    auto hunspell = std::make_unique<maxchat::spell::HunspellSpellchecker>();
    if (!hunspell->loadDictionary(languageIt->affPath, languageIt->dicPath)) {
        disableSpell();
        statusBar()->showMessage(tr("Failed to load the spelling dictionary."));
        return;
    }
    m_spellchecker = std::move(hunspell);
    wireActiveSpeller();
    if (osUnavailable) {
        statusBar()->showMessage(tr(
            "OS spell engine isn't in this build; using the internal dictionary "
            "(rebuild without the noosspell flag for the native engine)."));
    }
#else
    // No internal engine compiled in (e.g. the Windows build without Hunspell).
    disableSpell();
    statusBar()->showMessage(
        osRequested
            ? QStringLiteral("OS spell engine isn't in this build — rebuild without the noosspell flag.")
            : QStringLiteral("Spellcheck has no internal engine in this build — use the OS engine "
                             "(rebuild without the noosspell flag)."));
#endif
}

void maxchat::ui::MainWindow::showStatus(const QString& text, int timeoutMs) {
    statusBar()->showMessage(text, timeoutMs);
}

void maxchat::ui::MainWindow::clearStatus() {
    statusBar()->clearMessage();
}

void maxchat::ui::MainWindow::appendInputUrl(const QString& url) {
    if (m_input == nullptr) {
        return;
    }
    // Append at end; add a leading space if the input isn't empty / already spaced.
    const QString existing = m_input->toPlainText();
    const QString text =
        existing.isEmpty() ? url
                           : (existing.endsWith(QLatin1Char(' ')) ? url : QStringLiteral(" ") + url);
    m_input->moveCursor(QTextCursor::End);
    m_input->insertPlainText(text);
    m_input->setFocus();
}

void maxchat::ui::MainWindow::resizeMessageInput() {
    if (m_input == nullptr) {
        return;
    }
    // Grow with content up to 5 lines so multiline drafts (Shift+Enter or a
    // small paste) are actually VISIBLE — the box used to stay one line tall
    // with the scrollbar off, letting users send text they couldn't see.
    const int lines = std::clamp(m_input->document()->blockCount(), 1, 5);
    const int height = QFontMetrics(m_input->font()).lineSpacing() * lines + 16;
    m_input->setMinimumHeight(height);
    m_input->setMaximumHeight(height);
    m_input->setVerticalScrollBarPolicy(m_input->document()->blockCount() > 5
                                            ? Qt::ScrollBarAsNeeded
                                            : Qt::ScrollBarAlwaysOff);
}

void maxchat::ui::MainWindow::setMenuBarFont(const QFont& font) {
    if (menuBar() != nullptr) {
        menuBar()->setFont(font);
    }
}

// --- ChatPaneDelegate (render-pipeline R1) — out-of-line because MediaController
// is incomplete in the header ---
void maxchat::ui::MainWindow::chatAnchorClicked(const QUrl& url) {
    m_media->handleAnchorClicked(url);
}

void maxchat::ui::MainWindow::chatSeparatorMoved(int nickWidth) {
    setNickColumnWidth(nickWidth, true);
}

void maxchat::ui::MainWindow::applyCurrentSettings() {
    const QVariantMap settings = m_settings.loadWithDefaults();
    m_commandAliases = settings.value(QStringLiteral("command_aliases")).toMap();
    m_ignoreMasks = variantStringList(settings.value(QStringLiteral("ignores")));
    m_mutedChannelKeys = variantStringList(settings.value(QStringLiteral("muted_channels")));
    m_friendNicks = variantStringList(settings.value(QStringLiteral("friends")));
    m_autoReconnect = settings.value(QStringLiteral("auto_reconnect"), true).toBool();
    m_loggingEnabled = settings.value(QStringLiteral("logging"), true).toBool();
    m_replayLogEnabled = settings.value(QStringLiteral("replay_log"), true).toBool();
    m_dccEnabled = settings.value(QStringLiteral("dcc_enabled"), false).toBool();
    m_dccManager->setEnabled(m_dccEnabled);
    m_showTimestamps = settings.value(QStringLiteral("show_timestamps"), true).toBool();
    m_timestampFormat =
        settings.value(QStringLiteral("timestamp_format"), QStringLiteral("%I:%M %p")).toString();
    m_alignNicks = settings.value(QStringLiteral("align_nicks"), true).toBool();
    m_separatorLine = settings.value(QStringLiteral("separator_line"), true).toBool();
    m_hideJoinPart = settings.value(QStringLiteral("hide_joinpart"), false).toBool();
    m_showFormatting = settings.value(QStringLiteral("show_formatting"), true).toBool();
    const QString nickColorMode =
        settings
            .value(QStringLiteral("nick_color_mode"),
                   settings.value(QStringLiteral("colored_nicks"), true).toBool()
                       ? QStringLiteral("palette")
                       : QStringLiteral("off"))
            .toString();
    m_appearance->setNickColorMode(nickColorMode);
    m_coloredNicks = nickColorMode != QLatin1String("off");
    m_showMode = settings.value(QStringLiteral("show_mode"), true).toBool();
    m_pmEcho = settings.value(QStringLiteral("pm_echo"), true).toBool();
    m_indentWrap = settings.value(QStringLiteral("indent_wrap"), true).toBool();
    m_markerLine = settings.value(QStringLiteral("marker_line"), true).toBool();
    m_replayLines = settings.value(QStringLiteral("replay_lines"), 0).toInt();
    m_nickColorOverrides = settings.value(QStringLiteral("nick_colors")).toMap();
    m_sortByStatus =
        settings.value(QStringLiteral("sort_status"),
                       settings.value(QStringLiteral("sort_users_by_status"), true))
            .toBool();
    if (m_chatPane != nullptr) {
        m_chatPane->setStripColorsOnCopy(
            settings.value(QStringLiteral("strip_color_copy"), true).toBool());
    }
    m_pasteGuard = settings.value(QStringLiteral("paste_guard"), true).toBool();
    m_pasteLines = settings.value(QStringLiteral("paste_lines"), 4).toInt();
    m_autoRejoin = settings.value(QStringLiteral("auto_rejoin"), false).toBool();
    m_rejoinDelay = settings.value(QStringLiteral("rejoin_delay"), 2).toInt();
    m_ignoreInvites = settings.value(QStringLiteral("ignore_invites"), false).toBool();
    m_inviteProtect = settings.value(QStringLiteral("invite_protect"), true).toBool();
    m_confirmQuit = settings.value(QStringLiteral("confirm_quit"), true).toBool();
    m_scrollback = std::max(100, settings.value(QStringLiteral("scrollback"), 2000).toInt());
    m_chatBuffers.setMaxLinesPerBuffer(m_scrollback);
    if (m_chatView != nullptr && m_chatView->document() != nullptr) {
        // The live append path only inserts; without a block cap a buffer left
        // active for days grows the QTextDocument unbounded. The model keeps
        // m_scrollback lines; let the view hold a little more between renders.
        m_chatView->document()->setMaximumBlockCount(m_scrollback + 64);
    }
    m_autoAwayMins = settings.value(QStringLiteral("auto_away_mins"), 0).toInt();
    if (m_autoAwayMins > 0) {
        m_autoAwayTimer.start(m_autoAwayMins * 60 * 1000);
    } else {
        m_autoAwayTimer.stop();
    }
    applyCtcpVersion(settings);
    if (m_input != nullptr) {
        m_input->setPlaceholderText(
            settings.value(QStringLiteral("show_input_hint"), true).toBool()
                ? QStringLiteral(
                      "Message - Tab completes - Up/Down history - Ctrl+B/I/U/K format")
                : QString());
    }
    m_chatLogStore.setLogMask(settings.value(QStringLiteral("log_mask")).toString());
    applyNavShortcutOverrides(settings);
    // recolorMemberList NOT called here: m_currentChatTheme is updated further
    // down, and recolouring with the stale theme made chat and member list
    // briefly disagree. The renderActiveBufferMetadata() call at the end of
    // this function recolours with the fresh theme.
    m_ogRenderOptions.showSiteName    = settings.value(QStringLiteral("og_show_site_name"),    true).toBool();
    m_ogRenderOptions.showTitle       = settings.value(QStringLiteral("og_show_title"),        true).toBool();
    m_ogRenderOptions.showDescription = settings.value(QStringLiteral("og_show_description"),  true).toBool();
    m_ogRenderOptions.showImage       = settings.value(QStringLiteral("og_show_image"),        true).toBool();
    {
        const QColor bg = resolvedChatBackground();
        m_ogRenderOptions.darkChat =
            (0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue()) < 150.0;
    }
    // m_ogRenderOptions stays here for the inline-image scaling in
    // handlePreviewImageFetched; the card fetcher gets its own copy + the toggles.
    m_previewFetcher->toggles() = maxchat::services::linkPreviewTogglesFromSettings(settings);
    m_previewFetcher->renderOptions() = m_ogRenderOptions;
    if (m_mainSplitter != nullptr) {
        const QVariantList savedSizes = settings.value(QStringLiteral("splitter_sizes")).toList();
        if (savedSizes.size() >= 3) {
            QList<int> sizes;
            sizes.reserve(3);
            for (int index = 0; index < 3; ++index) {
                sizes.append(std::max(0, savedSizes.at(index).toInt()));
            }
            m_mainSplitter->setSizes(sizes);
        }
    }
    setButtonBarVisible(settings.value(QStringLiteral("show_button_bar"), true).toBool(), false);
    // setServerListVisible must run before setBufferTabsVisible: when both are on,
    // tabs win (they hide the tree). If tabs ran first and hid the tree, the
    // subsequent setSplitterPanelVisible call inside setServerListVisible would
    // restore it via splitter->setSizes, causing both to show at the same time.
    setServerListVisible(settings.value(QStringLiteral("server_list_visible"), true).toBool(),
                         false);
    setBufferTabsVisible(settings.value(QStringLiteral("buffer_tabs"), false).toBool(), false);
    setMembersVisible(settings.value(QStringLiteral("member_list_visible"), true).toBool(), false);
    m_nickColumnWidth = std::clamp(settings.value(QStringLiteral("nick_width"), 16).toInt(), 4, 40);
    if (m_chatSeparatorAction != nullptr && m_chatSeparatorAction->isChecked() != m_separatorLine) {
        const QSignalBlocker blocker(m_chatSeparatorAction);
        m_chatSeparatorAction->setChecked(m_separatorLine);
    }
    if (m_doNotDisturbAction != nullptr &&
        m_doNotDisturbAction->isChecked() !=
            settings.value(QStringLiteral("dnd"), false).toBool()) {
        const QSignalBlocker blocker(m_doNotDisturbAction);
        m_doNotDisturbAction->setChecked(settings.value(QStringLiteral("dnd"), false).toBool());
    }
    if (m_comicCaptionsAction != nullptr &&
        m_comicCaptionsAction->isChecked() !=
            settings.value(QStringLiteral("comic_captions"), true).toBool()) {
        const QSignalBlocker blocker(m_comicCaptionsAction);
        m_comicCaptionsAction->setChecked(
            settings.value(QStringLiteral("comic_captions"), true).toBool());
    }
    m_floodGuard.configure(settings.value(QStringLiteral("flood_protect"), false).toBool(),
                           settings.value(QStringLiteral("flood_msgs"), 10).toInt(),
                           settings.value(QStringLiteral("flood_secs"), 4).toInt());
    configureSpellcheck(settings);
    m_media->configure(settings);
    setAcceptDrops(m_media->isConfigured());
    m_connection.setIgnoreMasks(m_ignoreMasks);
    for (auto* irc : std::as_const(m_connectionsByNetwork)) {
        if (irc != nullptr) {
            irc->setIgnoreMasks(m_ignoreMasks);
        }
    }
    if (m_friendNicks.isEmpty() || !anyNetworkConnectionIsConnected()) {
        m_friendPollTimer.stop();
    } else {
        m_haveFriendSnapshot = false;
        m_haveFriendSnapshotByNetwork.clear();
        pollFriends();
        m_friendPollTimer.start();
    }
    const maxchat::ui::ResolvedFonts fonts = m_appearance->resolveFonts(settings);
    const QFont appFont = fonts.app;
    const QFont chatFont = fonts.chat;
    const QFont listFont = fonts.list;
    qApp->setFont(appFont);
    if (m_chatView != nullptr) {
        m_chatView->setFont(chatFont);
        m_chatView->setLineWrapMode(settings.value(QStringLiteral("word_wrap"), true).toBool()
                                        ? QTextEdit::WidgetWidth
                                        : QTextEdit::NoWrap);
    }
    if (m_input != nullptr) {
        m_input->setFont(chatFont);
        resizeMessageInput();
    }
    if (m_networkTree != nullptr) {
        m_networkTree->setFont(listFont);
    }
    if (m_memberList != nullptr) {
        m_memberList->setFont(listFont);
    }
    if (m_channelModesButton != nullptr) {
        m_channelModesButton->setFont(listFont);
    }
    if (m_scripts != nullptr) {
        // Terminal size 0 means "use the per-profile size" (ibm-vga 11, c64 13).
        m_scripts->setTerminalFont(
            settings.value(QStringLiteral("terminal_font_family"),
                           QStringLiteral("JetBrains Mono"))
                .toString(),
            settings.value(QStringLiteral("terminal_font_size"), 0).toInt(),
            settings.value(QStringLiteral("terminal_font_bold"), false).toBool());
    }

    m_appearance->loadFromSettings(settings);
    m_appearance->applyTheme(m_appearance->themeId());
    // Re-apply the app font AFTER the theme: setting a global stylesheet re-polishes
    // widgets and drops qApp->setFont for chrome (menu bar, menus, dialogs), so the
    // window/menu font would otherwise ignore the configured app font. Set it on the
    // menu bar explicitly too (popup menus inherit from it).
    qApp->setFont(appFont);
    if (menuBar() != nullptr) {
        menuBar()->setFont(appFont);
    }
    m_appearance->syncThemeActions(m_appearance->themeId());
    m_appearance->syncChatThemeActions(m_appearance->chatThemeId());
    m_appearance->syncWallpaperActions(m_appearance->wallpaperValue());

    // Per-area color overrides ride on widget-level style sheets so they win
    // over the window-level theme QSS; empty = follow the theme.
    const auto colorOverride = [&settings](const char* key) {
        return settings.value(QLatin1String(key)).toString().trimmed();
    };
    const QString chatTextColor = colorOverride("chat_text_color");
    if (m_chatView != nullptr) {
        m_chatView->setStyleSheet(
            chatTextColor.isEmpty()
                ? QString()
                : QStringLiteral("QTextBrowser#chatView{color:%1;}").arg(chatTextColor));
    }
    const QString treeColor = colorOverride("tree_color");
    if (m_networkTree != nullptr) {
        m_networkTree->setStyleSheet(
            treeColor.isEmpty() ? QString()
                                : QStringLiteral("QTreeWidget{color:%1;}").arg(treeColor));
    }
    const QString usersColor = colorOverride("userlist_color");
    if (m_memberList != nullptr) {
        m_memberList->setStyleSheet(
            usersColor.isEmpty() ? QString()
                                 : QStringLiteral("QListWidget{color:%1;}").arg(usersColor));
    }
    const QString statusColor = colorOverride("status_text_color");
    statusBar()->setStyleSheet(
        statusColor.isEmpty() ? QString()
                              : QStringLiteral("QStatusBar{color:%1;}").arg(statusColor));
    const QString topicColor = colorOverride("topic_color");
    if (m_topicLabel != nullptr) {
        m_topicLabel->setStyleSheet(
            topicColor.isEmpty() ? QString()
                                 : QStringLiteral("QLabel{color:%1;}").arg(topicColor));
    }
    m_appearance->setEventColor(colorOverride("event_color"));

    // Per-area fonts (list/nick/status/topic). Empty family / size 0 = inherit.
    const auto areaFont = [&settings](const char* familyKey, const char* sizeKey,
                                      const char* boldKey, const QFont& base) {
        QFont font = base;
        const QString family = settings.value(QLatin1String(familyKey)).toString().trimmed();
        if (!family.isEmpty()) {
            font.setFamily(family);
        }
        const int size = settings.value(QLatin1String(sizeKey)).toInt();
        if (size > 0) {
            font.setPointSize(size);
        }
        font.setBold(settings.value(QLatin1String(boldKey)).toBool());
        return font;
    };
    const QFont baseFont = qApp->font();
    if (m_networkTree != nullptr) {
        m_networkTree->setFont(
            areaFont("list_font_family", "list_font_size", "list_font_bold", baseFont));
    }
    if (m_memberList != nullptr) {
        m_memberList->setFont(
            areaFont("list_font_family", "list_font_size", "list_font_bold", baseFont));
    }
    if (m_membersHeader != nullptr) {
        m_membersHeader->setFont(
            areaFont("list_font_family", "list_font_size", "list_font_bold", baseFont));
    }
    if (m_channelModesButton != nullptr) {
        m_channelModesButton->setFont(
            areaFont("list_font_family", "list_font_size", "list_font_bold", baseFont));
    }
    if (m_topicLabel != nullptr) {
        m_topicLabel->setFont(
            areaFont("topic_font_family", "topic_font_size", "topic_font_bold", baseFont));
    }
    statusBar()->setFont(
        areaFont("status_font_family", "status_font_size", "status_font_bold", baseFont));

    // Your-nick label beside the input (text via updateNickLabel; font/colour here).
    if (m_nickLabel != nullptr) {
        updateNickLabel();
        m_nickLabel->setFont(
            areaFont("nick_font_family", "nick_font_size", "nick_font_bold", baseFont));
        const QString nickColor = colorOverride("nick_label_color");
        m_nickLabel->setStyleSheet(
            nickColor.isEmpty() ? QString() : QStringLiteral("QLabel{color:%1;}").arg(nickColor));
    }

    updateTrayIcon();
    m_notifyController->updateMinimizeToTrayFromSettings();
    m_highlightWords = settings.value(QStringLiteral("highlight_words")).toString().split(
        QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
    m_notifyPm = settings.value(QStringLiteral("notify_pm"), true).toBool();
    m_notifyHighlight = settings.value(QStringLiteral("notify_highlight"), true).toBool();
    m_beepHighlight = settings.value(QStringLiteral("beep_highlight"), true).toBool();
    m_notifyFlash = settings.value(QStringLiteral("notify_flash"), true).toBool();
    m_notifySound = settings.value(QStringLiteral("notify_sound"), false).toBool();
    m_notifyStyle = settings.value(QStringLiteral("notify_popup"), QStringLiteral("custom")).toString();
    m_notifyCorner = settings.value(QStringLiteral("notify_corner"), QStringLiteral("br")).toString();
    m_notifyDuration = settings.value(QStringLiteral("notify_duration"), 6).toInt();
    m_notifyTheme = settings.value(QStringLiteral("notify_theme"), QStringLiteral("follow")).toString();
    renderActiveBuffer();
    updateChatSeparatorGuide();
    renderActiveBufferMetadata();
    updateNetworkTreeLabels();
    updateChannelModeButton();
    // Script permissions are updated live via scriptPermissionChanged signal — no batch reload needed.
    }



void maxchat::ui::MainWindow::toggleWindowVisibility() {
    if (isVisible() && !isMinimized()) {
        hide();
    } else {
        // Minimize-to-tray hides the window while it is still minimized;
        // plain show() would restore it minimized to the taskbar.
        if (isMinimized()) {
            showNormal();
        } else {
            show();
        }
        raise();
        activateWindow();
    }
}



} // namespace maxchat::ui

// Thin forwarders into NotificationController (decomp Phase 4). notify() has
// external callers (notifyUser host hook, IrcRouter); updateTrayIcon() is the
// MainWindowHost override AppearanceController drives.
void maxchat::ui::MainWindow::notify(const QString& title, const QString& text,
                                     const QString& network, const QString& target) {
    m_notifyController->notify(title, text, network, target);
}

void maxchat::ui::MainWindow::updateTrayIcon() {
    m_notifyController->updateTrayIcon();
}
