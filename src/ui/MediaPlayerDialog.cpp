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
    connect(player_, &QMediaPlayer::durationChanged, this, [this](const qint64 duration) {
        seek_->setRange(0, static_cast<int>(duration));
    });
    connect(player_, &QMediaPlayer::positionChanged, this, [this](const qint64 position) {
        if (!seeking_) {
            seek_->setValue(static_cast<int>(position));
        }
    });
    connect(seek_, &QSlider::sliderPressed, this, [this]() { seeking_ = true; });
    connect(seek_, &QSlider::sliderReleased, this, [this]() {
        seeking_ = false;
        player_->setPosition(seek_->value());
    });

    auto* closeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(closeShortcut, &QShortcut::activated, this, &QDialog::close);

    resize(720, 460);
    player_->setSource(mediaUrl);
    player_->play();
}

} // namespace maxchat::ui
