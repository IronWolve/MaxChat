#pragma once

#include <QDialog>
#include <QString>

#include <functional>

class QCheckBox;
class QLineEdit;
class QSpinBox;

namespace maxchat::ui {

class ChannelModesDialog final : public QDialog {
  public:
    using ApplyCallback = std::function<void(const QString& change)>;

    explicit ChannelModesDialog(const QString& channel, const QString& networkName,
                                const QString& modeLine, ApplyCallback apply,
                                QWidget* parent = nullptr);

  private:
    void applyMode(const QString& change) const;
    void setKeyMode(bool enabled);
    void applyKeyValue();
    void setLimitMode(bool enabled);
    void applyLimitValue();

    QString channel_;
    ApplyCallback apply_;
    QCheckBox* keyEnabled_ = nullptr;
    QLineEdit* keyEdit_ = nullptr;
    QCheckBox* limitEnabled_ = nullptr;
    QSpinBox* limitSpin_ = nullptr;
    QString key_;
    int limit_ = 0;
};

} // namespace maxchat::ui
