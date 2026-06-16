#pragma once

#include <QFont>
#include <QString>
#include <QStringList>

class QWidget;
class QNetworkAccessManager;

namespace maxchat::core {
class SettingsStore;
}
namespace maxchat::irc {
class IrcConnection;
}

namespace maxchat::ui {

// The surface that decomposed MainWindow controllers (ScriptBridge,
// PreviewController, ...) call back into. MainWindow implements it; controllers
// take a MainWindowHost& so they can be unit-tested against a fake host instead
// of a 9k-line widget. Keep this focused — add a method only when a controller
// actually needs it, and prefer narrow intent-revealing operations over leaking
// widgets. As later phases extract the network layer (NetworkController), the
// IRC-send accessors here move behind that controller.
class MainWindowHost {
  public:
    virtual ~MainWindowHost() = default;

    // --- Context ------------------------------------------------------------
    // The network whose buffer is currently shown.
    [[nodiscard]] virtual QString activeNetwork() const = 0;
    // The active target (channel/query) within the active network ("" = server).
    [[nodiscard]] virtual QString currentTarget() const = 0;
    // The user's current nick on a network.
    [[nodiscard]] virtual QString nickFor(const QString& network) = 0;
    // Channels currently joined on a network.
    [[nodiscard]] virtual QStringList channelsFor(const QString& network) = 0;
    // Members of a network+target buffer.
    [[nodiscard]] virtual QStringList nicksFor(const QString& network,
                                               const QString& target) = 0;

    // --- Output -------------------------------------------------------------
    // Append a system/status line to the active buffer.
    virtual void appendActiveSystemLine(const QString& text) = 0;
    // Append a system/status line to a specific network+target buffer.
    virtual void appendSystemLine(const QString& network, const QString& target,
                                  const QString& text) = 0;
    // Echo a locally-originated outbound message ("<nick> text") into a buffer.
    virtual void echoOutbound(const QString& network, const QString& target,
                              const QString& text) = 0;
    // Insert text at the cursor in the message input box.
    virtual void insertInput(const QString& text) = 0;
    // Raise a desktop/tray notification for the active context.
    virtual void notifyUser(const QString& title, const QString& text) = 0;

    // --- IRC send path (Phase 7 NetworkController will own this) ------------
    [[nodiscard]] virtual maxchat::irc::IrcConnection* connectionFor(
        const QString& network) = 0;

    // --- Input / status (for MediaController) -------------------------------
    // Append a (just-uploaded) URL to the message input, space-separated, and
    // focus the box.
    virtual void appendInputUrl(const QString& url) = 0;
    // Transient status-bar message (timeoutMs == 0 → until replaced/cleared).
    virtual void showStatus(const QString& text, int timeoutMs = 0) = 0;
    virtual void clearStatus() = 0;

    // --- Services -----------------------------------------------------------
    // Network manager scripts borrow for api.http_get (SSRF-gated by caller).
    [[nodiscard]] virtual QNetworkAccessManager& scriptNetworkManager() = 0;
    // Network manager the image uploader / preview fetchers use.
    [[nodiscard]] virtual QNetworkAccessManager& previewNetworkManager() = 0;
    // Shared settings store (read/write).
    [[nodiscard]] virtual maxchat::core::SettingsStore& settings() = 0;
    // Parent widget for controller-owned dialogs.
    [[nodiscard]] virtual QWidget* dialogParent() = 0;
    // Rebuild the network/buffer tree (e.g. when the terminal list changes).
    virtual void rebuildTree() = 0;

    // --- Appearance refresh hooks (AppearanceController → window) ------------
    virtual void renderActiveBuffer() = 0;      // re-render chat after a theme change
    virtual void recolorMemberList() = 0;       // re-tint the member list
    virtual void updateChatSeparatorGuide() = 0;
    virtual void updateTrayIcon() = 0;          // tray icon follows the theme accent
    virtual void setMenuBarFont(const QFont& font) = 0; // QMainWindow-only, can't reach via QWidget*
    virtual void applyAllSettings() = 0;        // full re-apply (bundled theme fonts)
};

} // namespace maxchat::ui
