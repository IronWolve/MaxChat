#pragma once

#include <QUrl>
#include <QWidget>

class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QSlider;
class QToolButton;

namespace maxchat::ui {

// Inline audio transport under the chat: appears when an audio link is
// clicked, plays it with play/pause + seek, and hides on close.
class AudioPlayerBar final : public QWidget {
    Q_OBJECT

  public:
    explicit AudioPlayerBar(QWidget* parent = nullptr);

    void playUrl(const QUrl& url);
    void stopAndHide();

  private:
    static QString formatTime(qint64 milliseconds);
    void refreshTimeLabel();

    QMediaPlayer* player_ = nullptr;
    QAudioOutput* output_ = nullptr;
    QToolButton* playPause_ = nullptr;
    QLabel* nameLabel_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QSlider* seek_ = nullptr;
    bool seeking_ = false;
};

} // namespace maxchat::ui
