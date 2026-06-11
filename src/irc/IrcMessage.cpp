#include "irc/IrcMessage.h"

namespace maxchat::irc {

QString IrcMessage::nick() const {
    if (prefix.isEmpty()) {
        return {};
    }
    const int bang = prefix.indexOf(QLatin1Char('!'));
    return bang >= 0 ? prefix.left(bang) : prefix;
}

QString IrcMessage::trailing() const {
    return params.isEmpty() ? QString() : params.last();
}

QString unescapeTagValue(const QString& value) {
    QString out;
    out.reserve(value.size());

    for (qsizetype i = 0; i < value.size();) {
        if (value.at(i) == QLatin1Char('\\') && i + 1 < value.size()) {
            const QChar next = value.at(i + 1);
            if (next == QLatin1Char(':')) {
                out.append(QLatin1Char(';'));
            } else if (next == QLatin1Char('s')) {
                out.append(QLatin1Char(' '));
            } else if (next == QLatin1Char('\\')) {
                out.append(QLatin1Char('\\'));
            } else if (next == QLatin1Char('r')) {
                out.append(QLatin1Char('\r'));
            } else if (next == QLatin1Char('n')) {
                out.append(QLatin1Char('\n'));
            } else {
                out.append(next);
            }
            i += 2;
        } else {
            out.append(value.at(i));
            ++i;
        }
    }

    return out;
}

namespace {
// Strip leading spaces only — NOT trailing — so the trailing parameter keeps any
// significant whitespace (Python only rstrips \r\n from the whole line).
QString lstripSpaces(const QString& in) {
    int i = 0;
    while (i < in.size() && in.at(i) == QLatin1Char(' ')) {
        ++i;
    }
    return in.mid(i);
}
} // namespace

IrcMessage parseMessage(const QString& line) {
    QString s = line;
    while (s.endsWith(QLatin1Char('\r')) || s.endsWith(QLatin1Char('\n'))) {
        s.chop(1);
    }

    IrcMessage msg;
    msg.raw = s;

    if (s.startsWith(QLatin1Char('@'))) {
        const int space = s.indexOf(QLatin1Char(' '));
        const QString tagString = space >= 0 ? s.mid(1, space - 1) : s.mid(1);
        s = space >= 0 ? s.mid(space + 1) : QString();

        const auto items = tagString.split(QLatin1Char(';'));
        for (const QString& item : items) {
            if (item.isEmpty()) {
                continue;
            }
            const int equals = item.indexOf(QLatin1Char('='));
            if (equals >= 0) {
                msg.tags.insert(item.left(equals), unescapeTagValue(item.mid(equals + 1)));
            } else {
                msg.tags.insert(item, QString());
            }
        }
    }

    s = lstripSpaces(s);

    if (s.startsWith(QLatin1Char(':'))) {
        const int space = s.indexOf(QLatin1Char(' '));
        if (space >= 0) {
            msg.prefix = s.mid(1, space - 1);
            s = s.mid(space + 1);
        } else {
            msg.prefix = s.mid(1);
            s.clear();
        }
    }

    s = lstripSpaces(s);
    const int commandSpace = s.indexOf(QLatin1Char(' '));
    if (commandSpace >= 0) {
        msg.command = s.left(commandSpace).toUpper();
        s = lstripSpaces(s.mid(commandSpace + 1));
    } else {
        msg.command = s.toUpper();
        s.clear();
    }

    while (!s.isEmpty()) {
        if (s.startsWith(QLatin1Char(':'))) {
            msg.params.append(s.mid(1));
            break;
        }

        const int space = s.indexOf(QLatin1Char(' '));
        if (space >= 0) {
            msg.params.append(s.left(space));
            s = lstripSpaces(s.mid(space + 1));
        } else {
            msg.params.append(s);
            break;
        }
    }

    return msg;
}

} // namespace maxchat::irc
