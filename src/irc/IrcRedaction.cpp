#include "irc/IrcRedaction.h"

#include <QRegularExpression>

namespace maxchat::irc {

QString redactLine(const QString& line) {
    const QString upper = line.toUpper();
    if (upper.startsWith(QStringLiteral("PASS "))) {
        return QStringLiteral("PASS ****");
    }

    if (upper.startsWith(QStringLiteral("AUTHENTICATE "))) {
        const QString payload = line.mid(13).trimmed();
        if (payload != QStringLiteral("+") &&
            payload.compare(QStringLiteral("PLAIN"), Qt::CaseInsensitive) != 0 &&
            payload.compare(QStringLiteral("EXTERNAL"), Qt::CaseInsensitive) != 0) {
            return QStringLiteral("AUTHENTICATE ****");
        }
    }

    static const QRegularExpression servicesPassword(
        QStringLiteral(
            R"(^((?:PRIVMSG|NOTICE) \S+ :(?:IDENTIFY|REGISTER|GHOST|RECOVER|RELEASE|SIDENTIFY|LOGIN)\s+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = servicesPassword.match(line);
    if (match.hasMatch()) {
        return match.captured(1) + QStringLiteral("****");
    }

    return line;
}

} // namespace maxchat::irc
