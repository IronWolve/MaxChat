#include "core/SettingsStore.h"

#include "core/CommandAlias.h"
#include "core/DefaultNetworks.h"
#include "core/SettingsSecrets.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

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

SettingsStore::SettingsStore(SettingsPaths paths, std::shared_ptr<SecretStore> secrets)
    : paths_(std::move(paths)), secrets_(std::move(secrets)) {
  if (paths_.settingsPath.isEmpty() && !paths_.configDir.isEmpty()) {
    paths_.settingsPath =
        QDir(paths_.configDir).filePath(QStringLiteral("settings.json"));
  }
}

const SettingsPaths &SettingsStore::paths() const { return paths_; }

QString SettingsStore::errorString() const { return error_; }

void SettingsStore::setErrorHandler(std::function<void(const QString&)> handler) {
  errorHandler_ = std::move(handler);
  if (errorHandler_ && !error_.isEmpty()) errorHandler_(error_);
}

void SettingsStore::reportError(const QString& message) const {
  const bool changed = error_ != message;
  error_ = message;
  if (changed && errorHandler_) errorHandler_(message);
}

QVariantMap SettingsStore::withoutSecrets(const QVariantMap& settings) {
  return splitSettingsSecrets(settings).settings;
}

QVariantMap SettingsStore::loadRaw() const {
  // Stat-validated cache: re-parse only when the file changed on disk (the
  // Python app can write the same file, so the mtime/size check stays).
  const QFileInfo info(paths_.settingsPath);
  if (cacheValid_ && info.exists() && info.lastModified() == cachedMtime_ &&
      info.size() == cachedSize_) {
    return cachedRaw_;
  }

  QFile file(paths_.settingsPath);
  if (!file.open(QIODevice::ReadOnly)) {
    cacheValid_ = false;
    credentialId_.clear();
    storedSecrets_.clear();
    credentialsUnavailable_ = info.exists();
    legacyCredentials_ = false;
    if (credentialsUnavailable_)
      reportError(QStringLiteral("Existing settings could not be read; they will not be overwritten."));
    return {};
  }

  QJsonParseError error;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    cacheValid_ = false;
    credentialsUnavailable_ = true;
    reportError(QStringLiteral("Existing settings are not valid JSON; they will not be overwritten."));
    return {};
  }
  cachedRaw_ = document.object().toVariantMap();
  const QVariant reference = cachedRaw_.take(QLatin1String(CredentialReferenceKey));
  credentialId_ = reference.toString();
  credentialsUnavailable_ = false;
  storedSecrets_.clear();
  const QVariantMap legacySecrets = splitSettingsSecrets(cachedRaw_).secrets;
  legacyCredentials_ = !legacySecrets.isEmpty();
  if (reference.isValid()) {
    QByteArray bytes;
    QString error;
    const bool validId = !QUuid::fromString(credentialId_).isNull() &&
        QUuid::fromString(credentialId_).toString(QUuid::WithoutBraces) == credentialId_;
    if (!validId || !secrets_ || !secrets_->read(credentialId_, bytes, error)) {
      credentialsUnavailable_ = true;
    } else {
      const QJsonDocument values = QJsonDocument::fromJson(bytes);
      if (!values.isObject()) {
        credentialsUnavailable_ = true;
      } else {
        storedSecrets_ = values.object().toVariantMap();
        cachedRaw_ = restoreSettingsSecrets(cachedRaw_, storedSecrets_);
        // A manually added legacy password takes precedence over the old vault
        // value and is migrated on the next successful secure write.
        cachedRaw_ = restoreSettingsSecrets(cachedRaw_, legacySecrets);
      }
    }
    if (credentialsUnavailable_) {
      reportError(QStringLiteral("Saved credentials could not be read. Unlock the OS keychain and restart MaxChat before saving settings; existing credentials will be preserved."));
    }
  }
  cachedMtime_ = info.lastModified();
  cachedSize_ = info.size();
  cacheValid_ = true;
  return cachedRaw_;
}

QVariantMap SettingsStore::loadWithDefaults() const {
  QVariantMap settings = defaultSettings();
  const QVariantMap saved = loadRaw();
  for (auto it = saved.cbegin(); it != saved.cend(); ++it) {
    settings.insert(it.key(), it.value());
  }
  return settings;
}

QVariantMap SettingsStore::loadPublicWithDefaults() const {
  // Display preferences and exports do not need to unlock or read credentials.
  QVariantMap settings = defaultSettings();
  QFile file(paths_.settingsPath);
  if (file.open(QIODevice::ReadOnly)) {
    const QVariantMap saved = withoutSecrets(QJsonDocument::fromJson(file.readAll()).object().toVariantMap());
    for (auto it = saved.cbegin(); it != saved.cend(); ++it) settings.insert(it.key(), it.value());
  }
  return withoutSecrets(settings);
}

bool SettingsStore::saveRaw(const QVariantMap &settings, const bool preserveGeometry) const {
  // CONTRACT: replace-all. `settings` becomes the ENTIRE file (geom_* keys are
  // the only thing preserved when preserveGeometry is true). Callers MUST pass a
  // complete map — load loadRaw()/loadWithDefaults(), modify, then save — never a
  // partial subset, or the omitted keys are deleted. (Replace-all is deliberate:
  // it is how a key gets removed, e.g. deleting a network or resetAllSettings.)
  if (paths_.settingsPath.isEmpty()) {
    return false;
  }
  QDir().mkpath(QFileInfo(paths_.settingsPath).absolutePath());

  // Preserve geom_* keys from the existing file: window geometry is saved
  // independently (via attachGeometryPersist on QDialog::finished) and must
  // survive saveRaw calls that carry only a subset of settings (e.g. a
  // dialog's own settings() map).  Keys already present in `settings` win.
  // Pass preserveGeometry=false (e.g. resetAllSettings) to clear them too.
  QVariantMap merged = loadRaw();
  if (credentialsUnavailable_) {
    return false;
  }
  for (auto it = merged.begin(); it != merged.end(); ) {
    if (preserveGeometry && it.key().startsWith(QLatin1String("geom_")) && !settings.contains(it.key())) {
      ++it; // keep this geometry key
    } else {
      it = merged.erase(it); // will be replaced by `settings` below
    }
  }
  for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
    merged.insert(it.key(), it.value());
  }

  SettingsSecrets protectedSettings = splitSettingsSecrets(merged);
  const QString previousId = credentialId_;
  QString nextId;
  bool newCredential = false;
  if (!protectedSettings.secrets.isEmpty()) {
    if (!previousId.isEmpty() && protectedSettings.secrets == storedSecrets_) {
      nextId = previousId;
    } else {
      nextId = QUuid::createUuid().toString(QUuid::WithoutBraces);
      QString error;
      const QByteArray values = QJsonDocument::fromVariant(protectedSettings.secrets).toJson(QJsonDocument::Compact);
      if (!secrets_ || !secrets_->write(nextId, values, error)) {
        reportError(error.isEmpty() ? QStringLiteral("OS credential storage is unavailable. Existing settings were not changed; passwords will not be saved as plaintext.") : error);
        return false;
      }
      newCredential = true;
    }
    protectedSettings.settings.insert(QLatin1String(CredentialReferenceKey), nextId);
  }

  const auto rollback = [&]() {
    if (newCredential) (void)secrets_->remove(nextId);
    reportError(QStringLiteral("Could not write settings. Existing settings and credentials were preserved."));
    return false;
  };

  cacheValid_ = false; // commit below changes the file; re-stat next load
  QSaveFile file(paths_.settingsPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return rollback();
  }

  const QByteArray bytes = QJsonDocument::fromVariant(protectedSettings.settings).toJson(QJsonDocument::Indented);
  if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      file.write(bytes) != bytes.size() || !file.commit()) {
    file.cancelWriting();
    return rollback();
  }
  // Publish the new file before retiring its old credential revision. A failed
  // save cannot invalidate the last committed configuration.
  if (!previousId.isEmpty() && previousId != nextId) (void)secrets_->remove(previousId);
  credentialId_ = nextId;
  storedSecrets_ = protectedSettings.secrets;
  legacyCredentials_ = false;
  cachedRaw_ = merged;
  cachedRaw_.remove(QLatin1String(CredentialReferenceKey));
  const QFileInfo info(paths_.settingsPath);
  cachedMtime_ = info.lastModified();
  cachedSize_ = info.size();
  cacheValid_ = true;
  error_.clear();
  return true;
}

bool SettingsStore::migrateCredentials() const {
  const QVariantMap settings = loadRaw();
  if (credentialsUnavailable_) return false;
  return !legacyCredentials_ || saveRaw(settings);
}

bool SettingsStore::forgetCredentials() const {
  QFile input(paths_.settingsPath);
  if (!input.open(QIODevice::ReadOnly)) {
    reportError(QStringLiteral("Could not read settings; saved credentials were not changed."));
    return false;
  }
  const QJsonDocument original = QJsonDocument::fromJson(input.readAll());
  input.close();
  if (!original.isObject()) {
    reportError(QStringLiteral("Settings are not valid JSON; saved credentials were not changed."));
    return false;
  }
  const QVariantMap settings = original.object().toVariantMap();
  const QString oldId = settings.value(QLatin1String(CredentialReferenceKey)).toString();
  const QByteArray bytes = QJsonDocument::fromVariant(withoutSecrets(settings)).toJson(QJsonDocument::Indented);
  QSaveFile output(paths_.settingsPath);
  if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      output.write(bytes) != bytes.size() || !output.commit()) {
    output.cancelWriting();
    reportError(QStringLiteral("Could not update settings; saved credentials were not changed."));
    return false;
  }
  // If the keychain itself is inaccessible, unlink the profile's reference
  // without claiming that a locked/unavailable OS entry has been erased.
  if (!credentialsUnavailable_ && secrets_ && !QUuid::fromString(oldId).isNull())
    (void)secrets_->remove(oldId);
  cacheValid_ = false;
  cachedRaw_.clear();
  storedSecrets_.clear();
  credentialId_.clear();
  credentialsUnavailable_ = false;
  legacyCredentials_ = false;
  error_.clear();
  return true;
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
  // A copied keychain reference is not a portable credential. Legacy plaintext
  // exports remain importable, but saveRaw secures their values before writing.
  QVariantMap prepared = imported;
  prepared.remove(QLatin1String(CredentialReferenceKey));
  const QVariantMap current = loadRaw();
  const NetworkConfigList importedNetworks =
        networkConfigListFromVariant(prepared.value(QStringLiteral("networks")));
  if (!importedNetworks.isEmpty()) {
    NetworkConfigList merged =
        mergeImportedNetworks(importedNetworks, defaultNetworkConfigs());
    const NetworkConfigList existing = networkConfigListFromVariant(current.value(QStringLiteral("networks")));
    for (NetworkConfig& network : merged) {
      for (const NetworkConfig& local : existing) {
        if (network.value(QStringLiteral("name")).toString().compare(
                local.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) == 0) {
          bool sameEndpoint = true;
          for (const char* key : {"host", "port", "tls", "servers", "account", "username",
                                  "proxy_type", "proxy_host", "proxy_port", "proxy_username"}) {
            // JSON normalizes QStringList versus QVariantList after a reload.
            const auto encode = [key](const QVariantMap& value) {
              return QJsonDocument::fromVariant(QVariantMap{{QLatin1String(key), value.value(QLatin1String(key))}}).toJson(QJsonDocument::Compact);
            };
            if (encode(network) != encode(local)) sameEndpoint = false;
          }
          if (sameEndpoint) network = preserveLocalSecrets(network, local);
          break;
        }
      }
    }
    prepared.insert(QStringLiteral("networks"),
                    networkConfigListToVariantList(merged));
    prepared.insert(QStringLiteral("networks_merge_version"), 0);
  }
  // Redacted exports should not erase credentials already saved on this PC.
  QVariantMap top = current;
  top.remove(QStringLiteral("networks"));
  if (prepared.value(QStringLiteral("imgbox_username")) != current.value(QStringLiteral("imgbox_username")))
    top.remove(QStringLiteral("imgbox_password"));
  prepared = preserveLocalSecrets(prepared, top);
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
  // Built once and returned by (COW-cheap) value: loadWithDefaults() calls this
  // on every read, so the ~150 insert()s should not run each time.
  static const QVariantMap cached = []() {
  QVariantMap settings;
  settings.insert(QStringLiteral("theme"), QStringLiteral("synthwave"));
  settings.insert(QStringLiteral("chat_theme"), QStringLiteral("follow"));
  // Default to English explicitly (not "system") so the shipped builds are
  // English out of the box regardless of OS locale; users can switch to their
  // language — or "System default" — in Preferences ▸ Localization.
  settings.insert(QStringLiteral("interface_language"),
                  QStringLiteral("en"));
  settings.insert(QStringLiteral("spellcheck_enabled"), true);
  settings.insert(QStringLiteral("spellcheck_backend"), QStringLiteral("internal"));
  settings.insert(QStringLiteral("spell_language"), QStringLiteral("en"));
  settings.insert(QStringLiteral("spellcheck_autocorrect"), false);
  settings.insert(QStringLiteral("autocorrect_max_distance"), 2);
  settings.insert(QStringLiteral("og_show_site_name"),    true);
  settings.insert(QStringLiteral("og_show_title"),        true);
  settings.insert(QStringLiteral("og_show_description"),  true);
  settings.insert(QStringLiteral("og_show_image"),        true);
  settings.insert(QStringLiteral("image_upload_service"), QString());
  settings.insert(QStringLiteral("imgbb_tos"),       false);
  settings.insert(QStringLiteral("imgbb_api_key"),   QString());
  settings.insert(QStringLiteral("imgur_tos"),       false);
  settings.insert(QStringLiteral("imgur_client_id"), QString());
  settings.insert(QStringLiteral("postimages_tos"),  false);
  settings.insert(QStringLiteral("postimages_token"), QString());
  settings.insert(QStringLiteral("imgbox_tos"),      false);
  settings.insert(QStringLiteral("imgbox_username"), QString());
  settings.insert(QStringLiteral("imgbox_password"), QString());
  settings.insert(QStringLiteral("wallpaper"), QString());
  settings.insert(QStringLiteral("chat_opacity"), 100);
  settings.insert(QStringLiteral("show_timestamps"), true);
  settings.insert(QStringLiteral("timestamp_format"),
                  QStringLiteral("%I:%M %p"));
  settings.insert(QStringLiteral("colored_nicks"), true);
  settings.insert(QStringLiteral("nick_color_mode"), QStringLiteral("palette"));
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
  // Script/BBS terminals. Size 0 means "use the terminal profile's own size"
  // (ibm-vga 11, c64 13); a non-zero size overrides every profile. Default 12
  // so terminals open at a comfortable size without per-window tweaking.
  settings.insert(QStringLiteral("terminal_font_family"),
                  QStringLiteral("JetBrains Mono"));
  settings.insert(QStringLiteral("terminal_font_size"), 12);
  settings.insert(QStringLiteral("terminal_font_bold"), false);
  // Default fixed grid rows for new script/BBS terminals (80x25 or 80x40).
  settings.insert(QStringLiteral("terminal_rows"), 25);
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
  settings.insert(QStringLiteral("ctcp_respond_ping"), true);
  settings.insert(QStringLiteral("ctcp_respond_time"), true);
  settings.insert(QStringLiteral("ctcp_respond_clientinfo"), true);
  settings.insert(QStringLiteral("paste_guard"), true);
  settings.insert(QStringLiteral("paste_lines"), 4);
  settings.insert(QStringLiteral("ignore_invites"), false);
  settings.insert(QStringLiteral("invite_protect"), true);
  settings.insert(QStringLiteral("confirm_quit"), true);
  settings.insert(QStringLiteral("dcc_enabled"), false);
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
  settings.insert(QStringLiteral("notify_sound_file"), QStringLiteral("notify.wav"));
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
  // Master switch for clicked links: ON = a clicked URL opens (in the browser by
  // default; the content_services toggles above route image/audio/video links to
  // the in-app viewers instead). OFF = clicking a link does nothing.
  settings.insert(QStringLiteral("open_links_in_browser"), true);
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
  // TODO(release): flip this default to true when going live (it's off for now
  // so pre-release builds don't poll GitHub). The Notifications pref + Help >
  // Check for Updates already work; only the auto-on-startup default is gated.
  settings.insert(QStringLiteral("update_check"), false);
  // Lua script capabilities, keyed by script name — empty means every script
  // starts fully sandboxed (absent name → all-false perms). The live code key
  // is "scriptPerms"; an old "script_perms" default with the wrong shape was
  // dead code and is gone.
  settings.insert(QStringLiteral("scriptPerms"), QVariantMap());
  settings.insert(QStringLiteral("script_dirs"), QVariantList());
  // Comic Mode visual options (match the dialog's inline fallbacks so the
  // diff-save doesn't flag an untouched control as "changed").
  settings.insert(QStringLiteral("comic_balloon_tint"), true);
  settings.insert(QStringLiteral("comic_font"), QStringLiteral("Comic Relief"));
  settings.insert(QStringLiteral("networks"),
                  networkConfigListToVariantList(defaultNetworkConfigs()));
  return settings;
  }();
  return cached;
}

} // namespace maxchat::core
