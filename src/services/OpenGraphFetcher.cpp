#include "services/OpenGraphFetcher.h"

#include "services/LinkPreviewClassifier.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QScopeGuard>
#include <QTimer>

namespace maxchat::services {
namespace {

bool isHttpUrlWithoutCredentials(const QUrl &url) {
  const QString scheme = url.scheme().toLower();
  return url.isValid() &&
         (scheme == QStringLiteral("http") ||
          scheme == QStringLiteral("https")) &&
         !url.host().isEmpty() && url.userInfo().isEmpty();
}

bool canFetchUrl(const QUrl &url, bool allowPrivateNetwork) {
  if (!isHttpUrlWithoutCredentials(url)) {
    return false;
  }
  return allowPrivateNetwork || isAllowedPreviewFetchUrl(url);
}

bool looksLikeHtmlContent(const QString &contentType) {
  return contentType.isEmpty() || contentType.contains(QStringLiteral("html"));
}

qsizetype normalizedMaxBytes(qsizetype value) {
  return value > 0 ? value : 256 * 1024;
}

int normalizedTimeoutMs(int value) { return value > 0 ? value : 10000; }

} // namespace

OpenGraphFetcher::OpenGraphFetcher(QNetworkAccessManager *manager,
                                   QObject *parent)
    : QObject(parent), manager_(manager) {}

QNetworkRequest OpenGraphFetcher::buildRequest(const QUrl &url) {
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("MaxChat/0.1 link-preview"));
  request.setRawHeader("Accept",
                       "text/html,application/xhtml+xml;q=0.9,*/*;q=0.1");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setMaximumRedirectsAllowed(5);
  return request;
}

void OpenGraphFetcher::fetch(const QUrl &url, OpenGraphFetchOptions options) {
  if (manager_ == nullptr) {
    emit fetchFailed(url, QStringLiteral("network manager missing"));
    return;
  }
  if (!canFetchUrl(url, options.allowPrivateNetwork)) {
    emit fetchFailed(url, QStringLiteral("blocked preview URL"));
    return;
  }

  options.maxBytes = normalizedMaxBytes(options.maxBytes);
  options.timeoutMs = normalizedTimeoutMs(options.timeoutMs);
  QNetworkReply *reply = manager_->get(buildRequest(url));

  const QPointer<QNetworkReply> guardedReply(reply);
  QTimer::singleShot(options.timeoutMs, reply, [guardedReply]() {
    if (guardedReply != nullptr && !guardedReply->isFinished()) {
      guardedReply->setProperty("maxchat_timeout", true);
      guardedReply->abort();
    }
  });

  connect(reply, &QNetworkReply::finished, this, [this, reply, url, options]() {
    const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });

    if (reply->error() != QNetworkReply::NoError) {
      const QString reason = reply->property("maxchat_timeout").toBool()
                                 ? QStringLiteral("preview fetch timed out")
                                 : reply->errorString();
      emit fetchFailed(url, reason);
      return;
    }

    const QString contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
    if (!looksLikeHtmlContent(contentType)) {
      emit fetchFailed(url, QStringLiteral("preview response was not HTML"));
      return;
    }

    const QByteArray payload = reply->read(options.maxBytes + 1);
    const QString html = QString::fromUtf8(payload.left(options.maxBytes));
    const OpenGraphCard card = parseOpenGraphCard(html, reply->url());
    if (card.isEmpty()) {
      emit fetchFailed(url, QStringLiteral("preview metadata was not found"));
      return;
    }

    emit cardFetched(url, card);
  });
}

} // namespace maxchat::services
