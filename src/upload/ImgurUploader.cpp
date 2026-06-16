#include "upload/ImgurUploader.h"

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

ImgurUploader::ImgurUploader(const QString &clientId,
                               QNetworkAccessManager *manager, QObject *parent)
    : ImageUploader(manager, parent), clientId_(clientId) {}

void ImgurUploader::upload(const QImage &image) {
    if (clientId_.isEmpty()) {
        emit uploadFailed(QStringLiteral("No Imgur Client-ID configured"));
        return;
    }
    // Strip CR/LF to prevent header injection.
    const QString safeId = QString(clientId_).remove(QLatin1Char('\r')).remove(QLatin1Char('\n'));

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

    QNetworkRequest request(QUrl(QStringLiteral("https://api.imgur.com/3/image")));
    request.setRawHeader("Authorization",
                         QStringLiteral("Client-ID %1").arg(safeId).toUtf8());
    auto *reply = manager_->post(request, multipart);
    multipart->setParent(reply);

    handleJsonReply(reply, [this](const QJsonObject &root) {
        if (!root.value(QStringLiteral("success")).toBool()) {
            emit uploadFailed(root.value(QStringLiteral("data"))
                                  .toObject()
                                  .value(QStringLiteral("error"))
                                  .toString(QStringLiteral("Upload failed")));
            return;
        }
        finishWithHttpsUrl(root.value(QStringLiteral("data"))
                               .toObject()
                               .value(QStringLiteral("link"))
                               .toString());
    });
}

} // namespace maxchat::upload
