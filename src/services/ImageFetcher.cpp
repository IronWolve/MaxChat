#include "services/ImageFetcher.h"

#include "services/LinkPreviewClassifier.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QScopeGuard>
#include <QTimer>

namespace maxchat::services {
namespace {

qsizetype normalizedMaxBytes(qsizetype value) {
  return value > 0 ? value : 5 * 1024 * 1024;
}

int normalizedTimeoutMs(int value) { return value > 0 ? value : 12000; }

QNetworkRequest buildRequest(const QUrl &url) {
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("MaxChat/0.1 link-preview"));
  request.setRawHeader("Accept", "image/*;q=0.9,*/*;q=0.1");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setMaximumRedirectsAllowed(5);
  return request;
}

} // namespace

ImageFetcher::ImageFetcher(QNetworkAccessManager *manager, QObject *parent)
    : QObject(parent), manager_(manager) {}

void ImageFetcher::fetch(const QUrl &url, ImageFetchOptions options) {
  if (manager_ == nullptr) {
    emit imageFetchFailed(url, QStringLiteral("network manager missing"));
    return;
  }
  // SSRF gate runs its DNS off the GUI thread; the GET only fires once it passes.
  resolvePreviewUrlPublicAsync(
      url, options.allowPrivateNetwork, this, [this, url, options](bool allowed) {
        if (!allowed) {
          emit imageFetchFailed(url, QStringLiteral("blocked image URL"));
          return;
        }
        issueRequest(url, options);
      });
}

void ImageFetcher::issueRequest(const QUrl &url, ImageFetchOptions options) {
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

  // Re-run the SSRF guard on every redirect — a public host can 30x to a
  // private address, which NoLessSafeRedirectPolicy alone does not block.
  const bool allowPrivate = options.allowPrivateNetwork;
  connect(reply, &QNetworkReply::redirected, this,
          [this, guardedReply, allowPrivate](const QUrl &target) {
            // Literal private IPs reject synchronously inside the gate (abort
            // immediately); only public-domain hops do an off-thread resolve.
            resolvePreviewUrlPublicAsync(
                target, allowPrivate, this, [guardedReply](bool allowed) {
                  if (!allowed && guardedReply != nullptr) {
                    guardedReply->setProperty("maxchat_blocked_redirect", true);
                    guardedReply->abort();
                  }
                });
          });

  connect(reply, &QNetworkReply::finished, this, [this, reply, url, options]() {
    const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });

    if (reply->error() != QNetworkReply::NoError) {
      QString reason = reply->errorString();
      if (reply->property("maxchat_blocked_redirect").toBool()) {
        reason = QStringLiteral("blocked redirect to a private address");
      } else if (reply->property("maxchat_timeout").toBool()) {
        reason = QStringLiteral("image fetch timed out");
      }
      emit imageFetchFailed(url, reason);
      return;
    }

    const QString contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
    if (!contentType.isEmpty() &&
        !contentType.startsWith(QStringLiteral("image/"))) {
      emit imageFetchFailed(url, QStringLiteral("response was not an image"));
      return;
    }

    const QByteArray payload = reply->read(options.maxBytes + 1);
    if (static_cast<qsizetype>(payload.size()) > options.maxBytes) {
      emit imageFetchFailed(url, QStringLiteral("image exceeded size cap"));
      return;
    }

    QImage image;
    if (!image.loadFromData(payload) || image.isNull()) {
      emit imageFetchFailed(url, QStringLiteral("could not decode image"));
      return;
    }
    if (image.width() > options.maxWidth || image.height() > options.maxHeight) {
      image = image.scaled(options.maxWidth, options.maxHeight,
                           Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    emit imageFetched(url, image);
  });
}

} // namespace maxchat::services
