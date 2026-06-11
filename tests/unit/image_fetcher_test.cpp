#include "services/ImageFetcher.h"

#include <QBuffer>
#include <QHostAddress>
#include <QImage>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>

using maxchat::services::ImageFetcher;
using maxchat::services::ImageFetchOptions;

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
      QStringLiteral("http://127.0.0.1:%1/pic.png").arg(server.serverPort()));
}

QByteArray pngBytes() {
  QImage image(4, 4, QImage::Format_RGB32);
  image.fill(Qt::red);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return bytes;
}

} // namespace

class ImageFetcherTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() { qRegisterMetaType<QImage>(); }

  void fetchesAndDecodesImage() {
    QTcpServer server;
    const QUrl url =
        startLocalServer(server, QByteArrayLiteral("image/png"), pngBytes());
    QVERIFY(url.isValid());

    QNetworkAccessManager manager;
    ImageFetcher fetcher(&manager);
    QSignalSpy ok(&fetcher, &ImageFetcher::imageFetched);
    QSignalSpy fail(&fetcher, &ImageFetcher::imageFetchFailed);

    ImageFetchOptions options;
    options.allowPrivateNetwork = true; // loopback bypasses the SSRF gate
    fetcher.fetch(url, options);

    QVERIFY(ok.wait(5000));
    QCOMPARE(fail.count(), 0);
    QCOMPARE(ok.count(), 1);
    const QImage image = ok.front().at(1).value<QImage>();
    QVERIFY(!image.isNull());
  }

  void rejectsNonImageContentType() {
    QTcpServer server;
    const QUrl url = startLocalServer(server, QByteArrayLiteral("text/html"),
                                      QByteArrayLiteral("<html></html>"));
    QVERIFY(url.isValid());

    QNetworkAccessManager manager;
    ImageFetcher fetcher(&manager);
    QSignalSpy fail(&fetcher, &ImageFetcher::imageFetchFailed);

    ImageFetchOptions options;
    options.allowPrivateNetwork = true;
    fetcher.fetch(url, options);

    QVERIFY(fail.wait(5000));
    QCOMPARE(fail.count(), 1);
  }

  void blocksPrivateAddressByDefault() {
    QNetworkAccessManager manager;
    ImageFetcher fetcher(&manager);
    QSignalSpy ok(&fetcher, &ImageFetcher::imageFetched);
    QSignalSpy fail(&fetcher, &ImageFetcher::imageFetchFailed);

    // Default options: no allowPrivateNetwork → the loopback literal is blocked
    // by the SSRF gate before any request is made (synchronous failure).
    fetcher.fetch(QUrl(QStringLiteral("http://127.0.0.1/pic.png")));

    QCOMPARE(ok.count(), 0);
    QCOMPARE(fail.count(), 1);
  }
};

QTEST_MAIN(ImageFetcherTest)

#include "image_fetcher_test.moc"
