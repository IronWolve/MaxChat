#include "core/MacKeychain.h"
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest/QtTest>

class MacKeychainTest final : public QObject {
    Q_OBJECT
private slots:
    void isolatedRoundTripAndLockedFailure() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray path = directory.filePath(QStringLiteral("synthetic.keychain")).toUtf8();
        const QByteArray password("isolated-test-only");
        SecKeychainRef keychain = nullptr;
        QCOMPARE(SecKeychainSetUserInteractionAllowed(false), OSStatus(errSecSuccess));
        QCOMPARE(SecKeychainCreate(path.constData(), UInt32(password.size()), password.constData(),
                                  false, nullptr, &keychain), OSStatus(errSecSuccess));
        // Delete only the newly created test keychain, even if an assertion fails.
        struct Cleanup { SecKeychainRef key; ~Cleanup() { SecKeychainDelete(key); CFRelease(key); } } cleanup{keychain};
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString other = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QByteArray payload(32000, 'x'); payload[42] = '\0';
        QVERIFY(maxchat::core::macKeychainRequest(QStringLiteral("write"), id, payload, keychain));
        QByteArray read;
        QVERIFY(maxchat::core::macKeychainRequest(QStringLiteral("read"), id, read, keychain));
        QCOMPARE(read, payload);
        QVERIFY(!maxchat::core::macKeychainRequest(QStringLiteral("read"), other, read, keychain));
        QByteArray oversized(128 * 1024 + 1, 'x');
        QVERIFY(!maxchat::core::macKeychainRequest(QStringLiteral("write"), id, oversized, keychain));
        QVERIFY(maxchat::core::macKeychainRequest(QStringLiteral("read"), id, read, keychain));
        QCOMPARE(read, payload);
        QCOMPARE(SecKeychainLock(keychain), OSStatus(errSecSuccess));
        QVERIFY(!maxchat::core::macKeychainRequest(QStringLiteral("read"), id, read, keychain));
        QCOMPARE(SecKeychainUnlock(keychain, UInt32(password.size()), password.constData(), true), OSStatus(errSecSuccess));
        QVERIFY(maxchat::core::macKeychainRequest(QStringLiteral("remove"), other, read, keychain));
        QVERIFY(maxchat::core::macKeychainRequest(QStringLiteral("read"), id, read, keychain));
        QVERIFY(maxchat::core::macKeychainRequest(QStringLiteral("remove"), id, read, keychain));
        QVERIFY(!maxchat::core::macKeychainRequest(QStringLiteral("read"), id, read, keychain));
    }
};
QTEST_GUILESS_MAIN(MacKeychainTest)
#include "mac_keychain_test.moc"
