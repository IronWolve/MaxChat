#pragma once

#include <QDialog>
#include <QUrl>

class QAudioOutput;
class QMediaPlayer;
class QSlider;
class QToolButton;

namespace maxchat::ui {

// Simple video player for direct video links previewed in chat.
class MediaPlayerDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit MediaPlayerDialog(const QUrl& mediaUrl, QWidget* parent = nullptr);

  private:
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* output_ = nullptr;
    QToolButton* playPause_ = nullptr;
    QSlider* seek_ = nullptr;
    bool seeking_ = false;
};

} // namespace maxchat::ui
