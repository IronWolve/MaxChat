#include "upload/ImgbbUploader.h"

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
#include <QUrlQuery>

namespace maxchat::upload {

ImgbbUploader::ImgbbUploader(const QString &apiKey,
                               QNetworkAccessManager *manager, QObject *parent)
    : ImageUploader(manager, parent), apiKey_(apiKey) {}

void ImgbbUploader::upload(const QImage &image) {
    if (apiKey_.isEmpty()) {
        emit uploadFailed(QStringLiteral("No ImgBB API key configured"));
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

    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant(QStringLiteral("form-data; name=\"image\"")));
    imagePart.setBody(pngData.toBase64());
    multipart->append(imagePart);

    QUrl url(QStringLiteral("https://api.imgbb.com/1/upload"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), apiKey_);
    url.setQuery(query);

    QNetworkRequest request(url);
    auto *reply = manager_->post(request, multipart);
    multipart->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit uploadFailed(reply->errorString());
            return;
        }
        const QJsonObject root =
            QJsonDocument::fromJson(reply->readAll()).object();
        if (!root.value(QStringLiteral("success")).toBool()) {
            emit uploadFailed(
                root.value(QStringLiteral("error"))
                    .toObject()
                    .value(QStringLiteral("message"))
                    .toString(QStringLiteral("Upload failed")));
            return;
        }
        const QString directUrl =
            root.value(QStringLiteral("data"))
                .toObject()
                .value(QStringLiteral("url"))
                .toString();
        if (directUrl.isEmpty()) {
            emit uploadFailed(QStringLiteral("Empty URL in response"));
        } else {
            emit uploaded(directUrl);
        }
    });
}

} // namespace maxchat::upload
