#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace maxchat::core {

struct ChatLineFormatOptions {
    bool showTimestamp = true;
    bool alignNicks = true;
    bool separatorLine = true;
    bool renderFormatting = true;
    bool colorNicks = true;
    int nickColumnWidth = 16;
    QString timestamp;
    QString defaultForeground = QStringLiteral("#cfcfcf");
    QString defaultBackground = QStringLiteral("#0a0a0a");
    // Chat-theme terminal extras. Empty string = built-in default.
    QString timestampColor = QStringLiteral("#8a8a8a");
    QString bracketColor;             // colors <> -- around nicks separately
    QString systemColor;              // join/part/status lines, when systemLine
    QString bodyColor;                // wraps the message body (dim replay)
    bool monoNicks = false;           // irssi-style: nicks in plain chat fg
    QStringList nickPalette;          // hex colors; empty = default palette
    bool systemLine = false;          // this line is a system/event line
    // Per-user colors (lowercase nick -> hex). Win even in mono/colors-off.
    QHash<QString, QString> nickColorOverrides;
};

struct FormattedChatLine {
    QString plainText;
    QString html;
    QString prefixPlain;
    QString prefixHtml;
    QString messagePlain;
    QString messageHtml;
    bool hangingIndent = false;
};

[[nodiscard]] FormattedChatLine formatChatLine(const QString& line,
                                               const ChatLineFormatOptions& options);

} // namespace maxchat::core
