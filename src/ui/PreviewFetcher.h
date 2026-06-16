#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include "services/LinkPreviewClassifier.h" // LinkPreviewCandidate
#include "services/LinkPreviewPolicy.h"     // LinkPreviewToggles
#include "services/LinkPreviewRenderer.h"   // LinkPreviewRenderOptions
#include "services/OpenGraphFetcher.h"      // OpenGraphFetcher, OpenGraphCard

class QNetworkAccessManager;
class QUrl;

namespace maxchat::ui {

// Outputs from PreviewFetcher back up to its owner (MainWindow). Narrow seam:
// where to put a rendered card, plus the origin context a queued line carries.
class PreviewFetcherDelegate {
  public:
    virtual ~PreviewFetcherDelegate() = default;
    // Direct image/media card → the buffer currently being appended to.
    virtual void appendPreviewHtmlToActiveBuffer(const QString& html) = 0;
    // Async OG/X/Mastodon card → the buffer that originally posted the URL.
    virtual void appendPreviewHtmlToBuffer(const QString& network, const QString& target,
                                           const QString& html) = 0;
    [[nodiscard]] virtual QString previewOriginNetwork() = 0; // currentLogNetwork()
    [[nodiscard]] virtual QString previewOriginTarget() = 0;  // current target
    [[nodiscard]] virtual bool isReplayingLog() = 0;          // suppress during replay
};

// Owns the link-preview *fetch orchestration* extracted from MainWindow (decomp
// Phase 2b, the follow-on to render-pipeline R3). For each previewable URL in a
// chat line it renders direct image/media cards inline, and fetches OpenGraph/
// X/Mastodon cards asynchronously, routing each back to its origin buffer.
//
// ChatPane owns the *rendered* preview cache + the inline image fetch (R3); this
// owns the *card* fetch. The two share MainWindow's preview QNetworkAccessManager
// (passed in by reference).
class PreviewFetcher : public QObject {
    Q_OBJECT

  public:
    PreviewFetcher(PreviewFetcherDelegate& delegate, QNetworkAccessManager& networkManager,
                   QObject* parent = nullptr);

    // Live settings (set from MainWindow::applyCurrentSettings, and by tests).
    [[nodiscard]] maxchat::services::LinkPreviewToggles& toggles() { return toggles_; }
    [[nodiscard]] maxchat::services::LinkPreviewRenderOptions& renderOptions() {
        return renderOptions_;
    }

    // Scan a (plain) chat line and render/queue previews for previewable URLs.
    void queueFromLine(const QString& line);

  private:
    void onCardFetched(const QUrl& url, const maxchat::services::OpenGraphCard& card);
    void onFetchFailed(const QUrl& url, const QString& reason);

    PreviewFetcherDelegate& delegate_;
    maxchat::services::OpenGraphFetcher fetcher_;
    maxchat::services::LinkPreviewToggles toggles_;
    maxchat::services::LinkPreviewRenderOptions renderOptions_;
    QHash<QString, maxchat::services::LinkPreviewCandidate> pending_; // url key -> candidate
};

} // namespace maxchat::ui
