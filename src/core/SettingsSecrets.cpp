#include "core/SettingsSecrets.h"

#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
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
QVariantMap credentialBindings(const QVariantMap& settings, const QVariantMap& secrets) {
    QVariantMap bindings;
    const auto fingerprint = [](const QVariantMap& identity) {
        return QString::fromLatin1(QCryptographicHash::hash(
            QJsonDocument::fromVariant(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    };
    const QVariantList networks = settings.value(QStringLiteral("networks")).toList();
    for (auto it = secrets.cbegin(); it != secrets.cend(); ++it) {
        if (it.key().startsWith(QLatin1String("/networks/"))) {
            const QStringList parts = it.key().split(QLatin1Char('/'));
            bool valid = false;
            const int index = parts.value(2).toInt(&valid);
            QVariantMap identity;
            if (valid && index >= 0 && index < networks.size()) {
                const QVariantMap network = networks.at(index).toMap();
                for (const char* field : {"host", "port", "tls", "servers", "account", "username",
                                         "proxy_type", "proxy_host", "proxy_port", "proxy_username"})
                    identity.insert(QLatin1String(field), network.value(QLatin1String(field)));
                for (const char* field : {"accept_invalid_cert", "allow_insecure_auth"})
                    identity.insert(QLatin1String(field), network.value(QLatin1String(field)).toBool());
                if (network.value(QStringLiteral("account")).toString().trimmed().isEmpty())
                    identity.insert(QStringLiteral("nick"), network.value(QStringLiteral("nick")));
            } else { identity.insert(QStringLiteral("missing"), true); }
            bindings.insert(QStringLiteral("/networks/") + parts.value(2), fingerprint(identity));
        } else if (it.key() == QLatin1String("/imgbox_password")) {
            bindings.insert(it.key(), fingerprint({{QStringLiteral("username"), settings.value(QStringLiteral("imgbox_username"))}}));
        }
    }
    return bindings;
}
QByteArray encodeCredentialEnvelope(const QVariantMap& settings, const QVariantMap& secrets) {
    return QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("bindings"), QJsonObject::fromVariantMap(credentialBindings(settings, secrets))},
        {QStringLiteral("values"), QJsonObject::fromVariantMap(secrets)}}).toJson(QJsonDocument::Compact);
}
bool decodeCredentialEnvelope(const QVariantMap& settings, const QByteArray& bytes, QVariantMap& secrets) {
    secrets.clear();
    if (bytes.size() > 128 * 1024) return false;
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    if (!document.isObject()) return false;
    const auto object = document.object();
    if (object.value(QStringLiteral("version")) != QJsonValue(1) ||
        !object.value(QStringLiteral("values")).isObject() || !object.value(QStringLiteral("bindings")).isObject()) return false;
    const QVariantMap values = object.value(QStringLiteral("values")).toObject().toVariantMap();
    if (object.value(QStringLiteral("bindings")).toObject().toVariantMap() != credentialBindings(settings, values)) return false;
    secrets = values;
    return true;
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
