#include "upload/ImageUploader.h"

#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>

namespace maxchat::upload {

QByteArray ImageUploader::encodePng(const QImage &image) {
    QByteArray pngData;
    QBuffer buf(&pngData);
    buf.open(QIODevice::WriteOnly);
    if (!image.save(&buf, "PNG")) {
        emit uploadFailed(QStringLiteral("Failed to encode image as PNG"));
        return {};
    }
    return pngData;
}

void ImageUploader::handleJsonReply(QNetworkReply *reply,
                                    const std::function<void(const QJsonObject &)> &onJson) {
    armUploadTimeout(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, onJson]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit uploadFailed(reply->property("maxchat_timeout").toBool()
                                  ? QStringLiteral("Upload timed out")
                                  : reply->errorString());
            return;
        }
        onJson(QJsonDocument::fromJson(readCappedBody(reply)).object());
    });
}

void ImageUploader::finishWithHttpsUrl(const QString &url) {
    if (!isHttpsUrl(url)) {
        emit uploadFailed(QStringLiteral("Upload response had no valid https URL"));
    } else {
        emit uploaded(url);
    }
}

} // namespace maxchat::upload
