#include "ui/MediaController.h"

#include "services/LinkPreviewClassifier.h"
#include "ui/AudioPlayerBar.h"
#include "ui/ImageViewerDialog.h"
#include "ui/MainWindowHost.h"
#include "ui/MediaPlayerDialog.h"
#include "upload/ImageUploader.h"
#include "upload/ImageUploaderFactory.h"

#include <QDesktopServices>
#include <QImage>
#include <QUrl>

namespace maxchat::ui {

MediaController::MediaController(MainWindowHost& host, QObject* parent)
    : QObject(parent), host_(host) {}

MediaController::~MediaController() = default;

void MediaController::configure(const QVariantMap& settings) {
    uploader_ = maxchat::upload::makeImageUploader(settings, &host_.previewNetworkManager(), this);
}

bool MediaController::isConfigured() const {
    return uploader_ != nullptr;
}

void MediaController::setAudioBar(AudioPlayerBar* audioBar) {
    audioBar_ = audioBar;
}

void MediaController::uploadImage(const QImage& image) {
    if (uploader_ == nullptr) {
        host_.appendActiveSystemLine(QStringLiteral(
            "! Image paste ignored: no image hosting service is configured "
            "(Preferences > Image Hosting)."));
        return;
    }
    if (image.isNull()) {
        return;
    }
    host_.showStatus(QStringLiteral("Uploading image…"));
    // Reconnect fresh each call so there's only one active upload at a time.
    disconnect(uploader_.get(), nullptr, this, nullptr);
    connect(uploader_.get(), &maxchat::upload::ImageUploader::uploaded, this,
            [this](const QString& url) {
                host_.clearStatus();
                host_.appendInputUrl(url);
            });
    connect(uploader_.get(), &maxchat::upload::ImageUploader::uploadFailed, this,
            [this](const QString& reason) {
                host_.showStatus(QStringLiteral("Image upload failed: ") + reason, 6000);
            });
    uploader_->upload(image);
}

void MediaController::handleAnchorClicked(const QUrl& url) {
    if (!url.isValid()) {
        return;
    }

    using maxchat::services::LinkPreviewKind;
    const auto candidate = maxchat::services::classifyLinkPreview(url);
    switch (candidate.kind) {
    case LinkPreviewKind::DirectImage: {
        auto* viewer = new ImageViewerDialog(
            candidate.fetchUrl.isValid() ? candidate.fetchUrl : url, host_.dialogParent());
        viewer->show();
        return;
    }
    case LinkPreviewKind::DirectAudio: {
        if (audioBar_ == nullptr) {
            break;
        }
        // Same SSRF gate as previews: the static check can't catch a public
        // hostname that resolves to a LAN/metadata address, and the media
        // backend follows redirects we can't intercept.
        const QUrl mediaUrl = candidate.fetchUrl.isValid() ? candidate.fetchUrl : url;
        maxchat::services::resolvePreviewUrlPublicAsync(
            mediaUrl, /*allowPrivateNetwork=*/false, this, [this, mediaUrl](bool allowed) {
                if (allowed && audioBar_ != nullptr) {
                    audioBar_->playUrl(mediaUrl);
                } else if (!allowed) {
                    host_.appendActiveSystemLine(
                        QStringLiteral("! Blocked audio URL (private address)."));
                }
            });
        return;
    }
    case LinkPreviewKind::DirectVideo: {
        const QUrl mediaUrl = candidate.fetchUrl.isValid() ? candidate.fetchUrl : url;
        maxchat::services::resolvePreviewUrlPublicAsync(
            mediaUrl, /*allowPrivateNetwork=*/false, this, [this, mediaUrl](bool allowed) {
                if (!allowed) {
                    host_.appendActiveSystemLine(
                        QStringLiteral("! Blocked video URL (private address)."));
                    return;
                }
                // One player at a time: clicking links repeatedly used to
                // stack dialogs all playing audio simultaneously.
                if (mediaPlayer_ != nullptr) {
                    mediaPlayer_->close();
                }
                auto* player = new MediaPlayerDialog(mediaUrl, host_.dialogParent());
                mediaPlayer_ = player;
                player->show();
            });
        return;
    }
    default:
        break;
    }
    // Defence in depth before handing a clicked link to the OS: only open web /
    // mail schemes. A crafted anchor (e.g. file:, javascript:, or an app handler)
    // must not reach QDesktopServices::openUrl from a chat message.
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https") &&
        scheme != QLatin1String("ftp") && scheme != QLatin1String("mailto")) {
        host_.appendActiveSystemLine(
            QStringLiteral("! Refused to open link with scheme \"%1\".").arg(scheme));
        return;
    }
    QDesktopServices::openUrl(url);
}

} // namespace maxchat::ui
