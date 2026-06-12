#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace maxchat::ui {

class ChannelListDialog final : public QDialog {
  Q_OBJECT

public:
  explicit ChannelListDialog(QWidget *parent = nullptr);

  void clearChannels();
  void addChannel(const QString &channel, int users, const QString &topic);
  void setComplete(bool complete);
  void setFetching(bool fetching);

  [[nodiscard]] int channelCount() const;
  [[nodiscard]] QString selectedChannel() const;

signals:
  void joinRequested(const QString &channel);
  void listRequested();

private:
  void applyFilter();
  void copyRowsToClipboard() const;
  void updateActions();
  void updateStatus();

  QLineEdit *m_filterEdit = nullptr;
  QSpinBox *m_minUsers = nullptr;
  QTableWidget *m_table = nullptr;
  QLabel *m_statusLabel = nullptr;
  QPushButton *m_joinButton = nullptr;
  QPushButton *m_getListButton = nullptr;
  bool m_complete = false;
  bool m_fetching = false;
};

} // namespace maxchat::ui
