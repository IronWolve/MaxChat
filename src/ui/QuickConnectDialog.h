#pragma once

#include "core/NetworkImport.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace maxchat::ui {

class QuickConnectDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit QuickConnectDialog(QWidget* parent = nullptr);

    [[nodiscard]] maxchat::core::NetworkConfig network() const;
    void setConnectionValues(const QString& host, int port, bool tls, const QString& nick,
                             const QString& channels);

  private:
    void syncButtons();

    QLineEdit* host_ = nullptr;
    QSpinBox* port_ = nullptr;
    QCheckBox* tls_ = nullptr;
    QLineEdit* nick_ = nullptr;
    QLineEdit* channels_ = nullptr;
    QPushButton* connectButton_ = nullptr;
};

} // namespace maxchat::ui
