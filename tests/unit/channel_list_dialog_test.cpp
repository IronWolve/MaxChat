#include "ui/ChannelListDialog.h"

#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QtTest/QtTest>

using maxchat::ui::ChannelListDialog;

class ChannelListDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void sortsByUserCountFiltersAndEmitsJoin() {
    ChannelListDialog dialog;
    dialog.addChannel(QStringLiteral("#small"), 2, QStringLiteral("quiet"));
    dialog.addChannel(QStringLiteral("#large"), 100,
                      QStringLiteral("Linux help and chat"));

    QCOMPARE(dialog.channelCount(), 2);

    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("channel_table"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("#large"));
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("100"));

    auto *filter = dialog.findChild<QLineEdit *>(QStringLiteral("channel_filter"));
    QVERIFY(filter != nullptr);
    filter->setText(QStringLiteral("linux"));
    QVERIFY(!table->isRowHidden(0));
    QVERIFY(table->isRowHidden(1));

    table->selectRow(0);
    QCOMPARE(dialog.selectedChannel(), QStringLiteral("#large"));

    QSignalSpy joined(&dialog, &ChannelListDialog::joinRequested);
    auto *joinButton = dialog.findChild<QPushButton *>(QStringLiteral("join_button"));
    QVERIFY(joinButton != nullptr);
    joinButton->click();
    QCOMPARE(joined.count(), 1);
    QCOMPARE(joined.takeFirst().at(0).toString(), QStringLiteral("#large"));

    filter->setText(QStringLiteral("quiet"));
    QVERIFY(table->isRowHidden(0));
    QVERIFY(!table->isRowHidden(1));
    QCOMPARE(dialog.selectedChannel(), QString());
    QVERIFY(!joinButton->isEnabled());
  }
};

QTEST_MAIN(ChannelListDialogTest)

#include "channel_list_dialog_test.moc"
