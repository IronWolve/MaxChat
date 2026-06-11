#pragma once

#include "upload/ImageUploader.h"

namespace maxchat::upload {

// Uploads to api.imgur.com — requires a free anonymous Client-ID from imgur.com.
// POST with Authorization: Client-ID header and base64 "image" field;
// response is JSON with data.link pointing to the direct image URL.
class ImgurUploader final : public ImageUploader {
    Q_OBJECT

  public:
    explicit ImgurUploader(const QString &clientId,
                            QNetworkAccessManager *manager,
                            QObject *parent = nullptr);

    void upload(const QImage &image) override;
    QString serviceName() const override { return QStringLiteral("Imgur"); }

  private:
    QString clientId_;
};

} // namespace maxchat::upload
