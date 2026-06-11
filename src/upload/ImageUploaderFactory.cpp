#include "upload/ImageUploaderFactory.h"

#include "upload/ImgbbUploader.h"
#include "upload/ImgboxUploader.h"
#include "upload/ImgurUploader.h"
#include "upload/PostimagesUploader.h"

#include <QString>
#include <QVariantMap>

namespace maxchat::upload {

std::unique_ptr<ImageUploader> makeImageUploader(const QVariantMap &settings,
                                                  QNetworkAccessManager *manager,
                                                  QObject *parent) {
    const QString service =
        settings.value(QStringLiteral("image_upload_service")).toString();

    if (service == QLatin1String("imgbb")) {
        return std::make_unique<ImgbbUploader>(
            settings.value(QStringLiteral("imgbb_api_key")).toString(),
            manager, parent);
    }
    if (service == QLatin1String("imgur")) {
        return std::make_unique<ImgurUploader>(
            settings.value(QStringLiteral("imgur_client_id")).toString(),
            manager, parent);
    }
    if (service == QLatin1String("postimages")) {
        return std::make_unique<PostimagesUploader>(
            settings.value(QStringLiteral("postimages_token")).toString(),
            manager, parent);
    }
    if (service == QLatin1String("imgbox")) {
        return std::make_unique<ImgboxUploader>(
            settings.value(QStringLiteral("imgbox_username")).toString(),
            settings.value(QStringLiteral("imgbox_password")).toString(),
            manager, parent);
    }
    return nullptr; // disabled
}

} // namespace maxchat::upload
