// Unit tests for ScriptBridge — the script subsystem extracted from MainWindow
// (decomp phase 1). These exercise the routing/permission logic that used to be
// untestable while embedded in a 9k-line widget: bundled-vs-user ircSend default
// resolution, explicit-permission override, and MC-DATA network routing.

#include "core/SettingsStore.h"
#include "irc/IrcConnection.h"
#include "ui/MainWindowHost.h"
#include "ui/ScriptBridge.h"

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest/QtTest>

using maxchat::core::SettingsPaths;
using maxchat::core::SettingsStore;
using maxchat::ui::MainWindowHost;
using maxchat::ui::ScriptBridge;

namespace {

// Records the calls ScriptBridge makes so tests can assert on routing without a
// real window or a live IRC connection.
class FakeHost final : public MainWindowHost {
  public:
    explicit FakeHost(SettingsStore& store, QWidget* parent)
        : store_(store), parent_(parent) {}

    QString activeNetwork() const override { return QStringLiteral("ActiveNet"); }
    QString currentTarget() const override { return QStringLiteral("#chan"); }
    QString nickFor(const QString&) override { return QStringLiteral("me"); }
    QStringList channelsFor(const QString&) override { return {}; }
    QStringList nicksFor(const QString&, const QString&) override { return {}; }

    void appendActiveSystemLine(const QString& text) override { activeLines << text; }
    void appendSystemLine(const QString& network, const QString&,
                          const QString& text) override {
        systemLines.append(qMakePair(network, text));
    }
    void echoOutbound(const QString&, const QString&, const QString&) override {}
    void insertInput(const QString&) override {}
    void notifyUser(const QString&, const QString&) override {}

    maxchat::irc::IrcConnection* connectionFor(const QString& network) override {
        connectionRequests << network;
        return nullptr; // "not connected" — exercises the failure/route path
    }

    QNetworkAccessManager& scriptNetworkManager() override { return nam_; }
    SettingsStore& settings() override { return store_; }
    QWidget* dialogParent() override { return parent_; }
    void rebuildTree() override { ++rebuildCount; }

    QStringList activeLines;
    QList<QPair<QString, QString>> systemLines;
    QStringList connectionRequests;
    int rebuildCount = 0;

  private:
    SettingsStore& store_;
    QWidget* parent_;
    QNetworkAccessManager nam_;
};

// Drop a .bundled/<name>.lua snapshot so isBundledScript(name) returns true.
void markBundled(const QString& scriptsDir, const QString& name) {
    const QString bundledDir = QDir(scriptsDir).filePath(QStringLiteral(".bundled"));
    QDir().mkpath(bundledDir);
    QFile f(QDir(bundledDir).filePath(name + QStringLiteral(".lua")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("-- snapshot\n");
}

} // namespace

class ScriptBridgeTest : public QObject {
    Q_OBJECT

  private slots:
    void bundledScriptDefaultsToIrcSend() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        SettingsPaths paths;
        paths.configDir = tmp.path();
        paths.settingsPath = QDir(tmp.path()).filePath(QStringLiteral("settings.json"));
        SettingsStore store(paths);

        const QString scriptsDir = QDir(tmp.path()).filePath(QStringLiteral("scripts"));
        QDir().mkpath(scriptsDir);
        markBundled(scriptsDir, QStringLiteral("bbs"));

        QWidget parent;
        FakeHost host(store, &parent);
        ScriptBridge bridge(host, scriptsDir);

        QVERIFY(bridge.isBundledScript(QStringLiteral("bbs")));
        // Bundled scripts get IRC send by default (no saved perms entry).
        QVERIFY(bridge.buildScriptPermissionsFor(QStringLiteral("bbs")).ircSend);
    }

    void userScriptDefaultsToNoIrcSend() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        SettingsPaths paths;
        paths.configDir = tmp.path();
        paths.settingsPath = QDir(tmp.path()).filePath(QStringLiteral("settings.json"));
        SettingsStore store(paths);

        const QString scriptsDir = QDir(tmp.path()).filePath(QStringLiteral("scripts"));
        QDir().mkpath(scriptsDir);

        QWidget parent;
        FakeHost host(store, &parent);
        ScriptBridge bridge(host, scriptsDir);

        QVERIFY(!bridge.isBundledScript(QStringLiteral("mine")));
        // Unbundled user scripts must NOT get IRC send unless explicitly granted.
        QVERIFY(!bridge.buildScriptPermissionsFor(QStringLiteral("mine")).ircSend);
    }

    void explicitPermissionOverridesDefault() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        SettingsPaths paths;
        paths.configDir = tmp.path();
        paths.settingsPath = QDir(tmp.path()).filePath(QStringLiteral("settings.json"));
        SettingsStore store(paths);

        // A user script the user explicitly granted IRC send.
        QVariantMap perms;
        perms.insert(QStringLiteral("irc"), true);
        QVariantMap allPerms;
        allPerms.insert(QStringLiteral("mine"), perms);
        QVERIFY(store.setValue(QStringLiteral("scriptPerms"), allPerms));

        const QString scriptsDir = QDir(tmp.path()).filePath(QStringLiteral("scripts"));
        QDir().mkpath(scriptsDir);

        QWidget parent;
        FakeHost host(store, &parent);
        ScriptBridge bridge(host, scriptsDir);

        QVERIFY(bridge.buildScriptPermissionsFor(QStringLiteral("mine")).ircSend);
    }

    void mcDataEmptyNetworkRoutesToActive() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        SettingsPaths paths;
        paths.configDir = tmp.path();
        paths.settingsPath = QDir(tmp.path()).filePath(QStringLiteral("settings.json"));
        SettingsStore store(paths);

        const QString scriptsDir = QDir(tmp.path()).filePath(QStringLiteral("scripts"));
        QDir().mkpath(scriptsDir);

        QWidget parent;
        FakeHost host(store, &parent);
        ScriptBridge bridge(host, scriptsDir);

        // Empty network → resolve to the active network; not connected → false +
        // an error line on that network's buffer.
        const bool ok = bridge.scriptMcData(QString(), QStringLiteral("#room"),
                                            QStringLiteral("svc"), QStringLiteral("VERB"),
                                            QStringLiteral("payload"), false);
        QVERIFY(!ok);
        QCOMPARE(host.connectionRequests.size(), 1);
        QCOMPARE(host.connectionRequests.first(), QStringLiteral("ActiveNet"));
        QCOMPARE(host.systemLines.size(), 1);
        QCOMPARE(host.systemLines.first().first, QStringLiteral("ActiveNet"));
    }

    void mcDataExplicitNetworkRoutesThere() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        SettingsPaths paths;
        paths.configDir = tmp.path();
        paths.settingsPath = QDir(tmp.path()).filePath(QStringLiteral("settings.json"));
        SettingsStore store(paths);

        const QString scriptsDir = QDir(tmp.path()).filePath(QStringLiteral("scripts"));
        QDir().mkpath(scriptsDir);

        QWidget parent;
        FakeHost host(store, &parent);
        ScriptBridge bridge(host, scriptsDir);

        bridge.scriptMcData(QStringLiteral("OtherNet"), QStringLiteral("#room"),
                            QStringLiteral("svc"), QStringLiteral("VERB"),
                            QStringLiteral("payload"), false);
        QCOMPARE(host.connectionRequests.size(), 1);
        QCOMPARE(host.connectionRequests.first(), QStringLiteral("OtherNet"));
    }
};

QTEST_MAIN(ScriptBridgeTest)
#include "script_bridge_test.moc"
