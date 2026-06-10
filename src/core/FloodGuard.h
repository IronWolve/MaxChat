#pragma once

#include <QHash>
#include <QList>
#include <QString>

namespace maxchat::core {

class FloodGuard final {
  public:
    void configure(bool enabled, int maxMessages, int windowSeconds);
    void clear();

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] bool recordMessage(const QString& key, qint64 nowMs);

  private:
    bool enabled_ = false;
    int maxMessages_ = 10;
    qint64 windowMs_ = 4000;
    QHash<QString, QList<qint64>> messageTimes_;
};

} // namespace maxchat::core
