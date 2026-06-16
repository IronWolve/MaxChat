#include "ui/ComicController.h"

#include <QChar>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>

#include "irc/IrcFormat.h"

#include "comic/ComicArt.h"
#include "comic/ComicCharacter.h"
#include "comic/ComicEmotion.h"
#include "comic/ComicRenderer.h"
#include "ui/ComicSettingsDialog.h"
#include "ui/ComicView.h"
#include "ui/GeometryPersist.h"
#include "ui/MainWindow.h"

namespace maxchat::ui {

namespace {

// Per-buffer key used for comic view suppression / marker tracking. Shared with
// MainWindow's copy; trivial, so duplicated to keep the relocation simple.
QString comicKey(const QString& network, const QString& target) {
    return network + QChar(0x1f) + target;
}

} // namespace

void ComicController::openComicHelp() {
    QMessageBox::information(
        &m_window, QStringLiteral("Comic Chat Guide"),
        QStringList{
            QStringLiteral("Comic Mode turns the channel into a comic strip. Turn it on per "
                           "channel from the Comic menu (Ctrl+M), and set your art folder in "
                           "Comic ▸ Comic Settings."),
            QStringLiteral("HOW YOUR MESSAGE BECOMES A BALLOON:"),
            QStringLiteral("• Normal text → a speech balloon."),
            QStringLiteral("• (text in parentheses) → a thought cloud (the fluffy one)."),
            QStringLiteral("• ALL CAPS or ending in !! → a jagged shout balloon."),
            QStringLiteral("• /me action → a narration caption box (e.g. \"/me waves\")."),
            QStringLiteral("EXPRESSIONS: your character's face is guessed from your text — "
                           "smileys :) :( ;) :D, emoji, and words like lol/grr/zzz/omg. "
                           "Set a fixed expression with Comic ▸ Emotion."),
            QStringLiteral("TIPS: double-click a panel to zoom; right-click to copy or save "
                           "panels. Balloon font, speaker tinting, and panel count are in "
                           "Comic Settings."),
        }
            .join(QStringLiteral("\n\n")));
}

void ComicController::openComicSettings() {
    ensureComicArt();
    QStringList stems = m_window.m_comicCharacterPaths.keys();
    std::sort(stems.begin(), stems.end());
    QStringList bgs;
    for (auto it = m_window.m_comicBackgroundPaths.constBegin(); it != m_window.m_comicBackgroundPaths.constEnd();
         ++it) {
        bgs.append(it.key());
    }
    std::sort(bgs.begin(), bgs.end());
    // Per-channel overrides editor: offer every open channel as "net/target".
    QStringList channelKeys;
    for (const maxchat::core::ChatBufferId& id : m_window.m_chatBuffers.buffers()) {
        if (id.kind == maxchat::core::ChatBufferKind::Channel) {
            channelKeys.append(id.network + QStringLiteral("/") + id.target);
        }
    }
    std::sort(channelKeys.begin(), channelKeys.end());
    ComicSettingsDialog dialog(m_window.m_settings.loadWithDefaults(), stems, bgs, channelKeys, &m_window);
    attachGeometryPersist(&dialog, m_window.m_settings, QStringLiteral("geom_comic_settings"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (!m_window.m_settings.saveRaw(dialog.settings())) {
        m_window.appendSystemLine(tr("! Could not save comic settings."));
        return;
    }
    m_window.m_comicArtDirLoaded.clear(); // force art rescan
    m_window.m_comicBgCache.clear();
    refreshComic();
    m_window.appendSystemLine(tr("! Comic settings saved."));
}

void ComicController::saveComic() {
    if (m_window.m_comicView == nullptr || !m_window.m_comicView->hasPanels()) {
        m_window.appendSystemLine(tr("! No comic to save yet."));
        return;
    }
    const QPixmap sheet = m_window.m_comicView->sheet();
    if (sheet.isNull()) {
        return;
    }
    QString defaultName =
        QStringLiteral("comic-%1-%2.png")
            .arg(QString(m_window.m_currentTarget).remove(QLatin1Char('#')).remove(QLatin1Char('&')).replace(
                     QLatin1Char('/'), QLatin1Char('_')),
                 QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
    QString path = QFileDialog::getSaveFileName(&m_window, QStringLiteral("Save comic image"),
                                                defaultName, QStringLiteral("PNG image (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".png");
    }
    if (sheet.save(path, "PNG")) {
        m_window.appendSystemLine(tr("! Comic saved to %1").arg(path));
    } else {
        m_window.appendSystemLine(tr("! Could not save the comic image."));
    }
}

void ComicController::setComicMode(bool enabled) {
    // Comic Mode is per channel: the toggle affects only the active buffer.
    // The action is disabled on the server buffer (activateBufferTarget syncs
    // that), but guard anyway — Ctrl+M can fire before the first sync.
    if (m_window.m_currentTarget.trimmed().isEmpty()) {
        if (m_window.m_comicModeAction != nullptr && m_window.m_comicModeAction->isChecked()) {
            const QSignalBlocker blocker(m_window.m_comicModeAction);
            m_window.m_comicModeAction->setChecked(false);
        }
        m_window.statusBar()->showMessage(
            QStringLiteral("Comic Mode is per channel — switch to a channel to toggle it."));
        return;
    }

    const QString key = comicKey(m_window.activeNetworkName(), m_window.m_currentTarget);
    const bool backendWasOn = m_window.m_comicMode;
    if (enabled) {
        m_window.m_comicEnabledBuffers.insert(key);
    } else {
        m_window.m_comicEnabledBuffers.remove(key);
    }
    // The backend runs while any buffer has comic enabled; panel rendering for
    // non-opted-in buffers is skipped in refreshComic.
    m_window.m_comicMode = !m_window.m_comicEnabledBuffers.isEmpty();

    m_window.m_chatPane->setComicVisible(enabled);
    if (enabled) {
        m_window.m_chatPane->ensureComicSplit();
        ensureComicArt();
        refreshComic();
        if (!backendWasOn && m_window.m_comicCharacterPaths.isEmpty()) {
            m_window.appendSystemLine(tr(
                "! Comic Mode: no art loaded. Set your Comic Chat art folder in "
                "Comic > Comic Settings (the folder with the .avb/.bgb files)."));
        }
    }

    if (m_window.m_comicModeAction != nullptr && m_window.m_comicModeAction->isChecked() != enabled) {
        const QSignalBlocker blocker(m_window.m_comicModeAction);
        m_window.m_comicModeAction->setChecked(enabled);
    }
    m_window.statusBar()->showMessage(enabled ? QStringLiteral("Comic Mode on for this channel.")
                                     : QStringLiteral("Comic Mode off for this channel."));
}

namespace {

// Java-style deterministic hash used by the Python comic for character/pose.
quint32 comicHash(const QString& s) {
    quint32 h = 0;
    for (const QChar c : s) {
        h = h * 31u + c.unicode();
    }
    return h;
}

// Text-based emotion guess (port of _emotion_for).

struct ComicMsg {
    QString nick;
    QString text;
    bool think = false;
    bool action = false;
};

} // namespace

void ComicController::ensureComicArt() {
    const QVariantMap settings = m_window.m_settings.loadWithDefaults();
    const QString dir = settings.value(QStringLiteral("comic_art_dir")).toString().trimmed();
    const QString resolved = dir.isEmpty() ? maxchat::comic::bundledArtDir() : dir;
    if (resolved == m_window.m_comicArtDirLoaded) {
        return;
    }
    m_window.m_comicArtDirLoaded = resolved;
    m_window.m_comicCharacterPaths.clear();
    m_window.m_comicBackgroundPaths.clear();
    m_window.m_comicAutoChars.clear();
    m_window.m_comicPanelCache.clear(); // new art invalidates every rendered panel
    if (resolved.isEmpty()) {
        return;
    }
    QStringList backgrounds;
    QStringList characters;
    maxchat::comic::scanArtDir(resolved, backgrounds, characters);
    for (const QString& path : characters) {
        m_window.m_comicCharacterPaths.insert(QFileInfo(path).completeBaseName().toLower(), path);
    }
    for (const QString& path : backgrounds) {
        m_window.m_comicBackgroundPaths.insert(QFileInfo(path).fileName().toLower(), path);
    }
}

maxchat::comic::Character* ComicController::comicCharacterForNick(const QString& nick) {
    if (m_window.m_comicCharacterPaths.isEmpty()) {
        return nullptr;
    }
    const QString low = nick.trimmed().toLower();
    const QVariantMap settings = m_window.m_settings.loadWithDefaults();
    // A per-channel assignment (comic_channels[net/target].chars) wins over the
    // global comic_chars map.
    const QString chanKey =
        m_window.activeNetworkName().trimmed() + QStringLiteral("/") + m_window.m_currentTarget.trimmed();
    const QVariantMap chanChars = settings.value(QStringLiteral("comic_channels"))
                                      .toMap()
                                      .value(chanKey)
                                      .toMap()
                                      .value(QStringLiteral("chars"))
                                      .toMap();
    const QVariantMap manual = settings.value(QStringLiteral("comic_chars")).toMap();
    QString stem = chanChars.value(low).toString();
    if (stem.isEmpty()) {
        stem = manual.value(low).toString();
    }
    if (stem.isEmpty()) {
        const QString self = settings.value(QStringLiteral("comic_self_char")).toString();
        if (!self.isEmpty() && low == m_window.currentNickForNetwork(m_window.activeNetworkName()).toLower()) {
            stem = self;
        }
    }
    if (stem.isEmpty()) {
        if (m_window.m_comicAutoChars.contains(low)) {
            stem = m_window.m_comicAutoChars.value(low);
        } else {
            QStringList pool = m_window.m_comicCharacterPaths.keys();
            QStringList used = m_window.m_comicAutoChars.values();
            for (auto it = manual.constBegin(); it != manual.constEnd(); ++it) {
                used.append(it.value().toString().toLower());
            }
            QStringList freePool;
            for (const QString& s : pool) {
                if (!used.contains(s)) {
                    freePool.append(s);
                }
            }
            if (freePool.isEmpty()) {
                freePool = pool;
            }
            std::sort(freePool.begin(), freePool.end());
            stem = freePool.at(static_cast<int>(comicHash(low) % freePool.size()));
            m_window.m_comicAutoChars.insert(low, stem);
        }
    }
    const QString path = m_window.m_comicCharacterPaths.value(stem.toLower());
    return path.isEmpty() ? nullptr : maxchat::comic::loadCharacter(path);
}

QImage ComicController::comicBackground() {
    const QVariantMap settings = m_window.m_settings.loadWithDefaults();
    // A per-channel background (comic_channels[net/target].bg) wins over the
    // global comic_bg.
    const QString chanKey =
        m_window.activeNetworkName().trimmed() + QStringLiteral("/") + m_window.m_currentTarget.trimmed();
    const QVariantMap chanCfg =
        settings.value(QStringLiteral("comic_channels")).toMap().value(chanKey).toMap();
    QString file = chanCfg.value(QStringLiteral("bg")).toString().toLower();
    if (file.isEmpty()) {
        file = settings.value(QStringLiteral("comic_bg")).toString().toLower();
    }
    QString path = m_window.m_comicBackgroundPaths.value(file);
    if (path.isEmpty() && !m_window.m_comicBackgroundPaths.isEmpty()) {
        if (file.isEmpty() &&
            settings.value(QStringLiteral("comic_random_bg"), false).toBool()) {
            // No configured background + random enabled → pick one that's stable
            // per channel (seed by the current target, sorted keys for determinism).
            QStringList keys = m_window.m_comicBackgroundPaths.keys();
            std::sort(keys.begin(), keys.end());
            const QString seed = m_window.m_currentTarget.isEmpty()
                                     ? QStringLiteral("default")
                                     : m_window.m_currentTarget.toLower();
            const int index = static_cast<int>(comicHash(seed) % static_cast<quint32>(keys.size()));
            path = m_window.m_comicBackgroundPaths.value(keys.at(index));
        } else {
            path = *m_window.m_comicBackgroundPaths.constBegin();
        }
    }
    if (path.isEmpty()) {
        return {};
    }
    if (!m_window.m_comicBgCache.contains(path)) {
        m_window.m_comicBgCache.insert(path, maxchat::comic::loadBackground(path));
    }
    return m_window.m_comicBgCache.value(path);
}

QString ComicController::comicEmotionForMessage(const QString& nick,
                                                        const QString& text) {
    // Your own lines honour the emotion override (if not "auto"); everyone else
    // (and you, when auto) get the text-based guess.
    if (m_window.m_comicSelfEmotion != QStringLiteral("auto") &&
        nick.trimmed().toLower() ==
            m_window.currentNickForNetwork(m_window.activeNetworkName()).toLower()) {
        return m_window.m_comicSelfEmotion;
    }
    return maxchat::comic::guessEmotion(text);
}

void ComicController::refreshComic() {
    if (m_window.m_comicView == nullptr || !m_window.m_comicMode) {
        return;
    }
    if (!m_window.m_comicEnabledBuffers.contains(comicKey(m_window.activeNetworkName(), m_window.m_currentTarget))) {
        return; // this buffer hasn't opted in — skip render, backend still tracks state
    }
    ensureComicArt();
    const QVariantMap settings = m_window.m_settings.loadWithDefaults();
    const bool captions = settings.value(QStringLiteral("comic_captions"), true).toBool();
    const double captionScale = settings.value(QStringLiteral("comic_caption_scale"), 1.0).toDouble();
    const bool balloonTint = settings.value(QStringLiteral("comic_balloon_tint"), true).toBool();
    const QString comicFontFamily =
        settings.value(QStringLiteral("comic_font"), QStringLiteral("Comic Relief")).toString();
    const int panelCount = std::clamp(settings.value(QStringLiteral("comic_panels"), 4).toInt(), 1, 6);
    const int perPanel = std::clamp(settings.value(QStringLiteral("comic_per_panel"), 4).toInt(), 1, 6);
    const int minFont = std::clamp(settings.value(QStringLiteral("comic_min_font"), 9).toInt(), 6, 13);
    QStringList comicIgnore = settings.value(QStringLiteral("comic_ignore")).toStringList();
    // Per-channel "hide from comic" nicks add to the global comic_ignore list.
    const QString comicChanKey =
        m_window.activeNetworkName().trimmed() + QStringLiteral("/") + m_window.m_currentTarget.trimmed();
    const QVariantList chanIgnore = settings.value(QStringLiteral("comic_channels"))
                                        .toMap()
                                        .value(comicChanKey)
                                        .toMap()
                                        .value(QStringLiteral("ignore"))
                                        .toList();
    for (const QVariant& entry : chanIgnore) {
        comicIgnore.append(entry.toString().toLower());
    }
    const bool ignoreCmds = settings.value(QStringLiteral("comic_ignore_cmds"), true).toBool();
    const QStringList botPatterns = settings.value(QStringLiteral("comic_bot_patterns")).toStringList();
    QRegularExpression excludeRe;
    const QString excludeStr = settings.value(QStringLiteral("comic_exclude_regex")).toString();
    if (!excludeStr.isEmpty()) {
        excludeRe = QRegularExpression(excludeStr, QRegularExpression::CaseInsensitiveOption);
    }

    const auto filtered = [&](const QString& nick, const QString& text) {
        if (comicIgnore.contains(nick.toLower())) {
            return true;
        }
        const QString stripped = maxchat::irc::stripFormatting(text).trimmed();
        if (stripped.isEmpty()) {
            return true;
        }
        if (ignoreCmds) {
            const QString low = stripped.toLower();
            for (const QString& pat : botPatterns) {
                if (pat.isEmpty() || !low.startsWith(pat)) {
                    continue;
                }
                if (pat.size() == 1) {
                    if (low.size() > 1 && low.at(1).isLetterOrNumber()) {
                        return true;
                    }
                } else {
                    return true;
                }
            }
        }
        if (!excludeStr.isEmpty() && excludeRe.isValid() && excludeRe.match(stripped).hasMatch()) {
            return true;
        }
        return false;
    };

    // Collect eligible speech messages from the active buffer.
    const maxchat::core::ChatBufferSnapshot snapshot =
        m_window.m_chatBuffers.snapshot(m_window.bufferIdForTarget(m_window.m_currentTarget));
    QVector<ComicMsg> msgs;
    for (const maxchat::core::ChatBufferLine& line : snapshot.lines) {
        const QString src = line.sourceText;
        if (src.isEmpty()) {
            continue;
        }
        ComicMsg msg;
        if (src.startsWith(QLatin1Char('<'))) {
            const int end = src.indexOf(QStringLiteral("> "));
            if (end <= 0) {
                continue;
            }
            msg.nick = src.mid(1, end - 1);
            msg.text = maxchat::irc::stripFormatting(src.mid(end + 2)).trimmed();
        } else if (!line.systemLine && src.startsWith(QStringLiteral("* "))) {
            const int sp = src.indexOf(QLatin1Char(' '), 2);
            if (sp <= 2) {
                continue;
            }
            msg.nick = src.mid(2, sp - 2);
            msg.text = maxchat::irc::stripFormatting(src.mid(sp + 1)).trimmed();
            msg.action = true;
            // Replayed log lines lose the systemLine flag, so join/part/quit
            // events come back looking like /me actions — filter them by verb
            // (live events never get here; they keep systemLine = true).
            static const QStringList kEventVerbs = {
                QStringLiteral("joined "),         QStringLiteral("left "),
                QStringLiteral("quit"),            QStringLiteral("was kicked from "),
                QStringLiteral("is now known as "), QStringLiteral("sets mode "),
                QStringLiteral("changed modes for "), QStringLiteral("set the topic"),
                QStringLiteral("changed the topic")};
            bool isEvent = false;
            for (const QString& verb : kEventVerbs) {
                if (msg.text.startsWith(verb)) {
                    isEvent = true;
                    break;
                }
            }
            if (isEvent) {
                continue;
            }
        } else {
            continue;
        }
        if (msg.text.contains(QStringLiteral("http://")) ||
            msg.text.contains(QStringLiteral("https://")) ||
            msg.text.contains(QStringLiteral("www."))) {
            continue; // links are chat embeds, never panels
        }
        if (filtered(msg.nick, msg.text)) {
            continue;
        }
        if (!msg.action && msg.text.size() > 2 && msg.text.startsWith(QLatin1Char('(')) &&
            msg.text.endsWith(QLatin1Char(')'))) {
            msg.think = true;
            msg.text = msg.text.mid(1, msg.text.size() - 2);
        }
        msgs.append(msg);
    }

    // Group into panels (bubble cap + min-font spill).
    constexpr int kSize = 315;
    QVector<QVector<ComicMsg>> panels;
    for (const ComicMsg& msg : msgs) {
        bool extended = false;
        if (!panels.isEmpty() && panels.last().size() < perPanel) {
            QVector<ComicMsg> trial = panels.last();
            trial.append(msg);
            // Build actors/lines for the trial to check the resulting font size.
            QVector<maxchat::comic::ComicActor> actors;
            QHash<QString, int> actorIndex;
            QVector<maxchat::comic::ComicLineItem> rlines;
            for (const ComicMsg& m : trial) {
                if (!actorIndex.contains(m.nick.toLower())) {
                    maxchat::comic::ComicActor a;
                    a.nick = m.nick;
                    a.character = comicCharacterForNick(m.nick);
                    a.emotion = comicEmotionForMessage(m.nick, m.text);
                    a.pose = a.character && a.character->bodyCount() > 0
                                 ? static_cast<int>(comicHash(m.nick.toLower() + QStringLiteral("|") +
                                                              m.text) %
                                                    a.character->bodyCount())
                                 : 0;
                    actorIndex.insert(m.nick.toLower(), actors.size());
                    actors.append(a);
                }
                maxchat::comic::ComicLineItem li;
                li.actorIndex = actorIndex.value(m.nick.toLower());
                li.text = m.text;
                li.think = m.think;
                li.action = m.action;
                rlines.append(li);
            }
            if (maxchat::comic::panelMinFont(kSize, actors, rlines, comicFontFamily) >= minFont) {
                panels.last() = trial;
                extended = true;
            }
        }
        if (!extended) {
            panels.append({msg});
        }
    }
    while (panels.size() > 6) {
        panels.removeFirst();
    }

    // Caption colours: per-user nick_colors override else hashed nick colour.
    const QString captionMode = settings.value(QStringLiteral("comic_caption_mode"),
                                               QStringLiteral("nick")).toString();
    const QString fixedColor = settings.value(QStringLiteral("comic_caption_color"),
                                              QStringLiteral("#363636")).toString();
    const QImage background = comicBackground();

    QVector<QPixmap> rendered;
    const QVector<QVector<ComicMsg>> shown =
        panels.size() > panelCount ? panels.mid(panels.size() - panelCount) : panels;
    for (const QVector<ComicMsg>& panel : shown) {
        QVector<maxchat::comic::ComicActor> actors;
        QHash<QString, int> actorIndex;
        QVector<maxchat::comic::ComicLineItem> rlines;
        QHash<QString, QString> captionColors;
        for (const ComicMsg& m : panel) {
            const QString low = m.nick.toLower();
            if (!actorIndex.contains(low)) {
                maxchat::comic::ComicActor a;
                a.nick = m.nick;
                a.character = comicCharacterForNick(m.nick);
                if (a.character == nullptr) {
                    continue; // no art for this nick → skip (its lines too)
                }
                a.emotion = comicEmotionForMessage(m.nick, m.text);
                a.pose = a.character->bodyCount() > 0
                             ? static_cast<int>(comicHash(low + QStringLiteral("|") + m.text) %
                                                a.character->bodyCount())
                             : 0;
                actorIndex.insert(low, actors.size());
                actors.append(a);
                captionColors.insert(low, captionMode == QStringLiteral("fixed")
                                              ? fixedColor
                                              : (m_window.m_nickColorOverrides.value(low).toString().isEmpty()
                                                     ? QColor(maxchat::irc::nickColor(m.nick)).name()
                                                     : m_window.m_nickColorOverrides.value(low).toString()));
            }
            if (!actorIndex.contains(low)) {
                continue;
            }
            // Latest emotion/pose for the actor.
            maxchat::comic::ComicActor& a = actors[actorIndex.value(low)];
            a.emotion = comicEmotionForMessage(m.nick, m.text);
            if (a.character && a.character->bodyCount() > 0) {
                a.pose = static_cast<int>(comicHash(low + QStringLiteral("|") + m.text) %
                                          a.character->bodyCount());
            }
            maxchat::comic::ComicLineItem li;
            li.actorIndex = actorIndex.value(low);
            li.text = m.text;
            li.think = m.think;
            li.action = m.action;
            rlines.append(li);
        }
        if (actors.isEmpty()) {
            continue;
        }
        // Panel cache: rendering is the hot path (every new message re-renders
        // the whole strip, but only the LAST panel actually changes). The key
        // covers every visual input, so settings/theme/art changes simply miss.
        QString key = QStringLiteral("%1|%2|%3|%4|%5")
                          .arg(background.cacheKey())
                          .arg(captions ? captionScale : -1.0)
                          .arg(kSize)
                          .arg(comicFontFamily)
                          .arg(balloonTint ? 1 : 0);
        for (const maxchat::comic::ComicActor& a : actors) {
            key += QStringLiteral("|%1:%2:%3:%4")
                       .arg(a.nick, a.emotion, QString::number(a.pose),
                            captionColors.value(a.nick.toLower()));
        }
        for (const maxchat::comic::ComicLineItem& li : rlines) {
            key += QStringLiteral("|%1%2%3")
                       .arg(li.actorIndex)
                       .arg(li.think ? QLatin1Char('T')
                                     : (li.action ? QLatin1Char('A') : QLatin1Char('S')))
                       .arg(li.text);
        }
        QPixmap panelPm = m_window.m_comicPanelCache.value(key);
        if (panelPm.isNull()) {
            if (m_window.m_comicPanelCache.size() > 96) {
                m_window.m_comicPanelCache.clear(); // cheap flush beats LRU bookkeeping here
            }
            panelPm = maxchat::comic::renderComicPanel(kSize, background, actors, rlines,
                                                       captions, captionScale, captionColors,
                                                       balloonTint, comicFontFamily);
            m_window.m_comicPanelCache.insert(key, panelPm);
        }
        rendered.append(panelPm);
    }

    // Always present `panelCount` slots when art is available (Python parity):
    // pad with blank background panels so the configured number of panels is
    // visible even before enough messages arrive to fill them all. With no art at
    // all, leave it empty so ComicView shows its "set an art folder" hint.
    const bool haveArt = !m_window.m_comicCharacterPaths.isEmpty() || !background.isNull();
    if (haveArt && rendered.size() < panelCount) {
        const QString blankKey =
            QStringLiteral("blank|%1|%2").arg(background.cacheKey()).arg(kSize);
        QPixmap blank = m_window.m_comicPanelCache.value(blankKey);
        if (blank.isNull()) {
            blank = maxchat::comic::renderComicPanel(kSize, background, {}, {}, false, 1.0, {});
            m_window.m_comicPanelCache.insert(blankKey, blank);
        }
        while (rendered.size() < panelCount) {
            rendered.append(blank);
        }
    }
    m_window.m_comicView->setPanels(rendered);
}

} // namespace maxchat::ui
