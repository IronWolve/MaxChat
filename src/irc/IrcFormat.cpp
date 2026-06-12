#include "irc/IrcFormat.h"

#include <QHash>
#include <QStringBuilder>

namespace maxchat::irc {
namespace {

const QHash<int, QString>& mircColors() {
    static const QHash<int, QString> colors = {
        {0, QStringLiteral("FFFFFF")},  {1, QStringLiteral("000000")},
        {2, QStringLiteral("00007F")},  {3, QStringLiteral("009300")},
        {4, QStringLiteral("FF0000")},  {5, QStringLiteral("7F0000")},
        {6, QStringLiteral("9C009C")},  {7, QStringLiteral("FC7F00")},
        {8, QStringLiteral("FFFF00")},  {9, QStringLiteral("00FC00")},
        {10, QStringLiteral("009393")}, {11, QStringLiteral("00FFFF")},
        {12, QStringLiteral("0000FC")}, {13, QStringLiteral("FF00FF")},
        {14, QStringLiteral("7F7F7F")}, {15, QStringLiteral("D2D2D2")},
        {16, QStringLiteral("470000")}, {17, QStringLiteral("472100")},
        {18, QStringLiteral("474700")}, {19, QStringLiteral("324700")},
        {20, QStringLiteral("004700")}, {21, QStringLiteral("00472C")},
        {22, QStringLiteral("004747")}, {23, QStringLiteral("002747")},
        {24, QStringLiteral("000047")}, {25, QStringLiteral("2E0047")},
        {26, QStringLiteral("470047")}, {27, QStringLiteral("47002A")},
        {28, QStringLiteral("740000")}, {29, QStringLiteral("743A00")},
        {30, QStringLiteral("747400")}, {31, QStringLiteral("517400")},
        {32, QStringLiteral("007400")}, {33, QStringLiteral("007449")},
        {34, QStringLiteral("007474")}, {35, QStringLiteral("004074")},
        {36, QStringLiteral("000074")}, {37, QStringLiteral("4B0074")},
        {38, QStringLiteral("740074")}, {39, QStringLiteral("740045")},
        {40, QStringLiteral("B50000")}, {41, QStringLiteral("B56300")},
        {42, QStringLiteral("B5B500")}, {43, QStringLiteral("7DB500")},
        {44, QStringLiteral("00B500")}, {45, QStringLiteral("00B571")},
        {46, QStringLiteral("00B5B5")}, {47, QStringLiteral("0063B5")},
        {48, QStringLiteral("0000B5")}, {49, QStringLiteral("7500B5")},
        {50, QStringLiteral("B500B5")}, {51, QStringLiteral("B5006B")},
        {52, QStringLiteral("FF0000")}, {53, QStringLiteral("FF8C00")},
        {54, QStringLiteral("FFFF00")}, {55, QStringLiteral("B2FF00")},
        {56, QStringLiteral("00FF00")}, {57, QStringLiteral("00FFA0")},
        {58, QStringLiteral("00FFFF")}, {59, QStringLiteral("008CFF")},
        {60, QStringLiteral("0000FF")}, {61, QStringLiteral("A500FF")},
        {62, QStringLiteral("FF00FF")}, {63, QStringLiteral("FF0098")},
        {64, QStringLiteral("FF5959")}, {65, QStringLiteral("FFB459")},
        {66, QStringLiteral("FFFF71")}, {67, QStringLiteral("CFFF60")},
        {68, QStringLiteral("6FFF6F")}, {69, QStringLiteral("65FFC9")},
        {70, QStringLiteral("6DFFFF")}, {71, QStringLiteral("59B4FF")},
        {72, QStringLiteral("5959FF")}, {73, QStringLiteral("C459FF")},
        {74, QStringLiteral("FF66FF")}, {75, QStringLiteral("FF59BC")},
        {76, QStringLiteral("FF9C9C")}, {77, QStringLiteral("FFD39C")},
        {78, QStringLiteral("FFFF9C")}, {79, QStringLiteral("E2FF9C")},
        {80, QStringLiteral("9CFF9C")}, {81, QStringLiteral("9CFFDB")},
        {82, QStringLiteral("9CFFFF")}, {83, QStringLiteral("9CD3FF")},
        {84, QStringLiteral("9C9CFF")}, {85, QStringLiteral("DC9CFF")},
        {86, QStringLiteral("FF9CFF")}, {87, QStringLiteral("FF94D3")},
        {88, QStringLiteral("000000")}, {89, QStringLiteral("131313")},
        {90, QStringLiteral("282828")}, {91, QStringLiteral("363636")},
        {92, QStringLiteral("4D4D4D")}, {93, QStringLiteral("656565")},
        {94, QStringLiteral("818181")}, {95, QStringLiteral("9F9F9F")},
        {96, QStringLiteral("BCBCBC")}, {97, QStringLiteral("E2E2E2")},
        {98, QStringLiteral("FFFFFF")},
    };
    return colors;
}

QString colorForNumber(int number) {
    const QString value = mircColors().value(number);
    return value.isEmpty() ? QString() : QStringLiteral("#") + value;
}

bool isHex(QChar c) {
    return c.isDigit() || (c >= QLatin1Char('a') && c <= QLatin1Char('f')) ||
           (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
}

QString readDigits(const QString& text, qsizetype& index, qsizetype max) {
    QString digits;
    while (index < text.size() && text.at(index).isDigit() && digits.size() < max) {
        digits.append(text.at(index));
        ++index;
    }
    return digits;
}

bool hasHexRun(const QString& text, qsizetype index, qsizetype length) {
    if (index + length > text.size()) {
        return false;
    }
    for (qsizetype offset = 0; offset < length; ++offset) {
        if (!isHex(text.at(index + offset))) {
            return false;
        }
    }
    return true;
}

struct FormatState {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool reverse = false;
    QString foreground;
    QString background;
};

} // namespace

QStringList defaultNickPalette() {
    return {QStringLiteral("#e06c75"), QStringLiteral("#e59572"), QStringLiteral("#e5c07b"),
            QStringLiteral("#98c379"), QStringLiteral("#56b6c2"), QStringLiteral("#61afef"),
            QStringLiteral("#c678dd"), QStringLiteral("#d19a66"), QStringLiteral("#5fb3a1"),
            QStringLiteral("#7aa2f7"), QStringLiteral("#bb9af7"), QStringLiteral("#f7768e"),
            QStringLiteral("#e0af68"), QStringLiteral("#9ece6a"), QStringLiteral("#7dcfff"),
            QStringLiteral("#ff9e64")};
}

QString nickColor(const QString& nick, const QStringList& palette) {
    const QStringList colors = palette.isEmpty() ? defaultNickPalette() : palette;

    QString key = nick;
    while (!key.isEmpty() && QStringLiteral("~&@%+").contains(key.front())) {
        key.remove(0, 1);
    }
    key = key.toLower();
    if (key.isEmpty()) {
        key = nick;
    }

    quint32 hash = 0;
    for (QChar ch : key) {
        hash = (hash * 31u + static_cast<quint32>(ch.unicode())) & 0xFFFFFFFFu;
    }
    return colors.at(static_cast<int>(hash % static_cast<quint32>(colors.size())));
}

QString stripFormatting(const QString& text) {
    QString out;
    out.reserve(text.size());

    for (qsizetype i = 0; i < text.size();) {
        const ushort code = text.at(i).unicode();
        if (code == 0x02 || code == 0x1D || code == 0x1F || code == 0x1E || code == 0x16 ||
            code == 0x11 || code == 0x0F) {
            ++i;
        } else if (code == 0x03) {
            ++i;
            readDigits(text, i, 2);
            if (i + 1 < text.size() && text.at(i) == QLatin1Char(',') && text.at(i + 1).isDigit()) {
                ++i;
                readDigits(text, i, 2);
            }
        } else if (code == 0x04) {
            ++i;
            if (hasHexRun(text, i, 6)) {
                i += 6;
                if (i + 7 <= text.size() && text.at(i) == QLatin1Char(',') &&
                    hasHexRun(text, i + 1, 6)) {
                    i += 7;
                }
            }
        } else {
            out.append(text.at(i));
            ++i;
        }
    }

    return out;
}

QString toHtml(const QString& text, const QString& defaultForeground,
               const QString& defaultBackground) {
    QString out;
    QString buffer;
    FormatState state;

    auto flush = [&]() {
        if (buffer.isEmpty()) {
            return;
        }

        QString foreground = state.foreground;
        QString background = state.background;
        if (state.reverse) {
            foreground = background.isEmpty() ? defaultBackground : background;
            background = state.foreground.isEmpty() ? defaultForeground : state.foreground;
        }

        QStringList styles;
        if (!foreground.isEmpty()) {
            styles.append(QStringLiteral("color:") + foreground);
        }
        if (!background.isEmpty()) {
            styles.append(QStringLiteral("background-color:") + background);
        }
        if (state.bold) {
            styles.append(QStringLiteral("font-weight:bold"));
        }
        if (state.italic) {
            styles.append(QStringLiteral("font-style:italic"));
        }

        QStringList decorations;
        if (state.underline) {
            decorations.append(QStringLiteral("underline"));
        }
        if (state.strike) {
            decorations.append(QStringLiteral("line-through"));
        }
        if (!decorations.isEmpty()) {
            styles.append(QStringLiteral("text-decoration:") + decorations.join(QLatin1Char(' ')));
        }

        const QString escaped = buffer.toHtmlEscaped();
        out += styles.isEmpty() ? escaped
                                : QStringLiteral("<span style=\"") + styles.join(QLatin1Char(';')) +
                                      QStringLiteral("\">") + escaped + QStringLiteral("</span>");
        buffer.clear();
    };

    for (qsizetype i = 0; i < text.size();) {
        const ushort code = text.at(i).unicode();
        if (code == 0x02) {
            flush();
            state.bold = !state.bold;
            ++i;
        } else if (code == 0x1D) {
            flush();
            state.italic = !state.italic;
            ++i;
        } else if (code == 0x1F) {
            flush();
            state.underline = !state.underline;
            ++i;
        } else if (code == 0x1E) {
            flush();
            state.strike = !state.strike;
            ++i;
        } else if (code == 0x16) {
            flush();
            state.reverse = !state.reverse;
            ++i;
        } else if (code == 0x11) {
            ++i;
        } else if (code == 0x0F) {
            flush();
            state = FormatState{};
            ++i;
        } else if (code == 0x03) {
            flush();
            ++i;
            const QString digits = readDigits(text, i, 2);
            if (digits.isEmpty()) {
                state.foreground.clear();
                state.background.clear();
            } else {
                state.foreground = colorForNumber(digits.toInt());
                if (i + 1 < text.size() && text.at(i) == QLatin1Char(',') &&
                    text.at(i + 1).isDigit()) {
                    ++i;
                    state.background = colorForNumber(readDigits(text, i, 2).toInt());
                }
            }
        } else if (code == 0x04) {
            flush();
            ++i;
            if (hasHexRun(text, i, 6)) {
                state.foreground = QStringLiteral("#") + text.mid(i, 6);
                i += 6;
                if (i + 7 <= text.size() && text.at(i) == QLatin1Char(',') &&
                    hasHexRun(text, i + 1, 6)) {
                    state.background = QStringLiteral("#") + text.mid(i + 1, 6);
                    i += 7;
                }
            } else {
                state.foreground.clear();
                state.background.clear();
            }
        } else {
            buffer.append(text.at(i));
            ++i;
        }
    }
    flush();
    return out;
}

} // namespace maxchat::irc
