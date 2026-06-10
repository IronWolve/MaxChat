#include "core/FloodGuard.h"

#include <algorithm>

namespace maxchat::core {

void FloodGuard::configure(bool enabled, int maxMessages, int windowSeconds) {
    enabled_ = enabled;
    maxMessages_ = std::max(2, maxMessages);
    windowMs_ = std::max<qint64>(1000, qint64(std::max(1, windowSeconds)) * 1000);
    if (!enabled_) {
        clear();
    }
}

void FloodGuard::clear() {
    messageTimes_.clear();
}

bool FloodGuard::enabled() const {
    return enabled_;
}

bool FloodGuard::recordMessage(const QString& key, qint64 nowMs) {
    const QString normalized = key.trimmed().toCaseFolded();
    if (!enabled_ || normalized.isEmpty()) {
        return false;
    }

    QList<qint64>& times = messageTimes_[normalized];
    const qint64 cutoff = nowMs - windowMs_;
    times.erase(std::remove_if(times.begin(), times.end(),
                               [cutoff](qint64 timestamp) { return timestamp < cutoff; }),
                times.end());
    times.append(nowMs);
    return times.size() > maxMessages_;
}

} // namespace maxchat::core
