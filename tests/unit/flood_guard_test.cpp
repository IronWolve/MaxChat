#include "core/FloodGuard.h"

#include <QtTest/QtTest>

using maxchat::core::FloodGuard;

class FloodGuardTest final : public QObject {
    Q_OBJECT

  private slots:
    void disabledNeverTrips() {
        FloodGuard guard;
        guard.configure(false, 2, 4);

        QVERIFY(!guard.recordMessage(QStringLiteral("net/alice"), 1000));
        QVERIFY(!guard.recordMessage(QStringLiteral("net/alice"), 1100));
        QVERIFY(!guard.recordMessage(QStringLiteral("net/alice"), 1200));
    }

    void tripsAfterThresholdInsideWindow() {
        FloodGuard guard;
        guard.configure(true, 2, 4);

        QVERIFY(!guard.recordMessage(QStringLiteral("net/alice"), 1000));
        QVERIFY(!guard.recordMessage(QStringLiteral("net/alice"), 1100));
        QVERIFY(guard.recordMessage(QStringLiteral("net/alice"), 1200));
    }

    void oldMessagesExpire() {
        FloodGuard guard;
        guard.configure(true, 2, 1);

        QVERIFY(!guard.recordMessage(QStringLiteral("net/alice"), 1000));
        QVERIFY(!guard.recordMessage(QStringLiteral("net/alice"), 1100));
        QVERIFY(!guard.recordMessage(QStringLiteral("net/alice"), 2500));
    }
};

QTEST_APPLESS_MAIN(FloodGuardTest)

#include "flood_guard_test.moc"
