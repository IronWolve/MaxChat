#include "ui/ChatRenderTheme.h"

#include <utility>

namespace maxchat::ui {

namespace {

// ITU-R BT.601 luma; < 150 reads as a "dark" background. Matches the thresholds
// used throughout the render code (timestamp + nick contrast guards).
double luma(const QColor& c) {
    return 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
}

} // namespace

ChatRenderTheme resolveChatRenderTheme(const ChatThemeDefinition& chatTheme,
                                       const AppThemeDefinition& appTheme,
                                       const QColor& resolvedBackground, QStringList nickPalette,
                                       bool monoNicks, const QString& eventColorOverride) {
    ChatRenderTheme out;

    // Timestamp colour: an explicit chat-theme timestamp wins; otherwise pick a
    // light/dark grey from the (theme-derived) chat background. Note this uses
    // its OWN background derivation, not `resolvedBackground` — kept verbatim so
    // the invalid-colour edge case (treated as dark) is unchanged.
    if (chatTheme.timestamp.isValid()) {
        out.timestampColor = chatTheme.timestamp.name();
    } else {
        QColor chatBg = chatTheme.bg;
        if (chatTheme.id == QStringLiteral("follow")) {
            chatBg = appTheme.chatBg.isValid() ? appTheme.chatBg : appTheme.panel;
        }
        const bool darkChat = !chatBg.isValid() || luma(chatBg) < 150.0;
        out.timestampColor = darkChat ? QStringLiteral("#8a8a8a") : QStringLiteral("#6f6f6f");
    }

    if (chatTheme.bracket.isValid()) {
        out.bracketColor = chatTheme.bracket.name();
    }
    if (chatTheme.system.isValid()) {
        out.systemColor = chatTheme.system.name();
    }
    if (!eventColorOverride.isEmpty()) {
        out.systemColor = eventColorOverride; // Fonts-page override wins
    }

    out.defaultBackground = resolvedBackground.name();

    // Reverse-video (\x16) and dim-replay derive from the REAL chat colours, not
    // the hardcoded dark-theme pair the formatter struct defaults to.
    QColor fg = chatTheme.fg;
    if (!fg.isValid()) {
        fg = appTheme.chatFg.isValid() ? appTheme.chatFg : appTheme.text;
    }
    out.defaultForeground = fg.isValid() ? fg.name() : QStringLiteral("#cfcfcf");

    out.nickPalette = std::move(nickPalette);
    out.monoNicks = monoNicks;
    return out;
}

} // namespace maxchat::ui
