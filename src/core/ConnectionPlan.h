#pragma once

#include "core/NetworkImport.h"
#include "irc/ReconnectPlanner.h"

#include <QString>
#include <QStringList>

namespace maxchat::core {

struct NetworkConnectionPlan {
    QString networkName;
    QString nick = QStringLiteral("comicfan");
    QString username;
    QString realname;
    QString serverPassword;
    QString saslPassword;
    QString saslAccount;
    bool acceptInvalidCertificate = false;
    bool allowInsecureAuth = false;
    QStringList autojoin;
    QStringList perform;
    QString proxyType; // "", "none", "socks5", "http"
    QString proxyHost;
    int proxyPort = 1080;
    QString proxyUsername;
    QString proxyPassword;
    int connectTimeoutMs = 15000;
    maxchat::irc::ReconnectState reconnect;
};

[[nodiscard]] QStringList parseAutojoinChannels(const QString& channels);
[[nodiscard]] NetworkConnectionPlan connectionPlanFromNetwork(const NetworkConfig& network);
[[nodiscard]] bool hasConnectableServer(const NetworkConnectionPlan& plan);

} // namespace maxchat::core
