#include "upload/ImgboxUploader.h"

#include <QByteArray>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace maxchat::upload {

ImgboxUploader::ImgboxUploader(const QString &username, const QString &password,
                                QNetworkAccessManager *manager,
                                QObject *parent)
    : ImageUploader(manager, parent), username_(username), password_(password) {}

void ImgboxUploader::upload(const QImage &image) {
    if (username_.isEmpty() || password_.isEmpty()) {
        emit uploadFailed(QStringLiteral("Imgbox username and password are required"));
        return;
    }

    const QByteArray pngData = encodePng(image);
    if (pngData.isEmpty()) {
        return;
    }

    // Step 1: create a gallery token tied to the user's account.
    QJsonObject credentials;
    credentials.insert(QStringLiteral("username"), username_);
    credentials.insert(QStringLiteral("password"), password_);
    credentials.insert(QStringLiteral("content_type"), 1); // 1 = family safe
    credentials.insert(QStringLiteral("comments_enabled"), 0);

    QNetworkRequest tokenRequest(QUrl(QStringLiteral("https://imgbox.com/api/v1/token/create")));
    tokenRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                           QStringLiteral("application/json"));
    auto *tokenReply = manager_->post(tokenRequest,
                                      QJsonDocument(credentials).toJson(QJsonDocument::Compact));

    handleJsonReply(tokenReply, [this, pngData](const QJsonObject &root) {
        if (root.value(QStringLiteral("success")).toInt() != 1) {
            emit uploadFailed(root.value(QStringLiteral("message"))
                                  .toString(QStringLiteral("Token creation failed")));
            return;
        }
        const QString tokenId = root.value(QStringLiteral("token_id")).toString();
        const QString tokenSecret = root.value(QStringLiteral("token_secret")).toString();
        if (tokenId.isEmpty() || tokenSecret.isEmpty()) {
            emit uploadFailed(QStringLiteral("Invalid token in response"));
            return;
        }
        doUpload(pngData, tokenId, tokenSecret);
    });
}

void ImgboxUploader::doUpload(const QByteArray &pngData,
                               const QString &tokenId,
                               const QString &tokenSecret) {
    // Step 2: upload the image with the gallery token.
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType, this);

    auto addField = [&](const QString &name, const QByteArray &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"%1\"").arg(name)));
        part.setBody(value);
        multipart->append(part);
    };

    addField(QStringLiteral("token_id"), tokenId.toUtf8());
    addField(QStringLiteral("token_secret"), tokenSecret.toUtf8());
    addField(QStringLiteral("content_type"), "1");
    addField(QStringLiteral("thumbnail_size"), "350c");
    addField(QStringLiteral("comments_enabled"), "0");

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                       QVariant(QStringLiteral("image/png")));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"files[]\"; filename=\"image.png\"")));
    filePart.setBody(pngData);
    multipart->append(filePart);

    QNetworkRequest request(QUrl(QStringLiteral("https://imgbox.com/api/v1/images/upload")));
    auto *reply = manager_->post(request, multipart);
    multipart->setParent(reply);

    handleJsonReply(reply, [this](const QJsonObject &root) {
        if (root.value(QStringLiteral("success")).toInt() != 1) {
            emit uploadFailed(
                root.value(QStringLiteral("message")).toString(QStringLiteral("Upload failed")));
            return;
        }
        // images[] array; each entry has "original_url" for the full-size image.
        const QJsonArray images = root.value(QStringLiteral("images")).toArray();
        if (images.isEmpty()) {
            emit uploadFailed(QStringLiteral("Upload response had no images"));
            return;
        }
        finishWithHttpsUrl(images.first().toObject().value(QStringLiteral("original_url")).toString());
    });
}

} // namespace maxchat::upload
