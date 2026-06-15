#pragma once

#include <QString>

class QWidget;

namespace maxchat::core {
class SettingsStore;
}

namespace maxchat::ui {

// The narrow surface that decomposed MainWindow controllers (ScriptBridge,
// PreviewController, ...) call back into. MainWindow implements it; controllers
// take a MainWindowHost& so they can be unit-tested against a fake host instead
// of a 9k-line widget. Keep this minimal — add a method only when a controller
// actually needs it, and prefer signals for event flow.
class MainWindowHost {
  public:
    virtual ~MainWindowHost() = default;

    // The network whose buffer is currently shown.
    [[nodiscard]] virtual QString activeNetwork() const = 0;
    // The active target (channel/query) within the active network ("" = server).
    [[nodiscard]] virtual QString currentTarget() const = 0;

    // Append a system/status line to a specific network+target buffer.
    virtual void appendSystemLine(const QString& network, const QString& target,
                                  const QString& text) = 0;
    // Insert text at the cursor in the message input box.
    virtual void insertInput(const QString& text) = 0;

    // Shared settings store (read/write).
    [[nodiscard]] virtual maxchat::core::SettingsStore& settings() = 0;
    // Parent widget for controller-owned dialogs.
    [[nodiscard]] virtual QWidget* dialogParent() = 0;
};

} // namespace maxchat::ui
