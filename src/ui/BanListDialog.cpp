#include "ui/BanListDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace maxchat::ui {

namespace {

bool hasMask(const QTableWidget* table, const QString& mask) {
    if (table == nullptr) {
        return false;
    }
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* item = table->item(row, 0);
        if (item != nullptr && item->text().compare(mask, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

BanListDialog::BanListDialog(const QString& channel, const QString& networkName, BanCallback addBan,
                             BanCallback removeBan, QWidget* parent)
    : QDialog(parent), channel_(channel), addBan_(std::move(addBan)),
      removeBan_(std::move(removeBan)) {
    setWindowTitle(QStringLiteral("Ban List - %1").arg(channel_));

    auto* layout = new QVBoxLayout(this);
    auto* heading = new QLabel(QStringLiteral("<b>%1</b> - %2").arg(channel_, networkName), this);
    layout->addWidget(heading);

    table_ = new QTableWidget(0, 2, this);
    table_->setObjectName(QStringLiteral("ban_table"));
    table_->setHorizontalHeaderLabels({QStringLiteral("Mask"), QStringLiteral("Set By")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(table_, 1);

    auto* row = new QHBoxLayout();
    entry_ = new QLineEdit(this);
    entry_->setObjectName(QStringLiteral("ban_entry"));
    entry_->setPlaceholderText(QStringLiteral("nick!user@host or *!*@host"));
    auto* addButton = new QPushButton(QStringLiteral("Add"), this);
    addButton->setObjectName(QStringLiteral("ban_add"));
    auto* removeButton = new QPushButton(QStringLiteral("Remove"), this);
    removeButton->setObjectName(QStringLiteral("ban_remove"));
    connect(entry_, &QLineEdit::returnPressed, this, &BanListDialog::addEnteredMask);
    connect(addButton, &QPushButton::clicked, this, &BanListDialog::addEnteredMask);
    connect(removeButton, &QPushButton::clicked, this, &BanListDialog::removeSelectedMask);
    row->addWidget(entry_, 1);
    row->addWidget(addButton);
    row->addWidget(removeButton);
    layout->addLayout(row);

    status_ = new QLabel(QStringLiteral("Requesting ban list..."), this);
    status_->setObjectName(QStringLiteral("ban_status"));
    layout->addWidget(status_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
}

QString BanListDialog::channel() const {
    return channel_;
}

QStringList BanListDialog::masks() const {
    QStringList values;
    if (table_ == nullptr) {
        return values;
    }
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QTableWidgetItem* item = table_->item(row, 0);
        if (item != nullptr && !item->text().trimmed().isEmpty()) {
            values.append(item->text().trimmed());
        }
    }
    return values;
}

void BanListDialog::clearBans() {
    if (table_ != nullptr) {
        table_->setRowCount(0);
    }
}

void BanListDialog::addBan(const QString& mask, const QString& setter) {
    const QString cleanMask = mask.trimmed();
    if (table_ == nullptr || cleanMask.isEmpty() || hasMask(table_, cleanMask)) {
        return;
    }

    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, 0, new QTableWidgetItem(cleanMask));
    table_->setItem(row, 1, new QTableWidgetItem(setter.trimmed()));
}

void BanListDialog::setStatusText(const QString& text) {
    if (status_ != nullptr) {
        status_->setText(text);
    }
}

void BanListDialog::addEnteredMask() {
    const QString mask = entry_ == nullptr ? QString() : entry_->text().trimmed();
    if (mask.isEmpty()) {
        return;
    }
    if (addBan_) {
        addBan_(mask);
    }
    if (entry_ != nullptr) {
        entry_->clear();
    }
}

void BanListDialog::removeSelectedMask() {
    if (table_ == nullptr) {
        return;
    }
    const int row = table_->currentRow();
    if (row < 0) {
        return;
    }
    QTableWidgetItem* item = table_->item(row, 0);
    const QString mask = item == nullptr ? QString() : item->text().trimmed();
    if (!mask.isEmpty() && removeBan_) {
        removeBan_(mask);
    }
    table_->removeRow(row);
}

} // namespace maxchat::ui
