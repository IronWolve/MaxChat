#include "irc/ReconnectPlanner.h"

#include <algorithm>

namespace maxchat::irc {

bool hasUsableServer(const ReconnectState& state) {
    return !state.servers.isEmpty();
}

ServerEndpoint currentServer(const ReconnectState& state) {
    if (state.servers.isEmpty()) {
        return {};
    }
    const int index = std::clamp(state.serverIndex, 0, static_cast<int>(state.servers.size()) - 1);
    return state.servers.at(index);
}

ServerEndpoint chooseReconnectServer(ReconnectState& state, bool forceNext) {
    if (state.servers.isEmpty()) {
        state.serverIndex = 0;
        state.serverAttempt = 0;
        return {};
    }

    state.serverIndex =
        std::clamp(state.serverIndex, 0, static_cast<int>(state.servers.size()) - 1);

    if (state.servers.size() > 1) {
        if (forceNext || state.serverAttempt >= ServerRetryLimit) {
            state.serverIndex = (state.serverIndex + 1) % state.servers.size();
            state.serverAttempt = 1;
        } else {
            ++state.serverAttempt;
        }
    } else {
        state.serverAttempt = std::max(1, state.serverAttempt + 1);
    }

    return state.servers.at(state.serverIndex);
}

} // namespace maxchat::irc
