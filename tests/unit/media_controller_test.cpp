// Unit tests for MediaController (decomp phase 2). Focus on the security-relevant
// behaviour that moved out of MainWindow::handleChatAnchorClicked: the clicked-link
// scheme allow-list (no file:/javascript:/app handlers reach the OS), and the
// graceful "no uploader configured" path.

#include "ui/MainWindowHost.h"
#include "ui/MediaController.h"

#include <QImage>
#include <QDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <cstring>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QWidget>
#include <QtTest/QtTest>

using maxchat::ui::MainWindowHost;
using maxchat::ui::MediaController;

namespace {

// Intercept requests before the network: no DNS or external service is used.
class MediaReply final : public QNetworkReply {
  public:
    QByteArray body;
    int* aborted;
    MediaReply(const QNetworkRequest& request, QByteArray bytes, bool stalled,
               int* abortCount, QObject* parent)
        : QNetworkReply(parent), body(std::move(bytes)), aborted(abortCount) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        if (!stalled) QTimer::singleShot(0, this, [this] {
            emit readyRead(); setFinished(true); emit finished();
        });
    }
    void abort() override { ++*aborted; setFinished(true); }
    qint64 bytesAvailable() const override { return body.size() + QNetworkReply::bytesAvailable(); }
    qint64 readData(char* out, qint64 limit) override {
        const qint64 count = qMin(limit, qint64(body.size()));
        if (!count) return -1;
        std::memcpy(out, body.constData(), size_t(count)); body.remove(0, count);
        return count;
    }
};
class MediaManager final : public QNetworkAccessManager {
  public:
    QByteArray body;
    bool stalled = false;
    int requests = 0;
    int aborted = 0;
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        ++requests;
        return new MediaReply(request, body, stalled, &aborted, this);
    }
};

class FakeHost final : public MainWindowHost {
  public:
    explicit FakeHost(QWidget* parent) : parent_(parent) {}

    QString activeNetwork() const override { return QStringLiteral("Net"); }
    QString currentTarget() const override { return QStringLiteral("#chan"); }
    QString nickFor(const QString&) override { return QStringLiteral("me"); }
    QStringList channelsFor(const QString&) override { return {}; }
    QStringList nicksFor(const QString&, const QString&) override { return {}; }

    void appendActiveSystemLine(const QString& text) override { activeLines << text; }
    void appendSystemLine(const QString&, const QString&, const QString&) override {}
    void echoOutbound(const QString&, const QString&, const QString&) override {}
    void insertInput(const QString&) override {}
    void notifyUser(const QString&, const QString&) override {}
    void appendInputUrl(const QString& url) override { insertedUrls << url; }
    void showStatus(const QString& value, int) override { status = value; }
    void clearStatus() override { status.clear(); }

    maxchat::irc::IrcConnection* connectionFor(const QString&) override { return nullptr; }
    QNetworkAccessManager& scriptNetworkManager() override { return nam_; }
    QNetworkAccessManager& previewNetworkManager() override { return nam_; }
    maxchat::core::SettingsStore& settings() override {
        Q_ASSERT(false); // not needed by these tests
        std::abort();
    }
    QWidget* dialogParent() override { return parent_; }
    void rebuildTree() override {}
    void renderActiveBuffer() override {}
    void recolorMemberList() override {}
    void updateChatSeparatorGuide() override {}
    void updateTrayIcon() override {}
    void setMenuBarFont(const QFont&) override {}
    void applyAllSettings() override {}

    QStringList activeLines;
    QStringList insertedUrls;
    QString status;
    MediaManager nam_;

  private:
    QWidget* parent_;

};

} // namespace

class MediaControllerTest : public QObject {
    Q_OBJECT

  private slots:
    void refusesFileScheme() {
        QWidget parent;
        FakeHost host(&parent);
        MediaController media(host);
        media.handleAnchorClicked(QUrl(QStringLiteral("file:///etc/passwd")));
        QCOMPARE(host.activeLines.size(), 1);
        QVERIFY(host.activeLines.first().contains(QStringLiteral("file")));
        QVERIFY(host.activeLines.first().contains(QStringLiteral("Refused")));
    }

    void refusesJavascriptScheme() {
        QWidget parent;
        FakeHost host(&parent);
        MediaController media(host);
        media.handleAnchorClicked(QUrl(QStringLiteral("javascript:alert(1)")));
        QCOMPARE(host.activeLines.size(), 1);
        QVERIFY(host.activeLines.first().contains(QStringLiteral("Refused")));
    }

    void masterOffSwallowsClick() {
        QWidget parent;
        FakeHost host(&parent);
        MediaController media(host);
        QVariantMap s;
        s.insert(QStringLiteral("open_links_in_browser"), false);
        media.configure(s);
        // With the master off, even a normally-refused scheme produces no output:
        // the click is dropped before the scheme allow-list runs.
        media.handleAnchorClicked(QUrl(QStringLiteral("file:///etc/passwd")));
        QCOMPARE(host.activeLines.size(), 0);
    }

    void imageToggleOffRoutesExternally() {
        QWidget parent;
        FakeHost host(&parent);
        MediaController media(host);
        // Master on, image handling off → an image link is no longer opened in the
        // in-app viewer; it falls through to the OS path, where the scheme guard
        // still refuses a file: URL (proving it took the external branch).
        QVariantMap services;
        services.insert(QStringLiteral("images"), false);
        QVariantMap s;
        s.insert(QStringLiteral("content_services"), services);
        media.configure(s);
        media.handleAnchorClicked(QUrl(QStringLiteral("file:blocked-test.png")));
        QCOMPARE(host.activeLines.size(), 1);
        QVERIFY(host.activeLines.first().contains(QStringLiteral("Refused")));
    }

    void playlistResponseNeverReachesPlayer() {
        QWidget parent;
        FakeHost host(&parent);
        host.nam_.body = "#EXTM3U\nhttp://127.0.0.1/private\n";
        MediaController media(host);
        media.configure({{QStringLiteral("content_services"),
            QVariantMap{{QStringLiteral("media"), true}}}});
        media.handleAnchorClicked(QUrl(QStringLiteral("https://8.8.8.8/clip.mp4")));
        QTRY_COMPARE(host.activeLines.size(), 1);
        QCOMPARE(host.nam_.requests, 1);
        QVERIFY(host.activeLines.first().contains(QStringLiteral("Unsupported media")));
        QVERIFY(host.status.isEmpty());
        QVERIFY(parent.findChildren<QDialog*>().isEmpty());
    }

    void disablingMediaCancelsPendingDownload() {
        QWidget parent;
        FakeHost host(&parent);
        host.nam_.stalled = true;
        MediaController media(host);
        media.configure({{QStringLiteral("content_services"),
            QVariantMap{{QStringLiteral("media"), true}}}});
        media.handleAnchorClicked(QUrl(QStringLiteral("https://8.8.8.8/clip.mp4")));
        QTRY_COMPARE(host.nam_.requests, 1);
        media.configure({}); // default-off policy
        QTRY_COMPARE(host.nam_.aborted, 1);
        QVERIFY(host.activeLines.isEmpty());
        QVERIFY(host.status.isEmpty());
        QVERIFY(parent.findChildren<QDialog*>().isEmpty());
    }

    void uploadWithoutServiceWarns() {
        QWidget parent;
        FakeHost host(&parent);
        MediaController media(host);
        QVERIFY(!media.isConfigured());
        // A 1x1 image is enough; the uploader-null check fires before image checks.
        QImage img(1, 1, QImage::Format_RGB32);
        media.uploadImage(img);
        QCOMPARE(host.activeLines.size(), 1);
        QVERIFY(host.activeLines.first().contains(QStringLiteral("no image hosting")));
        QVERIFY(host.insertedUrls.isEmpty());
    }
};

QTEST_MAIN(MediaControllerTest)
#include "media_controller_test.moc"
