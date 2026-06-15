#pragma once

#include <QObject>
#include <QPointer>
#include <QNetworkReply>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>

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

    // Shared hardening so no backend can hang or be abused by a hostile endpoint.
    static constexpr int kUploadTimeoutMs = 30000;
    static constexpr qint64 kMaxResponseBytes = 1 << 20; // 1 MiB of JSON is plenty

    // Abort the reply if it has not finished within kUploadTimeoutMs; flags it so
    // the finished handler can report a timeout. Safe to call right after post().
    void armUploadTimeout(QNetworkReply *reply) {
        const QPointer<QNetworkReply> guarded(reply);
        QTimer::singleShot(kUploadTimeoutMs, reply, [guarded]() {
            if (guarded != nullptr && !guarded->isFinished()) {
                guarded->setProperty("maxchat_timeout", true);
                guarded->abort();
            }
        });
    }
    // Read at most kMaxResponseBytes from the reply (an endpoint must not be able
    // to stream an unbounded body into memory).
    static QByteArray readCappedBody(QNetworkReply *reply) {
        return reply->read(kMaxResponseBytes);
    }
    // A returned image URL must be https before we hand it back to the user.
    static bool isHttpsUrl(const QString &url) {
        return QUrl(url).scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace maxchat::upload
