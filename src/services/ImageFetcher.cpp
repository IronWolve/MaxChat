#include "services/ImageFetcher.h"
#include "services/PublicHttpFetch.h"
#include <QBuffer>
#include <QImageReader>
#include <QNetworkAccessManager>
namespace maxchat::services {
ImageFetcher::ImageFetcher(QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent), manager_(manager) {}
void ImageFetcher::fetch(const QUrl& url, ImageFetchOptions options) {
    PublicHttpOptions request;
    request.maxBytes = options.maxBytes > 0 ? options.maxBytes : 5 * 1024 * 1024;
    request.timeoutMs = options.timeoutMs > 0 ? options.timeoutMs : 12000;
    request.allowPrivateNetwork = options.allowPrivateNetwork;
    request.accept = "image/*;q=0.9,*/*;q=0.1";
    fetchPublicHttp(manager_, url, request, this, [this, url, options](PublicHttpResult result) {
        if (!result.error.isEmpty()) {
            emit imageFetchFailed(url, result.error == QLatin1String("blocked preview URL")
                ? QStringLiteral("blocked image URL") : result.error);
            return;
        }
        const QByteArray type = result.contentType.toLower();
        if (!type.isEmpty() && !type.startsWith("image/")) {
            emit imageFetchFailed(url, QStringLiteral("response was not an image")); return;
        }
        QBuffer buffer(&result.body);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const QByteArray format = reader.format().toLower();
        if (format != "png" && format != "jpeg" && format != "jpg" && format != "gif" &&
            format != "webp" && format != "bmp") {
            emit imageFetchFailed(url, QStringLiteral("unsupported preview image format")); return;
        }
        const QSize size = reader.size();
        constexpr qint64 MaxPixels = 16 * 1024 * 1024;
        if (!size.isValid() || qint64(size.width()) * size.height() > MaxPixels) {
            emit imageFetchFailed(url, QStringLiteral("image dimensions exceed decode limit")); return;
        }
        const QSize maximum(qBound(1, options.maxWidth, 8192), qBound(1, options.maxHeight, 8192));
        if (size.width() > maximum.width() || size.height() > maximum.height())
            reader.setScaledSize(size.scaled(maximum, Qt::KeepAspectRatio));
        QImage image = reader.read();
        if (image.isNull()) {
            emit imageFetchFailed(url, QStringLiteral("could not decode image")); return;
        }
        if (image.width() > maximum.width() || image.height() > maximum.height())
            image = image.scaled(maximum, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        emit imageFetched(url, image);
    });
}
}
