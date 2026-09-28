#pragma once

#include "core/NetworkImport.h"
#include "core/SecretStore.h"

#include <QString>
#include <QVariantList>
#include <QDateTime>
#include <QVariantMap>
#include <functional>

namespace maxchat::core {

inline constexpr int NetworksMergeVersion = 4;

struct SettingsPaths {
    QString configDir;
    QString cacheDir;
    QString settingsPath;
};

[[nodiscard]] SettingsPaths standardSettingsPaths();
[[nodiscard]] NetworkConfigList defaultNetworkConfigs();
[[nodiscard]] QVariantList networkConfigListToVariantList(const NetworkConfigList& networks);
[[nodiscard]] NetworkConfigList networkConfigListFromVariant(const QVariant& value);

class SettingsStore final {
  public:
    explicit SettingsStore(SettingsPaths paths = standardSettingsPaths(),
                           std::shared_ptr<SecretStore> secrets = platformSecretStore());

    [[nodiscard]] const SettingsPaths& paths() const;
    [[nodiscard]] QVariantMap loadRaw() const;
    [[nodiscard]] QVariantMap loadWithDefaults() const;
    [[nodiscard]] QVariantMap loadPublicWithDefaults() const;
    [[nodiscard]] bool saveRaw(const QVariantMap& settings, bool preserveGeometry = true) const;
    [[nodiscard]] bool setValue(const QString& key, const QVariant& value) const;
    [[nodiscard]] bool resetServerList() const;
    [[nodiscard]] QVariantMap prepareImportedSettings(const QVariantMap& imported) const;
    [[nodiscard]] bool mergeDefaultNetworks() const;
    [[nodiscard]] bool migrateCredentials() const;
    // Explicit user-confirmed recovery for a lost keychain or copied profile.
    [[nodiscard]] bool forgetCredentials() const;
    [[nodiscard]] QString errorString() const;
    void setErrorHandler(std::function<void(const QString&)> handler);
    [[nodiscard]] static QVariantMap withoutSecrets(const QVariantMap& settings);

    [[nodiscard]] static QVariantMap defaultSettings();

  private:
    SettingsPaths paths_;
    std::shared_ptr<SecretStore> secrets_;
    std::function<void(const QString&)> errorHandler_;
    void reportError(const QString& message) const;
    mutable QString error_;
    mutable QString credentialId_;
    mutable QVariantMap storedSecrets_;
    mutable QVariantMap storedBindings_;
    mutable bool credentialsUnavailable_ = false;
    mutable bool legacyCredentials_ = false;
    // mtime+size cache for loadRaw: startup alone used to parse settings.json
    // 11 times; an external writer (the Python app shares this file) is still
    // detected through the stat check. mutable: the load methods stay const.
    mutable QVariantMap cachedRaw_;
    mutable QDateTime cachedMtime_;
    mutable qint64 cachedSize_ = -1;
    mutable bool cacheValid_ = false;
};

} // namespace maxchat::core
