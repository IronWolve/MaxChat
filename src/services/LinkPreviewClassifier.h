#pragma once

#include <QString>
#include <QUrl>

#include <functional>

class QObject;

namespace maxchat::services {

enum class LinkPreviewKind {
  None,
  DirectImage,
  DirectAudio,
  DirectVideo,
  OpenGraph,
  XPost,
  MastodonPost,
};

struct LinkPreviewCandidate {
  LinkPreviewKind kind = LinkPreviewKind::None;
  QUrl originalUrl;
  QUrl fetchUrl;
  QString serviceName;
  QString normalizedHost;
  // Buffer where the URL was posted — set by MainWindow so async results
  // (OG card fetches) go back to the originating channel, not the current one.
  QString originNetwork;
  QString originTarget;

  [[nodiscard]] bool isPreviewable() const;
  [[nodiscard]] bool needsHtmlFetch() const;
  [[nodiscard]] bool needsImageFetch() const;
  [[nodiscard]] bool needsMediaPlayer() const;
};

[[nodiscard]] LinkPreviewCandidate classifyLinkPreview(const QString &urlText);
[[nodiscard]] LinkPreviewCandidate classifyLinkPreview(const QUrl &url);
[[nodiscard]] bool isAllowedPreviewFetchUrl(const QUrl &url);
// Full fetch gate: http(s), no credentials, allowed host, and (unless
// allowPrivateNetwork) every resolved DNS address is public. Shared by the
// OpenGraph and image fetchers so SSRF rules can't drift apart.
[[nodiscard]] bool canFetchPreviewUrl(const QUrl &url,
                                      bool allowPrivateNetwork = false);
// Same gate as canFetchPreviewUrl, but the DNS resolution runs OFF the GUI
// thread (QHostInfo::lookupHost) so a slow/black-hole DNS can't stall the UI.
// `callback` is invoked on `context`'s thread with the allow/deny result. Fast
// rejects and IP-literal/allow-private cases call back synchronously (no DNS).
void resolvePreviewUrlPublicAsync(const QUrl &url, bool allowPrivateNetwork,
                                  const QObject *context,
                                  std::function<void(bool)> callback);
[[nodiscard]] bool isDirectRasterImageUrl(const QUrl &url);

} // namespace maxchat::services
