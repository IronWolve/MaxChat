#include "upload/ImgbbUploader.h"

#include <QByteArray>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QImage>
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

    const QByteArray pngData = encodePng(image);
    if (pngData.isEmpty()) {
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

    handleJsonReply(reply, [this](const QJsonObject &root) {
        if (!root.value(QStringLiteral("success")).toBool()) {
            emit uploadFailed(root.value(QStringLiteral("error"))
                                  .toObject()
                                  .value(QStringLiteral("message"))
                                  .toString(QStringLiteral("Upload failed")));
            return;
        }
        finishWithHttpsUrl(root.value(QStringLiteral("data"))
                               .toObject()
                               .value(QStringLiteral("url"))
                               .toString());
    });
}

} // namespace maxchat::upload
