#pragma once

#include <QVariantMap>

namespace maxchat::core {
inline constexpr auto CredentialReferenceKey = "_maxchat_credentials";
struct SettingsSecrets {
    QVariantMap settings;
    QVariantMap secrets;
};
[[nodiscard]] SettingsSecrets splitSettingsSecrets(const QVariantMap& settings);
[[nodiscard]] QVariantMap restoreSettingsSecrets(const QVariantMap& settings,
                                                const QVariantMap& secrets);
[[nodiscard]] QVariantMap preserveLocalSecrets(const QVariantMap& imported,
                                              const QVariantMap& local);
} // namespace maxchat::core
