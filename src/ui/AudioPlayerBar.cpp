#include "ui/AudioPlayerBar.h"

#include <QAudioOutput>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QSlider>
#include <QToolButton>

namespace maxchat::ui {

AudioPlayerBar::AudioPlayerBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("audioPlayerBar"));
    player_ = new QMediaPlayer(this);
    output_ = new QAudioOutput(this);
    player_->setAudioOutput(output_);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 3, 6, 3);

    playPause_ = new QToolButton(this);
    playPause_->setObjectName(QStringLiteral("audioPlayPause"));
    playPause_->setText(QStringLiteral("Pause"));
    playPause_->setCheckable(true);
    playPause_->setChecked(true);
    layout->addWidget(playPause_);

    nameLabel_ = new QLabel(this);
    nameLabel_->setObjectName(QStringLiteral("audioName"));
    // The filename is attacker-controlled (percent-decoded from the URL);
    // AutoText would render embedded HTML.
    nameLabel_->setTextFormat(Qt::PlainText);
    layout->addWidget(nameLabel_);

    seek_ = new QSlider(Qt::Horizontal, this);
    seek_->setObjectName(QStringLiteral("audioSeek"));
    layout->addWidget(seek_, 1);

    timeLabel_ = new QLabel(QStringLiteral("0:00 / 0:00"), this);
    timeLabel_->setObjectName(QStringLiteral("audioTime"));
    layout->addWidget(timeLabel_);

    auto* close = new QToolButton(this);
    close->setObjectName(QStringLiteral("audioClose"));
    close->setText(QStringLiteral("X"));
    layout->addWidget(close);

    connect(playPause_, &QToolButton::toggled, this, [this](const bool playing) {
        playPause_->setText(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
        if (playing) {
            player_->play();
        } else {
            player_->pause();
        }
    });
    connect(close, &QToolButton::clicked, this, &AudioPlayerBar::stopAndHide);
    connect(player_, &QMediaPlayer::durationChanged, this, [this](const qint64 duration) {
        seek_->setRange(0, static_cast<int>(duration));
        refreshTimeLabel();
    });
    connect(player_, &QMediaPlayer::positionChanged, this, [this](const qint64 position) {
        if (!seeking_) {
            seek_->setValue(static_cast<int>(position));
        }
        refreshTimeLabel();
    });
    connect(seek_, &QSlider::sliderPressed, this, [this]() { seeking_ = true; });
    connect(seek_, &QSlider::sliderReleased, this, [this]() {
        seeking_ = false;
        player_->setPosition(seek_->value());
    });
    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& errorString) {
                nameLabel_->setText(QStringLiteral("Playback error: %1").arg(errorString));
            });
    // Keep the Play/Pause button truthful: track end, errors, or backend
    // stalls used to leave it stuck on "Pause" while nothing played.
    connect(player_, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                const bool playing = state == QMediaPlayer::PlayingState;
                const QSignalBlocker blocker(playPause_);
                playPause_->setChecked(playing);
                playPause_->setText(playing ? QStringLiteral("Pause")
                                            : QStringLiteral("Play"));
            });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::EndOfMedia) {
                    player_->setPosition(0); // replay from the top on next Play
                }
            });

    hide();
}

void AudioPlayerBar::playUrl(const QUrl& url) {
    nameLabel_->setText(url.fileName().isEmpty() ? url.toDisplayString() : url.fileName());
    player_->setSource(url);
    {
        const QSignalBlocker blocker(playPause_);
        playPause_->setChecked(true);
        playPause_->setText(QStringLiteral("Pause"));
    }
    show();
    player_->play();
}

void AudioPlayerBar::stopAndHide() {
    player_->stop();
    player_->setSource(QUrl());
    hide();
}

QString AudioPlayerBar::formatTime(const qint64 milliseconds) {
    const qint64 totalSeconds = milliseconds / 1000;
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

void AudioPlayerBar::refreshTimeLabel() {
    timeLabel_->setText(QStringLiteral("%1 / %2").arg(formatTime(player_->position()),
                                                      formatTime(player_->duration())));
}

} // namespace maxchat::ui
