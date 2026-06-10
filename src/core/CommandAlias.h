#pragma once

#include <QString>
#include <QVariantMap>

namespace maxchat::core {

struct CommandAliasExpansion {
    QString commandLine;
    bool expanded = false;
    int expansionCount = 0;
};

[[nodiscard]] QVariantMap defaultCommandAliases();

// selfNick / currentChannel feed the $me and $chan placeholders (empty = expand
// to nothing, matching Python when there is no nick/channel context).
[[nodiscard]] CommandAliasExpansion
expandCommandAliases(const QString& input, const QVariantMap& aliases,
                     const QString& selfNick = {}, const QString& currentChannel = {},
                     int maxDepth = 8);

} // namespace maxchat::core
