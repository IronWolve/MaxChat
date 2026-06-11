#pragma once

#include "services/LinkPreviewClassifier.h"
#include "services/OpenGraphParser.h"

#include <QString>

namespace maxchat::services {

struct LinkPreviewRenderOptions {
  int maxTitleChars = 120;
  int maxDescriptionChars = 160;
  int maxImageWidth = 320;
  int maxImageHeight = 240;
  bool showSiteName    = true;
  bool showTitle       = true;
  bool showDescription = true;
  bool showImage       = true;
};

[[nodiscard]] QString primaryDomainForPreview(const QUrl &url);
[[nodiscard]] QString
renderOpenGraphPreviewHtml(const LinkPreviewCandidate &candidate,
                           const OpenGraphCard &card,
                           LinkPreviewRenderOptions options = {});
[[nodiscard]] QString
renderDirectImagePreviewHtml(const LinkPreviewCandidate &candidate,
                             LinkPreviewRenderOptions options = {});
[[nodiscard]] QString
renderDirectMediaPreviewHtml(const LinkPreviewCandidate &candidate);

} // namespace maxchat::services
