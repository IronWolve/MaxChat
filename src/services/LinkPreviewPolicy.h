#pragma once

#include "services/LinkPreviewClassifier.h"

#include <QVariantMap>

namespace maxchat::services {

struct LinkPreviewToggles {
  bool images = true;
  bool media = true;
  bool xCards = true;
  bool webCards = true;
};

[[nodiscard]] LinkPreviewToggles
linkPreviewTogglesFromServices(const QVariantMap &services);
[[nodiscard]] LinkPreviewToggles
linkPreviewTogglesFromSettings(const QVariantMap &settings);
[[nodiscard]] bool isLinkPreviewEnabled(const LinkPreviewCandidate &candidate,
                                        const LinkPreviewToggles &toggles);

} // namespace maxchat::services
