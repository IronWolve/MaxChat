#pragma once

#include "upload/ImageUploader.h"

namespace maxchat::upload {

// Uploads to postimages.org using an account API token.
// POST multipart with "upload[]" (image) + "token" (from account settings);
// response is JSON with data.url (viewer page) and data.direct_link (direct PNG URL).
class PostimagesUploader final : public ImageUploader {
    Q_OBJECT

  public:
    explicit PostimagesUploader(const QString &token, QNetworkAccessManager *manager,
                                QObject *parent = nullptr);

    void upload(const QImage &image) override;
    QString serviceName() const override { return QStringLiteral("Postimages"); }

  private:
    QString token_;
};

} // namespace maxchat::upload
