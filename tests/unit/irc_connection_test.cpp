#include "irc/IrcConnection.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>

using maxchat::irc::IrcConnection;

class IrcConnectionTest final : public QObject {
    Q_OBJECT

  private:
    static IrcConnection::ConnectConfig configFor(const QTcpServer& server) {
        IrcConnection::ConnectConfig config;
        config.host = QStringLiteral("127.0.0.1");
        config.port = int(server.serverPort());
        config.tls = false;
        config.nick = QStringLiteral("bob");
        config.username = QStringLiteral("bob");
        config.realname = QStringLiteral("Bob");
        config.connectTimeoutMs = 3000;
        config.registrationTimeoutMs = 3000;
        return config;
    }

    static QTcpSocket* acceptPeer(QTcpServer& server, IrcConnection& connection) {
        QSignalSpy connected(&connection, &IrcConnection::connected);
        if (!server.waitForNewConnection(1000)) {
            return nullptr;
        }
        auto* peer = server.nextPendingConnection();
        if (peer == nullptr) {
            return nullptr;
        }
        if (!connected.wait(1000) && connected.count() == 0) {
            return nullptr;
        }
        return peer;
    }

    static QByteArray readUntil(QTcpSocket* peer, const QByteArray& needle, int timeoutMs = 1000) {
        QByteArray data = peer->readAll();
        QElapsedTimer timer;
        timer.start();
        while (!data.contains(needle) && timer.elapsed() < timeoutMs) {
            peer->waitForReadyRead(50);
            QCoreApplication::processEvents();
            data.append(peer->readAll());
        }
        return data;
    }

  private slots:
    void plainConnectionRegistersOnConnect() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        IrcConnection connection;

        connection.connectTo(configFor(server));
        QTcpSocket* peer = acceptPeer(server, connection);
        QVERIFY(peer != nullptr);

        const QByteArray data = readUntil(peer, "USER bob");
        QVERIFY(data.contains("CAP LS 302\r\n"));
        QVERIFY(data.contains("NICK bob\r\n"));
        QVERIFY(data.contains("USER bob 0 * :Bob\r\n"));
    }

    void serverWelcomeUpdatesSessionNickAndEmitsRegistered() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        IrcConnection connection;

        connection.connectTo(configFor(server));
        QTcpSocket* peer = acceptPeer(server, connection);
        QVERIFY(peer != nullptr);
        readUntil(peer, "USER bob");

        QSignalSpy registered(&connection, &IrcConnection::registered);
        peer->write(":srv 001 bob_ :Welcome\r\n");
        peer->flush();

        QVERIFY(registered.wait(1000) || registered.count() > 0);
        QCOMPARE(connection.nick(), QStringLiteral("bob_"));
    }

    void sendRawWritesToSocketAndRedactsRawLog() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        IrcConnection connection;

        connection.connectTo(configFor(server));
        QTcpSocket* peer = acceptPeer(server, connection);
        QVERIFY(peer != nullptr);
        readUntil(peer, "USER bob");

        QSignalSpy rawLines(&connection, &IrcConnection::rawLine);
        QVERIFY(connection.sendRaw(QStringLiteral("PASS secret")));

        const QByteArray data = readUntil(peer, "PASS secret");
        QVERIFY(data.contains("PASS secret\r\n"));
        QCOMPARE(rawLines.count(), 1);
        QCOMPARE(rawLines.at(0).at(0).toString(), QStringLiteral(">>"));
        QCOMPARE(rawLines.at(0).at(1).toString(), QStringLiteral("PASS ****"));
    }

    void failedConnectEmitsFailoverFriendlyDisconnect() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const quint16 port = server.serverPort();
        server.close();

        IrcConnection connection;
        auto config = configFor(server);
        config.port = int(port);
        config.connectTimeoutMs = 100;
        QSignalSpy disconnected(&connection, &IrcConnection::disconnected);
        QSignalSpy errors(&connection, &IrcConnection::errorOccurred);

        connection.connectTo(config);

        QVERIFY(disconnected.wait(1500) || disconnected.count() > 0);
        const QString reason = disconnected.takeFirst().at(0).toString();
        QVERIFY2(!reason.trimmed().isEmpty(), qPrintable(reason));
        QVERIFY(errors.count() > 0);
    }

    void registrationTimeoutEmitsFailoverFriendlyDisconnect() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        IrcConnection connection;
        auto config = configFor(server);
        config.registrationTimeoutMs = 100;
        QSignalSpy disconnected(&connection, &IrcConnection::disconnected);
        QSignalSpy errors(&connection, &IrcConnection::errorOccurred);

        connection.connectTo(config);
        QTcpSocket* peer = acceptPeer(server, connection);
        QVERIFY(peer != nullptr);
        readUntil(peer, "USER bob");

        QVERIFY(disconnected.wait(1500) || disconnected.count() > 0);
        QCOMPARE(disconnected.takeFirst().at(0).toString(),
                 QStringLiteral("registration timed out"));
        QVERIFY(errors.count() > 0);
    }
};

QTEST_MAIN(IrcConnectionTest)

#include "irc_connection_test.moc"
