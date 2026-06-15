#include "services/OpenGraphFetcher.h"

#include <QStringConverter>

#include "services/LinkPreviewClassifier.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QScopeGuard>
#include <QTimer>

namespace maxchat::services {
namespace {

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
  // SSRF gate runs its DNS off the GUI thread; the GET only fires once it passes.
  resolvePreviewUrlPublicAsync(
      url, options.allowPrivateNetwork, this, [this, url, options](bool allowed) {
        if (!allowed) {
          emit fetchFailed(url, QStringLiteral("blocked preview URL"));
          return;
        }
        issueRequest(url, options);
      });
}

void OpenGraphFetcher::issueRequest(const QUrl &url, OpenGraphFetchOptions options) {
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
  // redirects to private IPs. Literal private IPs reject synchronously inside
  // the gate; public-domain hops resolve off-thread, then abort if private.
  // Abort while the body is still streaming once it exceeds the cap — reading
  // only at finished() lets a hostile server buffer gigabytes of HTML in memory
  // first (ImageFetcher does the same for image bytes).
  const qint64 maxBytes = options.maxBytes;
  connect(reply, &QNetworkReply::downloadProgress, reply,
          [guardedReply, maxBytes](qint64 received, qint64 /*total*/) {
            if (guardedReply != nullptr && received > maxBytes && !guardedReply->isFinished()) {
              guardedReply->setProperty("maxchat_oversize", true);
              guardedReply->abort();
            }
          });

  const bool allowPrivate = options.allowPrivateNetwork;
  connect(reply, &QNetworkReply::redirected, this,
          [this, guardedReply, allowPrivate](const QUrl &target) {
            resolvePreviewUrlPublicAsync(
                target, allowPrivate, this, [guardedReply](bool allowed) {
                  if (allowed || guardedReply == nullptr) {
                    return;
                  }
                  guardedReply->setProperty("maxchat_blocked_redirect", true);
                  guardedReply->abort();
                });
          });

  connect(reply, &QNetworkReply::finished, this, [this, reply, url, options]() {
    const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });

    const bool oversize = reply->property("maxchat_oversize").toBool();
    if (reply->error() != QNetworkReply::NoError && !oversize) {
      QString reason = reply->errorString();
      if (reply->property("maxchat_blocked_redirect").toBool()) {
        reason = QStringLiteral("blocked redirect to a private address");
      } else if (reply->property("maxchat_timeout").toBool()) {
        reason = QStringLiteral("preview fetch timed out");
      }
      emit fetchFailed(url, reason);
      return;
    }
    // An oversize page was aborted mid-stream: the first maxBytes we buffered
    // almost always hold the <head> OG tags, so parse them best-effort.

    const QString contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
    if (!looksLikeHtmlContent(contentType)) {
      emit fetchFailed(url, QStringLiteral("preview response was not HTML"));
      return;
    }

    const QByteArray payload = reply->read(options.maxBytes + 1);
    const QByteArray bounded = payload.left(options.maxBytes);
    // Honour the page's charset (meta/BOM sniff) — fromUtf8 alone garbled
    // titles on ISO-8859-1 / Shift-JIS pages.
    QString html;
    if (const auto encoding = QStringConverter::encodingForHtml(bounded)) {
      html = QStringDecoder(*encoding).decode(bounded);
    } else {
      html = QString::fromUtf8(bounded);
    }
    const OpenGraphCard card = parseOpenGraphCard(html, reply->url());
    if (card.isEmpty()) {
      emit fetchFailed(url, QStringLiteral("preview metadata was not found"));
      return;
    }

    emit cardFetched(url, card);
  });
}

} // namespace maxchat::services
