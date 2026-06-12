#pragma once

#include <QString>
#include <QStringList>

namespace maxchat::irc {

[[nodiscard]] QString nickColor(const QString& nick, const QStringList& palette = {});
// The built-in 16-colour nick palette used when no theme palette is given.
[[nodiscard]] QStringList defaultNickPalette();
[[nodiscard]] QString stripFormatting(const QString& text);
[[nodiscard]] QString toHtml(const QString& text,
                             const QString& defaultForeground = QStringLiteral("#cfcfcf"),
                             const QString& defaultBackground = QStringLiteral("#0a0a0a"));

} // namespace maxchat::irc
