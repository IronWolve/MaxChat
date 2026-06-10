#include "ui/ChannelListDialog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace maxchat::ui {

namespace {

constexpr int ChannelColumn = 0;
constexpr int UsersColumn = 1;
constexpr int TopicColumn = 2;

class NumericTableWidgetItem final : public QTableWidgetItem {
public:
  explicit NumericTableWidgetItem(int value)
      : QTableWidgetItem(QString::number(value)) {
    setData(Qt::UserRole, value);
  }

  bool operator<(const QTableWidgetItem &other) const override {
    return data(Qt::UserRole).toInt() < other.data(Qt::UserRole).toInt();
  }
};

QString rowText(const QTableWidget *table, int row) {
  QStringList fields;
  fields.reserve(table->columnCount());
  for (int column = 0; column < table->columnCount(); ++column) {
    const QTableWidgetItem *item = table->item(row, column);
    fields.append(item == nullptr ? QString() : item->text());
  }
  return fields.join(QLatin1Char('\t'));
}

} // namespace

ChannelListDialog::ChannelListDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(QStringLiteral("Channel List"));
  resize(820, 520);

  auto *layout = new QVBoxLayout(this);

  m_filterEdit = new QLineEdit(this);
  m_filterEdit->setObjectName(QStringLiteral("channel_filter"));
  m_filterEdit->setPlaceholderText(QStringLiteral("Filter channels or topics"));
  layout->addWidget(m_filterEdit);

  m_table = new QTableWidget(this);
  m_table->setObjectName(QStringLiteral("channel_table"));
  m_table->setColumnCount(3);
  m_table->setHorizontalHeaderLabels(
      {QStringLiteral("Channel"), QStringLiteral("Users"),
       QStringLiteral("Topic")});
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setAlternatingRowColors(true);
  m_table->setSortingEnabled(true);
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->horizontalHeader()->setSectionResizeMode(ChannelColumn,
                                                    QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(UsersColumn,
                                                    QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSortIndicator(UsersColumn,
                                                Qt::DescendingOrder);
  m_table->verticalHeader()->setVisible(false);
  layout->addWidget(m_table, 1);

  m_statusLabel = new QLabel(QStringLiteral("0 channels"), this);
  layout->addWidget(m_statusLabel);

  auto *buttons = new QDialogButtonBox(this);
  m_joinButton = buttons->addButton(QStringLiteral("Join"),
                                    QDialogButtonBox::AcceptRole);
  m_joinButton->setObjectName(QStringLiteral("join_button"));
  QPushButton *copyButton =
      buttons->addButton(QStringLiteral("Copy"), QDialogButtonBox::ActionRole);
  copyButton->setObjectName(QStringLiteral("copy_button"));
  QPushButton *closeButton =
      buttons->addButton(QStringLiteral("Close"), QDialogButtonBox::RejectRole);
  closeButton->setObjectName(QStringLiteral("close_button"));
  layout->addWidget(buttons);

  connect(m_filterEdit, &QLineEdit::textChanged, this,
          &ChannelListDialog::applyFilter);
  connect(m_table, &QTableWidget::itemSelectionChanged, this,
          &ChannelListDialog::updateActions);
  connect(m_table, &QTableWidget::itemDoubleClicked, this,
          [this](QTableWidgetItem *) {
            const QString channel = selectedChannel();
            if (!channel.isEmpty()) {
              emit joinRequested(channel);
            }
          });
  connect(m_joinButton, &QPushButton::clicked, this, [this]() {
    const QString channel = selectedChannel();
    if (!channel.isEmpty()) {
      emit joinRequested(channel);
    }
  });
  connect(copyButton, &QPushButton::clicked, this,
          &ChannelListDialog::copyRowsToClipboard);
  connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

  updateActions();
}

void ChannelListDialog::clearChannels() {
  m_complete = false;
  m_table->setSortingEnabled(false);
  m_table->setRowCount(0);
  m_table->setSortingEnabled(true);
  m_table->sortItems(UsersColumn, Qt::DescendingOrder);
  updateActions();
  updateStatus();
}

void ChannelListDialog::addChannel(const QString &channel, int users,
                                   const QString &topic) {
  const QString trimmedChannel = channel.trimmed();
  if (trimmedChannel.isEmpty()) {
    return;
  }

  m_table->setSortingEnabled(false);

  int row = -1;
  const QList<QTableWidgetItem *> existing =
      m_table->findItems(trimmedChannel, Qt::MatchFixedString);
  for (QTableWidgetItem *item : existing) {
    if (item != nullptr && item->column() == ChannelColumn) {
      row = item->row();
      break;
    }
  }

  if (row < 0) {
    row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, ChannelColumn,
                     new QTableWidgetItem(trimmedChannel));
  }
  m_table->setItem(row, UsersColumn, new NumericTableWidgetItem(users));
  m_table->setItem(row, TopicColumn, new QTableWidgetItem(topic.trimmed()));

  m_table->setSortingEnabled(true);
  const int sortColumn = m_table->horizontalHeader()->sortIndicatorSection();
  const Qt::SortOrder sortOrder =
      m_table->horizontalHeader()->sortIndicatorOrder();
  m_table->sortItems(sortColumn < 0 ? UsersColumn : sortColumn, sortOrder);

  applyFilter();
  updateActions();
  updateStatus();
}

void ChannelListDialog::setComplete(bool complete) {
  m_complete = complete;
  updateStatus();
}

int ChannelListDialog::channelCount() const { return m_table->rowCount(); }

QString ChannelListDialog::selectedChannel() const {
  const QList<QTableWidgetItem *> selected = m_table->selectedItems();
  for (const QTableWidgetItem *selectedItem : selected) {
    if (selectedItem == nullptr) {
      continue;
    }
    const int row = selectedItem->row();
    if (m_table->isRowHidden(row)) {
      continue;
    }
    const QTableWidgetItem *item = m_table->item(row, ChannelColumn);
    return item == nullptr ? QString() : item->text();
  }
  const int row = m_table->currentRow();
  if (row >= 0 && !m_table->isRowHidden(row)) {
    const QTableWidgetItem *item = m_table->item(row, ChannelColumn);
    return item == nullptr ? QString() : item->text();
  }
  return {};
}

void ChannelListDialog::applyFilter() {
  const QString filter = m_filterEdit->text().trimmed();
  int visibleCount = 0;
  for (int row = 0; row < m_table->rowCount(); ++row) {
    const QTableWidgetItem *channel = m_table->item(row, ChannelColumn);
    const QTableWidgetItem *topic = m_table->item(row, TopicColumn);
    const bool matches =
        filter.isEmpty() ||
        (channel != nullptr &&
         channel->text().contains(filter, Qt::CaseInsensitive)) ||
        (topic != nullptr && topic->text().contains(filter, Qt::CaseInsensitive));
    m_table->setRowHidden(row, !matches);
    if (matches) {
      ++visibleCount;
    }
  }
  Q_UNUSED(visibleCount);
  updateActions();
  updateStatus();
}

void ChannelListDialog::copyRowsToClipboard() const {
  QStringList rows;
  const QList<QTableWidgetSelectionRange> ranges = m_table->selectedRanges();
  if (ranges.isEmpty()) {
    for (int row = 0; row < m_table->rowCount(); ++row) {
      if (!m_table->isRowHidden(row)) {
        rows.append(rowText(m_table, row));
      }
    }
  } else {
    QSet<int> copiedRows;
    for (const QTableWidgetSelectionRange &range : ranges) {
      for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
        if (!copiedRows.contains(row) && !m_table->isRowHidden(row)) {
          copiedRows.insert(row);
          rows.append(rowText(m_table, row));
        }
      }
    }
  }

  QApplication::clipboard()->setText(rows.join(QLatin1Char('\n')));
}

void ChannelListDialog::updateActions() {
  const QString channel = selectedChannel();
  m_joinButton->setEnabled(!channel.isEmpty());
}

void ChannelListDialog::updateStatus() {
  int visibleCount = 0;
  for (int row = 0; row < m_table->rowCount(); ++row) {
    if (!m_table->isRowHidden(row)) {
      ++visibleCount;
    }
  }

  const QString state = m_complete ? QStringLiteral("loaded")
                                   : QStringLiteral("loading");
  if (visibleCount == m_table->rowCount()) {
    m_statusLabel->setText(
        QStringLiteral("%1 channels %2").arg(m_table->rowCount()).arg(state));
  } else {
    m_statusLabel->setText(
        QStringLiteral("%1 of %2 channels shown, %3")
            .arg(visibleCount)
            .arg(m_table->rowCount())
            .arg(state));
  }
}

} // namespace maxchat::ui
