#pragma once

#include "irc/ReconnectPlanner.h"

#include <QList>
#include <QString>

namespace maxchat::core {

struct NetworkDefaults {
    QString name;
    maxchat::irc::ServerEndpoint primary;
    QString nick;
    QString username;
    QString password;
    QString channels;
    QString website;
    QList<maxchat::irc::ServerEndpoint> failovers;
};

[[nodiscard]] QList<NetworkDefaults> defaultNetworks();
[[nodiscard]] QList<maxchat::irc::ServerEndpoint> allServers(const NetworkDefaults& network);
[[nodiscard]] maxchat::irc::ServerEndpoint
parseServerSpec(const QString& spec, int defaultPort = 6667, bool defaultTls = false);
[[nodiscard]] QString serverSpec(const maxchat::irc::ServerEndpoint& server);

} // namespace maxchat::core
