#pragma once

#include "ui/ThemeCatalog.h"

#include <QColor>
#include <QString>
#include <QStringList>

namespace maxchat::ui {

// The resolved chat-render colour set — the *pure output* of (chat theme, app
// theme, nick-colour mode, event-colour override). It carries no widget or
// window state: it is the "the chat theme is just data" value object. Build it
// once from the selected themes and the renderer consumes it. Render *toggles*
// (show-timestamps, align-nicks, …) are user preferences, not theme, and stay
// out of here.
//
// This is step R0 of RENDER_PIPELINE_DESIGN.md: the seam that later lets
// AppearanceController hand a ChatRenderTheme to the chat pane instead of the
// render code reaching into live theme members.
struct ChatRenderTheme {
    QString timestampColor;                 // always set
    QString bracketColor;                   // "" → leave the formatter default
    QString systemColor;                    // "" → leave the formatter default
    QString defaultForeground;
    QString defaultBackground;
    QStringList nickPalette;
    bool monoNicks = false;
};

// Resolve the chat-render colours. Pure: same inputs → same output, no globals.
//
// `resolvedBackground` is the chat pane's actual background (the caller's
// resolvedChatBackground()), used for `defaultBackground`. `nickPalette` /
// `monoNicks` come pre-resolved from the caller's effectiveNickPalette() (it is
// also consumed by the member list, so it stays a shared helper). `chatTheme` /
// `appTheme` supply the remaining colours and the "follow" / fallback chains.
// `eventColorOverride` is the Fonts-page system-colour override ("" = none).
[[nodiscard]] ChatRenderTheme resolveChatRenderTheme(const ChatThemeDefinition& chatTheme,
                                                     const AppThemeDefinition& appTheme,
                                                     const QColor& resolvedBackground,
                                                     QStringList nickPalette, bool monoNicks,
                                                     const QString& eventColorOverride);

} // namespace maxchat::ui
