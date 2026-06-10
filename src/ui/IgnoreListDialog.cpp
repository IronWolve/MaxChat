#include "ui/IgnoreListDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace maxchat::ui {

namespace {

QString normalizeMask(QString mask) {
    mask = mask.trimmed();
    if (mask.isEmpty()) {
        return {};
    }
    return mask.contains(QLatin1Char('!')) || mask.contains(QLatin1Char('@'))
               ? mask
               : QStringLiteral("%1!*@*").arg(mask);
}

bool hasMask(const QListWidget* list, const QString& mask) {
    if (list == nullptr) {
        return false;
    }
    for (int row = 0; row < list->count(); ++row) {
        const QListWidgetItem* item = list->item(row);
        if (item != nullptr && item->text().compare(mask, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

IgnoreListDialog::IgnoreListDialog(const QStringList& ignores, SaveCallback save, QWidget* parent)
    : QDialog(parent), save_(std::move(save)) {
    setWindowTitle(QStringLiteral("Ignore List"));

    auto* layout = new QVBoxLayout(this);
    auto* intro =
        new QLabel(QStringLiteral("Hidden senders - masks over nick!user@host, for example "
                                  "spammer!*@* or *!*@bad.host."),
                   this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("ignore_list"));
    list_->addItems(ignores);
    layout->addWidget(list_, 1);

    auto* row = new QHBoxLayout();
    entry_ = new QLineEdit(this);
    entry_->setObjectName(QStringLiteral("ignore_entry"));
    entry_->setPlaceholderText(QStringLiteral("nick or nick!user@host"));
    auto* addButton = new QPushButton(QStringLiteral("Add"), this);
    addButton->setObjectName(QStringLiteral("ignore_add"));
    auto* removeButton = new QPushButton(QStringLiteral("Remove"), this);
    removeButton->setObjectName(QStringLiteral("ignore_remove"));
    connect(entry_, &QLineEdit::returnPressed, this, &IgnoreListDialog::addMask);
    connect(addButton, &QPushButton::clicked, this, &IgnoreListDialog::addMask);
    connect(removeButton, &QPushButton::clicked, this, &IgnoreListDialog::removeSelected);
    row->addWidget(entry_, 1);
    row->addWidget(addButton);
    row->addWidget(removeButton);
    layout->addLayout(row);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("ignore_buttons"));
    connect(buttons, &QDialogButtonBox::accepted, this, &IgnoreListDialog::saveAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QStringList IgnoreListDialog::masks() const {
    QStringList values;
    if (list_ == nullptr) {
        return values;
    }
    for (int row = 0; row < list_->count(); ++row) {
        const QListWidgetItem* item = list_->item(row);
        if (item != nullptr && !item->text().trimmed().isEmpty()) {
            values.append(item->text().trimmed());
        }
    }
    return values;
}

void IgnoreListDialog::addMask() {
    const QString mask = normalizeMask(entry_ == nullptr ? QString() : entry_->text());
    if (!mask.isEmpty() && !hasMask(list_, mask)) {
        list_->addItem(mask);
    }
    if (entry_ != nullptr) {
        entry_->clear();
    }
}

void IgnoreListDialog::removeSelected() {
    if (list_ == nullptr) {
        return;
    }
    const QList<QListWidgetItem*> selected =
        list_->selectedItems().isEmpty() && list_->currentItem() != nullptr
            ? QList<QListWidgetItem*>{list_->currentItem()}
            : list_->selectedItems();
    for (QListWidgetItem* item : selected) {
        delete list_->takeItem(list_->row(item));
    }
}

void IgnoreListDialog::saveAndAccept() {
    if (save_) {
        save_(masks());
    }
    accept();
}

} // namespace maxchat::ui
