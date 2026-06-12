#pragma once

#include <QDialog>
#include <QUrl>

class QLabel;
class QNetworkAccessManager;
class QScrollArea;

namespace maxchat::services {
class ImageFetcher;
}

namespace maxchat::ui {

// Full-size viewer for an image link previewed in chat. Fetches through
// services::ImageFetcher so it gets the same SSRF gate, per-redirect
// re-vetting, timeout, and download cap as the inline previews.
class ImageViewerDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit ImageViewerDialog(const QUrl& imageUrl, QWidget* parent = nullptr);

  private:
    void startFetch(const QUrl& imageUrl);

    QLabel* imageLabel_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QNetworkAccessManager* network_ = nullptr;
    maxchat::services::ImageFetcher* fetcher_ = nullptr;
};

} // namespace maxchat::ui
