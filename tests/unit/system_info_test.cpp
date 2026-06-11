#include "ui/SystemInfo.h"

#include <QtTest/QtTest>

using maxchat::ui::formatGiB;
using maxchat::ui::formatUptime;

class SystemInfoTest final : public QObject {
    Q_OBJECT

  private slots:
    void uptimeFormats() {
        QCOMPARE(formatUptime(0), QString());
        QCOMPARE(formatUptime(-5), QString());
        QCOMPARE(formatUptime(59), QStringLiteral("0m"));
        QCOMPARE(formatUptime(60), QStringLiteral("1m"));
        QCOMPARE(formatUptime(3661), QStringLiteral("1h 1m"));   // 1h 1m 1s
        QCOMPARE(formatUptime(90061), QStringLiteral("1d 1h 1m")); // 1d 1h 1m 1s
        // matches the example's shape: 12d 7h 41m
        QCOMPARE(formatUptime(12 * 86400 + 7 * 3600 + 41 * 60), QStringLiteral("12d 7h 41m"));
    }

    void gibFormats() {
        QCOMPARE(formatGiB(0), QStringLiteral("0.0 GB"));
        QCOMPARE(formatGiB(quint64(32) * 1024 * 1024 * 1024), QStringLiteral("32.0 GB"));
        // ~14.2 GB
        QCOMPARE(formatGiB(quint64(15246485914)), QStringLiteral("14.2 GB"));
    }
};

QTEST_APPLESS_MAIN(SystemInfoTest)

#include "system_info_test.moc"
