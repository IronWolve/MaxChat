#include "app/AppInfo.h"

#include <QtTest/QtTest>

class AppInfoTest final : public QObject {
    Q_OBJECT

  private slots:
    void metadataIsPresent() {
        QVERIFY(!maxchat::app::applicationName().isEmpty());
        QVERIFY(!maxchat::app::displayName().isEmpty());
        QVERIFY(!maxchat::app::version().isEmpty());
        QCOMPARE(maxchat::app::applicationName(), QStringLiteral("MaxChat"));
    }
};

QTEST_MAIN(AppInfoTest)

#include "app_info_test.moc"
