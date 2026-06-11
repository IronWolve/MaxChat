#pragma once

#include <QImage>
#include <QObject>
#include <QUrl>

#include <cstddef>

class QNetworkAccessManager;

namespace maxchat::services {

struct ImageFetchOptions {
  qsizetype maxBytes = 5 * 1024 * 1024; // 5 MiB
  int timeoutMs = 12000;
  int maxWidth = 380;  // inline previews are scaled down to fit the chat column
  int maxHeight = 300;
  bool allowPrivateNetwork = false;
};

// Downloads an image for an inline chat preview. Reuses the shared SSRF gate
// (canFetchPreviewUrl) — http(s) only, no credentials, public-only DNS, and the
// guard is re-run on every redirect hop. Decodes + scales down before emitting.
class ImageFetcher final : public QObject {
  Q_OBJECT
public:
  explicit ImageFetcher(QNetworkAccessManager *manager, QObject *parent = nullptr);

  void fetch(const QUrl &url, ImageFetchOptions options = {});

signals:
  void imageFetched(const QUrl &url, const QImage &image);
  void imageFetchFailed(const QUrl &url, const QString &reason);

private:
  QNetworkAccessManager *manager_ = nullptr;
};

} // namespace maxchat::services
