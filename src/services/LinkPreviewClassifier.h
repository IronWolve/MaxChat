#pragma once

#include <QString>
#include <QUrl>

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
[[nodiscard]] bool isDirectRasterImageUrl(const QUrl &url);

} // namespace maxchat::services
