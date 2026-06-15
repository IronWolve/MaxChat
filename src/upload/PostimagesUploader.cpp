#include "upload/PostimagesUploader.h"

#include <QBuffer>
#include <QByteArray>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace maxchat::upload {

PostimagesUploader::PostimagesUploader(const QString &token,
                                       QNetworkAccessManager *manager,
                                       QObject *parent)
    : ImageUploader(manager, parent), token_(token) {}

void PostimagesUploader::upload(const QImage &image) {
    if (token_.isEmpty()) {
        emit uploadFailed(QStringLiteral("No Postimages API token configured"));
        return;
    }

    QByteArray pngData;
    QBuffer buf(&pngData);
    buf.open(QIODevice::WriteOnly);
    if (!image.save(&buf, "PNG")) {
        emit uploadFailed(QStringLiteral("Failed to encode image as PNG"));
        return;
    }

    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType, this);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                       QVariant(QStringLiteral("image/png")));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"upload[]\"; filename=\"image.png\"")));
    filePart.setBody(pngData);
    multipart->append(filePart);

    QHttpPart tokenPart;
    tokenPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant(QStringLiteral("form-data; name=\"token\"")));
    tokenPart.setBody(token_.toUtf8());
    multipart->append(tokenPart);

    QNetworkRequest request(QUrl(QStringLiteral("https://postimages.org/json/rr")));
    auto *reply = manager_->post(request, multipart);
    multipart->setParent(reply);
    armUploadTimeout(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit uploadFailed(reply->property("maxchat_timeout").toBool()
                                  ? QStringLiteral("Upload timed out")
                                  : reply->errorString());
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(readCappedBody(reply)).object();
        if (root.value(QStringLiteral("status")).toString() != QLatin1String("OK")) {
            emit uploadFailed(
                root.value(QStringLiteral("error")).toString(QStringLiteral("Upload failed")));
            return;
        }
        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
        // Prefer the direct image link; fall back to the viewer URL.
        const QString directLink = data.value(QStringLiteral("direct_link")).toString();
        const QString url =
            directLink.isEmpty() ? data.value(QStringLiteral("url")).toString() : directLink;
        // PostImages also double-decodes its direct_link; cache the lookup.
        if (!isHttpsUrl(url)) {
            emit uploadFailed(QStringLiteral("Upload response had no valid https URL"));
        } else {
            emit uploaded(url);
        }
    });
}

} // namespace maxchat::upload
