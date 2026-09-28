#pragma once

#include "services/LinkPreviewClassifier.h"

#include <QVariantMap>

namespace maxchat::services {

struct LinkPreviewToggles {
  bool images = false;
  bool media = false;
  bool xCards = false;
  bool webCards = false;
};

[[nodiscard]] LinkPreviewToggles
linkPreviewTogglesFromServices(const QVariantMap &services);
[[nodiscard]] LinkPreviewToggles
linkPreviewTogglesFromSettings(const QVariantMap &settings);
[[nodiscard]] bool isLinkPreviewEnabled(const LinkPreviewCandidate &candidate,
                                        const LinkPreviewToggles &toggles);

} // namespace maxchat::services
