#include "core/CommandAlias.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace maxchat::core {

namespace {

struct CommandParts {
    QString command;
    QString arguments;
};

CommandParts splitSlashCommand(const QString& input) {
    const QString trimmed = input.trimmed();
    if (!trimmed.startsWith(QLatin1Char('/'))) {
        return {};
    }

    const QString withoutSlash = trimmed.mid(1).trimmed();
    if (withoutSlash.isEmpty()) {
        return {};
    }

    const int space = withoutSlash.indexOf(QRegularExpression(QStringLiteral("\\s")));
    if (space < 0) {
        return {withoutSlash.toLower(), QString()};
    }
    return {withoutSlash.left(space).toLower(), withoutSlash.mid(space + 1).trimmed()};
}

QStringList argumentTokens(const QString& arguments) {
    return arguments.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

QString normalizeTemplate(QString aliasTemplate) {
    aliasTemplate = aliasTemplate.trimmed();
    if (!aliasTemplate.isEmpty() && !aliasTemplate.startsWith(QLatin1Char('/'))) {
        aliasTemplate.prepend(QLatin1Char('/'));
    }
    return aliasTemplate;
}

QString applyTemplate(QString aliasTemplate, const QString& arguments,
                      const QString& selfNick, const QString& currentChannel) {
    aliasTemplate = normalizeTemplate(aliasTemplate);
    if (aliasTemplate.isEmpty()) {
        return {};
    }

    const QStringList tokens = argumentTokens(arguments);
    bool usedPlaceholder = false;

    // $me / $chan / $* / $N / $N- — matches the Python _expand_alias token set.
    QRegularExpression placeholder(QStringLiteral(R"(\$(me|chan|\*|\d+-?))"));
    QRegularExpressionMatchIterator matches = placeholder.globalMatch(aliasTemplate);
    QString expanded;
    qsizetype last = 0;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        expanded += aliasTemplate.mid(last, match.capturedStart() - last);
        last = match.capturedEnd();
        usedPlaceholder = true;

        const QString token = match.captured(1);
        if (token == QStringLiteral("me")) {
            expanded += selfNick;
            continue;
        }
        if (token == QStringLiteral("chan")) {
            expanded += currentChannel;
            continue;
        }
        if (token == QStringLiteral("*")) {
            expanded += arguments;
            continue;
        }

        const bool rest = token.endsWith(QLatin1Char('-'));
        const int index = token.left(rest ? token.size() - 1 : token.size()).toInt() - 1;
        if (index < 0 || index >= tokens.size()) {
            continue;
        }
        if (rest) {
            expanded += tokens.mid(index).join(QLatin1Char(' '));
        } else {
            expanded += tokens.at(index);
        }
    }
    expanded += aliasTemplate.mid(last);

    if (!usedPlaceholder && !arguments.isEmpty()) {
        expanded += QLatin1Char(' ');
        expanded += arguments;
    }

    return expanded.trimmed();
}

} // namespace

QVariantMap defaultCommandAliases() {
    QVariantMap aliases;
    // Command shortcuts (C++ extra — not in Python defaults; see BACKPORTS).
    aliases.insert(QStringLiteral("j"), QStringLiteral("/join $*"));
    aliases.insert(QStringLiteral("p"), QStringLiteral("/part $*"));
    aliases.insert(QStringLiteral("w"), QStringLiteral("/whois $*"));
    // Classic fun aliases shipped by the Python app (parity).
    aliases.insert(QStringLiteral("slap"),
                   QStringLiteral("/me slaps $1- around a bit with a large trout"));
    aliases.insert(QStringLiteral("fish"),
                   QStringLiteral("/me slaps $1- around the face with a wet fish"));
    return aliases;
}

CommandAliasExpansion expandCommandAliases(const QString& input, const QVariantMap& aliases,
                                           const QString& selfNick,
                                           const QString& currentChannel, int maxDepth) {
    CommandAliasExpansion result;
    result.commandLine = input.trimmed();
    if (result.commandLine.isEmpty() || aliases.isEmpty() || maxDepth <= 0) {
        return result;
    }

    QSet<QString> seen;
    for (int depth = 0; depth < maxDepth; ++depth) {
        const CommandParts parts = splitSlashCommand(result.commandLine);
        if (parts.command.isEmpty() || seen.contains(parts.command)) {
            break;
        }
        seen.insert(parts.command);

        const QVariant value = aliases.value(parts.command);
        if (!value.isValid()) {
            break;
        }

        const QString next =
            applyTemplate(value.toString(), parts.arguments, selfNick, currentChannel);
        if (next.isEmpty() || next == result.commandLine) {
            break;
        }

        result.commandLine = next;
        result.expanded = true;
        ++result.expansionCount;
    }
    return result;
}

} // namespace maxchat::core
