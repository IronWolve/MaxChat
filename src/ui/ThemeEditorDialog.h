#pragma once

#include <QColor>
#include <QDialog>
#include <QHash>
#include <QString>

class QCheckBox;
class QLineEdit;

namespace maxchat::ui {

// Customizer for an app or chat theme: starts from an existing theme, lets the
// user repaint its key colors, and saves the result as a reusable user theme.
class ThemeEditorDialog final : public QDialog {
    Q_OBJECT

  public:
    enum class Scope { App, Chat };

    ThemeEditorDialog(Scope scope, const QString& baseId, QWidget* parent = nullptr);

    // The id of the saved user theme ("u-<slug>"), empty if cancelled/failed.
    [[nodiscard]] QString resultId() const { return resultId_; }

  private:
    void save();

    Scope scope_;
    QString baseId_;
    QString resultId_;
    QLineEdit* name_ = nullptr;
    QHash<QString, QColor> colors_;
    QCheckBox* fixedFont_ = nullptr;
    QCheckBox* monoNicks_ = nullptr;
};

} // namespace maxchat::ui
