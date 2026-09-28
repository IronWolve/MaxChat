#pragma once
#include <QByteArray>
#include <QHostAddress>
#include <QNetworkRequest>
#include <QUrl>
#include <functional>
class QObject;
class QNetworkAccessManager;
namespace maxchat::services {
struct PublicHttpOptions {
    qint64 maxBytes = 256 * 1024;
    int timeoutMs = 10000;
    bool allowPrivateNetwork = false; // only explicit local fixtures/administration
    bool allowTruncatedBody = false;
    QByteArray accept = "*/*";
};
struct PublicHttpResult {
    QUrl finalUrl;
    QByteArray body;
    QByteArray contentType;
    QString error;
    int status = 0;
    bool truncated = false;
};
using PublicHttpCallback = std::function<void(PublicHttpResult)>;
// The actual network URL contains a validated IP. Host and TLS peer identity
// remain the original host. Every redirect is separately resolved and pinned.
QNetworkRequest pinnedHttpRequest(const QUrl& logicalUrl, const QHostAddress& address,
                                  const PublicHttpOptions& options);
QObject* fetchPublicHttp(QNetworkAccessManager* manager, const QUrl& url,
                         PublicHttpOptions options, QObject* context, PublicHttpCallback done);
}
