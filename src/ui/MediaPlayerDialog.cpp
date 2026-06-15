#include "ui/MediaPlayerDialog.h"

#include <QAudioOutput>
#include <QHBoxLayout>
#include <QMediaPlayer>
#include <QShortcut>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVideoWidget>

namespace maxchat::ui {

MediaPlayerDialog::MediaPlayerDialog(const QUrl& mediaUrl, QWidget* parent) : QDialog(parent) {
    setWindowTitle(mediaUrl.fileName().isEmpty() ? mediaUrl.toDisplayString()
                                                 : mediaUrl.fileName());
    setAttribute(Qt::WA_DeleteOnClose);

    player_ = new QMediaPlayer(this);
    output_ = new QAudioOutput(this);
    player_->setAudioOutput(output_);

    auto* root = new QVBoxLayout(this);
    auto* video = new QVideoWidget(this);
    player_->setVideoOutput(video);
    root->addWidget(video, 1);

    auto* controls = new QHBoxLayout();
    playPause_ = new QToolButton(this);
    playPause_->setObjectName(QStringLiteral("videoPlayPause"));
    playPause_->setText(QStringLiteral("Pause"));
    playPause_->setCheckable(true);
    playPause_->setChecked(true);
    controls->addWidget(playPause_);
    seek_ = new QSlider(Qt::Horizontal, this);
    controls->addWidget(seek_, 1);
    root->addLayout(controls);

    connect(playPause_, &QToolButton::toggled, this, [this](const bool playing) {
        playPause_->setText(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
        if (playing) {
            player_->play();
        } else {
            player_->pause();
        }
    });
    // Fixed 0..1000 fractional range: feeding raw millisecond positions into an
    // int slider overflows for media longer than ~24 days (INT_MAX ms).
    seek_->setRange(0, 1000);
    connect(player_, &QMediaPlayer::positionChanged, this, [this](const qint64 position) {
        const qint64 duration = player_->duration();
        if (!seeking_ && duration > 0) {
            seek_->setValue(static_cast<int>(position * 1000 / duration));
        }
    });
    connect(seek_, &QSlider::sliderPressed, this, [this]() { seeking_ = true; });
    connect(seek_, &QSlider::sliderReleased, this, [this]() {
        seeking_ = false;
        const qint64 duration = player_->duration();
        player_->setPosition(static_cast<qint64>(seek_->value()) * duration / 1000);
    });
    // A 404 / unsupported codec / dropped stream used to leave a silent black
    // dialog claiming it was playing.
    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& errorString) {
                setWindowTitle(QStringLiteral("Playback error: %1").arg(errorString));
                const QSignalBlocker blocker(playPause_);
                playPause_->setChecked(false);
                playPause_->setText(QStringLiteral("Play"));
            });
    connect(player_, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                const bool playing = state == QMediaPlayer::PlayingState;
                const QSignalBlocker blocker(playPause_);
                playPause_->setChecked(playing);
                playPause_->setText(playing ? QStringLiteral("Pause")
                                            : QStringLiteral("Play"));
            });

    auto* closeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(closeShortcut, &QShortcut::activated, this, &QDialog::close);

    resize(720, 460);
    player_->setSource(mediaUrl);
    player_->play();
}

} // namespace maxchat::ui
