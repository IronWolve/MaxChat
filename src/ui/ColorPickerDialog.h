#pragma once

#include <QDialog>
#include <QString>

namespace maxchat::ui {

// IRC colour picker (Ctrl+K): the classic 16 colours as swatches. Returns
// the two-digit colour code to insert after \x03 ("" if cancelled).
class ColorPickerDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit ColorPickerDialog(QWidget* parent = nullptr);

    [[nodiscard]] QString selectedCode() const;

  private:
    QString selectedCode_;
};

} // namespace maxchat::ui
