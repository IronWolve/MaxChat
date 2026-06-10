#include "core/UrlDetector.h"

#include <QRegularExpression>
#include <QSet>

namespace maxchat::core {

namespace {

QString trimTrailingPunctuation(QString url) {
    static const QString simpleTrailing = QStringLiteral(".,;:!?");
    while (!url.isEmpty() && simpleTrailing.contains(url.back())) {
        url.chop(1);
    }

    while (url.endsWith(QLatin1Char(')')) &&
           url.count(QLatin1Char(')')) > url.count(QLatin1Char('('))) {
        url.chop(1);
    }
    while (url.endsWith(QLatin1Char(']')) &&
           url.count(QLatin1Char(']')) > url.count(QLatin1Char('['))) {
        url.chop(1);
    }
    while (url.endsWith(QLatin1Char('}')) &&
           url.count(QLatin1Char('}')) > url.count(QLatin1Char('{'))) {
        url.chop(1);
    }
    return url;
}

} // namespace

QStringList extractUrls(const QString& text) {
    static const QRegularExpression urlPattern(
        QStringLiteral(R"(\b((?:https?://|ftp://|www\.)[^\s<>"']+))"),
        QRegularExpression::CaseInsensitiveOption);

    QStringList urls;
    QSet<QString> seen;
    QRegularExpressionMatchIterator matches = urlPattern.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString url = trimTrailingPunctuation(match.captured(1));
        if (url.isEmpty()) {
            continue;
        }

        const QString key = url.toCaseFolded();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        urls.append(url);
    }
    return urls;
}

} // namespace maxchat::core
