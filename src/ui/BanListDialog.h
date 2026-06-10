#pragma once

#include <QDialog>
#include <QString>

#include <functional>

class QLabel;
class QLineEdit;
class QTableWidget;

namespace maxchat::ui {

class BanListDialog final : public QDialog {
  public:
    using BanCallback = std::function<void(const QString& mask)>;

    explicit BanListDialog(const QString& channel, const QString& networkName, BanCallback addBan,
                           BanCallback removeBan, QWidget* parent = nullptr);

    [[nodiscard]] QString channel() const;
    [[nodiscard]] QStringList masks() const;

    void clearBans();
    void addBan(const QString& mask, const QString& setter);
    void setStatusText(const QString& text);

  private:
    void addEnteredMask();
    void removeSelectedMask();

    QString channel_;
    BanCallback addBan_;
    BanCallback removeBan_;
    QTableWidget* table_ = nullptr;
    QLineEdit* entry_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace maxchat::ui
