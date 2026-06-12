#include "ui/DccManager.h"

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QtTest/QtTest>

using maxchat::ui::DccManager;
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
    QCOMPARE(dccWritableChunk(0, 0, 65536), qint64(0));
    QCOMPARE(dccWritableChunk(-1, 0, 65536), qint64(0));
    QCOMPARE(dccWritableChunk(-999999, 12345, 65536), qint64(0));
  }

  void handlesEmptyReads() {
    QCOMPARE(dccWritableChunk(100, 0, 0), qint64(0));
    QCOMPARE(dccWritableChunk(100, 50, -5), qint64(0));
  }

  // ── DCC enabled gate ────────────────────────────────────────────────────

  void offerSendBlockedWhenDisabled() {
    DccManager mgr;
    // enabled_ defaults to false

    QSignalSpy ctcpSpy(&mgr, &DccManager::ctcpToSend);
    QSignalSpy statusSpy(&mgr, &DccManager::status);

    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    tmp.write("hello");
    tmp.flush();

    mgr.offerSend(QStringLiteral("testpeer"), tmp.fileName());

    QCOMPARE(ctcpSpy.count(), 0);
    QVERIFY(statusSpy.count() >= 1);
    QVERIFY(statusSpy.at(0).at(0).toString().contains(QStringLiteral("disabled")));
  }

  void handleIncomingBlockedWhenDisabled() {
    DccManager mgr;

    QSignalSpy transfersSpy(&mgr, &DccManager::transfersChanged);
    QSignalSpy ctcpSpy(&mgr, &DccManager::ctcpToSend);

    // Simulate a remote peer offering us a file
    mgr.handleIncoming(QStringLiteral("sender"),
                       QStringLiteral("DCC SEND file.txt 2130706433 1234 1024"));

    QCOMPARE(transfersSpy.count(), 0);
    QCOMPARE(ctcpSpy.count(), 0);
  }

  void offerSendEmitsCtcpWhenEnabled() {
    DccManager mgr;
    mgr.setEnabled(true);
    mgr.setPassive(true); // passive: emits ctcpToSend without opening a port

    QSignalSpy ctcpSpy(&mgr, &DccManager::ctcpToSend);
    QSignalSpy transfersSpy(&mgr, &DccManager::transfersChanged);

    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    tmp.write("hello world");
    tmp.flush();

    mgr.offerSend(QStringLiteral("testpeer"), tmp.fileName());

    QCOMPARE(ctcpSpy.count(), 1);
    QCOMPARE(ctcpSpy.at(0).at(0).toString(), QStringLiteral("testpeer"));
    QVERIFY(ctcpSpy.at(0).at(1).toString().contains(QStringLiteral("SEND")));
    QVERIFY(transfersSpy.count() >= 1);
  }

  void handleIncomingAcceptsOfferWhenEnabled() {
    DccManager mgr;
    mgr.setEnabled(true);
    mgr.setDownloadDir(QDir::tempPath());

    QSignalSpy transfersSpy(&mgr, &DccManager::transfersChanged);

    // Peer sends us a passive DCC SEND offer (port 0 = reverse)
    mgr.handleIncoming(QStringLiteral("sender"),
                       QStringLiteral("DCC SEND file.txt 2130706433 0 1024 99"));

    // transfersChanged should fire when the transfer is queued
    QVERIFY(transfersSpy.count() >= 1);
    const auto transfers = mgr.transfers();
    QCOMPARE(transfers.size(), 1);
    QCOMPARE(transfers.at(0).fileName, QStringLiteral("file.txt"));
    QCOMPARE(transfers.at(0).size, qint64(1024));
  }
};

QTEST_MAIN(DccManagerTest)

#include "dcc_manager_test.moc"
