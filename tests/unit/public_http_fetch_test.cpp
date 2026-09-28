#include "services/PublicHttpFetch.h"
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QFile>
#include <QSslSocket>
#include <QSslKey>
#include <QSslError>
#include <QSslCertificate>
#include <QNetworkProxy>
#include <QStandardPaths>
#include <QtTest/QtTest>
using namespace maxchat::services;
namespace {
class Reply final : public QNetworkReply {
public:
    Reply(const QNetworkRequest& request, QObject* parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 302);
        setAttribute(QNetworkRequest::RedirectionTargetAttribute, QUrl(QStringLiteral("http://127.0.0.1/private")));
        QTimer::singleShot(0, this, [this]() { setFinished(true); emit finished(); });
    }
    void abort() override { setFinished(true); }
    qint64 readData(char*, qint64) override { return -1; }
};
class RedirectManager final : public QNetworkAccessManager {
public:
    int requests = 0;
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        ++requests; return new Reply(request, this);
    }
};
QUrl serve(QTcpServer& server, QByteArray response, QByteArray* received = nullptr) {
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, response, received]() {
        auto* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, response, received]() {
            if (socket->property("answered").toBool()) return;
            const auto request = socket->readAll();
            if (received) *received += request;
            if (!request.contains("\r\n\r\n")) return;
            socket->setProperty("answered", true);
            socket->write(response); socket->disconnectFromHost();
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    if (!server.listen(QHostAddress::LocalHost)) return {};
    return QUrl(QStringLiteral("http://127.0.0.1:%1/start").arg(server.serverPort()));
}
}
class TlsFixture final : public QTcpServer {
public:
    QSslCertificate certificate;
    QSslCertificate authority;
    QSslKey key;
    void incomingConnection(qintptr descriptor) override {
        auto* socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(descriptor)) { delete socket; return; }
        socket->setLocalCertificateChain({certificate, authority}); socket->setPrivateKey(key);
        socket->setPeerVerifyMode(QSslSocket::VerifyNone); // no client certificate required
        connect(socket, &QIODevice::readyRead, socket, [socket]() {
            if (!socket->isEncrypted() || !socket->readAll().contains("\r\n\r\n")) return;
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
            socket->disconnectFromHost();
        });
        connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        socket->startServerEncryption();
    }
};
class PublicHttpFetchTest final : public QObject {
    Q_OBJECT
private slots:
    void pinnedTlsVerifiesOriginalHostAndRejectsWrongHost() {
        const QString openssl = QStandardPaths::findExecutable(QStringLiteral("openssl"));
        if (openssl.isEmpty() || !QSslSocket::supportsSsl()) QSKIP("OpenSSL fixture generator/TLS backend unavailable");
        QTemporaryDir directory;
        const QString keyPath = directory.filePath(QStringLiteral("fixture-key.pem"));
        const QString certPath = directory.filePath(QStringLiteral("fixture-cert.pem"));
        // A real CA/leaf chain with explicit SHA-256, key usage and server EKU
        // satisfies both OpenSSL and Apple's stricter TLS certificate policy.
#ifdef Q_OS_MACOS
        qputenv("QT_SSL_USE_TEMPORARY_KEYCHAIN", "1"); // also isolate Qt on macOS 13/14
#endif
        const QString configPath = directory.filePath(QStringLiteral("fixture.cnf"));
        const QString caKey = directory.filePath(QStringLiteral("ca-key.pem"));
        const QString caPath = directory.filePath(QStringLiteral("ca-cert.pem"));
        const QString csr = directory.filePath(QStringLiteral("fixture.csr"));
        QFile config(configPath);
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("[req]\nprompt=no\ndistinguished_name=dn\n[dn]\nCN=localhost\n"
                     "[ca]\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n"
                     "[server]\nsubjectAltName=DNS:localhost\nbasicConstraints=critical,CA:FALSE\n"
                     "keyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n");
        config.close();
        const auto generate = [&](const QStringList& arguments) {
            QProcess process;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert(QStringLiteral("RANDFILE"), directory.filePath(QStringLiteral("random-state")));
            process.setProcessEnvironment(environment);
            process.start(openssl, arguments);
            return process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        };
        QVERIFY(generate({QStringLiteral("req"), QStringLiteral("-x509"), QStringLiteral("-newkey"),
            QStringLiteral("rsa:2048"), QStringLiteral("-nodes"), QStringLiteral("-keyout"), caKey,
            QStringLiteral("-out"), caPath, QStringLiteral("-days"), QStringLiteral("1"),
            QStringLiteral("-sha256"), QStringLiteral("-config"), configPath, QStringLiteral("-extensions"),
            QStringLiteral("ca"), QStringLiteral("-subj"), QStringLiteral("/CN=MaxChat test CA")}));
        QVERIFY(generate({QStringLiteral("req"), QStringLiteral("-new"), QStringLiteral("-newkey"),
            QStringLiteral("rsa:2048"), QStringLiteral("-nodes"), QStringLiteral("-keyout"), keyPath,
            QStringLiteral("-out"), csr, QStringLiteral("-sha256"), QStringLiteral("-config"), configPath}));
        QVERIFY(generate({QStringLiteral("x509"), QStringLiteral("-req"), QStringLiteral("-in"), csr,
            QStringLiteral("-CA"), caPath, QStringLiteral("-CAkey"), caKey, QStringLiteral("-set_serial"),
            QStringLiteral("1"), QStringLiteral("-out"), certPath, QStringLiteral("-days"), QStringLiteral("1"),
            QStringLiteral("-sha256"), QStringLiteral("-extfile"), configPath,
            QStringLiteral("-extensions"), QStringLiteral("server")}));
        QFile certificateFile(certPath), keyFile(keyPath);
        QVERIFY(certificateFile.open(QIODevice::ReadOnly)); QVERIFY(keyFile.open(QIODevice::ReadOnly));
        QFile caFile(caPath);
        QVERIFY(caFile.open(QIODevice::ReadOnly));
        TlsFixture server;
        server.authority = QSslCertificate(caFile.readAll());
        server.certificate = QSslCertificate(certificateFile.readAll());
        server.key = QSslKey(keyFile.readAll(), QSsl::Rsa);
        QVERIFY(!server.certificate.isNull() && !server.key.isNull());
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        for (const QString& host : {QStringLiteral("localhost"), QStringLiteral("wrong.invalid")}) {
            const QUrl logical(QStringLiteral("https://%1:%2/test").arg(host).arg(server.serverPort()));
            auto request = pinnedHttpRequest(logical, QHostAddress::LocalHost, {});
            auto ssl = request.sslConfiguration();
            ssl.addCaCertificate(server.authority); request.setSslConfiguration(ssl);
            auto* reply = manager.get(request);
            QStringList tlsErrors;
            QObject tlsErrorContext;
            connect(reply, &QNetworkReply::sslErrors, &tlsErrorContext, [&](const QList<QSslError>& errors) {
                for (const auto& error : errors) tlsErrors.append(error.errorString());
            });
            QSignalSpy finished(reply, &QNetworkReply::finished);
            QVERIFY(finished.wait(5000));
            if (host == QLatin1String("localhost")) {
                QVERIFY2(reply->error() == QNetworkReply::NoError,
                         qPrintable(reply->errorString() + QStringLiteral(": ") + tlsErrors.join(QStringLiteral("; "))));
                QCOMPARE(reply->readAll(), QByteArray("ok"));
            } else { QVERIFY(reply->error() != QNetworkReply::NoError); }
            reply->deleteLater();
        }
    }
    void pinnedRequestKeepsHttpAndTlsIdentity() {
        const QUrl logical(QStringLiteral("https://example.test:8443/image.png"));
        const auto request = pinnedHttpRequest(logical, QHostAddress(QStringLiteral("8.8.8.8")), {});
        QCOMPARE(request.url().host(), QStringLiteral("8.8.8.8"));
        QCOMPARE(request.url().path(), QStringLiteral("/image.png"));
        QCOMPARE(request.rawHeader("Host"), QByteArray("example.test:8443"));
        QCOMPARE(request.peerVerifyName(), QStringLiteral("example.test"));
        QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
        QCOMPARE(request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(), int(QNetworkRequest::Manual));
        QVERIFY(!request.attribute(QNetworkRequest::Http2AllowedAttribute).toBool());
    }
    void redirectToPrivateAddressNeverIssuesSecondRequest() {
        RedirectManager manager; QObject context; bool complete = false; PublicHttpResult result;
        fetchPublicHttp(&manager, QUrl(QStringLiteral("http://8.8.8.8/start")), {}, &context,
                        [&](auto value) { result = value; complete = true; });
        QTRY_VERIFY(complete);
        QCOMPARE(manager.requests, 1);
        QCOMPARE(result.error, QStringLiteral("blocked preview URL"));
    }
    void relativeRedirectRetainsLogicalUrl() {
        QTcpServer first, second;
        const auto final = serve(second, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
        const auto start = serve(first, "HTTP/1.1 302 Found\r\nLocation: " + final.toEncoded() + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        QVERIFY(start.isValid() && final.isValid());
        QNetworkAccessManager manager; QObject context; bool complete = false; PublicHttpResult result;
        PublicHttpOptions options; options.allowPrivateNetwork = true;
        fetchPublicHttp(&manager, start, options, &context, [&](auto value) { result = value; complete = true; });
        QTRY_VERIFY(complete);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.body, QByteArray("ok")); QCOMPARE(result.finalUrl, final);
    }
    void bodyLimitStopsOversizedStream() {
        QTcpServer server;
        const auto url = serve(server, "HTTP/1.1 200 OK\r\nContent-Length: 65536\r\nConnection: close\r\n\r\n" + QByteArray(65536, 'x'));
        QNetworkAccessManager manager; QObject context; bool complete = false; PublicHttpResult result;
        PublicHttpOptions options; options.allowPrivateNetwork = true; options.maxBytes = 32;
        fetchPublicHttp(&manager, url, options, &context, [&](auto value) { result = value; complete = true; });
        QTRY_VERIFY(complete);
        QCOMPARE(result.body.size(), 32);
        QVERIFY(result.error.contains(QStringLiteral("size cap")));
    }
    void aggregateAdmissionLimitRejectsExcessRequests() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        QNetworkAccessManager manager; QObject context;
        PublicHttpOptions options; options.allowPrivateNetwork = true; options.timeoutMs = 5000;
        const QUrl url(QStringLiteral("http://127.0.0.1:%1/wait").arg(server.serverPort()));
        int rejected = 0;
        for (int i = 0; i < 17; ++i)
            fetchPublicHttp(&manager, url, options, &context, [&](auto result) {
                if (result.error.contains(QStringLiteral("request limit"))) ++rejected;
            });
        QCOMPARE(rejected, 1);
    }
    void stalledRequestHasDeadline() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        QNetworkAccessManager manager; QObject context; bool complete = false; PublicHttpResult result;
        PublicHttpOptions options; options.allowPrivateNetwork = true; options.timeoutMs = 30;
        fetchPublicHttp(&manager, QUrl(QStringLiteral("http://127.0.0.1:%1/wait").arg(server.serverPort())), options, &context,
                        [&](auto value) { result = value; complete = true; });
        QTRY_VERIFY(complete);
        QVERIFY(result.error.contains(QStringLiteral("timed out")));
    }
};
QTEST_GUILESS_MAIN(PublicHttpFetchTest)
#include "public_http_fetch_test.moc"
