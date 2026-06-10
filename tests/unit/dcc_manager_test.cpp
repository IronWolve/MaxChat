#include "ui/DccManager.h"

#include <QtTest/QtTest>

using maxchat::ui::dccWritableChunk;

class DccManagerTest final : public QObject {
  Q_OBJECT

private slots:
  void capsWriteToRemainingOfferedSize() {
    QCOMPARE(dccWritableChunk(100, 0, 40), qint64(40));   // room to spare
    QCOMPARE(dccWritableChunk(100, 90, 40), qint64(10));  // cap to what's left
    QCOMPARE(dccWritableChunk(100, 100, 40), qint64(0));  // already complete
  }

  void rejectsZeroAndNegativeOfferedSize() {
    // The security fix: a peer offering size 0 (or negative) must not be able to
    // stream unbounded data to disk — writableChunk returns 0 regardless of how
    // much they send.
    QCOMPARE(dccWritableChunk(0, 0, 65536), qint64(0));
    QCOMPARE(dccWritableChunk(-1, 0, 65536), qint64(0));
    QCOMPARE(dccWritableChunk(-999999, 12345, 65536), qint64(0));
  }

  void handlesEmptyReads() {
    QCOMPARE(dccWritableChunk(100, 0, 0), qint64(0));
    QCOMPARE(dccWritableChunk(100, 50, -5), qint64(0));
  }
};

QTEST_APPLESS_MAIN(DccManagerTest)

#include "dcc_manager_test.moc"
