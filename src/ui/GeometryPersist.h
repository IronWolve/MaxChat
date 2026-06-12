#pragma once

#include <QString>

class QDialog;

namespace maxchat::core {
class SettingsStore;
}

namespace maxchat::ui {

// Restore saved dialog geometry from settings[key], and re-save on every close.
// Call once right after constructing the dialog, before show/exec.
// Safe for both stack-allocated modal dialogs and heap-allocated persistent ones.
void attachGeometryPersist(QDialog* dlg, maxchat::core::SettingsStore& store,
                           const QString& key);

} // namespace maxchat::ui
