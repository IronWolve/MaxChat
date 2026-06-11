#pragma once

#include "upload/ImageUploader.h"

namespace maxchat::upload {

// Uploads to imgbox.com using account credentials (username + password).
// Two-step: POST /api/v1/token/create with credentials → receive token_id/secret,
// then POST /api/v1/images/upload with the token + image file.
// The token ties uploads to the user's account.
class ImgboxUploader final : public ImageUploader {
    Q_OBJECT

  public:
    explicit ImgboxUploader(const QString &username, const QString &password,
                             QNetworkAccessManager *manager,
                             QObject *parent = nullptr);

    void upload(const QImage &image) override;
    QString serviceName() const override { return QStringLiteral("Imgbox"); }

  private:
    void doUpload(const QByteArray &pngData,
                  const QString &tokenId, const QString &tokenSecret);

    QString username_;
    QString password_;
};

} // namespace maxchat::upload
