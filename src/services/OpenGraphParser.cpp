#include "services/OpenGraphParser.h"

#include <QHash>
#include <QRegularExpression>

namespace maxchat::services {
namespace {

QString decodeHtmlEntities(const QString& text) {
    static const QRegularExpression entityPattern(
        QStringLiteral(R"(&(#x[0-9A-Fa-f]+|#\d+|amp|lt|gt|quot|apos|nbsp);)"),
        QRegularExpression::CaseInsensitiveOption);

    QString decoded;
    qsizetype last = 0;
    QRegularExpressionMatchIterator matches = entityPattern.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        decoded += text.mid(last, match.capturedStart() - last);

        const QString entity = match.captured(1).toLower();
        if (entity == QStringLiteral("amp")) {
            decoded += QLatin1Char('&');
        } else if (entity == QStringLiteral("lt")) {
            decoded += QLatin1Char('<');
        } else if (entity == QStringLiteral("gt")) {
            decoded += QLatin1Char('>');
        } else if (entity == QStringLiteral("quot")) {
            decoded += QLatin1Char('"');
        } else if (entity == QStringLiteral("apos")) {
            decoded += QLatin1Char('\'');
        } else if (entity == QStringLiteral("nbsp")) {
            decoded += QLatin1Char(' ');
        } else {
            bool ok = false;
            const uint codepoint = entity.startsWith(QStringLiteral("#x"))
                                       ? entity.mid(2).toUInt(&ok, 16)
                                       : entity.mid(1).toUInt(&ok, 10);
            if (ok) {
                const char32_t character = static_cast<char32_t>(codepoint);
                decoded += QString::fromUcs4(&character, 1);
            } else {
                decoded += match.captured(0);
            }
        }
        last = match.capturedEnd();
    }
    decoded += text.mid(last);
    return decoded;
}

QString normalizeText(QString text) {
    text = decodeHtmlEntities(text.trimmed());
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

QString resolveUrlText(const QString& text, const QUrl& pageUrl) {
    const QString value = normalizeText(text);
    if (value.isEmpty()) {
        return {};
    }
    const QUrl url(value);
    return pageUrl.isValid() ? pageUrl.resolved(url).toString() : url.toString();
}

QHash<QString, QString> attributesForTag(const QString& tag) {
    static const QRegularExpression attrPattern(
        QStringLiteral(R"(([A-Za-z_:][-A-Za-z0-9_:.]*)\s*=\s*("[^"]*"|'[^']*'|[^\s"'<>]+))"),
        QRegularExpression::UseUnicodePropertiesOption);

    QHash<QString, QString> attrs;
    QRegularExpressionMatchIterator matches = attrPattern.globalMatch(tag);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        QString value = match.captured(2).trimmed();
        if ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) ||
            (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))) {
            value = value.mid(1, value.size() - 2);
        }
        attrs.insert(match.captured(1).toLower(), normalizeText(value));
    }
    return attrs;
}

void insertIfMissing(QHash<QString, QString>& values, const QString& key, const QString& value) {
    if (!value.isEmpty() && !values.contains(key)) {
        values.insert(key, value);
    }
}

QString firstValue(const QHash<QString, QString>& values, const QStringList& keys) {
    for (const QString& key : keys) {
        const QString value = values.value(key);
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString titleFromHtml(const QString& html) {
    static const QRegularExpression titlePattern(
        QStringLiteral(R"(<title\b[^>]*>(.*?)</title>)"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = titlePattern.match(html);
    return match.hasMatch() ? normalizeText(match.captured(1)) : QString();
}

} // namespace

bool OpenGraphCard::isEmpty() const {
    return title.isEmpty() && description.isEmpty() && imageUrl.isEmpty() &&
           canonicalUrl.isEmpty() && siteName.isEmpty() && type.isEmpty();
}

OpenGraphCard parseOpenGraphCard(const QString& html, const QUrl& pageUrl) {
    static const QRegularExpression metaPattern(QStringLiteral(R"(<meta\b[^>]*>)"),
                                                QRegularExpression::CaseInsensitiveOption |
                                                    QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression linkPattern(QStringLiteral(R"(<link\b[^>]*>)"),
                                                QRegularExpression::CaseInsensitiveOption |
                                                    QRegularExpression::DotMatchesEverythingOption);

    QHash<QString, QString> values;

    QRegularExpressionMatchIterator metaMatches = metaPattern.globalMatch(html);
    while (metaMatches.hasNext()) {
        const QHash<QString, QString> attrs = attributesForTag(metaMatches.next().captured(0));
        const QString key =
            firstValue(attrs, {QStringLiteral("property"), QStringLiteral("name")}).toLower();
        const QString content = attrs.value(QStringLiteral("content"));
        if (key.startsWith(QStringLiteral("og:")) || key.startsWith(QStringLiteral("twitter:")) ||
            key == QStringLiteral("description")) {
            insertIfMissing(values, key, content);
        }
    }

    QRegularExpressionMatchIterator linkMatches = linkPattern.globalMatch(html);
    while (linkMatches.hasNext()) {
        const QHash<QString, QString> attrs = attributesForTag(linkMatches.next().captured(0));
        if (attrs.value(QStringLiteral("rel")).toLower() == QStringLiteral("canonical")) {
            insertIfMissing(values, QStringLiteral("canonical"),
                            attrs.value(QStringLiteral("href")));
        }
    }

    OpenGraphCard card;
    card.title = firstValue(values, {QStringLiteral("og:title"), QStringLiteral("twitter:title")});
    if (card.title.isEmpty()) {
        card.title = titleFromHtml(html);
    }
    card.description =
        firstValue(values, {QStringLiteral("og:description"), QStringLiteral("twitter:description"),
                            QStringLiteral("description")});
    card.siteName = values.value(QStringLiteral("og:site_name"));
    card.type = values.value(QStringLiteral("og:type"));

    const QString image =
        firstValue(values, {QStringLiteral("og:image"), QStringLiteral("twitter:image"),
                            QStringLiteral("twitter:image:src")});
    card.imageUrl = QUrl(resolveUrlText(image, pageUrl));

    const QString canonical =
        firstValue(values, {QStringLiteral("og:url"), QStringLiteral("canonical")});
    card.canonicalUrl = QUrl(resolveUrlText(canonical, pageUrl));

    if (card.canonicalUrl.isEmpty() && pageUrl.isValid()) {
        card.canonicalUrl = pageUrl;
    }
    return card;
}

} // namespace maxchat::services
