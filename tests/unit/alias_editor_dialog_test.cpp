#include "ui/AliasEditorDialog.h"

#include "core/CommandAlias.h"

#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QtTest/QtTest>

using maxchat::core::defaultCommandAliases;
using maxchat::ui::AliasEditorDialog;

namespace {

int rowForAlias(const QTableWidget *table, const QString &alias) {
  if (table == nullptr) {
    return -1;
  }
  for (int row = 0; row < table->rowCount(); ++row) {
    const QTableWidgetItem *item = table->item(row, 0);
    if (item != nullptr && item->text() == alias) {
      return row;
    }
  }
  return -1;
}

} // namespace

class AliasEditorDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void addsUpdatesRemovesAndSavesAliases() {
    QVariantMap saved;
    QVariantMap initial;
    initial.insert(QStringLiteral("j"), QStringLiteral("/join $*"));
    initial.insert(QStringLiteral("w"), QStringLiteral("/whois $*"));
    AliasEditorDialog dialog(
        initial, [&saved](const QVariantMap &aliases) { saved = aliases; });

    auto *table =
        dialog.findChild<QTableWidget *>(QStringLiteral("alias_table"));
    auto *name = dialog.findChild<QLineEdit *>(QStringLiteral("alias_name"));
    auto *aliasTemplate =
        dialog.findChild<QLineEdit *>(QStringLiteral("alias_template"));
    auto *add =
        dialog.findChild<QPushButton *>(QStringLiteral("alias_add_update"));
    auto *remove =
        dialog.findChild<QPushButton *>(QStringLiteral("alias_remove"));
    auto *buttons =
        dialog.findChild<QDialogButtonBox *>(QStringLiteral("alias_buttons"));

    QVERIFY(table != nullptr);
    QVERIFY(name != nullptr);
    QVERIFY(aliasTemplate != nullptr);
    QVERIFY(add != nullptr);
    QVERIFY(remove != nullptr);
    QVERIFY(buttons != nullptr);

    name->setText(QStringLiteral("/kb"));
    aliasTemplate->setText(QStringLiteral("mode $1 +b $2-"));
    add->click();
    QCOMPARE(dialog.aliases().value(QStringLiteral("kb")).toString(),
             QStringLiteral("/mode $1 +b $2-"));

    aliasTemplate->setText(QStringLiteral("/kick $1 $2-"));
    add->click();
    QCOMPARE(dialog.aliases().value(QStringLiteral("kb")).toString(),
             QStringLiteral("/kick $1 $2-"));

    table->selectRow(rowForAlias(table, QStringLiteral("j")));
    remove->click();
    QVERIFY(!dialog.aliases().contains(QStringLiteral("j")));

    buttons->button(QDialogButtonBox::Ok)->click();
    QCOMPARE(saved.value(QStringLiteral("kb")).toString(),
             QStringLiteral("/kick $1 $2-"));
    QVERIFY(!saved.contains(QStringLiteral("j")));
  }

  void restoresDefaultAliases() {
    QVariantMap initial;
    initial.insert(QStringLiteral("kb"), QStringLiteral("/kick $1 $2-"));
    AliasEditorDialog dialog(initial);

    auto *restore = dialog.findChild<QPushButton *>(
        QStringLiteral("alias_restore_defaults"));
    QVERIFY(restore != nullptr);

    restore->click();
    QCOMPARE(dialog.aliases(), defaultCommandAliases());
  }
};

QTEST_MAIN(AliasEditorDialogTest)

#include "alias_editor_dialog_test.moc"
