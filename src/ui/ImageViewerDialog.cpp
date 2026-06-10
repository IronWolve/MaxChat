#include "ui/ImageViewerDialog.h"

#include "services/LinkPreviewClassifier.h"

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
    imageLabel_ = new QLabel(QStringLiteral("Loading image..."), this);
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

    QNetworkRequest request(imageUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(4);
    QNetworkReply* reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            imageLabel_->setText(
                QStringLiteral("Could not load image: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray payload = reply->read(MaxImageBytes);
        QPixmap pixmap;
        if (!pixmap.loadFromData(payload)) {
            imageLabel_->setText(QStringLiteral("The downloaded data is not a usable image."));
            return;
        }

        QSize available(900, 700);
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            available = screen->availableSize() * 0.85;
        }
        if (pixmap.width() > available.width() || pixmap.height() > available.height()) {
            pixmap = pixmap.scaled(available, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        imageLabel_->setPixmap(pixmap);
        imageLabel_->adjustSize();
        resize(qMin(pixmap.width() + 24, available.width()),
               qMin(pixmap.height() + 24, available.height()));
    });
}

} // namespace maxchat::ui
