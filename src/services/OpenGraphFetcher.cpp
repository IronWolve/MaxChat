#include "services/OpenGraphFetcher.h"
#include "services/PublicHttpFetch.h"
#include <QNetworkAccessManager>
#include <QStringConverter>
namespace maxchat::services {
OpenGraphFetcher::OpenGraphFetcher(QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent), manager_(manager) {}
void OpenGraphFetcher::fetch(const QUrl& url, OpenGraphFetchOptions options) {
    PublicHttpOptions request;
    request.maxBytes = options.maxBytes > 0 ? options.maxBytes : 256 * 1024;
    request.timeoutMs = options.timeoutMs > 0 ? options.timeoutMs : 10000;
    request.allowPrivateNetwork = options.allowPrivateNetwork;
    request.allowTruncatedBody = true;
    request.accept = "text/html,application/xhtml+xml;q=0.9,*/*;q=0.1";
    fetchPublicHttp(manager_, url, request, this, [this, url](PublicHttpResult result) {
        if (!result.error.isEmpty()) { emit fetchFailed(url, result.error); return; }
        const QByteArray type = result.contentType.toLower();
        if (!type.isEmpty() && !type.contains("html")) {
            emit fetchFailed(url, QStringLiteral("preview response was not HTML")); return;
        }
        QString html;
        if (const auto encoding = QStringConverter::encodingForHtml(result.body))
            html = QStringDecoder(*encoding).decode(result.body);
        else html = QString::fromUtf8(result.body);
        const auto card = parseOpenGraphCard(html, result.finalUrl);
        if (card.isEmpty()) { emit fetchFailed(url, QStringLiteral("preview metadata was not found")); return; }
        emit cardFetched(url, card);
    });
}
}
