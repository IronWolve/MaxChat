#pragma once

#include <QList>
#include <QString>

namespace maxchat::irc {

inline constexpr int ServerRetryLimit = 3;

struct ServerEndpoint {
    QString host;
    int port = 6667;
    bool tls = false;
};

struct ReconnectState {
    QList<ServerEndpoint> servers;
    int serverIndex = 0;
    int serverAttempt = 0;
};

[[nodiscard]] bool hasUsableServer(const ReconnectState& state);
[[nodiscard]] ServerEndpoint currentServer(const ReconnectState& state);
[[nodiscard]] ServerEndpoint chooseReconnectServer(ReconnectState& state, bool forceNext = false);

} // namespace maxchat::irc
