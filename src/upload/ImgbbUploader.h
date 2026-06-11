#pragma once

#include "upload/ImageUploader.h"

namespace maxchat::upload {

// Uploads to api.imgbb.com — requires a free API key from imgbb.com.
// POST multipart with field "image" (base64-encoded PNG); response is JSON
// with data.url pointing to the direct image link.
class ImgbbUploader final : public ImageUploader {
    Q_OBJECT

  public:
    explicit ImgbbUploader(const QString &apiKey, QNetworkAccessManager *manager,
                           QObject *parent = nullptr);

    void upload(const QImage &image) override;
    QString serviceName() const override { return QStringLiteral("ImgBB"); }

  private:
    QString apiKey_;
};

} // namespace maxchat::upload
