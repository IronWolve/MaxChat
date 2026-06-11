#include "core/SettingsStore.h"

#include "core/CommandAlias.h"
#include "core/DefaultNetworks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <utility>

namespace maxchat::core {

namespace {

QString fallbackHomeConfigDir() {
  return QDir::home().filePath(QStringLiteral(".config/maxchat"));
}

QString fallbackHomeCacheDir() {
  return QDir::home().filePath(QStringLiteral(".cache/maxchat"));
}

QVariantMap networkConfigFromDefaults(const NetworkDefaults &network) {
  QVariantMap config;
  config.insert(QStringLiteral("name"), network.name);
  config.insert(QStringLiteral("host"), network.primary.host);
  config.insert(QStringLiteral("port"), network.primary.port);
  config.insert(QStringLiteral("tls"), network.primary.tls);
  config.insert(QStringLiteral("nick"), network.nick);
  config.insert(QStringLiteral("username"), network.username);
  config.insert(QStringLiteral("password"), network.password);
  config.insert(QStringLiteral("channels"), network.channels);
  config.insert(QStringLiteral("website"), network.website);

  QStringList failovers;
  failovers.reserve(network.failovers.size());
  for (const auto &server : network.failovers) {
    failovers.append(serverSpec(server));
  }
  config.insert(QStringLiteral("servers"), failovers);
  return config;
}

} // namespace

SettingsPaths standardSettingsPaths() {
  const QString configRoot =
      QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  const QString cacheRoot =
      QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);

  SettingsPaths paths;
  paths.configDir =
      QDir(configRoot.isEmpty() ? fallbackHomeConfigDir() : configRoot)
          .filePath(QStringLiteral("maxchat"));
  paths.cacheDir =
      QDir(cacheRoot.isEmpty() ? fallbackHomeCacheDir() : cacheRoot)
          .filePath(QStringLiteral("maxchat"));
  paths.settingsPath =
      QDir(paths.configDir).filePath(QStringLiteral("settings.json"));
  return paths;
}

NetworkConfigList defaultNetworkConfigs() {
  NetworkConfigList networks;
  const auto defaults = defaultNetworks();
  networks.reserve(defaults.size());
  for (const NetworkDefaults &network : defaults) {
    networks.append(networkConfigFromDefaults(network));
  }
  return networks;
}

QVariantList networkConfigListToVariantList(const NetworkConfigList &networks) {
  QVariantList values;
  values.reserve(networks.size());
  for (const NetworkConfig &network : networks) {
    values.append(network);
  }
  return values;
}

NetworkConfigList networkConfigListFromVariant(const QVariant &value) {
  NetworkConfigList networks;
  const QVariantList values = value.toList();
  networks.reserve(values.size());
  for (const QVariant &item : values) {
    const QVariantMap network = item.toMap();
    if (!network.isEmpty()) {
      networks.append(network);
    }
  }
  return networks;
}

SettingsStore::SettingsStore(SettingsPaths paths) : paths_(std::move(paths)) {
  if (paths_.settingsPath.isEmpty() && !paths_.configDir.isEmpty()) {
    paths_.settingsPath =
        QDir(paths_.configDir).filePath(QStringLiteral("settings.json"));
  }
}

const SettingsPaths &SettingsStore::paths() const { return paths_; }

QVariantMap SettingsStore::loadRaw() const {
  QFile file(paths_.settingsPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }

  QJsonParseError error;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return {};
  }
  return document.object().toVariantMap();
}

QVariantMap SettingsStore::loadWithDefaults() const {
  QVariantMap settings = defaultSettings();
  const QVariantMap saved = loadRaw();
  for (auto it = saved.cbegin(); it != saved.cend(); ++it) {
    settings.insert(it.key(), it.value());
  }
  return settings;
}

bool SettingsStore::saveRaw(const QVariantMap &settings) const {
  if (paths_.settingsPath.isEmpty()) {
    return false;
  }
  QDir().mkpath(QFileInfo(paths_.settingsPath).absolutePath());

  QSaveFile file(paths_.settingsPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }

  const QJsonDocument document = QJsonDocument::fromVariant(settings);
  file.write(document.toJson(QJsonDocument::Indented));
  return file.commit();
}

bool SettingsStore::setValue(const QString &key, const QVariant &value) const {
  QVariantMap settings = loadRaw();
  settings.insert(key, value);
  return saveRaw(settings);
}

bool SettingsStore::resetServerList() const {
  QVariantMap settings = loadRaw();
  settings.insert(QStringLiteral("networks"),
                  networkConfigListToVariantList(defaultNetworkConfigs()));
  settings.insert(QStringLiteral("networks_merge_version"), 0);
  return saveRaw(settings);
}

QVariantMap
SettingsStore::prepareImportedSettings(const QVariantMap &imported) const {
  QVariantMap prepared = imported;
  const NetworkConfigList importedNetworks =
      networkConfigListFromVariant(prepared.value(QStringLiteral("networks")));
  if (!importedNetworks.isEmpty()) {
    const NetworkConfigList merged =
        mergeImportedNetworks(importedNetworks, defaultNetworkConfigs());
    prepared.insert(QStringLiteral("networks"),
                    networkConfigListToVariantList(merged));
    prepared.insert(QStringLiteral("networks_merge_version"), 0);
  }
  return prepared;
}

bool SettingsStore::mergeDefaultNetworks() const {
  QVariantMap settings = loadRaw();
  if (settings.value(QStringLiteral("networks_merge_version")).toInt() >=
      NetworksMergeVersion) {
    return true;
  }

  NetworkConfigList saved =
      networkConfigListFromVariant(settings.value(QStringLiteral("networks")));
  if (!saved.isEmpty()) {
    const NetworkConfigList defaults = defaultNetworkConfigs();
    QHash<QString, NetworkConfig> defaultsByName;
    QSet<QString> savedNames;
    for (const NetworkConfig &network : defaults) {
      defaultsByName.insert(
          network.value(QStringLiteral("name")).toString().toCaseFolded(),
          network);
    }

    for (NetworkConfig &network : saved) {
      const QString name =
          network.value(QStringLiteral("name")).toString().toCaseFolded();
      if (!name.isEmpty()) {
        savedNames.insert(name);
      }
      const NetworkConfig defaultNetwork = defaultsByName.value(name);
      if (defaultNetwork.isEmpty()) {
        continue;
      }

      QStringList servers =
          network.value(QStringLiteral("servers")).toStringList();
      QSet<QString> seen;
      for (const QString &server : servers) {
        seen.insert(server.toCaseFolded());
      }
      for (const QString &server :
           defaultNetwork.value(QStringLiteral("servers")).toStringList()) {
        if (!seen.contains(server.toCaseFolded())) {
          servers.append(server);
          seen.insert(server.toCaseFolded());
        }
      }
      network.insert(QStringLiteral("servers"), servers);

      if (network.value(QStringLiteral("website")).toString().isEmpty() &&
          !defaultNetwork.value(QStringLiteral("website"))
               .toString()
               .isEmpty()) {
        network.insert(QStringLiteral("website"),
                       defaultNetwork.value(QStringLiteral("website")));
      }
    }

    for (const NetworkConfig &network : defaults) {
      const QString name =
          network.value(QStringLiteral("name")).toString().toCaseFolded();
      if (!savedNames.contains(name)) {
        saved.append(network);
      }
    }
    settings.insert(QStringLiteral("networks"),
                    networkConfigListToVariantList(saved));
  }

  settings.insert(QStringLiteral("networks_merge_version"),
                  NetworksMergeVersion);
  return saveRaw(settings);
}

QVariantMap SettingsStore::defaultSettings() {
  QVariantMap settings;
  settings.insert(QStringLiteral("theme"), QStringLiteral("synthwave"));
  settings.insert(QStringLiteral("chat_theme"), QStringLiteral("follow"));
  settings.insert(QStringLiteral("interface_language"),
                  QStringLiteral("system"));
  settings.insert(QStringLiteral("spellcheck_enabled"), true);
  settings.insert(QStringLiteral("spell_language"), QStringLiteral("en"));
  settings.insert(QStringLiteral("wallpaper"), QString());
  settings.insert(QStringLiteral("show_timestamps"), true);
  settings.insert(QStringLiteral("timestamp_format"),
                  QStringLiteral("%I:%M %p"));
  settings.insert(QStringLiteral("colored_nicks"), true);
  settings.insert(QStringLiteral("show_formatting"), true);
  settings.insert(QStringLiteral("hide_joinpart"), false);
  settings.insert(QStringLiteral("chat_font_family"),
                  QStringLiteral("JetBrains Mono"));
  settings.insert(QStringLiteral("chat_font_size"), 14);
  settings.insert(QStringLiteral("chat_font_bold"), true);
  settings.insert(QStringLiteral("app_font_family"),
                  QStringLiteral("JetBrains Mono"));
  settings.insert(QStringLiteral("app_font_size"), 14);
  settings.insert(QStringLiteral("app_font_bold"), true);
  settings.insert(QStringLiteral("list_font_family"),
                  QStringLiteral("JetBrains Mono"));
  settings.insert(QStringLiteral("list_font_size"), 14);
  settings.insert(QStringLiteral("list_font_bold"), true);
  settings.insert(QStringLiteral("nick_font_family"),
                  QStringLiteral("JetBrains Mono"));
  settings.insert(QStringLiteral("nick_font_size"), 14);
  settings.insert(QStringLiteral("nick_font_bold"), true);
  settings.insert(QStringLiteral("status_font_family"),
                  QStringLiteral("JetBrains Mono"));
  settings.insert(QStringLiteral("status_font_size"), 14);
  settings.insert(QStringLiteral("status_font_bold"), true);
  settings.insert(QStringLiteral("topic_font_family"),
                  QStringLiteral("JetBrains Mono"));
  settings.insert(QStringLiteral("topic_font_size"), 14);
  settings.insert(QStringLiteral("topic_font_bold"), true);
  settings.insert(QStringLiteral("auto_reconnect"), true);
  settings.insert(QStringLiteral("command_aliases"), defaultCommandAliases());
  settings.insert(QStringLiteral("ignores"), QStringList());
  settings.insert(QStringLiteral("muted_channels"), QStringList());
  settings.insert(QStringLiteral("friends"), QStringList());
  settings.insert(QStringLiteral("flood_protect"), false);
  settings.insert(QStringLiteral("flood_msgs"), 10);
  settings.insert(QStringLiteral("flood_secs"), 4);
  settings.insert(QStringLiteral("scrollback"), 2000);
  settings.insert(QStringLiteral("auto_rejoin"), false);
  settings.insert(QStringLiteral("rejoin_delay"), 2);
  settings.insert(QStringLiteral("auto_away_mins"), 0);
  settings.insert(QStringLiteral("hide_version"), false);
  settings.insert(QStringLiteral("ctcp_version"), QString());
  settings.insert(QStringLiteral("paste_guard"), true);
  settings.insert(QStringLiteral("paste_lines"), 4);
  settings.insert(QStringLiteral("ignore_invites"), false);
  settings.insert(QStringLiteral("invite_protect"), true);
  settings.insert(QStringLiteral("confirm_quit"), true);
  settings.insert(QStringLiteral("dcc_dir"), QString());
  settings.insert(QStringLiteral("dcc_accept"), QStringLiteral("ask"));
  settings.insert(QStringLiteral("dcc_trusted"), QStringList());
  settings.insert(QStringLiteral("dcc_passive"), true);
  settings.insert(QStringLiteral("dcc_ip"), QString());
  settings.insert(QStringLiteral("dcc_port_first"), 0);
  settings.insert(QStringLiteral("dcc_port_last"), 0);
  settings.insert(QStringLiteral("dnd"), false);
  settings.insert(QStringLiteral("notify_popup"), QStringLiteral("custom"));
  settings.insert(QStringLiteral("notify_pm"), true);
  settings.insert(QStringLiteral("notify_highlight"), true);
  settings.insert(QStringLiteral("highlight_words"), QString());
  settings.insert(QStringLiteral("notify_flash"), true);
  settings.insert(QStringLiteral("notify_corner"), QStringLiteral("br"));
  settings.insert(QStringLiteral("notify_duration"), 6);
  settings.insert(QStringLiteral("notify_theme"), QStringLiteral("follow"));
  settings.insert(QStringLiteral("beep_highlight"), true);
  settings.insert(QStringLiteral("notify_sound"), false);
  settings.insert(QStringLiteral("ctcp_sound"), false);
  settings.insert(QStringLiteral("minimize_to_tray"), false);
  settings.insert(QStringLiteral("logging"), true);
  settings.insert(QStringLiteral("replay_log"), true);
  settings.insert(QStringLiteral("replay_lines"), 0);
  settings.insert(QStringLiteral("log_mask"), QStringLiteral("%network-%channel"));
  settings.insert(QStringLiteral("pm_echo"), true);
  settings.insert(QStringLiteral("show_mode"), true);
  settings.insert(QStringLiteral("indent_wrap"), true);
  settings.insert(QStringLiteral("marker_line"), true);
  settings.insert(QStringLiteral("nick_colors"), QVariantMap());
  settings.insert(QStringLiteral("show_input_hint"), true);
  settings.insert(QStringLiteral("strip_color_copy"), true);
  settings.insert(QStringLiteral("sort_status"), true);
  settings.insert(QStringLiteral("tray_icon"), QStringLiteral("bubble"));
  // Per-area colors; empty = follow the theme.
  settings.insert(QStringLiteral("chat_text_color"), QString());
  settings.insert(QStringLiteral("event_color"), QString());
  settings.insert(QStringLiteral("tree_color"), QString());
  settings.insert(QStringLiteral("userlist_color"), QString());
  settings.insert(QStringLiteral("nick_label_color"), QString());
  settings.insert(QStringLiteral("status_text_color"), QString());
  settings.insert(QStringLiteral("topic_color"), QString());
  settings.insert(QStringLiteral("align_nicks"), true);
  settings.insert(QStringLiteral("separator_line"), true);
  settings.insert(QStringLiteral("nick_width"), 16);
  settings.insert(QStringLiteral("nick_width_autoset"), false);
  settings.insert(QStringLiteral("word_wrap"), true);
  QVariantMap contentServices;
  contentServices.insert(QStringLiteral("images"), true);
  contentServices.insert(QStringLiteral("media"), true);
  contentServices.insert(QStringLiteral("xcards"), true);
  contentServices.insert(QStringLiteral("webcards"), true);
  settings.insert(QStringLiteral("content_services"), contentServices);
  settings.insert(QStringLiteral("server_list_visible"), true);
  settings.insert(QStringLiteral("member_list_visible"), true);
  settings.insert(QStringLiteral("show_button_bar"), false);
  settings.insert(QStringLiteral("buffer_tabs"), false);
  settings.insert(QStringLiteral("connect_on_start"), false);
  // Used by the shortcut editor and the saved-looks menu; give them a home in
  // the central defaults so they round-trip on export (Python parity).
  settings.insert(QStringLiteral("shortcuts"), QVariantMap());
  settings.insert(QStringLiteral("looks"), QVariantMap());
  // Quietly check GitHub Releases for a newer build shortly after launch.
  settings.insert(QStringLiteral("update_check"), true);
  settings.insert(QStringLiteral("networks"),
                  networkConfigListToVariantList(defaultNetworkConfigs()));
  return settings;
}

} // namespace maxchat::core
