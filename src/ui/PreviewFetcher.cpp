#include "ui/PreviewFetcher.h"

#include <QNetworkAccessManager>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "core/UrlDetector.h"                // maxchat::core::extractUrls
#include "services/LinkPreviewClassifier.h"  // classifyLinkPreview, isAllowedPreviewFetchUrl
#include "services/LinkPreviewPolicy.h"      // isLinkPreviewEnabled
#include "services/LinkPreviewRenderer.h"    // render*PreviewHtml

namespace maxchat::ui {

namespace {

// Stable cache/dedup key for a fetch URL (matches the old MainWindow helper).
[[nodiscard]] QString previewKey(const QUrl& url) {
    return url.toString(QUrl::FullyEncoded).toCaseFolded();
}

} // namespace

PreviewFetcher::PreviewFetcher(PreviewFetcherDelegate& delegate,
                               QNetworkAccessManager& networkManager, QObject* parent)
    : QObject(parent), delegate_(delegate), fetcher_(&networkManager) {
    connect(&fetcher_, &maxchat::services::OpenGraphFetcher::cardFetched, this,
            &PreviewFetcher::onCardFetched);
    connect(&fetcher_, &maxchat::services::OpenGraphFetcher::fetchFailed, this,
            &PreviewFetcher::onFetchFailed);
}

void PreviewFetcher::queueFromLine(const QString& line) {
    if (delegate_.isReplayingLog()) {
        return;
    }

    const QStringList urls = maxchat::core::extractUrls(line);
    for (const QString& urlText : urls) {
        const maxchat::services::LinkPreviewCandidate candidate =
            maxchat::services::classifyLinkPreview(urlText);
        if (!candidate.isPreviewable() ||
            !maxchat::services::isLinkPreviewEnabled(candidate, toggles_)) {
            continue;
        }

        switch (candidate.kind) {
        case maxchat::services::LinkPreviewKind::DirectImage:
            delegate_.appendPreviewHtmlToActiveBuffer(
                maxchat::services::renderDirectImagePreviewHtml(candidate));
            break;
        case maxchat::services::LinkPreviewKind::DirectAudio:
        case maxchat::services::LinkPreviewKind::DirectVideo:
            delegate_.appendPreviewHtmlToActiveBuffer(
                maxchat::services::renderDirectMediaPreviewHtml(candidate));
            break;
        case maxchat::services::LinkPreviewKind::OpenGraph:
        case maxchat::services::LinkPreviewKind::XPost:
        case maxchat::services::LinkPreviewKind::MastodonPost: {
            const QString key = previewKey(candidate.fetchUrl);
            if (pending_.contains(key)) {
                break;
            }
            maxchat::services::LinkPreviewCandidate tagged = candidate;
            tagged.originNetwork = delegate_.previewOriginNetwork();
            tagged.originTarget = delegate_.previewOriginTarget();
            pending_.insert(key, tagged);
            fetcher_.fetch(candidate.fetchUrl);
            break;
        }
        case maxchat::services::LinkPreviewKind::None:
            break;
        }
    }
}

void PreviewFetcher::onCardFetched(const QUrl& url,
                                   const maxchat::services::OpenGraphCard& card) {
    const QString key = previewKey(url);
    const auto iterator = pending_.find(key);
    if (iterator == pending_.end()) {
        return;
    }

    const maxchat::services::LinkPreviewCandidate candidate = iterator.value();
    pending_.erase(iterator);
    if (!maxchat::services::isLinkPreviewEnabled(candidate, toggles_)) {
        return;
    }

    maxchat::services::OpenGraphCard displayCard = card;
    if (displayCard.imageUrl.isValid() &&
        !maxchat::services::isAllowedPreviewFetchUrl(displayCard.imageUrl)) {
        displayCard.imageUrl = QUrl();
    }
    const QString html =
        maxchat::services::renderOpenGraphPreviewHtml(candidate, displayCard, renderOptions_);
    // Route to the buffer that posted the URL, not the currently-visible one.
    const QString net = candidate.originNetwork.isEmpty() ? delegate_.previewOriginNetwork()
                                                          : candidate.originNetwork;
    const QString tgt = candidate.originTarget.isEmpty() ? delegate_.previewOriginTarget()
                                                         : candidate.originTarget;
    delegate_.appendPreviewHtmlToBuffer(net, tgt, html);
}

void PreviewFetcher::onFetchFailed(const QUrl& url, const QString& reason) {
    Q_UNUSED(reason);
    pending_.remove(previewKey(url));
}

} // namespace maxchat::ui
