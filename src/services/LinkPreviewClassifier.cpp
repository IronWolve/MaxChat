#include "services/LinkPreviewClassifier.h"

#include <QRegularExpression>
#include <QStringList>

#include <array>
#include <utility>

namespace maxchat::services {
namespace {

using Ipv4Octets = std::array<int, 4>;

QString normalizedHost(const QUrl &url) {
  QString host = url.host().toLower();
  while (host.endsWith(QLatin1Char('.'))) {
    host.chop(1);
  }
  if (host.startsWith(QStringLiteral("www."))) {
    host = host.mid(4);
  }
  if (host.startsWith(QStringLiteral("mobile."))) {
    host = host.mid(7);
  }
  return host;
}

bool parseIpv4Literal(const QString &host, Ipv4Octets &octets) {
  static const QRegularExpression ipv4Pattern(
      QStringLiteral(R"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$)"));

  const QRegularExpressionMatch match = ipv4Pattern.match(host);
  if (!match.hasMatch()) {
    return false;
  }

  for (int index = 0; index < 4; ++index) {
    bool ok = false;
    const int octet = match.captured(index + 1).toInt(&ok);
    if (!ok || octet < 0 || octet > 255) {
      return false;
    }
    octets[static_cast<size_t>(index)] = octet;
  }
  return true;
}

bool isPublicIpv4Literal(const Ipv4Octets &octets) {
  const int a = octets[0];
  const int b = octets[1];
  const int c = octets[2];

  if (a == 0 || a == 10 || a == 127 || a >= 224) {
    return false;
  }
  if (a == 100 && b >= 64 && b <= 127) {
    return false;
  }
  if (a == 169 && b == 254) {
    return false;
  }
  if (a == 172 && b >= 16 && b <= 31) {
    return false;
  }
  if (a == 192 && b == 168) {
    return false;
  }
  if (a == 192 && b == 0 && (c == 0 || c == 2)) {
    return false;
  }
  if (a == 198 && (b == 18 || b == 19)) {
    return false;
  }
  if (a == 198 && b == 51 && c == 100) {
    return false;
  }
  if (a == 203 && b == 0 && c == 113) {
    return false;
  }
  return true;
}

bool hostLooksPrivateName(const QString &host) {
  static const QStringList privateSuffixes = {
      QStringLiteral(".localhost"), QStringLiteral(".local"),
      QStringLiteral(".lan"),       QStringLiteral(".home"),
      QStringLiteral(".internal"),  QStringLiteral(".corp"),
  };

  if (host == QStringLiteral("localhost")) {
    return true;
  }
  for (const QString &suffix : privateSuffixes) {
    if (host.endsWith(suffix)) {
      return true;
    }
  }
  return false;
}

bool hasExtension(const QString &path, const QStringList &extensions) {
  for (const QString &extension : extensions) {
    if (path.endsWith(extension)) {
      return true;
    }
  }
  return false;
}

QString normalizedPath(const QUrl &url) {
  return url.path(QUrl::FullyDecoded).toLower();
}

bool isDirectAudioUrl(const QUrl &url) {
  static const QStringList extensions = {
      QStringLiteral(".mp3"),  QStringLiteral(".wav"), QStringLiteral(".ogg"),
      QStringLiteral(".flac"), QStringLiteral(".m4a"), QStringLiteral(".opus"),
  };
  return hasExtension(normalizedPath(url), extensions);
}

bool isDirectVideoUrl(const QUrl &url) {
  static const QStringList extensions = {
      QStringLiteral(".mp4"), QStringLiteral(".webm"), QStringLiteral(".mov"),
      QStringLiteral(".m4v"), QStringLiteral(".ogv"),
  };
  return hasExtension(normalizedPath(url), extensions);
}

bool isUnsupportedDirectAsset(const QUrl &url) {
  static const QStringList extensions = {
      QStringLiteral(".svg"), QStringLiteral(".pdf"), QStringLiteral(".zip"),
      QStringLiteral(".7z"),  QStringLiteral(".rar"), QStringLiteral(".gz"),
      QStringLiteral(".tgz"), QStringLiteral(".bz2"), QStringLiteral(".xz"),
      QStringLiteral(".exe"), QStringLiteral(".msi"), QStringLiteral(".dmg"),
      QStringLiteral(".iso"), QStringLiteral(".deb"), QStringLiteral(".rpm"),
  };
  return hasExtension(normalizedPath(url), extensions);
}

bool isXStatusUrl(const QString &host, const QUrl &url) {
  if (host != QStringLiteral("x.com") &&
      host != QStringLiteral("twitter.com")) {
    return false;
  }

  static const QRegularExpression statusPattern(QStringLiteral(
      R"(^/(?:i/web/status|[^/]+/status(?:es)?)/[0-9]+(?:/|$))"));
  return statusPattern.match(normalizedPath(url)).hasMatch();
}

bool isMastodonStatusUrl(const QUrl &url) {
  static const QRegularExpression actorStatusPattern(
      QStringLiteral(R"(^/@[^/]+/[0-9]+(?:/|$))"));
  static const QRegularExpression usersStatusPattern(
      QStringLiteral(R"(^/users/[^/]+/statuses/[0-9]+(?:/|$))"));

  const QString path = normalizedPath(url);
  return actorStatusPattern.match(path).hasMatch() ||
         usersStatusPattern.match(path).hasMatch();
}

LinkPreviewCandidate makeCandidate(LinkPreviewKind kind, const QUrl &url,
                                   QString serviceName) {
  LinkPreviewCandidate candidate;
  candidate.kind = kind;
  candidate.originalUrl = url;
  candidate.fetchUrl = url;
  candidate.serviceName = std::move(serviceName);
  candidate.normalizedHost = normalizedHost(url);
  return candidate;
}

// x.com / twitter.com ship a JS shell with no OpenGraph tags, so fetching them
// directly yields an empty card. FixTweet (fxtwitter.com) serves proper OG meta
// for tweet embeds — rewrite the *fetch* URL to it. Display/click-through still
// use the original x.com URL (candidate.originalUrl).
QUrl xPostFetchUrl(const QUrl &url) {
  const QString path = normalizedPath(url);
  static const QRegularExpression iWebStatus(
      QStringLiteral(R"(^/i/web/status/([0-9]+))"));
  const QRegularExpressionMatch iWeb = iWebStatus.match(path);
  if (iWeb.hasMatch()) {
    return QUrl(
        QStringLiteral("https://fxtwitter.com/i/status/%1").arg(iWeb.captured(1)));
  }
  static const QRegularExpression userStatus(
      QStringLiteral(R"(^/([^/]+)/status(?:es)?/([0-9]+))"));
  const QRegularExpressionMatch user = userStatus.match(path);
  if (user.hasMatch()) {
    return QUrl(QStringLiteral("https://fxtwitter.com/%1/status/%2")
                   .arg(user.captured(1), user.captured(2)));
  }
  return url;
}

} // namespace

bool LinkPreviewCandidate::isPreviewable() const {
  return kind != LinkPreviewKind::None && fetchUrl.isValid();
}

bool LinkPreviewCandidate::needsHtmlFetch() const {
  return kind == LinkPreviewKind::OpenGraph || kind == LinkPreviewKind::XPost ||
         kind == LinkPreviewKind::MastodonPost;
}

bool LinkPreviewCandidate::needsImageFetch() const {
  return kind == LinkPreviewKind::DirectImage;
}

bool LinkPreviewCandidate::needsMediaPlayer() const {
  return kind == LinkPreviewKind::DirectAudio ||
         kind == LinkPreviewKind::DirectVideo;
}

bool isAllowedPreviewFetchUrl(const QUrl &url) {
  const QString scheme = url.scheme().toLower();
  if (!url.isValid() ||
      (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
    return false;
  }
  if (!url.userInfo().isEmpty()) {
    return false;
  }

  const QString host = normalizedHost(url);
  if (host.isEmpty() || hostLooksPrivateName(host)) {
    return false;
  }
  if (host.contains(QLatin1Char(':'))) {
    return false;
  }

  Ipv4Octets octets = {0, 0, 0, 0};
  if (parseIpv4Literal(host, octets)) {
    return isPublicIpv4Literal(octets);
  }

  return host.contains(QLatin1Char('.'));
}

bool isDirectRasterImageUrl(const QUrl &url) {
  static const QStringList extensions = {
      QStringLiteral(".jpg"), QStringLiteral(".jpeg"), QStringLiteral(".png"),
      QStringLiteral(".gif"), QStringLiteral(".webp"), QStringLiteral(".bmp"),
  };
  return hasExtension(normalizedPath(url), extensions);
}

LinkPreviewCandidate classifyLinkPreview(const QString &urlText) {
  QString text = urlText.trimmed();
  if (text.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)) {
    text.prepend(QStringLiteral("https://"));
  }
  return classifyLinkPreview(QUrl(text, QUrl::TolerantMode));
}

LinkPreviewCandidate classifyLinkPreview(const QUrl &url) {
  if (!isAllowedPreviewFetchUrl(url)) {
    return {};
  }

  if (isDirectRasterImageUrl(url)) {
    return makeCandidate(LinkPreviewKind::DirectImage, url,
                         QStringLiteral("Image"));
  }
  if (isDirectAudioUrl(url)) {
    return makeCandidate(LinkPreviewKind::DirectAudio, url,
                         QStringLiteral("Audio"));
  }
  if (isDirectVideoUrl(url)) {
    return makeCandidate(LinkPreviewKind::DirectVideo, url,
                         QStringLiteral("Video"));
  }
  if (isUnsupportedDirectAsset(url)) {
    return {};
  }

  const QString host = normalizedHost(url);
  if (isXStatusUrl(host, url)) {
    return makeCandidate(LinkPreviewKind::XPost, url,
                         QStringLiteral("X / Twitter"));
  }
  if (isMastodonStatusUrl(url)) {
    return makeCandidate(LinkPreviewKind::MastodonPost, url,
                         QStringLiteral("Mastodon"));
  }

  return makeCandidate(LinkPreviewKind::OpenGraph, url,
                       QStringLiteral("Website"));
}

} // namespace maxchat::services
