#include "services/PublicHttpFetch.h"
#include "services/LinkPreviewClassifier.h"
#include <QHostInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <algorithm>
namespace maxchat::services {
namespace {
constexpr int RequestLimit = 16;
constexpr qint64 BufferLimit = 32 * 1024 * 1024;
QMutex limitsMutex;
int activeRequests = 0;
qint64 bufferedBytes = 0;
bool publicAddress(const QHostAddress& address) {
    // Use only explicitly classified addresses; never ask the transport to
    // resolve the hostname again or choose an unchecked fallback address.
    QUrl probe; probe.setScheme(QStringLiteral("https")); probe.setHost(address.toString());
    return isAllowedPreviewFetchUrl(probe);
}
class Fetch final : public QObject {
public:
    Fetch(QNetworkAccessManager* manager, PublicHttpOptions options, QObject* context,
          PublicHttpCallback callback)
        : QObject(context), manager_(manager), options_(std::move(options)), callback_(std::move(callback)) {
        timeout_.setSingleShot(true);
        connect(&timeout_, &QTimer::timeout, this, [this]() { finish(QStringLiteral("request timed out")); });
    }
    ~Fetch() override {
        if (lookup_ >= 0) QHostInfo::abortHostLookup(lookup_);
        if (reply_) { reply_->disconnect(this); reply_->abort(); reply_->deleteLater(); }
        release();
    }
    void start(const QUrl& url) {
        {
            QMutexLocker locker(&limitsMutex);
            if (activeRequests < RequestLimit) { ++activeRequests; admitted_ = true; }
        }
        if (!admitted_) { finish(QStringLiteral("preview request limit reached")); return; }
        timeout_.start(std::clamp(options_.timeoutMs, 1, 30000));
        resolve(url);
    }
private:
    void release() {
        QMutexLocker locker(&limitsMutex);
        if (admitted_) { --activeRequests; admitted_ = false; }
        bufferedBytes -= accounted_; accounted_ = 0;
    }
    void finish(const QString& error = {}) {
        if (finished_) return;
        finished_ = true; timeout_.stop();
        if (lookup_ >= 0) { QHostInfo::abortHostLookup(lookup_); lookup_ = -1; }
        if (reply_) { reply_->disconnect(this); reply_->abort(); reply_->deleteLater(); reply_ = nullptr; }
        result_.error = error;
        auto callback = std::move(callback_);
        // Release the request slot before a callback requests a dependent image.
        release();
        const QPointer<Fetch> alive(this);
        if (callback) callback(std::move(result_));
        if (alive) deleteLater();
    }
    void resolve(const QUrl& url) {
        if (finished_) return;
        const QString scheme = url.scheme().toLower();
        if (!url.isValid() || (scheme != QLatin1String("http") && scheme != QLatin1String("https")) ||
            url.host().isEmpty() || !url.userInfo().isEmpty() || url.toString().size() > 8192 ||
            (secure_ && scheme != QLatin1String("https")) ||
            (!options_.allowPrivateNetwork && !isAllowedPreviewFetchUrl(url))) {
            finish(QStringLiteral("blocked preview URL")); return;
        }
        secure_ = secure_ || scheme == QLatin1String("https");
        result_.finalUrl = url;
        const QHostAddress literal(url.host());
        if (!literal.isNull()) { connectTo(url, {literal}); return; }
        lookup_ = QHostInfo::lookupHost(url.host(), this, [this, url](const QHostInfo& info) {
            lookup_ = -1;
            if (finished_) return;
            if (info.error() != QHostInfo::NoError) { finish(QStringLiteral("hostname could not be resolved")); return; }
            connectTo(url, info.addresses());
        });
    }
    void connectTo(const QUrl& logicalUrl, const QList<QHostAddress>& addresses) {
        QHostAddress chosen;
        for (const auto& address : addresses) {
            if (options_.allowPrivateNetwork || publicAddress(address)) { chosen = address; break; }
        }
        if (chosen.isNull()) { finish(QStringLiteral("blocked preview URL")); return; }
        if (!manager_) { finish(QStringLiteral("network manager missing")); return; }
        reply_ = manager_->get(pinnedHttpRequest(logicalUrl, chosen, options_));
        reply_->setReadBufferSize(64 * 1024);
        connect(reply_, &QIODevice::readyRead, this, [this]() { readBody(); });
        connect(reply_, &QNetworkReply::finished, this, [this]() {
            if (finished_ || !reply_) return;
            readBody();
            if (finished_ || !reply_) return;
            result_.status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            result_.contentType = reply_->header(QNetworkRequest::ContentTypeHeader).toByteArray();
            const QUrl target = reply_->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
            if (result_.status >= 300 && result_.status < 400 && !target.isEmpty()) {
                if (++redirects_ > 5) { finish(QStringLiteral("redirect limit exceeded")); return; }
                const QUrl next = result_.finalUrl.resolved(target);
                reply_->deleteLater(); reply_ = nullptr;
                {
                    QMutexLocker locker(&limitsMutex);
                    bufferedBytes -= accounted_; accounted_ = 0;
                }
                result_.body.clear(); result_.contentType.clear();
                resolve(next); return;
            }
            if (reply_->error() != QNetworkReply::NoError || result_.status < 200 || result_.status >= 300) {
                finish(QStringLiteral("request failed")); return;
            }
            finish();
        });
    }
    void readBody() {
        if (!reply_ || finished_) return;
        result_.status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result_.contentType = reply_->header(QNetworkRequest::ContentTypeHeader).toByteArray();
        while (reply_ && reply_->bytesAvailable() > 0 && !finished_) {
            const QByteArray chunk = reply_->read(qMin<qint64>(64 * 1024, options_.maxBytes - result_.body.size() + 1));
            if (chunk.isEmpty()) break;
            const qint64 remaining = options_.maxBytes - result_.body.size();
            const qint64 count = qMin<qint64>(remaining, chunk.size());
            bool enough = false;
            {
                QMutexLocker locker(&limitsMutex);
                enough = bufferedBytes + count <= BufferLimit;
                if (enough) { bufferedBytes += count; accounted_ += count; }
            }
            if (!enough) { finish(QStringLiteral("preview memory limit reached")); return; }
            result_.body.append(chunk.constData(), count);
            if (count < chunk.size()) {
                result_.truncated = true;
                const bool usable = options_.allowTruncatedBody && result_.status >= 200 && result_.status < 300;
                finish(usable ? QString() : QStringLiteral("response exceeded size cap"));
                return;
            }
        }
    }
    QPointer<QNetworkAccessManager> manager_;
    PublicHttpOptions options_;
    PublicHttpCallback callback_;
    PublicHttpResult result_;
    QPointer<QNetworkReply> reply_;
    QTimer timeout_;
    int lookup_ = -1, redirects_ = 0;
    qint64 accounted_ = 0;
    bool admitted_ = false, finished_ = false, secure_ = false;
};
}
QNetworkRequest pinnedHttpRequest(const QUrl& logicalUrl, const QHostAddress& address,
                                  const PublicHttpOptions& options) {
    QUrl transport = logicalUrl;
    transport.setHost(address.toString());
    QNetworkRequest request(transport);
    QByteArray host = logicalUrl.host(QUrl::FullyEncoded).toUtf8();
    if (host.contains(':')) host = '[' + host + ']';
    const int port = logicalUrl.port();
    if (port >= 0) host += ':' + QByteArray::number(port);
    request.setRawHeader("Host", host);
    request.setPeerVerifyName(logicalUrl.host());
    request.setRawHeader("User-Agent", "MaxChat/1.0.2 preview");
    request.setRawHeader("Accept", options.accept);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(options.timeoutMs);
    request.setDecompressedSafetyCheckThreshold(64 * 1024);
    return request;
}
QObject* fetchPublicHttp(QNetworkAccessManager* manager, const QUrl& url,
                         PublicHttpOptions options, QObject* context, PublicHttpCallback done) {
    options.maxBytes = std::clamp<qint64>(options.maxBytes, 1, 25 * 1024 * 1024);
    options.timeoutMs = std::clamp(options.timeoutMs, 1, 30000);
    auto* fetch = new Fetch(manager, std::move(options), context, std::move(done));
    const QPointer<Fetch> alive(fetch);
    fetch->start(url);
    return alive.data();
}
}
