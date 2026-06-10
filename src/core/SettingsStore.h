#pragma once

#include "core/NetworkImport.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>

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
    explicit SettingsStore(SettingsPaths paths = standardSettingsPaths());

    [[nodiscard]] const SettingsPaths& paths() const;
    [[nodiscard]] QVariantMap loadRaw() const;
    [[nodiscard]] QVariantMap loadWithDefaults() const;
    [[nodiscard]] bool saveRaw(const QVariantMap& settings) const;
    [[nodiscard]] bool setValue(const QString& key, const QVariant& value) const;
    [[nodiscard]] bool resetServerList() const;
    [[nodiscard]] QVariantMap prepareImportedSettings(const QVariantMap& imported) const;
    [[nodiscard]] bool mergeDefaultNetworks() const;

    [[nodiscard]] static QVariantMap defaultSettings();

  private:
    SettingsPaths paths_;
};

} // namespace maxchat::core
