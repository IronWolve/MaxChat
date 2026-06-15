#pragma once

#include <QFont>
#include <QObject>
#include <QString>
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

  private:
    MainWindowHost& host_;
};

} // namespace maxchat::ui
