#include "services/LinkPreviewRenderer.h"

namespace maxchat::services {
namespace {

QString htmlUrl(const QUrl &url) {
  return url.toString(QUrl::FullyEncoded).toHtmlEscaped();
}

QString cleanedText(QString text, int maxChars) {
  text = text.simplified();
  if (maxChars > 3 && text.size() > maxChars) {
    return text.left(maxChars - 3).trimmed() + QStringLiteral("...");
  }
  return text;
}

QString imageStyle(const LinkPreviewRenderOptions &options) {
  return QStringLiteral(
             "max-width:%1px;max-height:%2px;border:0;margin-top:4px;")
      .arg(options.maxImageWidth)
      .arg(options.maxImageHeight);
}

QString mediaLabel(const LinkPreviewCandidate &candidate) {
  if (candidate.kind == LinkPreviewKind::DirectAudio) {
    return QStringLiteral("Audio");
  }
  if (candidate.kind == LinkPreviewKind::DirectVideo) {
    return QStringLiteral("Video");
  }
  return QStringLiteral("Media");
}

QString mediaName(const QUrl &url) {
  const QString fileName = url.fileName(QUrl::FullyDecoded).trimmed();
  if (!fileName.isEmpty()) {
    return fileName;
  }
  return primaryDomainForPreview(url);
}

} // namespace

QString primaryDomainForPreview(const QUrl &url) {
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

QString renderOpenGraphPreviewHtml(const LinkPreviewCandidate &candidate,
                                   const OpenGraphCard &card,
                                   LinkPreviewRenderOptions options) {
  if (card.isEmpty()) {
    return {};
  }

  // For X posts the fetch URL is the fxtwitter proxy; the click-through must be
  // the original x.com link the user typed, not the proxy or its canonical.
  const QUrl targetUrl =
      candidate.kind == LinkPreviewKind::XPost
          ? candidate.originalUrl
          : (card.canonicalUrl.isValid() ? card.canonicalUrl : candidate.fetchUrl);
  if (!targetUrl.isValid()) {
    return {};
  }

  QString title = cleanedText(card.title, options.maxTitleChars);
  if (title.isEmpty()) {
    title = !candidate.serviceName.isEmpty()
                ? candidate.serviceName
                : primaryDomainForPreview(targetUrl);
  }
  const QString description =
      cleanedText(card.description, options.maxDescriptionChars);
  const QString domain = !card.siteName.isEmpty()
                             ? cleanedText(card.siteName, 80)
                             : primaryDomainForPreview(targetUrl);

  // Card box: site name + title + description.
  QString html = QStringLiteral(
      "<div style=\"margin:4px 0 2px 0;padding:6px 8px;border-left:3px solid "
      "#6f8cff;background:rgba(255,255,255,0.06);\">");
  if (options.showSiteName && !domain.isEmpty()) {
    html += QStringLiteral(
                "<div style=\"font-size:small;opacity:0.7;\">%1</div>")
                .arg(domain.toHtmlEscaped());
  }
  if (options.showTitle) {
    html += QStringLiteral("<div style=\"margin-top:1px;\"><a href=\"%1\"><b>%2</b></a></div>")
                .arg(htmlUrl(targetUrl), title.toHtmlEscaped());
  }
  const bool showDesc =
      options.showDescription &&
      !description.isEmpty() &&
      description.compare(title, Qt::CaseInsensitive) != 0;
  if (showDesc) {
    html += QStringLiteral("<div style=\"margin-top:2px;\">%1</div>")
                .arg(description.toHtmlEscaped());
  }
  html += QStringLiteral("</div>");

  // Photo below the card, not inside it.
  if (options.showImage && card.imageUrl.isValid()) {
    html += QStringLiteral(
                "<div style=\"margin:0 0 6px 0;\"><a href=\"%1\">"
                "<img src=\"%2\" style=\"%3\"></a></div>")
                .arg(htmlUrl(targetUrl), htmlUrl(card.imageUrl),
                     imageStyle(options));
  }
  return html;
}

QString renderDirectImagePreviewHtml(const LinkPreviewCandidate &candidate,
                                     LinkPreviewRenderOptions options) {
  if (!candidate.fetchUrl.isValid()) {
    return {};
  }
  return QStringLiteral("<div style=\"margin:4px 0 6px 0;\"><a "
                        "href=\"%1\"><img src=\"%1\" "
                        "style=\"%2\"></a></div>")
      .arg(htmlUrl(candidate.fetchUrl), imageStyle(options));
}

QString renderDirectMediaPreviewHtml(const LinkPreviewCandidate &candidate) {
  if (!candidate.fetchUrl.isValid()) {
    return {};
  }
  if (candidate.kind != LinkPreviewKind::DirectAudio &&
      candidate.kind != LinkPreviewKind::DirectVideo) {
    return {};
  }

  return QStringLiteral(
             "<div style=\"margin:4px 0 6px 0;padding:5px 8px;border-left:3px "
             "solid #6f8cff;background:rgba(255,255,255,0.05);\">"
             "<a href=\"%1\"><b>%2</b>: %3</a>"
             "<div style=\"margin-top:2px;font-size:small;opacity:0.78;\">"
             "%4</div></div>")
      .arg(htmlUrl(candidate.fetchUrl), mediaLabel(candidate).toHtmlEscaped(),
           mediaName(candidate.fetchUrl).toHtmlEscaped(),
           primaryDomainForPreview(candidate.fetchUrl).toHtmlEscaped());
}

} // namespace maxchat::services
