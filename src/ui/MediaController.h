#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include "services/LinkPreviewPolicy.h"

#include <memory>

class QImage;
class QUrl;

namespace maxchat::upload {
class ImageUploader;
}

namespace maxchat::ui {

class AudioPlayerBar;
class MediaPlayerDialog;
class MainWindowHost;

// Inline-media + image-upload responsibilities lifted out of MainWindow (decomp
// phase 2). Owns the image uploader and the single reusable video player, and
// handles clicked chat links that resolve to images/audio/video (everything
// else falls through to the OS after a scheme allow-list check). The audio bar
// is a chat-column child widget owned by the window; the controller only drives
// it. The render-bound link-PREVIEW fetch/cache stays in MainWindow for now —
// it's interleaved with the chat-render pipeline (see MAINWINDOW.MD phase 2).
class MediaController final : public QObject {
    Q_OBJECT

  public:
    explicit MediaController(MainWindowHost& host, QObject* parent = nullptr);
    ~MediaController() override;

    // Rebuild the uploader from settings (service/key/etc). nullptr → uploads off.
    void configure(const QVariantMap& settings);
    [[nodiscard]] bool isConfigured() const;

    // The window wires its chat-column audio bar in once it's constructed.
    void setAudioBar(AudioPlayerBar* audioBar);

    // Upload an image (drag/drop or paste); inserts the resulting URL into input.
    void uploadImage(const QImage& image);

    // A chat anchor was clicked: open inline (image viewer / audio bar / video
    // player) or hand off to the OS for web/mail schemes.
    void handleAnchorClicked(const QUrl& url);

  private:
    MainWindowHost& host_;
    std::unique_ptr<maxchat::upload::ImageUploader> uploader_;
    QPointer<MediaPlayerDialog> mediaPlayer_; // one at a time
    QPointer<AudioPlayerBar> audioBar_;       // owned by the window
    // Click-handling policy, refreshed in configure(). openLinks_ off → clicked
    // links do nothing; the per-type toggles route image/audio/video links to the
    // in-app viewers vs. the OS browser (same flags the preview fetcher uses).
    bool openLinks_ = true;
    maxchat::services::LinkPreviewToggles linkToggles_;
};

} // namespace maxchat::ui
