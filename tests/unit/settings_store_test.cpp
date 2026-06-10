#include "core/SettingsStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using maxchat::core::defaultNetworkConfigs;
using maxchat::core::NetworkConfig;
using maxchat::core::NetworkConfigList;
using maxchat::core::networkConfigListFromVariant;
using maxchat::core::NetworksMergeVersion;
using maxchat::core::SettingsPaths;
using maxchat::core::SettingsStore;

class SettingsStoreTest final : public QObject {
    Q_OBJECT

  private:
    static SettingsStore makeStore(QTemporaryDir& dir) {
        SettingsPaths paths;
        paths.configDir = QDir(dir.path()).filePath(QStringLiteral("config/maxchat"));
        paths.cacheDir = QDir(dir.path()).filePath(QStringLiteral("cache/maxchat"));
        paths.settingsPath = QDir(paths.configDir).filePath(QStringLiteral("settings.json"));
        return SettingsStore(paths);
    }

    static void writeText(const QString& path, const QByteArray& data) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(data), qint64(data.size()));
    }

    static qsizetype indexOfNetwork(const NetworkConfigList& networks, const QString& name) {
        for (qsizetype i = 0; i < networks.size(); ++i) {
            if (networks.at(i).value(QStringLiteral("name")).toString() == name) {
                return i;
            }
        }
        return -1;
    }

  private slots:
    void defaultSettingsContainLaunchCriticalDefaults() {
        const QVariantMap settings = SettingsStore::defaultSettings();

        QCOMPARE(settings.value(QStringLiteral("theme")).toString(), QStringLiteral("synthwave"));
        QCOMPARE(settings.value(QStringLiteral("chat_theme")).toString(), QStringLiteral("follow"));
        QCOMPARE(settings.value(QStringLiteral("interface_language")).toString(),
                 QStringLiteral("system"));
        QCOMPARE(settings.value(QStringLiteral("spellcheck_enabled")).toBool(), true);
        QCOMPARE(settings.value(QStringLiteral("spell_language")).toString(), QStringLiteral("en"));
        QCOMPARE(settings.value(QStringLiteral("app_font_family")).toString(),
                 QStringLiteral("JetBrains Mono"));
        QCOMPARE(settings.value(QStringLiteral("chat_font_bold")).toBool(), true);
        QCOMPARE(settings.value(QStringLiteral("nick_width")).toInt(), 16);
        QCOMPARE(settings.value(QStringLiteral("server_list_visible")).toBool(), true);
        QCOMPARE(settings.value(QStringLiteral("member_list_visible")).toBool(), true);
        QCOMPARE(settings.value(QStringLiteral("show_button_bar")).toBool(), false);
        QCOMPARE(settings.value(QStringLiteral("connect_on_start")).toBool(), false);

        const NetworkConfigList networks =
            networkConfigListFromVariant(settings.value(QStringLiteral("networks")));
        QCOMPARE(networks.size(), 146);
        QCOMPARE(networks.first().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Libera.Chat"));
    }

    void defaultNetworkConfigsMatchServerListShape() {
        const NetworkConfigList networks = defaultNetworkConfigs();

        const qsizetype efnetIndex = indexOfNetwork(networks, QStringLiteral("EFnet"));
        QVERIFY(efnetIndex >= 0);
        QCOMPARE(networks.at(efnetIndex).value(QStringLiteral("host")).toString(),
                 QStringLiteral("irc.efnet.org"));
        QCOMPARE(networks.at(efnetIndex).value(QStringLiteral("website")).toString(),
                 QStringLiteral("https://www.efnet.org/"));
        QVERIFY(networks.at(efnetIndex)
                    .value(QStringLiteral("servers"))
                    .toStringList()
                    .contains(QStringLiteral("irc.underworld.no:+6697")));
    }

    void rawSettingsRoundTripAtomically() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const SettingsStore store = makeStore(dir);

        QVariantMap settings;
        settings.insert(QStringLiteral("theme"), QStringLiteral("system"));
        settings.insert(QStringLiteral("spellcheck_enabled"), false);
        QVERIFY(store.saveRaw(settings));

        const QVariantMap loaded = store.loadRaw();
        QCOMPARE(loaded.value(QStringLiteral("theme")).toString(), QStringLiteral("system"));
        QCOMPARE(loaded.value(QStringLiteral("spellcheck_enabled")).toBool(), false);
        QVERIFY(QFile::exists(store.paths().settingsPath));
    }

    void invalidOrMissingSettingsLoadAsEmpty() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const SettingsStore store = makeStore(dir);

        QVERIFY(store.loadRaw().isEmpty());
        writeText(store.paths().settingsPath, QByteArrayLiteral("{not-json"));

        QVERIFY(store.loadRaw().isEmpty());
    }

    void loadWithDefaultsOverlaysSavedValues() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const SettingsStore store = makeStore(dir);

        QVERIFY(store.saveRaw({
            {QStringLiteral("theme"), QStringLiteral("system")},
            {QStringLiteral("chat_font_size"), 18},
        }));

        const QVariantMap settings = store.loadWithDefaults();
        QCOMPARE(settings.value(QStringLiteral("theme")).toString(), QStringLiteral("system"));
        QCOMPARE(settings.value(QStringLiteral("chat_font_size")).toInt(), 18);
        QCOMPARE(settings.value(QStringLiteral("spellcheck_enabled")).toBool(), true);
        QVERIFY(
            !networkConfigListFromVariant(settings.value(QStringLiteral("networks"))).isEmpty());
    }

    void resetServerListPreservesOtherSettingsAndResetsMergeVersion() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const SettingsStore store = makeStore(dir);

        QVERIFY(store.saveRaw({
            {QStringLiteral("theme"), QStringLiteral("system")},
            {QStringLiteral("networks_merge_version"), 999},
            {QStringLiteral("networks"),
             QVariantList{NetworkConfig{{QStringLiteral("name"), QStringLiteral("Old")},
                                        {QStringLiteral("host"), QStringLiteral("old.example")}}}},
        }));

        QVERIFY(store.resetServerList());
        const QVariantMap settings = store.loadRaw();
        QCOMPARE(settings.value(QStringLiteral("theme")).toString(), QStringLiteral("system"));
        QCOMPARE(settings.value(QStringLiteral("networks_merge_version")).toInt(), 0);
        const NetworkConfigList networks =
            networkConfigListFromVariant(settings.value(QStringLiteral("networks")));
        QCOMPARE(networks.size(), 146);
        QCOMPARE(networks.first().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Libera.Chat"));
    }

    void prepareImportedSettingsKeepsCurrentCatalogAndResetsMergeVersion() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const SettingsStore store = makeStore(dir);

        QVariantMap imported;
        imported.insert(QStringLiteral("theme"), QStringLiteral("system"));
        imported.insert(QStringLiteral("networks_merge_version"), 999);
        imported.insert(QStringLiteral("networks"),
                        QVariantList{NetworkConfig{
                            {QStringLiteral("name"), QStringLiteral("Libera.Chat")},
                            {QStringLiteral("host"), QStringLiteral("stale.example")},
                            {QStringLiteral("password"), QStringLiteral("secret")},
                        }});

        const QVariantMap prepared = store.prepareImportedSettings(imported);

        QCOMPARE(prepared.value(QStringLiteral("theme")).toString(), QStringLiteral("system"));
        QCOMPARE(prepared.value(QStringLiteral("networks_merge_version")).toInt(), 0);
        const NetworkConfigList networks =
            networkConfigListFromVariant(prepared.value(QStringLiteral("networks")));
        QCOMPARE(networks.first().value(QStringLiteral("host")).toString(),
                 QStringLiteral("irc.libera.chat"));
        QCOMPARE(networks.first().value(QStringLiteral("password")).toString(),
                 QStringLiteral("secret"));
        QCOMPARE(imported.value(QStringLiteral("networks"))
                     .toList()
                     .first()
                     .toMap()
                     .value(QStringLiteral("host"))
                     .toString(),
                 QStringLiteral("stale.example"));
    }

    void mergeDefaultNetworksFillsFailoversAndAppendsMissingDefaults() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const SettingsStore store = makeStore(dir);

        QVERIFY(store.saveRaw({
            {QStringLiteral("networks_merge_version"), 0},
            {QStringLiteral("networks"),
             QVariantList{
                 NetworkConfig{{QStringLiteral("name"), QStringLiteral("Libera.Chat")},
                               {QStringLiteral("host"), QStringLiteral("custom.libera.example")},
                               {QStringLiteral("servers"),
                                QStringList{QStringLiteral("custom.libera.example:6667")}}},
                 NetworkConfig{{QStringLiteral("name"), QStringLiteral("My Bouncer")},
                               {QStringLiteral("host"), QStringLiteral("bouncer.example")}},
             }},
        }));

        QVERIFY(store.mergeDefaultNetworks());
        const QVariantMap settings = store.loadRaw();
        QCOMPARE(settings.value(QStringLiteral("networks_merge_version")).toInt(),
                 NetworksMergeVersion);

        const NetworkConfigList networks =
            networkConfigListFromVariant(settings.value(QStringLiteral("networks")));
        QCOMPARE(networks.at(0).value(QStringLiteral("host")).toString(),
                 QStringLiteral("custom.libera.example"));
        QCOMPARE(networks.at(0).value(QStringLiteral("website")).toString(),
                 QStringLiteral("https://libera.chat/"));
        QVERIFY(networks.at(0)
                    .value(QStringLiteral("servers"))
                    .toStringList()
                    .contains(QStringLiteral("irc.libera.chat:6667")));
        QCOMPARE(networks.at(1).value(QStringLiteral("name")).toString(),
                 QStringLiteral("My Bouncer"));
        QVERIFY(indexOfNetwork(networks, QStringLiteral("EFnet")) > 1);
    }

    void mergeDefaultNetworksIsNoOpWhenAlreadyCurrent() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const SettingsStore store = makeStore(dir);

        QVERIFY(store.saveRaw({
            {QStringLiteral("networks_merge_version"), NetworksMergeVersion},
            {QStringLiteral("networks"),
             QVariantList{NetworkConfig{{QStringLiteral("name"), QStringLiteral("Only")},
                                        {QStringLiteral("host"), QStringLiteral("only.example")}}}},
        }));

        QVERIFY(store.mergeDefaultNetworks());
        const NetworkConfigList networks =
            networkConfigListFromVariant(store.loadRaw().value(QStringLiteral("networks")));
        QCOMPARE(networks.size(), 1);
        QCOMPARE(networks.first().value(QStringLiteral("host")).toString(),
                 QStringLiteral("only.example"));
    }
};

QTEST_MAIN(SettingsStoreTest)

#include "settings_store_test.moc"
