#include "ui/MediaController.h"
#include "core/SettingsStore.h"

#include "services/LinkPreviewClassifier.h"
#include "services/PublicHttpFetch.h"
#include "ui/AudioPlayerBar.h"
#include "ui/ImageViewerDialog.h"
#include "ui/MainWindowHost.h"
#include "ui/MediaPlayerDialog.h"
#include "upload/ImageUploader.h"
#include "upload/ImageUploaderFactory.h"

#include <QDesktopServices>
#include <QImage>
#include <QUrl>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>

namespace maxchat::ui {

MediaController::MediaController(MainWindowHost& host, QObject* parent)
    : QObject(parent), host_(host) {}

MediaController::~MediaController() = default;

void MediaController::configure(const QVariantMap& settings) {
    uploader_ = maxchat::upload::makeImageUploader(settings, &host_.previewNetworkManager(), this);
    openLinks_ = settings.value(QStringLiteral("open_links_in_browser"), true).toBool();
    linkToggles_ = maxchat::services::linkPreviewTogglesFromSettings(settings);
    if (!openLinks_ || !linkToggles_.media) {
        ++mediaRequest_;
        if (pendingMedia_) { delete pendingMedia_; host_.clearStatus(); }
        pendingMedia_ = nullptr;
    }
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
    // Master switch: when off, a clicked link does nothing at all.
    if (!openLinks_) {
        return;
    }

    using maxchat::services::LinkPreviewKind;
    const auto candidate = maxchat::services::classifyLinkPreview(url);
    switch (candidate.kind) {
    case LinkPreviewKind::DirectImage: {
        // Only open the in-app viewer when image handling is enabled; otherwise
        // fall through and let the OS browser open the link.
        if (linkToggles_.images) {
            auto* viewer = new ImageViewerDialog(
                candidate.fetchUrl.isValid() ? candidate.fetchUrl : url, host_.dialogParent());
            viewer->show();
            return;
        }
        break;
    }
    case LinkPreviewKind::DirectAudio: {
        // Off → open externally; null bar → open externally (unchanged).
        if (!linkToggles_.media || audioBar_ == nullptr) {
            break;
        }
        fetchMedia(candidate.fetchUrl.isValid() ? candidate.fetchUrl : url, false);
        return;
    }
    case LinkPreviewKind::DirectVideo: {
        if (!linkToggles_.media) {
            break; // open externally
        }
        fetchMedia(candidate.fetchUrl.isValid() ? candidate.fetchUrl : url, true);
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

void MediaController::fetchMedia(const QUrl& url, bool video) {
    const quint64 request = ++mediaRequest_;
    if (pendingMedia_) delete pendingMedia_;
    host_.showStatus(QStringLiteral("Downloading media…"));
    maxchat::services::PublicHttpOptions options;
    options.maxBytes = 25 * 1024 * 1024;
    options.timeoutMs = 30000;
    options.accept = "audio/*,video/*,application/octet-stream;q=0.5";
    pendingMedia_ = maxchat::services::fetchPublicHttp(&host_.previewNetworkManager(), url, options, this,
        [this, request, url, video](maxchat::services::PublicHttpResult result) {
            if (request != mediaRequest_ || !openLinks_ || !linkToggles_.media) return;
            pendingMedia_ = nullptr;
            host_.clearStatus();
            if (!result.error.isEmpty()) {
                host_.appendActiveSystemLine(QStringLiteral("! Media download failed: %1. Inline files are limited to 25 MiB.").arg(result.error));
                return;
            }
            // Reject playlist/script responses. Playback receives only a local
            // media file; the multimedia backend never owns a remote URL.
            const QByteArray& bytes = result.body;
            const bool knownContainer = bytes.startsWith("ID3") || bytes.startsWith("OggS") ||
                bytes.startsWith("fLaC") || bytes.startsWith(QByteArray::fromHex("1a45dfa3")) ||
                (bytes.size() >= 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WAVE") ||
                (bytes.size() >= 12 && bytes.mid(4, 4) == "ftyp") ||
                (bytes.size() >= 2 && uchar(bytes[0]) == 0xff && (uchar(bytes[1]) & 0xe0) == 0xe0);
            if (!knownContainer) {
                host_.appendActiveSystemLine(QStringLiteral("! Unsupported media response.")); return;
            }
            const QString cache = QDir(maxchat::core::standardSettingsPaths().cacheDir)
                                      .filePath(QStringLiteral("media"));
            if (!QDir().mkpath(cache)) { host_.appendActiveSystemLine(QStringLiteral("! Could not prepare the media cache.")); return; }
            auto* file = new QTemporaryFile(QDir(cache).filePath(QStringLiteral("clip-XXXXXX.media")), this);
            if (!file->open() || file->write(bytes) != bytes.size() || !file->flush()) {
                delete file; host_.appendActiveSystemLine(QStringLiteral("! Could not save the media preview.")); return;
            }
            const QUrl local = QUrl::fromLocalFile(file->fileName());
            file->close();
            if (video) {
                if (mediaPlayer_) mediaPlayer_->close();
                auto* player = new MediaPlayerDialog(local, host_.dialogParent());
                player->setWindowTitle(url.fileName());
                file->setParent(player);
                mediaPlayer_ = player;
                player->show();
            } else if (audioBar_) {
                audioBar_->stopAndHide();
                if (audioFile_) audioFile_->deleteLater();
                audioFile_ = file;
                audioBar_->playUrl(local, url.fileName());
            } else { delete file; }
        });
}

} // namespace maxchat::ui
