#include "ui/IgnoreListDialog.h"

#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QtTest/QtTest>

using maxchat::ui::IgnoreListDialog;

class IgnoreListDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void addsNormalizesRemovesAndSavesMasks() {
        QStringList saved;
        IgnoreListDialog dialog({QStringLiteral("*!*@bad.host")},
                                [&saved](const QStringList& masks) { saved = masks; });

        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("ignore_list"));
        auto* entry = dialog.findChild<QLineEdit*>(QStringLiteral("ignore_entry"));
        auto* add = dialog.findChild<QPushButton*>(QStringLiteral("ignore_add"));
        auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("ignore_remove"));
        auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("ignore_buttons"));

        QVERIFY(list != nullptr);
        QVERIFY(entry != nullptr);
        QVERIFY(add != nullptr);
        QVERIFY(remove != nullptr);
        QVERIFY(buttons != nullptr);

        entry->setText(QStringLiteral("spammer"));
        add->click();
        QCOMPARE(dialog.masks(),
                 QStringList({QStringLiteral("*!*@bad.host"), QStringLiteral("spammer!*@*")}));

        list->setCurrentRow(0);
        remove->click();
        QCOMPARE(dialog.masks(), QStringList({QStringLiteral("spammer!*@*")}));

        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(saved, QStringList({QStringLiteral("spammer!*@*")}));
    }
};

QTEST_MAIN(IgnoreListDialogTest)

#include "ignore_list_dialog_test.moc"
