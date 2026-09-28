#include "ui/DccManager.h"
#include "core/SettingsStore.h"

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QtTest/QtTest>

using maxchat::ui::DccManager;
using maxchat::ui::dccWritableChunk;

class DccManagerTest final : public QObject {
  Q_OBJECT

private slots:
  void completedTransferHistoryIsBounded() {
    QTemporaryDir directory;
    DccManager manager; manager.setEnabled(true); manager.setDownloadDir(directory.path());
    for (int i = 0; i < 600; ++i) {
      manager.handleIncoming(QStringLiteral("sender"),
          QStringLiteral("DCC SEND file.txt 2130706433 0 100 %1").arg(i + 1));
      QVERIFY(!manager.transfers().isEmpty());
      manager.cancelTransfer(manager.transfers().last().id);
    }
    QVERIFY(manager.transfers().size() <= 512);
  }

  void unownedPartialIsPreservedAndNotResumed() {
    QTemporaryDir dir;
    QFile part(dir.filePath(QStringLiteral("file.txt.part")));
    QVERIFY(part.open(QIODevice::WriteOnly)); part.write("old"); part.close();
    DccManager manager; manager.setEnabled(true); manager.setDownloadDir(dir.path());
    manager.handleIncoming(QStringLiteral("sender"), QStringLiteral("DCC SEND file.txt 2130706433 0 100 99"));
    QCOMPARE(manager.transfers().size(), 1);
    QVERIFY(manager.transfers().first().localPath != dir.filePath(QStringLiteral("file.txt")));
    QVERIFY(part.open(QIODevice::ReadOnly)); QCOMPARE(part.readAll(), QByteArray("old"));
  }

  void symlinkPartialAndDanglingDestinationAreNeverReused() {
#ifdef Q_OS_WIN
    QSKIP("POSIX symlink fixture");
#endif
    QTemporaryDir dir;
    const QString outside = dir.filePath(QStringLiteral("outside.txt"));
    QFile file(outside); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("old"); file.close();
    QVERIFY(QFile::link(outside, dir.filePath(QStringLiteral("file.txt.part"))));
    QVERIFY(QFile::link(dir.filePath(QStringLiteral("missing")), dir.filePath(QStringLiteral("other.txt"))));
    DccManager manager; manager.setEnabled(true); manager.setDownloadDir(dir.path());
    manager.handleIncoming(QStringLiteral("sender"), QStringLiteral("DCC SEND file.txt 2130706433 0 100 99"));
    manager.handleIncoming(QStringLiteral("sender"), QStringLiteral("DCC SEND other.txt 2130706433 0 100 100"));
    QCOMPARE(manager.transfers().size(), 2);
    QVERIFY(manager.transfers().at(0).localPath != dir.filePath(QStringLiteral("file.txt")));
    QVERIFY(manager.transfers().at(1).localPath != dir.filePath(QStringLiteral("other.txt")));
  }

  void ownedPartialResumesForMatchingPeerOnly() {
    QTemporaryDir dir;
    const QString final = dir.filePath(QStringLiteral("file.txt"));
    QFile part(final + QStringLiteral(".part"));
    QVERIFY(part.open(QIODevice::WriteOnly)); part.write("old"); part.close();
    const QString cache = QDir(maxchat::core::standardSettingsPaths().cacheDir)
                              .filePath(QStringLiteral("dcc-resume"));
    QVERIFY(QDir().mkpath(cache));
    const QString recordPath = QDir(cache).filePath(QString::fromLatin1(QCryptographicHash::hash(
        QFileInfo(final).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256).toHex()) + QStringLiteral(".json"));
    const auto cleanup = qScopeGuard([&]() { QFile::remove(recordPath); });
    QFile record(recordPath); QVERIFY(record.open(QIODevice::WriteOnly));
    record.write(QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("maxchat-dcc-partial-v1")},
        {QStringLiteral("name"), QStringLiteral("file.txt")}, {QStringLiteral("peer"), QStringLiteral("sender")},
        {QStringLiteral("size"), QStringLiteral("100")}}).toJson()); record.close();
    DccManager manager; manager.setEnabled(true); manager.setDownloadDir(dir.path());
    manager.handleIncoming(QStringLiteral("sender"), QStringLiteral("DCC SEND file.txt 2130706433 1234 100"));
    QCOMPARE(manager.transfers().first().localPath, final);
    QSignalSpy sent(&manager, &DccManager::ctcpToSend);
    manager.acceptTransfer(manager.transfers().first().id);
    QCOMPARE(sent.count(), 1);
    QVERIFY(sent.first().at(1).toString().startsWith(QStringLiteral("RESUME")));
  }

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
