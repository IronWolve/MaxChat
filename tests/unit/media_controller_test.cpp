// Unit tests for MediaController (decomp phase 2). Focus on the security-relevant
// behaviour that moved out of MainWindow::handleChatAnchorClicked: the clicked-link
// scheme allow-list (no file:/javascript:/app handlers reach the OS), and the
// graceful "no uploader configured" path.

#include "ui/MainWindowHost.h"
#include "ui/MediaController.h"

#include <QImage>
#include <QNetworkAccessManager>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QWidget>
#include <QtTest/QtTest>

using maxchat::ui::MainWindowHost;
using maxchat::ui::MediaController;

namespace {

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
    void showStatus(const QString&, int) override {}
    void clearStatus() override {}

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

  private:
    QWidget* parent_;
    QNetworkAccessManager nam_;
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
        media.handleAnchorClicked(QUrl(QStringLiteral("file:///home/me/pic.png")));
        QCOMPARE(host.activeLines.size(), 1);
        QVERIFY(host.activeLines.first().contains(QStringLiteral("Refused")));
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
