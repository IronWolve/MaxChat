#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QVariantMap>

class QKeySequenceEdit;

namespace maxchat::ui {

struct NavShortcutSpec {
    QString id;
    QString label;
    QString defaultKey;
};

[[nodiscard]] QList<NavShortcutSpec> navShortcutSpecs();

class ShortcutEditorDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit ShortcutEditorDialog(QVariantMap overrides, QWidget* parent = nullptr);

    // Bindings that differ from the defaults, keyed by shortcut id.
    [[nodiscard]] QVariantMap overrides() const;

  private:
    QList<QKeySequenceEdit*> edits_;
};

} // namespace maxchat::ui
