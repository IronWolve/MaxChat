#include "ui/FriendsNotifyDialog.h"

#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QtTest/QtTest>

using maxchat::ui::FriendsNotifyDialog;

class FriendsNotifyDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void addsStripsPrefixesRemovesAndSavesFriends() {
        QStringList saved;
        FriendsNotifyDialog dialog({QStringLiteral("alice")},
                                   [&saved](const QStringList& friends) { saved = friends; });

        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("friends_list"));
        auto* entry = dialog.findChild<QLineEdit*>(QStringLiteral("friends_entry"));
        auto* add = dialog.findChild<QPushButton*>(QStringLiteral("friends_add"));
        auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("friends_remove"));
        auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("friends_buttons"));

        QVERIFY(list != nullptr);
        QVERIFY(entry != nullptr);
        QVERIFY(add != nullptr);
        QVERIFY(remove != nullptr);
        QVERIFY(buttons != nullptr);

        entry->setText(QStringLiteral("@bob"));
        add->click();
        QCOMPARE(dialog.friends(), QStringList({QStringLiteral("alice"), QStringLiteral("bob")}));

        list->setCurrentRow(0);
        remove->click();
        QCOMPARE(dialog.friends(), QStringList({QStringLiteral("bob")}));

        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(saved, QStringList({QStringLiteral("bob")}));
    }
};

QTEST_MAIN(FriendsNotifyDialogTest)

#include "friends_notify_dialog_test.moc"
