#include "services/LinkPreviewPolicy.h"

namespace maxchat::services {

namespace {

bool boolValue(const QVariantMap &map, const QString &key, bool fallback) {
  return map.contains(key) ? map.value(key).toBool() : fallback;
}

} // namespace

LinkPreviewToggles linkPreviewTogglesFromServices(const QVariantMap &services) {
  LinkPreviewToggles toggles;
  toggles.images = boolValue(services, QStringLiteral("images"), true);
  toggles.media = boolValue(services, QStringLiteral("media"), true);
  toggles.xCards = boolValue(services, QStringLiteral("xcards"), true);
  toggles.webCards = boolValue(services, QStringLiteral("webcards"), true);
  return toggles;
}

LinkPreviewToggles linkPreviewTogglesFromSettings(const QVariantMap &settings) {
  return linkPreviewTogglesFromServices(
      settings.value(QStringLiteral("content_services")).toMap());
}

bool isLinkPreviewEnabled(const LinkPreviewCandidate &candidate,
                          const LinkPreviewToggles &toggles) {
  switch (candidate.kind) {
  case LinkPreviewKind::DirectImage:
    return toggles.images;
  case LinkPreviewKind::DirectAudio:
  case LinkPreviewKind::DirectVideo:
    return toggles.media;
  case LinkPreviewKind::XPost:
    return toggles.xCards;
  case LinkPreviewKind::OpenGraph:
  case LinkPreviewKind::MastodonPost:
    return toggles.webCards;
  case LinkPreviewKind::None:
    return false;
  }
  return false;
}

} // namespace maxchat::services
