#pragma once

#include <QVariantMap>
#include <QByteArray>

namespace maxchat::core {
inline constexpr auto CredentialReferenceKey = "_maxchat_credentials";
struct SettingsSecrets {
    QVariantMap settings;
    QVariantMap secrets;
};
[[nodiscard]] SettingsSecrets splitSettingsSecrets(const QVariantMap& settings);
[[nodiscard]] QVariantMap restoreSettingsSecrets(const QVariantMap& settings,
                                                const QVariantMap& secrets);
[[nodiscard]] QVariantMap credentialBindings(const QVariantMap& settings, const QVariantMap& secrets);
[[nodiscard]] QByteArray encodeCredentialEnvelope(const QVariantMap& settings, const QVariantMap& secrets);
[[nodiscard]] bool decodeCredentialEnvelope(const QVariantMap& settings, const QByteArray& bytes, QVariantMap& secrets);
[[nodiscard]] QVariantMap preserveLocalSecrets(const QVariantMap& imported,
                                              const QVariantMap& local);
} // namespace maxchat::core
