#include "core/ChatLineFormatter.h"

#include "core/UrlDetector.h"
#include "irc/IrcFormat.h"

#include <QStringBuilder>

#include <algorithm>

namespace maxchat::core {
namespace {

struct ParsedLine {
    QString scope;
    QString label;
    QString labelNick;
    QString message;
    QString rest;
    bool hasLabel = false;
};

QString htmlSpaces(int count) {
    return QStringLiteral("&nbsp;").repeated(std::max(0, count));
}

QString separatorPlain(const ChatLineFormatOptions& options) {
    Q_UNUSED(options);
    return QStringLiteral(" ");
}

QString separatorHtml(const ChatLineFormatOptions& options) {
    Q_UNUSED(options);
    return QStringLiteral(" ");
}

QString displayLabelPlain(const ParsedLine& parsed) {
    const QString scope = parsed.scope.trimmed();
    if (parsed.hasLabel && !scope.isEmpty()) {
        return QStringLiteral("%1 %2").arg(scope, parsed.label);
    }
    if (parsed.hasLabel) {
        return parsed.label;
    }
    return scope;
}

QString displayMessagePlain(const ParsedLine& parsed) {
    return parsed.hasLabel ? parsed.message : parsed.rest;
}

ParsedLine parseLineShape(const QString& line) {
    ParsedLine parsed;
    QString body = line;
    if (body.startsWith(QLatin1Char('['))) {
        const qsizetype scopeEnd = body.indexOf(QStringLiteral("] "));
        if (scopeEnd > 0) {
            parsed.scope = body.left(scopeEnd + 2);
            body = body.mid(scopeEnd + 2);
        }
    }
    parsed.rest = body;

    if (body.startsWith(QLatin1Char('<'))) {
        const qsizetype end = body.indexOf(QStringLiteral("> "));
        if (end > 0) {
            parsed.label = body.left(end + 1);
            parsed.labelNick = body.mid(1, end - 1);
            parsed.message = body.mid(end + 2);
            parsed.hasLabel = true;
        }
    } else if (body.startsWith(QLatin1Char('-'))) {
        const qsizetype end = body.indexOf(QStringLiteral("- "), 1);
        if (end > 0) {
            // Reject degenerate "nicks" so divider rules like "--- Chat ended ---"
            // aren't split into a "-" nick label + body. A real notice nick has
            // at least one non-dash character.
            const QString candidateNick = body.mid(1, end - 1);
            const QString trimmedNick = candidateNick.trimmed();
            if (!trimmedNick.isEmpty() && trimmedNick != QStringLiteral("-")) {
                parsed.label = body.left(end + 1);
                parsed.labelNick = candidateNick;
                parsed.message = body.mid(end + 2);
                parsed.hasLabel = true;
            }
        }
    } else if (body.startsWith(QStringLiteral("* "))) {
        const qsizetype end = body.indexOf(QLatin1Char(' '), 2);
        if (end > 2) {
            parsed.label = body.left(end);
            parsed.labelNick = body.mid(2, end - 2);
            parsed.message = body.mid(end + 1);
            parsed.hasLabel = true;
        }
    }

    return parsed;
}

QString linkifyUrls(const QString& html, const QString& plainText) {
    const QStringList urls = maxchat::core::extractUrls(plainText);
    if (urls.isEmpty()) {
        return html;
    }
    QString result = html;
    for (const QString& url : urls) {
        const QString escaped = url.toHtmlEscaped();
        // Only replace bare URL text — skip anything already inside an href.
        // Simple guard: don't touch a match that is immediately preceded by '"'.
        int pos = 0;
        while ((pos = result.indexOf(escaped, pos)) != -1) {
            if (pos > 0 && result.at(pos - 1) == QLatin1Char('"')) {
                pos += escaped.size();
                continue;
            }
            const QString linked = QStringLiteral("<a href=\"") + escaped
                                   + QStringLiteral("\">") + escaped + QStringLiteral("</a>");
            result.replace(pos, escaped.size(), linked);
            pos += linked.size();
        }
    }
    return result;
}

QString messageHtml(const QString& text, const ChatLineFormatOptions& options) {
    QString html;
    if (options.renderFormatting) {
        html = maxchat::irc::toHtml(text, options.defaultForeground, options.defaultBackground);
    } else {
        html = maxchat::irc::stripFormatting(text).toHtmlEscaped();
    }
    return linkifyUrls(html, maxchat::irc::stripFormatting(text));
}

QString colorSpan(const QString& color, const QString& html) {
    return QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color, html);
}

QString labelHtml(const ParsedLine& parsed, const ChatLineFormatOptions& options) {
    const QString escapedLabel = parsed.label.toHtmlEscaped();
    const QString nick = parsed.labelNick;
    if (nick.trimmed().isEmpty()) {
        return escapedLabel;
    }
    // System lines are tinted whole-line by the caller; skip nick coloring.
    if (options.systemLine && !options.systemColor.isEmpty()) {
        return escapedLabel;
    }

    QString nickColor = options.nickColorOverrides.value(nick.trimmed().toLower());
    if (nickColor.isEmpty() && options.colorNicks && !options.monoNicks) {
        nickColor = maxchat::irc::nickColor(nick, options.nickPalette);
    }

    if (options.bracketColor.isEmpty()) {
        return nickColor.isEmpty() ? escapedLabel : colorSpan(nickColor, escapedLabel);
    }

    // Tint the <> / - - / * marks around the nick separately (BitchX-style).
    const qsizetype nickStart = parsed.label.indexOf(nick);
    if (nickStart < 0) {
        return nickColor.isEmpty() ? escapedLabel : colorSpan(nickColor, escapedLabel);
    }
    const QString open = parsed.label.left(nickStart).toHtmlEscaped();
    const QString close = parsed.label.mid(nickStart + nick.size()).toHtmlEscaped();
    const QString nickHtml = nick.toHtmlEscaped();
    QString html;
    if (!open.isEmpty()) {
        html += colorSpan(options.bracketColor, open);
    }
    html += nickColor.isEmpty() ? nickHtml : colorSpan(nickColor, nickHtml);
    if (!close.isEmpty()) {
        html += colorSpan(options.bracketColor, close);
    }
    return html;
}

QString displayLabelHtml(const ParsedLine& parsed, const ChatLineFormatOptions& options) {
    const QString scope = parsed.scope.trimmed().toHtmlEscaped();
    if (parsed.hasLabel && !scope.isEmpty()) {
        return QStringLiteral("%1 %2").arg(scope, labelHtml(parsed, options));
    }
    if (parsed.hasLabel) {
        return labelHtml(parsed, options);
    }
    return scope;
}

QString displayMessageHtml(const ParsedLine& parsed, const ChatLineFormatOptions& options) {
    return messageHtml(parsed.hasLabel ? parsed.message : parsed.rest, options);
}

QString renderPlainBody(const ParsedLine& parsed, const ChatLineFormatOptions& options) {
    const int width = std::clamp(options.nickColumnWidth, 4, 40);
    if (!options.alignNicks) {
        const QString label = displayLabelPlain(parsed);
        if (label.isEmpty()) {
            return displayMessagePlain(parsed);
        }
        return QStringLiteral("%1 %2").arg(label, displayMessagePlain(parsed));
    }

    const QString label = displayLabelPlain(parsed);
    const QString paddedLabel =
        label.size() < width ? QString(width - label.size(), QLatin1Char(' ')) + label : label;
    return paddedLabel + separatorPlain(options) + displayMessagePlain(parsed);
}

QString renderInlineHtmlBody(const ParsedLine& parsed, const ChatLineFormatOptions& options) {
    const QString label = displayLabelHtml(parsed, options);
    if (label.isEmpty()) {
        return displayMessageHtml(parsed, options);
    }
    return label + QLatin1Char(' ') + displayMessageHtml(parsed, options);
}

QString alignedPrefixPlain(const ParsedLine& parsed, const ChatLineFormatOptions& options) {
    const int width = std::clamp(options.nickColumnWidth, 4, 40);
    const QString label = displayLabelPlain(parsed);
    const QString paddedLabel =
        label.size() < width ? QString(width - label.size(), QLatin1Char(' ')) + label : label;
    return paddedLabel + separatorPlain(options);
}

QString alignedPrefixHtml(const ParsedLine& parsed, const ChatLineFormatOptions& options) {
    const int width = std::clamp(options.nickColumnWidth, 4, 40);
    const int labelTextWidth = static_cast<int>(displayLabelPlain(parsed).size());
    const int pad = std::max(0, width - labelTextWidth);
    return htmlSpaces(pad) + displayLabelHtml(parsed, options) + separatorHtml(options);
}

QString timestampPlainPrefix(const ChatLineFormatOptions& options) {
    return options.showTimestamp && !options.timestamp.isEmpty()
               ? options.timestamp + QLatin1Char(' ')
               : QString();
}

QString timestampHtmlPrefix(const ChatLineFormatOptions& options) {
    if (!options.showTimestamp || options.timestamp.isEmpty()) {
        return {};
    }
    const QString color = options.timestampColor.isEmpty() ? QStringLiteral("#8a8a8a")
                                                           : options.timestampColor;
    return colorSpan(color, options.timestamp.toHtmlEscaped()) + QLatin1Char(' ');
}

QString systemTint(const QString& html, const ChatLineFormatOptions& options) {
    if (!options.systemLine || options.systemColor.isEmpty() || html.isEmpty()) {
        return html;
    }
    return colorSpan(options.systemColor, html);
}

} // namespace

FormattedChatLine formatChatLine(const QString& line, const ChatLineFormatOptions& options) {
    const ParsedLine plain = parseLineShape(maxchat::irc::stripFormatting(line));
    const ParsedLine rich = parseLineShape(line);
    if (!options.alignNicks) {
        const QString plainBody = renderPlainBody(plain, options);
        const QString htmlBody = systemTint(renderInlineHtmlBody(rich, options), options);
        FormattedChatLine line;
        line.prefixPlain = timestampPlainPrefix(options);
        line.prefixHtml = timestampHtmlPrefix(options);
        line.messagePlain = plainBody;
        line.messageHtml = htmlBody;
        line.plainText = line.prefixPlain + line.messagePlain;
        line.html = line.prefixHtml + line.messageHtml;
        return line;
    }

    FormattedChatLine out;
    out.prefixPlain = timestampPlainPrefix(options) + alignedPrefixPlain(plain, options);
    out.prefixHtml =
        timestampHtmlPrefix(options) + systemTint(alignedPrefixHtml(rich, options), options);
    out.messagePlain = displayMessagePlain(plain);
    out.messageHtml = systemTint(displayMessageHtml(rich, options), options);
    out.plainText = out.prefixPlain + out.messagePlain;
    out.html = out.prefixHtml + out.messageHtml;
    out.hangingIndent = true;
    return out;
}

} // namespace maxchat::core
