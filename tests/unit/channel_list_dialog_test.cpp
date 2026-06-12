#include "ui/ChannelListDialog.h"

#include <QClipboard>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QtTest/QtTest>

using maxchat::ui::ChannelListDialog;

class ChannelListDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void sortsByUserCountFiltersAndEmitsJoin() {
    ChannelListDialog dialog;
    dialog.addChannel(QStringLiteral("#small"), 5, QStringLiteral("quiet"));
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

  void minUsersFilterHidesLowTrafficChannels() {
    ChannelListDialog dialog;
    dialog.addChannel(QStringLiteral("#dead"), 1, QStringLiteral(""));
    dialog.addChannel(QStringLiteral("#small"), 5, QStringLiteral(""));
    dialog.addChannel(QStringLiteral("#busy"), 200, QStringLiteral(""));

    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("channel_table"));
    QVERIFY(table != nullptr);
    auto *minUsers = dialog.findChild<QSpinBox *>(QStringLiteral("min_users"));
    QVERIFY(minUsers != nullptr);
    minUsers->setValue(0);  // reset to show all — tests filter behaviour, not the default

    // min=0: all visible
    QCOMPARE(dialog.channelCount(), 3);
    int visible = 0;
    for (int i = 0; i < table->rowCount(); ++i) {
      if (!table->isRowHidden(i)) { ++visible; }
    }
    QCOMPARE(visible, 3);

    // Min 5: hides #dead
    minUsers->setValue(5);
    visible = 0;
    for (int i = 0; i < table->rowCount(); ++i) {
      if (!table->isRowHidden(i)) { ++visible; }
    }
    QCOMPARE(visible, 2);

    // Min 100: only #busy
    minUsers->setValue(100);
    visible = 0;
    for (int i = 0; i < table->rowCount(); ++i) {
      if (!table->isRowHidden(i)) { ++visible; }
    }
    QCOMPARE(visible, 1);
  }

  void getListButtonEmitsListRequested() {
    ChannelListDialog dialog;
    QSignalSpy spy(&dialog, &ChannelListDialog::listRequested);
    auto *btn = dialog.findChild<QPushButton *>(QStringLiteral("get_list_button"));
    QVERIFY(btn != nullptr);
    btn->click();
    QCOMPARE(spy.count(), 1);
  }

  void copyButtonCopiesVisibleRows() {
    ChannelListDialog dialog;
    dialog.addChannel(QStringLiteral("#one"), 10, QStringLiteral("topic one"));
    dialog.addChannel(QStringLiteral("#two"), 20, QStringLiteral("topic two"));

    auto *filter = dialog.findChild<QLineEdit *>(QStringLiteral("channel_filter"));
    QVERIFY(filter != nullptr);
    filter->setText(QStringLiteral("#one"));

    auto *copy = dialog.findChild<QPushButton *>(QStringLiteral("copy_button"));
    QVERIFY(copy != nullptr);
    copy->click();

    const QString clipboard = QApplication::clipboard()->text();
    QVERIFY(clipboard.contains(QStringLiteral("#one")));
    QVERIFY(!clipboard.contains(QStringLiteral("#two")));
  }
};

QTEST_MAIN(ChannelListDialogTest)

#include "channel_list_dialog_test.moc"
