#include "services/OpenGraphFetcher.h"

#include "services/LinkPreviewClassifier.h"

#include <QHostAddress>
#include <QHostInfo>
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

// Resolve the host and reject if ANY resolved address is private/loopback/etc.
// (the hostname string check can't catch a public-looking domain whose DNS A
// record points at 127.0.0.1 / 169.254.169.254 / 10.x …). Mirrors the Python
// is_safe_fetch_url getaddrinfo check. Synchronous, like Python's.
bool resolvesToPublicOnly(const QUrl &url) {
  const QString host = url.host();
  if (host.isEmpty()) {
    return false;
  }
  // An IP literal was already vetted by isAllowedPreviewFetchUrl — no DNS.
  if (!QHostAddress(host).isNull()) {
    return true;
  }
  const QHostInfo info = QHostInfo::fromName(host);
  if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
    return false; // can't resolve → don't fetch (Python parity)
  }
  for (const QHostAddress &address : info.addresses()) {
    QUrl probe;
    probe.setScheme(url.scheme());
    probe.setHost(address.toString());
    // Reuse the literal-address rules: blocks IPv6 (colon) and every
    // private/loopback/link-local/reserved IPv4 range.
    if (!isAllowedPreviewFetchUrl(probe)) {
      return false;
    }
  }
  return true;
}

bool canFetchUrl(const QUrl &url, bool allowPrivateNetwork) {
  if (!isHttpUrlWithoutCredentials(url)) {
    return false;
  }
  if (allowPrivateNetwork) {
    return true;
  }
  return isAllowedPreviewFetchUrl(url) && resolvesToPublicOnly(url);
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

  // Re-run the SSRF guard on every redirect hop — a public host can 30x to an
  // internal address, and NoLessSafeRedirectPolicy only blocks downgrades, not
  // redirects to private IPs.
  const bool allowPrivate = options.allowPrivateNetwork;
  connect(reply, &QNetworkReply::redirected, this,
          [this, reply, allowPrivate](const QUrl &target) {
            if (!canFetchUrl(target, allowPrivate)) {
              reply->setProperty("maxchat_blocked_redirect", true);
              reply->abort();
            }
          });

  connect(reply, &QNetworkReply::finished, this, [this, reply, url, options]() {
    const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });

    if (reply->error() != QNetworkReply::NoError) {
      QString reason = reply->errorString();
      if (reply->property("maxchat_blocked_redirect").toBool()) {
        reason = QStringLiteral("blocked redirect to a private address");
      } else if (reply->property("maxchat_timeout").toBool()) {
        reason = QStringLiteral("preview fetch timed out");
      }
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
