#pragma once

#include <QObject>
#include <QString>

class QImage;
class QNetworkAccessManager;

namespace maxchat::upload {

// Abstract base for image-upload backends.
// All implementations take the shared QNetworkAccessManager from MainWindow
// so they share the connection pool and respect Qt's SSL/proxy config.
class ImageUploader : public QObject {
    Q_OBJECT

  public:
    explicit ImageUploader(QNetworkAccessManager *manager,
                           QObject *parent = nullptr)
        : QObject(parent), manager_(manager) {}

    // Encode image as PNG and POST to the service. Emits uploaded() or
    // uploadFailed() exactly once per call.
    virtual void upload(const QImage &image) = 0;

    // Human-readable service name for UI labels.
    virtual QString serviceName() const = 0;

  signals:
    void uploaded(const QString &url);
    void uploadFailed(const QString &reason);

  protected:
    QNetworkAccessManager *manager_ = nullptr;
};

} // namespace maxchat::upload
