#include "services/OpenGraphFetcher.h"

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>

using maxchat::services::OpenGraphCard;
using maxchat::services::OpenGraphFetcher;
using maxchat::services::OpenGraphFetchOptions;

namespace {

QUrl startLocalServer(QTcpServer &server, const QByteArray &contentType,
                      const QByteArray &body) {
  QObject::connect(
      &server, &QTcpServer::newConnection, &server,
      [&server, contentType, body]() {
        QTcpSocket *socket = server.nextPendingConnection();
        if (socket == nullptr) {
          return;
        }
        auto respond = [socket, contentType, body]() {
          if (socket->property("maxchat_responded").toBool()) {
            return;
          }
          socket->setProperty("maxchat_responded", true);
          socket->readAll();
          const QByteArray response =
              QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: ") +
              contentType + QByteArrayLiteral("\r\nContent-Length: ") +
              QByteArray::number(body.size()) +
              QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
          socket->write(response);
          socket->disconnectFromHost();
        };
        QObject::connect(socket, &QTcpSocket::readyRead, socket, respond);
        if (socket->bytesAvailable() > 0) {
          respond();
        }
      });

  if (!server.listen(QHostAddress::LocalHost)) {
    return {};
  }
  return QUrl(
      QStringLiteral("http://127.0.0.1:%1/post").arg(server.serverPort()));
}

} // namespace

class OpenGraphFetcherTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() { qRegisterMetaType<OpenGraphCard>(); }

  void fetchesOpenGraphCardFromAllowedLocalServer() {
    const QByteArray body = QByteArrayLiteral(R"(
            <html><head>
            <meta property="og:title" content="Local Preview">
            <meta property="og:description" content="Fetched from a test server">
            <meta property="og:image" content="/preview.png">
            </head></html>
        )");
    QTcpServer server;
    const QUrl url = startLocalServer(
        server, QByteArrayLiteral("text/html; charset=utf-8"), body);
    QVERIFY(url.isValid());

    QNetworkAccessManager manager;
    OpenGraphFetcher fetcher(&manager);
    QSignalSpy fetched(&fetcher, &OpenGraphFetcher::cardFetched);
    QSignalSpy failed(&fetcher, &OpenGraphFetcher::fetchFailed);

    OpenGraphFetchOptions options;
    options.allowPrivateNetwork = true;
    options.timeoutMs = 2000;
    fetcher.fetch(url, options);

    QVERIFY(fetched.wait(3000));
    QCOMPARE(failed.count(), 0);
    const QList<QVariant> args = fetched.takeFirst();
    QCOMPARE(args.at(0).toUrl(), url);
    const OpenGraphCard card = qvariant_cast<OpenGraphCard>(args.at(1));
    QCOMPARE(card.title, QStringLiteral("Local Preview"));
    QCOMPARE(card.description, QStringLiteral("Fetched from a test server"));
    QCOMPARE(card.imageUrl.toString(),
             QStringLiteral("http://127.0.0.1:%1/preview.png").arg(url.port()));
  }

  void blocksPrivateTargetsByDefault() {
    QNetworkAccessManager manager;
    OpenGraphFetcher fetcher(&manager);
    QSignalSpy failed(&fetcher, &OpenGraphFetcher::fetchFailed);

    fetcher.fetch(QUrl(QStringLiteral("http://127.0.0.1:9/post")));

    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.takeFirst().at(1).toString(),
             QStringLiteral("blocked preview URL"));
  }

  void rejectsNonHtmlResponses() {
    QTcpServer server;
    const QUrl url = startLocalServer(server, QByteArrayLiteral("image/png"),
                                      QByteArrayLiteral("png"));
    QVERIFY(url.isValid());

    QNetworkAccessManager manager;
    OpenGraphFetcher fetcher(&manager);
    QSignalSpy failed(&fetcher, &OpenGraphFetcher::fetchFailed);

    OpenGraphFetchOptions options;
    options.allowPrivateNetwork = true;
    options.timeoutMs = 2000;
    fetcher.fetch(url, options);

    QVERIFY(failed.wait(3000));
    QCOMPARE(failed.takeFirst().at(1).toString(),
             QStringLiteral("preview response was not HTML"));
  }
};

QTEST_GUILESS_MAIN(OpenGraphFetcherTest)

#include "open_graph_fetcher_test.moc"
