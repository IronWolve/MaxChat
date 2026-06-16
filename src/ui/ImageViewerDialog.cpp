#include "ui/ImageViewerDialog.h"

#include "services/ImageFetcher.h"
#include "services/LinkPreviewClassifier.h"

#include <QImage>

#include <QGuiApplication>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QVBoxLayout>

namespace maxchat::ui {

namespace {
constexpr qint64 MaxImageBytes = 25 * 1024 * 1024;
} // namespace

ImageViewerDialog::ImageViewerDialog(const QUrl& imageUrl, QWidget* parent)
    : QDialog(parent), network_(new QNetworkAccessManager(this)) {
    setWindowTitle(imageUrl.fileName().isEmpty() ? imageUrl.toDisplayString()
                                                 : imageUrl.fileName());
    setAttribute(Qt::WA_DeleteOnClose);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    imageLabel_ = new QLabel(tr("Loading image..."), this);
    imageLabel_->setAlignment(Qt::AlignCenter);
    scrollArea_->setWidget(imageLabel_);
    root->addWidget(scrollArea_);

    auto* closeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(closeShortcut, &QShortcut::activated, this, &QDialog::close);

    resize(640, 480);
    startFetch(imageUrl);
}

void ImageViewerDialog::startFetch(const QUrl& imageUrl) {
    if (!maxchat::services::isAllowedPreviewFetchUrl(imageUrl)) {
        imageLabel_->setText(QStringLiteral("This image URL is not allowed."));
        return;
    }

    // ImageFetcher brings the DNS-resolution SSRF gate, per-redirect
    // re-vetting, the 15s timeout, and an in-flight download cap — the old
    // bespoke fetch here had none of those.
    fetcher_ = new maxchat::services::ImageFetcher(network_, this);
    connect(fetcher_, &maxchat::services::ImageFetcher::imageFetched, this,
            [this](const QUrl&, const QImage& image) {
                QSize available(900, 700);
                if (QScreen* screen = QGuiApplication::primaryScreen()) {
                    available = screen->availableSize() * 0.85;
                }
                QPixmap pixmap = QPixmap::fromImage(image);
                if (pixmap.width() > available.width() ||
                    pixmap.height() > available.height()) {
                    pixmap = pixmap.scaled(available, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
                }
                imageLabel_->setPixmap(pixmap);
                imageLabel_->adjustSize();
                resize(qMin(pixmap.width() + 24, available.width()),
                       qMin(pixmap.height() + 24, available.height()));
            });
    connect(fetcher_, &maxchat::services::ImageFetcher::imageFetchFailed, this,
            [this](const QUrl&, const QString& reason) {
                imageLabel_->setText(QStringLiteral("Could not load image: %1").arg(reason));
            });
    maxchat::services::ImageFetchOptions options;
    options.maxBytes = MaxImageBytes;
    options.timeoutMs = 15000;
    options.maxWidth = 8192; // full-size viewer: don't pre-shrink
    options.maxHeight = 8192;
    fetcher_->fetch(imageUrl, options);
}

} // namespace maxchat::ui
