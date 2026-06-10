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
[[nodiscard]] CommandAliasExpansion
expandCommandAliases(const QString& input, const QVariantMap& aliases, int maxDepth = 8);

} // namespace maxchat::core
