#include "ui/BanListDialog.h"

#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QtTest/QtTest>

using maxchat::ui::BanListDialog;

class BanListDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void addsDisplaysAndRequestsRemove() {
        QStringList added;
        QStringList removed;
        BanListDialog dialog(
            QStringLiteral("#chat"), QStringLiteral("TestNet"),
            [&added](const QString& mask) { added.append(mask); },
            [&removed](const QString& mask) { removed.append(mask); });

        dialog.addBan(QStringLiteral("*!*@bad.host"), QStringLiteral("oper"));
        dialog.addBan(QStringLiteral("*!*@bad.host"), QStringLiteral("oper"));
        QCOMPARE(dialog.masks(), QStringList({QStringLiteral("*!*@bad.host")}));

        auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("ban_table"));
        auto* entry = dialog.findChild<QLineEdit*>(QStringLiteral("ban_entry"));
        auto* add = dialog.findChild<QPushButton*>(QStringLiteral("ban_add"));
        auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("ban_remove"));

        QVERIFY(table != nullptr);
        QVERIFY(entry != nullptr);
        QVERIFY(add != nullptr);
        QVERIFY(remove != nullptr);

        entry->setText(QStringLiteral("bad!*@*"));
        add->click();
        QCOMPARE(added, QStringList({QStringLiteral("bad!*@*")}));

        table->setCurrentCell(0, 0);
        remove->click();
        QCOMPARE(removed, QStringList({QStringLiteral("*!*@bad.host")}));
        QCOMPARE(dialog.masks(), QStringList());
    }
};

QTEST_MAIN(BanListDialogTest)

#include "ban_list_dialog_test.moc"
