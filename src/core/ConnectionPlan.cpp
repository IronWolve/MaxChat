#include "core/ConnectionPlan.h"

#include "core/DefaultNetworks.h"

#include <QRegularExpression>
#include <QSet>
#include <QVariantList>

namespace maxchat::core {

namespace {

QString endpointKey(const maxchat::irc::ServerEndpoint& endpoint) {
    return QStringLiteral("%1:%2:%3")
        .arg(endpoint.host.toCaseFolded())
        .arg(endpoint.port)
        .arg(endpoint.tls);
}

QStringList stringListFromVariant(const QVariant& value) {
    QStringList strings = value.toStringList();
    if (!strings.isEmpty()) {
        return strings;
    }

    const QVariantList list = value.toList();
    for (const QVariant& item : list) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty()) {
            strings.append(text);
        }
    }
    if (!strings.isEmpty()) {
        return strings;
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return {};
    }
    return text.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
}

void appendUniqueServer(QList<maxchat::irc::ServerEndpoint>& servers, QSet<QString>& seen,
                        const maxchat::irc::ServerEndpoint& server) {
    const QString host = server.host.trimmed();
    if (host.isEmpty()) {
        return;
    }

    maxchat::irc::ServerEndpoint normalized = server;
    normalized.host = host;
    const QString key = endpointKey(normalized);
    if (seen.contains(key)) {
        return;
    }
    seen.insert(key);
    servers.append(normalized);
}

int positiveIntOrDefault(const QVariant& value, int fallback) {
    bool ok = false;
    const int parsed = value.toInt(&ok);
    return ok && parsed > 0 ? parsed : fallback;
}

} // namespace

QStringList parseAutojoinChannels(const QString& channels) {
    QString normalized = channels;
    normalized.replace(QLatin1Char(','), QLatin1Char(' '));

    QStringList parsed;
    const QStringList tokens =
        normalized.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (QString channel : tokens) {
        channel = channel.trimmed();
        if (channel.isEmpty()) {
            continue;
        }
        if (!channel.startsWith(QLatin1Char('#')) && !channel.startsWith(QLatin1Char('&'))) {
            channel.prepend(QLatin1Char('#'));
        }
        parsed.append(channel);
    }
    return parsed;
}

NetworkConnectionPlan connectionPlanFromNetwork(const NetworkConfig& network) {
    NetworkConnectionPlan plan;
    plan.networkName = network.value(QStringLiteral("name")).toString().trimmed();
    plan.nick = network.value(QStringLiteral("nick")).toString().trimmed();
    if (plan.nick.isEmpty()) {
        plan.nick = QStringLiteral("comicfan");
    }
    plan.username = network.value(QStringLiteral("username")).toString().trimmed();
    plan.realname = network.value(QStringLiteral("realname")).toString().trimmed();
    plan.saslAccount = network.value(QStringLiteral("account")).toString().trimmed();
    plan.saslPassword = network.value(QStringLiteral("password")).toString();
    plan.serverPassword = network.value(QStringLiteral("server_pass")).toString();
    plan.acceptInvalidCertificate = network.value(QStringLiteral("accept_invalid_cert")).toBool();
    plan.allowInsecureAuth = network.value(QStringLiteral("allow_insecure_auth")).toBool();
    plan.connectTimeoutMs =
        positiveIntOrDefault(network.value(QStringLiteral("connect_timeout_ms")), 15000);
    // Registration (CAP/SASL/NICK/USER) can legitimately outlast the TCP connect;
    // defaults to the connect timeout so existing behaviour is unchanged, but is
    // independently configurable. (Was previously fed the connect timeout in
    // MainWindow — a copy-paste that conflated the two phases.)
    plan.registrationTimeoutMs = positiveIntOrDefault(
        network.value(QStringLiteral("registration_timeout_ms")), plan.connectTimeoutMs);
    plan.autojoin = parseAutojoinChannels(network.value(QStringLiteral("channels")).toString());
    plan.perform = stringListFromVariant(network.value(QStringLiteral("perform")));
    plan.proxyType = network.value(QStringLiteral("proxy_type")).toString().trimmed().toLower();
    plan.proxyHost = network.value(QStringLiteral("proxy_host")).toString().trimmed();
    plan.proxyPort = positiveIntOrDefault(network.value(QStringLiteral("proxy_port")), 1080);
    plan.proxyUsername = network.value(QStringLiteral("proxy_username")).toString();
    plan.proxyPassword = network.value(QStringLiteral("proxy_password")).toString();

    const int primaryPort = positiveIntOrDefault(network.value(QStringLiteral("port")), 6667);
    const bool primaryTls = network.value(QStringLiteral("tls")).toBool();

    QSet<QString> seenServers;
    appendUniqueServer(plan.reconnect.servers, seenServers,
                       {network.value(QStringLiteral("host")).toString(), primaryPort, primaryTls});

    const QStringList failovers = stringListFromVariant(network.value(QStringLiteral("servers")));
    for (const QString& spec : failovers) {
        appendUniqueServer(plan.reconnect.servers, seenServers,
                           parseServerSpec(spec, primaryPort, primaryTls));
    }

    if (plan.networkName.isEmpty() && !plan.reconnect.servers.isEmpty()) {
        plan.networkName = plan.reconnect.servers.first().host;
    }
    return plan;
}

bool hasConnectableServer(const NetworkConnectionPlan& plan) {
    return maxchat::irc::hasUsableServer(plan.reconnect);
}

} // namespace maxchat::core
