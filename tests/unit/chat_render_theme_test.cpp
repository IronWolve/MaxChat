// Unit tests for resolveChatRenderTheme (RENDER_PIPELINE_DESIGN.md R0). This is
// the pure chat-theme → render-colour resolution lifted out of
// MainWindow::chatLineFormatOptions — now testable with no window at all.

#include "ui/ChatRenderTheme.h"
#include "ui/ThemeCatalog.h"

#include <QColor>
#include <QtTest/QtTest>

using maxchat::ui::AppThemeDefinition;
using maxchat::ui::ChatRenderTheme;
using maxchat::ui::ChatThemeDefinition;
using maxchat::ui::resolveChatRenderTheme;

class ChatRenderThemeTest : public QObject {
    Q_OBJECT

  private slots:
    void explicitTimestampColourWins() {
        ChatThemeDefinition chat;
        chat.id = QStringLiteral("ocean");
        chat.timestamp = QColor(QStringLiteral("#112233"));
        const ChatRenderTheme rt =
            resolveChatRenderTheme(chat, {}, QColor(20, 20, 20), {}, false, QString());
        QCOMPARE(rt.timestampColor, QColor(QStringLiteral("#112233")).name());
    }

    void darkBackgroundPicksLightTimestamp() {
        ChatThemeDefinition chat;
        chat.id = QStringLiteral("dark");
        chat.bg = QColor(20, 20, 20); // luma < 150 → dark
        const ChatRenderTheme rt =
            resolveChatRenderTheme(chat, {}, chat.bg, {}, false, QString());
        QCOMPARE(rt.timestampColor, QStringLiteral("#8a8a8a"));
    }

    void lightBackgroundPicksDarkTimestamp() {
        ChatThemeDefinition chat;
        chat.id = QStringLiteral("light");
        chat.bg = QColor(240, 240, 240); // luma > 150 → light
        const ChatRenderTheme rt =
            resolveChatRenderTheme(chat, {}, chat.bg, {}, false, QString());
        QCOMPARE(rt.timestampColor, QStringLiteral("#6f6f6f"));
    }

    void followThemeDerivesTimestampFromAppTheme() {
        ChatThemeDefinition chat;
        chat.id = QStringLiteral("follow"); // bg comes from the app theme
        AppThemeDefinition app;
        app.chatBg = QColor(245, 245, 245); // light → dark timestamp
        const ChatRenderTheme rt =
            resolveChatRenderTheme(chat, app, app.chatBg, {}, false, QString());
        QCOMPARE(rt.timestampColor, QStringLiteral("#6f6f6f"));
    }

    void eventColourOverridesSystemColour() {
        ChatThemeDefinition chat;
        chat.system = QColor(QStringLiteral("#010203"));
        const ChatRenderTheme rt = resolveChatRenderTheme(
            chat, {}, QColor(20, 20, 20), {}, false, QStringLiteral("#ff8800"));
        QCOMPARE(rt.systemColor, QStringLiteral("#ff8800"));
    }

    void foregroundFallsBackThroughAppTheme() {
        ChatThemeDefinition chat; // no chat fg
        AppThemeDefinition app;
        app.chatFg = QColor(QStringLiteral("#abcdef"));
        const ChatRenderTheme rt =
            resolveChatRenderTheme(chat, app, QColor(20, 20, 20), {}, false, QString());
        QCOMPARE(rt.defaultForeground, QColor(QStringLiteral("#abcdef")).name());
    }

    void foregroundUltimateFallback() {
        ChatThemeDefinition chat; // no chat fg
        AppThemeDefinition app;   // no app chatFg/text either
        const ChatRenderTheme rt =
            resolveChatRenderTheme(chat, app, QColor(20, 20, 20), {}, false, QString());
        QCOMPARE(rt.defaultForeground, QStringLiteral("#cfcfcf"));
    }

    void backgroundAndNickPalettePassThrough() {
        ChatThemeDefinition chat;
        const QStringList palette = {QStringLiteral("#111111"), QStringLiteral("#222222")};
        const ChatRenderTheme rt =
            resolveChatRenderTheme(chat, {}, QColor(10, 20, 30), palette, true, QString());
        QCOMPARE(rt.defaultBackground, QColor(10, 20, 30).name());
        QCOMPARE(rt.nickPalette, palette);
        QVERIFY(rt.monoNicks);
    }

    void unsetOptionalColoursStayEmpty() {
        ChatThemeDefinition chat; // no bracket/system
        const ChatRenderTheme rt =
            resolveChatRenderTheme(chat, {}, QColor(20, 20, 20), {}, false, QString());
        QVERIFY(rt.bracketColor.isEmpty());
        QVERIFY(rt.systemColor.isEmpty());
    }
};

QTEST_APPLESS_MAIN(ChatRenderThemeTest)
#include "chat_render_theme_test.moc"
