#include "core/SettingsSecrets.h"

#include <QSet>
#include <QStringList>

namespace maxchat::core {
namespace {
bool secretKey(const QString& key) {
    static const QSet<QString> keys = {
        QStringLiteral("password"), QStringLiteral("server_pass"),
        QStringLiteral("proxy_password"), QStringLiteral("nickserv"),
        QStringLiteral("sasl"), QStringLiteral("sasl_password"),
        QStringLiteral("nickserv_password"), QStringLiteral("imgbb_api_key"),
        QStringLiteral("imgur_client_id"), QStringLiteral("postimages_token"),
        QStringLiteral("imgbox_password"), QStringLiteral("token_id"),
        QStringLiteral("token_secret")};
    const QString normalized = key.toLower();
    return keys.contains(normalized) || normalized.endsWith(QLatin1String("_password")) ||
           normalized.endsWith(QLatin1String("_api_key")) ||
           normalized.endsWith(QLatin1String("_token")) ||
           normalized.endsWith(QLatin1String("_secret"));
}
QString part(QString key) {
    return key.replace(QLatin1Char('~'), QStringLiteral("~0"))
              .replace(QLatin1Char('/'), QStringLiteral("~1"));
}
QVariant walk(const QVariant& value, const QString& path, QVariantMap* extracted,
              const QVariantMap* restored) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap result = value.toMap();
        for (auto it = result.begin(); it != result.end(); ++it) {
            const QString child = path + QLatin1Char('/') + part(it.key());
            const bool legacyFlag = (it.key() == QLatin1String("sasl") || it.key() == QLatin1String("nickserv")) &&
                                    it.value().metaType().id() == QMetaType::Bool;
            if (secretKey(it.key()) && !legacyFlag) {
                if (extracted) {
                    if (!it.value().isNull() &&
                        !(it.value().metaType().id() == QMetaType::QString && it.value().toString().isEmpty()))
                        extracted->insert(child, it.value());
                    it.value() = QString();
                } else if (restored->contains(child)) {
                    it.value() = restored->value(child);
                }
            } else {
                it.value() = walk(it.value(), child, extracted, restored);
            }
        }
        return result;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList result = value.toList();
        for (qsizetype i = 0; i < result.size(); ++i)
            result[i] = walk(result[i], path + QLatin1Char('/') + QString::number(i), extracted, restored);
        return result;
    }
    return value;
}
} // namespace

SettingsSecrets splitSettingsSecrets(const QVariantMap& settings) {
    SettingsSecrets result;
    QVariantMap source = settings;
    source.remove(QLatin1String(CredentialReferenceKey));
    result.settings = walk(source, {}, &result.secrets, nullptr).toMap();
    return result;
}
QVariantMap restoreSettingsSecrets(const QVariantMap& settings, const QVariantMap& secrets) {
    return walk(settings, {}, nullptr, &secrets).toMap();
}
QVariantMap preserveLocalSecrets(const QVariantMap& imported, const QVariantMap& local) {
    QVariantMap result = imported;
    for (auto it = local.cbegin(); it != local.cend(); ++it) {
        if (secretKey(it.key()) && it.value().metaType().id() == QMetaType::QString) {
            if (result.value(it.key()).toString().isEmpty()) result.insert(it.key(), it.value());
        } else if (it.value().metaType().id() == QMetaType::QVariantMap) {
            const QVariantMap nested = preserveLocalSecrets(result.value(it.key()).toMap(), it.value().toMap());
            if (!nested.isEmpty()) result.insert(it.key(), nested);
        }
    }
    return result;
}
} // namespace maxchat::core
