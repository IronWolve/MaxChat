#include "core/SettingsStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using maxchat::core::defaultNetworkConfigs;
using maxchat::core::NetworkConfig;
using maxchat::core::NetworkConfigList;
using maxchat::core::networkConfigListFromVariant;
using maxchat::core::NetworksMergeVersion;
using maxchat::core::SettingsPaths;
using maxchat::core::SettingsStore;

class FakeSecretStore final : public maxchat::core::SecretStore {
public:
    QHash<QString, QByteArray> values;
    bool failRead = false;
    bool failWrite = false;
    int reads = 0;
    int writes = 0;
    std::function<void()> afterWrite;
    bool read(const QString& id, QByteArray& value, QString& error) override {
        ++reads;
        if (failRead || !values.contains(id)) { error = QStringLiteral("Keychain unavailable"); return false; }
        value = values.value(id);
        return true;
    }
    bool write(const QString& id, const QByteArray& value, QString& error) override {
        ++writes;
        if (failWrite) { error = QStringLiteral("Keychain unavailable"); return false; }
        values.insert(id, value);
        if (afterWrite) afterWrite();
        return true;
    }
    bool remove(const QString& id) override { values.remove(id); return true; }
};

class SettingsStoreTest final : public QObject {
    Q_OBJECT

  private:
    static SettingsStore makeStore(QTemporaryDir& dir,
                                   std::shared_ptr<FakeSecretStore> secrets = std::make_shared<FakeSecretStore>()) {
        SettingsPaths paths;
        paths.configDir = QDir(dir.path()).filePath(QStringLiteral("config/maxchat"));
        paths.cacheDir = QDir(dir.path()).filePath(QStringLiteral("cache/maxchat"));
        paths.settingsPath = QDir(paths.configDir).filePath(QStringLiteral("settings.json"));
        return SettingsStore(paths, std::move(secrets));
    }

    static QByteArray diskContents(const SettingsStore& store) {
        QFile file(store.paths().settingsPath);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
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
    void credentialsRoundTripWithoutAppearingInSettingsJson() {
        QTemporaryDir dir;
        auto vault = std::make_shared<FakeSecretStore>();
        const SettingsStore store = makeStore(dir, vault);
        const QVariantMap settings{
            {QStringLiteral("theme"), QStringLiteral("system")},
            {QStringLiteral("imgbb_api_key"), QStringLiteral("test-only-image-key")},
            {QStringLiteral("postimages_token"), QStringLiteral("test-only-upload-token")},
            {QStringLiteral("imgbox_password"), QStringLiteral("test-only-image-password")},
            {QStringLiteral("networks"), QVariantList{QVariantMap{
                {QStringLiteral("name"), QStringLiteral("Example")},
                {QStringLiteral("host"), QStringLiteral("irc.example")},
                {QStringLiteral("password"), QStringLiteral("test-only-sasl-password")},
                {QStringLiteral("server_pass"), QStringLiteral("test-only-server-password")},
                {QStringLiteral("proxy_password"), QStringLiteral("test-only-proxy-password")}}}}};
        QVERIFY(store.saveRaw(settings));
        QVERIFY(!diskContents(store).contains("test-only-"));
        QVERIFY(diskContents(store).contains("_maxchat_credentials"));
        QCOMPARE(vault->values.size(), 1);
        const SettingsStore reopened(store.paths(), vault);
        QCOMPARE(reopened.loadRaw(), settings);
        const int writes = vault->writes;
        QVariantMap edited = reopened.loadRaw();
        edited.insert(QStringLiteral("theme"), QStringLiteral("other"));
        QVERIFY(reopened.saveRaw(edited));
        QCOMPARE(vault->writes, writes); // Ordinary preference saves reuse the vault entry.
    }

    void failedKeychainWritePreservesLastCommittedSettingsAndSecrets() {
        QTemporaryDir dir;
        auto vault = std::make_shared<FakeSecretStore>();
        const SettingsStore store = makeStore(dir, vault);
        QVariantMap settings{{QStringLiteral("password"), QStringLiteral("first-test-secret")}};
        QVERIFY(store.saveRaw(settings));
        const QByteArray before = diskContents(store);
        const auto values = vault->values;
        vault->failWrite = true;
        settings.insert(QStringLiteral("password"), QStringLiteral("second-test-secret"));
        QVERIFY(!store.saveRaw(settings));
        QCOMPARE(diskContents(store), before);
        QCOMPARE(vault->values, values);
        QVERIFY(!store.errorString().isEmpty());
    }

    void numericAndStructuredSecretValuesCannotLeakThroughExports() {
        QTemporaryDir dir;
        auto vault = std::make_shared<FakeSecretStore>();
        const SettingsStore store = makeStore(dir, vault);
        const QVariantMap settings{
            {QStringLiteral("token_id"), 12345678},
            {QStringLiteral("password"), QVariantMap{{QStringLiteral("value"), QStringLiteral("structured-test-secret")}}},
            {QStringLiteral("sasl"), true}};
        QVERIFY(store.saveRaw(settings));
        QVERIFY(!diskContents(store).contains("12345678"));
        QVERIFY(!diskContents(store).contains("structured-test-secret"));
        QVERIFY(store.loadPublicWithDefaults().value(QStringLiteral("sasl")).toBool());
        const SettingsStore reopened(store.paths(), vault);
        QCOMPARE(reopened.loadRaw(), settings);
    }

    void explicitLegacyEditTakesPrecedenceDuringMigration() {
        QTemporaryDir dir;
        auto vault = std::make_shared<FakeSecretStore>();
        const SettingsStore store = makeStore(dir, vault);
        QVERIFY(store.saveRaw({{QStringLiteral("password"), QStringLiteral("old-test-secret")}}));
        auto disk = QJsonDocument::fromJson(diskContents(store)).object();
        disk.insert(QStringLiteral("password"), QStringLiteral("new-test-secret"));
        writeText(store.paths().settingsPath, QJsonDocument(disk).toJson());
        const SettingsStore reopened(store.paths(), vault);
        QVERIFY(reopened.migrateCredentials());
        QCOMPARE(reopened.loadRaw().value(QStringLiteral("password")).toString(), QStringLiteral("new-test-secret"));
        QVERIFY(!diskContents(reopened).contains("new-test-secret"));
        QCOMPARE(vault->values.size(), 1);
    }

    void lockedKeychainCannotEraseStoredPasswords() {
        QTemporaryDir dir;
        auto vault = std::make_shared<FakeSecretStore>();
        const SettingsStore store = makeStore(dir, vault);
        QVERIFY(store.saveRaw({{QStringLiteral("password"), QStringLiteral("test-only-password")}}));
        const QByteArray before = diskContents(store);
        vault->failRead = true;
        const SettingsStore locked(store.paths(), vault);
        QVariantMap settings = locked.loadRaw();
        QVERIFY(settings.value(QStringLiteral("password")).toString().isEmpty());
        settings.insert(QStringLiteral("theme"), QStringLiteral("other"));
        QVERIFY(!locked.saveRaw(settings));
        QCOMPARE(diskContents(store), before);
        QCOMPARE(vault->values.size(), 1);
    }

    void migrationOnlyReplacesPlaintextAfterSuccessfulSecureWrite() {
        QTemporaryDir dir;
        auto vault = std::make_shared<FakeSecretStore>();
        const SettingsStore store = makeStore(dir, vault);
        const QByteArray original = R"({"password":"legacy-test-secret","theme":"system"})";
        writeText(store.paths().settingsPath, original);
        vault->failWrite = true;
        QVERIFY(!store.migrateCredentials());
        QCOMPARE(diskContents(store), original);
        vault->failWrite = false;
        QVERIFY(store.migrateCredentials());
        QVERIFY(!diskContents(store).contains("legacy-test-secret"));
        QCOMPARE(store.loadRaw().value(QStringLiteral("password")).toString(), QStringLiteral("legacy-test-secret"));
        QCOMPARE(vault->values.size(), 1);
    }

    void explicitRecoveryKeepsPreferencesWhenKeychainIsLost() {
        QTemporaryDir dir;
        auto vault = std::make_shared<FakeSecretStore>();
        const SettingsStore store = makeStore(dir, vault);
        QVERIFY(store.saveRaw({{QStringLiteral("password"), QStringLiteral("test-only-password")},
                               {QStringLiteral("theme"), QStringLiteral("system")},
                               {QStringLiteral("host"), QStringLiteral("irc.example")}}));
        vault->failRead = true;
        const SettingsStore lost(store.paths(), vault);
        (void)lost.loadRaw();
        QVERIFY(lost.forgetCredentials());
        const QVariantMap recovered = lost.loadRaw();
        QCOMPARE(recovered.value(QStringLiteral("theme")).toString(), QStringLiteral("system"));
        QCOMPARE(recovered.value(QStringLiteral("host")).toString(), QStringLiteral("irc.example"));
        QVERIFY(recovered.value(QStringLiteral("password")).toString().isEmpty());
        QVERIFY(!diskContents(lost).contains("_maxchat_credentials"));
        QVERIFY(lost.saveRaw(recovered));
    }

    void failedFileCommitRemovesOnlyTheNewCredentialRevision() {
        QTemporaryDir dir;
        auto vault = std::make_shared<FakeSecretStore>();
        const SettingsStore store = makeStore(dir, vault);
        vault->afterWrite = [&store]() { QDir().mkpath(store.paths().settingsPath); };
        QVERIFY(!store.saveRaw({{QStringLiteral("password"), QStringLiteral("test-only-password")}}));
        QVERIFY(vault->values.isEmpty());
        QVERIFY(QFileInfo(store.paths().settingsPath).isDir());
    }

    void clearingPasswordsRetiresTheirCredentialEntry() {
        QTemporaryDir dir;
        auto vault = std::make_shared<FakeSecretStore>();
        const SettingsStore store = makeStore(dir, vault);
        QVERIFY(store.saveRaw({{QStringLiteral("password"), QStringLiteral("test-only-password")}}));
        QVERIFY(store.saveRaw({{QStringLiteral("password"), QString()}}));
        QVERIFY(vault->values.isEmpty());
        QVERIFY(!diskContents(store).contains("_maxchat_credentials"));
    }

    void exportDoesNotUnlockKeychainOrIncludeLegacyPasswords() {
        QTemporaryDir dir;
        auto vault = std::make_shared<FakeSecretStore>();
        const SettingsStore store = makeStore(dir, vault);
        writeText(store.paths().settingsPath,
                  R"({"imgbb_api_key":"legacy-test-secret","networks":[{"name":"Example","password":"nested-test-secret"}]})");
        const QByteArray legacyExport = QJsonDocument::fromVariant(store.loadPublicWithDefaults()).toJson();
        QVERIFY(!legacyExport.contains("test-secret"));
        QVERIFY(store.migrateCredentials());
        vault->failRead = true;
        const int reads = vault->reads;
        const SettingsStore locked(store.paths(), vault);
        const QByteArray exported = QJsonDocument::fromVariant(locked.loadPublicWithDefaults()).toJson();
        QVERIFY(!exported.contains("test-secret"));
        QVERIFY(!exported.contains("_maxchat_credentials"));
        QCOMPARE(vault->reads, reads);
    }

    void redactedImportsKeepLocalSecretsOnlyForTheSameEndpoint() {
        QTemporaryDir dir;
        auto vault = std::make_shared<FakeSecretStore>();
        const SettingsStore store = makeStore(dir, vault);
        const QVariantMap localNetwork{
            {QStringLiteral("name"), QStringLiteral("Private example")},
            {QStringLiteral("host"), QStringLiteral("irc.example")},
            {QStringLiteral("password"), QStringLiteral("local-test-secret")}};
        QVERIFY(store.saveRaw({{QStringLiteral("networks"), QVariantList{localNetwork}}}));
        QVariantMap imported = SettingsStore::withoutSecrets(store.loadRaw());
        imported.insert(QStringLiteral("_maxchat_credentials"), QStringLiteral("untrusted-reference"));
        const QVariantMap prepared = store.prepareImportedSettings(imported);
        QVERIFY(!prepared.contains(QStringLiteral("_maxchat_credentials")));
        QCOMPARE(prepared.value(QStringLiteral("networks")).toList().first().toMap()
                     .value(QStringLiteral("password")).toString(), QStringLiteral("local-test-secret"));
        QVariantMap redirected = imported.value(QStringLiteral("networks")).toList().first().toMap();
        redirected.insert(QStringLiteral("host"), QStringLiteral("different.example"));
        imported.insert(QStringLiteral("networks"), QVariantList{redirected});
        const QVariantMap rejected = store.prepareImportedSettings(imported);
        QVERIFY(rejected.value(QStringLiteral("networks")).toList().first().toMap()
                    .value(QStringLiteral("password")).toString().isEmpty());
    }

    void defaultSettingsContainLaunchCriticalDefaults() {
        const QVariantMap settings = SettingsStore::defaultSettings();

        QCOMPARE(settings.value(QStringLiteral("theme")).toString(), QStringLiteral("synthwave"));
        QCOMPARE(settings.value(QStringLiteral("chat_theme")).toString(), QStringLiteral("follow"));
        QCOMPARE(settings.value(QStringLiteral("interface_language")).toString(),
                 QStringLiteral("en"));
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
