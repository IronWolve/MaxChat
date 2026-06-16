#pragma once

#include "ui/ChatRenderTheme.h"
#include "ui/ThemeCatalog.h"

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace maxchat::ui {

class MainWindowHost;

// The resolved per-area fonts the window applies to its widgets. Plain value
// type — the *resolution* (settings → QFont) is pure and lives here; the window
// still owns *applying* them, since it owns the widgets (it is the app shell).
struct ResolvedFonts {
    QFont app;   // app-wide / dialogs / labels
    QFont chat;  // chat view + input box
    QFont list;  // network tree + member list + mode button
};

// Appearance / theming coordinator being lifted out of MainWindow (decomp
// phase 3, RENDER_PIPELINE_DESIGN.md follow-on). Owns the visual-styling
// concerns that are *not* chat logic: bundled-font registration, per-area font
// resolution, and (incrementally) theme / chat-theme / wallpaper / opacity
// selection + apply. Because applying styling targets qApp + the window's child
// widgets, this is a coordinator that calls back through MainWindowHost — not a
// standalone like ScriptBridge.
//
// This first slice owns font loading + resolution (the cleanly-separable part).
class AppearanceController final : public QObject {
    Q_OBJECT

  public:
    explicit AppearanceController(MainWindowHost& host, QObject* parent = nullptr);

    // Register the bundled .ttf families (JetBrains Mono, Comic Relief, Nerd
    // symbols) into the Qt font database. Call once at startup.
    void registerBundledFonts();

    // Resolve the app/chat/list fonts from the settings map (family/size/bold
    // per area, JetBrains Mono 14 bold as the fallback). Pure.
    [[nodiscard]] ResolvedFonts resolveFonts(const QVariantMap& settings) const;

    // --- Theme selection state (the "what colours" the renderer asks for) -----
    // Set the current theme ids from a settings map (normalized). Used by
    // applyCurrentSettings; does NOT apply — the window calls applyTheme after.
    void loadFromSettings(const QVariantMap& settings);

    void setThemeId(const QString& theme);          // normalizes
    void setChatThemeId(const QString& chatTheme);  // normalizes
    void setWallpaperValue(const QString& wallpaper); // normalizes
    void setChatOpacity(int opacity);               // clamped 20..100
    void setNickColorMode(const QString& mode);     // off / palette / irc
    void setEventColor(const QString& color);       // "" = theme/default

    [[nodiscard]] QString themeId() const { return currentTheme_; }
    [[nodiscard]] QString chatThemeId() const { return currentChatTheme_; }
    [[nodiscard]] QString wallpaperValue() const { return currentWallpaper_; }
    [[nodiscard]] int chatOpacity() const { return chatOpacity_; }
    [[nodiscard]] QString nickColorMode() const { return nickColorMode_; }
    [[nodiscard]] QString eventColor() const { return eventColor_; }
    [[nodiscard]] AppThemeDefinition appTheme() const { return appThemeById(currentTheme_); }
    [[nodiscard]] ChatThemeDefinition chatThemeDef() const {
        return chatThemeById(currentChatTheme_);
    }

    // --- Theme-derived render inputs (pure-ish; read the state above) ---------
    // THE chat background (chat theme bg, "follow" → app theme, fallback). Shared
    // by the chat renderer + member list + the QSS so they agree.
    [[nodiscard]] QColor resolvedChatBackground() const;
    // THE nick-colour palette (chat view + member list both call this). monoOut
    // receives whether the theme wants monochrome nicks.
    [[nodiscard]] QStringList effectiveNickPalette(bool* monoOut) const;
    // The full resolved chat-render colour set for ChatLineFormatOptions.
    [[nodiscard]] ChatRenderTheme buildChatRenderTheme() const;

  private:
    MainWindowHost& host_;

    QString currentTheme_ = QStringLiteral("synthwave");
    QString currentChatTheme_ = QStringLiteral("follow");
    QString currentWallpaper_;
    int chatOpacity_ = 100; // chat bg opacity %, 100 = auto (theme decides)
    QString nickColorMode_ = QStringLiteral("palette"); // off / palette / irc
    QString eventColor_;                                // "" = chat theme / default
};

} // namespace maxchat::ui
