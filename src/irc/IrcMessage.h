#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace maxchat::irc {

struct IrcMessage {
    QString raw;
    QString command;
    QStringList params;
    QString prefix;
    QHash<QString, QString> tags;

    [[nodiscard]] QString nick() const;
    [[nodiscard]] QString trailing() const;
};

[[nodiscard]] QString unescapeTagValue(const QString& value);
[[nodiscard]] IrcMessage parseMessage(const QString& line);

} // namespace maxchat::irc
