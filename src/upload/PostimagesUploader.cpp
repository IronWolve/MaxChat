#include "upload/PostimagesUploader.h"

#include <QByteArray>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QImage>
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

    const QByteArray pngData = encodePng(image);
    if (pngData.isEmpty()) {
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

    handleJsonReply(reply, [this](const QJsonObject &root) {
        if (root.value(QStringLiteral("status")).toString() != QLatin1String("OK")) {
            emit uploadFailed(
                root.value(QStringLiteral("error")).toString(QStringLiteral("Upload failed")));
            return;
        }
        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
        // Prefer the direct image link; fall back to the viewer URL.
        const QString directLink = data.value(QStringLiteral("direct_link")).toString();
        finishWithHttpsUrl(directLink.isEmpty() ? data.value(QStringLiteral("url")).toString()
                                                : directLink);
    });
}

} // namespace maxchat::upload
