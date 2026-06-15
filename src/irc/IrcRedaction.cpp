#include "irc/IrcRedaction.h"

#include <QRegularExpression>

namespace maxchat::irc {

QString redactLine(const QString& line) {
    const QString upper = line.toUpper();
    if (upper.startsWith(QStringLiteral("PASS "))) {
        return QStringLiteral("PASS ****");
    }

    // OPER <name> <password> — mask the password (the second argument).
    if (upper.startsWith(QStringLiteral("OPER "))) {
        static const QRegularExpression oper(QStringLiteral(R"(^(OPER\s+\S+\s+).*)"),
                                             QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = oper.match(line);
        if (m.hasMatch()) {
            return m.captured(1) + QStringLiteral("****");
        }
    }

    if (upper.startsWith(QStringLiteral("AUTHENTICATE "))) {
        const QString payload = line.mid(13).trimmed();
        if (payload != QStringLiteral("+") &&
            payload.compare(QStringLiteral("PLAIN"), Qt::CaseInsensitive) != 0 &&
            payload.compare(QStringLiteral("EXTERNAL"), Qt::CaseInsensitive) != 0) {
            return QStringLiteral("AUTHENTICATE ****");
        }
    }

    static const QString verbs =
        QStringLiteral("IDENTIFY|REGISTER|GHOST|RECOVER|RELEASE|SIDENTIFY|LOGIN");
    // PRIVMSG/NOTICE NickServ :IDENTIFY <pw>
    static const QRegularExpression servicesPassword(
        QStringLiteral(R"(^((?:PRIVMSG|NOTICE) \S+ :(?:%1)\s+))").arg(verbs),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = servicesPassword.match(line);
    if (match.hasMatch()) {
        return match.captured(1) + QStringLiteral("****");
    }
    // Server-alias form: NICKSERV / NS / CHANSERV / CS IDENTIFY <pw>
    static const QRegularExpression serviceAlias(
        QStringLiteral(R"(^((?:NICKSERV|CHANSERV|NS|CS)\s+(?:%1)\s+))").arg(verbs),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch aliasMatch = serviceAlias.match(line);
    if (aliasMatch.hasMatch()) {
        return aliasMatch.captured(1) + QStringLiteral("****");
    }

    return line;
}

} // namespace maxchat::irc
