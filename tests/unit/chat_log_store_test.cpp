#include "core/ChatLogStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTime>

using maxchat::core::ChatLogStore;

class ChatLogStoreTest final : public QObject {
    Q_OBJECT

  private slots:
    void sanitizesPathParts() {
        QCOMPARE(ChatLogStore::safePathPart(QStringLiteral("../Bad:Name?")),
                 QStringLiteral("_Bad_Name_"));
        QCOMPARE(ChatLogStore::safePathPart(QString()), QStringLiteral("server"));
        QVERIFY(ChatLogStore::safePathPart(QString(120, QLatin1Char('a'))).size() <= 80);
    }

    void appendsAndReadsRecentLines() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const ChatLogStore store(temp.path());
        const QDate date(2026, 6, 8);
        QVERIFY(store.appendLine(QStringLiteral("Libera.Chat"), QStringLiteral("#maxchat"),
                                 QStringLiteral("<nick> first"), QDateTime(date, QTime(1, 2, 3))));
        QVERIFY(store.appendLine(QStringLiteral("Libera.Chat"), QStringLiteral("#maxchat"),
                                 QStringLiteral("<nick> second"), QDateTime(date, QTime(1, 2, 4))));
        QVERIFY(store.appendLine(QStringLiteral("Libera.Chat"), QStringLiteral("#maxchat"),
                                 QStringLiteral("<nick> third"), QDateTime(date, QTime(1, 2, 5))));

        const QString path =
            store.logFilePath(QStringLiteral("Libera.Chat"), QStringLiteral("#maxchat"), date);
        QVERIFY(QFile::exists(path));

        const QStringList recent =
            store.recentLines(QStringLiteral("Libera.Chat"), QStringLiteral("#maxchat"), 2, date);
        QCOMPARE(recent.size(), 2);
        QVERIFY(recent.at(0).contains(QStringLiteral("second")));
        QVERIFY(recent.at(1).contains(QStringLiteral("third")));
    }

    void expandsDateTokensInMask() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ChatLogStore store(temp.path());
        store.setLogMask(QStringLiteral("%network/%Y/%m-%d"));
        const QString path =
            store.logFilePath(QStringLiteral("Libera"), QStringLiteral("#x"), QDate(2026, 6, 8));
        QVERIFY2(path.endsWith(QStringLiteral("Libera/2026/06-08.log")), qPrintable(path));
    }

    void ignoresInvalidOrEmptyWrites() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const ChatLogStore store(temp.path());
        QVERIFY(!store.appendLine(QStringLiteral("Net"), QStringLiteral("#chan"), QString()));
        QVERIFY(store.recentLines(QStringLiteral("Net"), QStringLiteral("#chan")).isEmpty());
    }
};

QTEST_MAIN(ChatLogStoreTest)

#include "chat_log_store_test.moc"
