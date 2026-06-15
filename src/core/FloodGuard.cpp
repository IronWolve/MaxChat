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

    const qint64 cutoff = nowMs - windowMs_;
    // A key touched once would otherwise live forever; when the map grows large
    // sweep out senders whose window has fully expired.
    if (messageTimes_.size() > 512) {
        for (auto it = messageTimes_.begin(); it != messageTimes_.end();) {
            if (it.key() != normalized && (it.value().isEmpty() || it.value().last() < cutoff)) {
                it = messageTimes_.erase(it);
            } else {
                ++it;
            }
        }
    }

    QList<qint64>& times = messageTimes_[normalized];
    times.erase(std::remove_if(times.begin(), times.end(),
                               [cutoff](qint64 timestamp) { return timestamp < cutoff; }),
                times.end());
    times.append(nowMs);
    // Counts the just-added message: allows maxMessages_ in the window, trips on
    // the next one. (Intentional — not an off-by-one.)
    return times.size() > maxMessages_;
}

} // namespace maxchat::core
