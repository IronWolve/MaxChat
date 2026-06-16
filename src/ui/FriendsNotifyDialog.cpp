#include "ui/FriendsNotifyDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace maxchat::ui {

namespace {

bool hasNick(const QListWidget* list, const QString& nick) {
    if (list == nullptr) {
        return false;
    }
    for (int row = 0; row < list->count(); ++row) {
        const QListWidgetItem* item = list->item(row);
        if (item != nullptr && item->text().compare(nick, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString cleanNick(QString nick) {
    static const QString prefixes = QStringLiteral("~&@%+");
    nick = nick.trimmed();
    while (!nick.isEmpty() && prefixes.contains(nick.front())) {
        nick.remove(0, 1);
    }
    return nick;
}

} // namespace

FriendsNotifyDialog::FriendsNotifyDialog(const QStringList& friends, SaveCallback save,
                                         QWidget* parent)
    : QDialog(parent), save_(std::move(save)) {
    setWindowTitle(tr("Friends / Notify"));

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        QStringLiteral("Nicks MaxChat watches with ISON while connected. Online/offline changes "
                       "are shown as status lines."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("friends_list"));
    list_->addItems(friends);
    layout->addWidget(list_, 1);

    auto* row = new QHBoxLayout();
    entry_ = new QLineEdit(this);
    entry_->setObjectName(QStringLiteral("friends_entry"));
    entry_->setPlaceholderText(tr("nickname"));
    auto* addButton = new QPushButton(tr("Add"), this);
    addButton->setObjectName(QStringLiteral("friends_add"));
    auto* removeButton = new QPushButton(tr("Remove"), this);
    removeButton->setObjectName(QStringLiteral("friends_remove"));
    connect(entry_, &QLineEdit::returnPressed, this, &FriendsNotifyDialog::addFriend);
    connect(addButton, &QPushButton::clicked, this, &FriendsNotifyDialog::addFriend);
    connect(removeButton, &QPushButton::clicked, this, &FriendsNotifyDialog::removeSelected);
    row->addWidget(entry_, 1);
    row->addWidget(addButton);
    row->addWidget(removeButton);
    layout->addLayout(row);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("friends_buttons"));
    connect(buttons, &QDialogButtonBox::accepted, this, &FriendsNotifyDialog::saveAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QStringList FriendsNotifyDialog::friends() const {
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

void FriendsNotifyDialog::addFriend() {
    const QString nick = cleanNick(entry_ == nullptr ? QString() : entry_->text());
    if (!nick.isEmpty() && !hasNick(list_, nick)) {
        list_->addItem(nick);
    }
    if (entry_ != nullptr) {
        entry_->clear();
    }
}

void FriendsNotifyDialog::removeSelected() {
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

void FriendsNotifyDialog::saveAndAccept() {
    if (save_) {
        save_(friends());
    }
    accept();
}

} // namespace maxchat::ui
