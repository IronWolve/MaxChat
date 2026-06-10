#pragma once

#include <QDialog>
#include <QUrl>

class QLabel;
class QNetworkAccessManager;
class QScrollArea;

namespace maxchat::ui {

// Full-size viewer for an image link previewed in chat. Downloads the image
// itself (size-capped) and scales it to fit the screen.
class ImageViewerDialog final : public QDialog {
    Q_OBJECT

  public:
    explicit ImageViewerDialog(const QUrl& imageUrl, QWidget* parent = nullptr);

  private:
    void startFetch(const QUrl& imageUrl);

    QLabel* imageLabel_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QNetworkAccessManager* network_ = nullptr;
};

} // namespace maxchat::ui
