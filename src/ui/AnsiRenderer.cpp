#include "ui/AnsiRenderer.h"

#include <QUrl>

namespace maxchat::ui {

namespace {

struct StyleState {
    QString fg;
    QString bg;
    bool bold = false;
    bool underline = false;

    [[nodiscard]] bool active() const {
        return !fg.isEmpty() || !bg.isEmpty() || bold || underline;
    }
};

QString colorForCode(const int code) {
    switch (code) {
    case 30: return QStringLiteral("#000000");
    case 31: return QStringLiteral("#aa0000");
    case 32: return QStringLiteral("#00aa00");
    case 33: return QStringLiteral("#aa5500");
    case 34: return QStringLiteral("#0000aa");
    case 35: return QStringLiteral("#aa00aa");
    case 36: return QStringLiteral("#00aaaa");
    case 37: return QStringLiteral("#aaaaaa");
    case 90: return QStringLiteral("#555555");
    case 91: return QStringLiteral("#ff5555");
    case 92: return QStringLiteral("#55ff55");
    case 93: return QStringLiteral("#ffff55");
    case 94: return QStringLiteral("#5555ff");
    case 95: return QStringLiteral("#ff55ff");
    case 96: return QStringLiteral("#55ffff");
    case 97: return QStringLiteral("#ffffff");
    default: return {};
    }
}

QString styleAttr(const StyleState& style) {
    QStringList rules;
    if (!style.fg.isEmpty()) {
        rules.append(QStringLiteral("color:%1").arg(style.fg));
    }
    if (!style.bg.isEmpty()) {
        rules.append(QStringLiteral("background-color:%1").arg(style.bg));
    }
    if (style.bold) {
        rules.append(QStringLiteral("font-weight:700"));
    }
    if (style.underline) {
        rules.append(QStringLiteral("text-decoration:underline"));
    }
    return rules.isEmpty() ? QString() : QStringLiteral(" style=\"%1\"").arg(rules.join(';'));
}

QString escapedText(const QString& text) {
    QString out = text.toHtmlEscaped();
    out.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return out;
}

QString hotspotHtml(const QString& encodedAction, const QString& encodedLabel) {
    const QString href = QStringLiteral("mc-term:%1").arg(encodedAction);
    const QString label = QUrl::fromPercentEncoding(encodedLabel.toUtf8()).toHtmlEscaped();
    return QStringLiteral("<a href=\"%1\">%2</a>").arg(href, label);
}

void appendRun(QString& out, const QString& run, const StyleState& style) {
    if (run.isEmpty()) {
        return;
    }
    const QString escaped = escapedText(run);
    if (!style.active()) {
        out += escaped;
        return;
    }
    out += QStringLiteral("<span%1>%2</span>").arg(styleAttr(style), escaped);
}

void applySgr(const QString& params, StyleState& style) {
    const QStringList parts = params.isEmpty() ? QStringList{QStringLiteral("0")}
                                               : params.split(QLatin1Char(';'));
    for (const QString& part : parts) {
        bool ok = false;
        const int code = part.isEmpty() ? 0 : part.toInt(&ok);
        if (!ok) {
            continue;
        }
        if (code == 0) {
            style = {};
        } else if (code == 1) {
            style.bold = true;
        } else if (code == 22) {
            style.bold = false;
        } else if (code == 4) {
            style.underline = true;
        } else if (code == 24) {
            style.underline = false;
        } else if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97)) {
            style.fg = colorForCode(code);
        } else if (code == 39) {
            style.fg.clear();
        } else if ((code >= 40 && code <= 47) || (code >= 100 && code <= 107)) {
            const int fgCode = code >= 100 ? code - 10 : code - 10;
            style.bg = colorForCode(fgCode);
        } else if (code == 49) {
            style.bg.clear();
        }
    }
}

} // namespace

QString AnsiRenderer::toHtml(const QString& text) {
    QString out;
    QString run;
    StyleState style;

    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('\x1b') && i + 1 < text.size()) {
            if (text.at(i + 1) == QLatin1Char('[')) {
                const qsizetype end = text.indexOf(QLatin1Char('m'), i + 2);
                if (end < 0) {
                    run += ch;
                    continue;
                }
                appendRun(out, run, style);
                run.clear();
                applySgr(text.mid(i + 2, end - i - 2), style);
                i = end;
                continue;
            }
            if (text.at(i + 1) == QLatin1Char(']')) {
                const qsizetype end = text.indexOf(QLatin1Char('\x07'), i + 2);
                if (end < 0) {
                    run += ch;
                    continue;
                }
                const QString payload = text.mid(i + 2, end - i - 2);
                static const QString prefix = QStringLiteral("MC-HOTSPOT;");
                if (payload.startsWith(prefix)) {
                    const QString rest = payload.mid(prefix.size());
                    const qsizetype sep = rest.indexOf(QLatin1Char(';'));
                    if (sep > 0) {
                        appendRun(out, run, style);
                        run.clear();
                        out += hotspotHtml(rest.left(sep), rest.mid(sep + 1));
                        i = end;
                        continue;
                    }
                }
            }
        }
        if (ch == QLatin1Char('\r')) {
            continue;
        }
        run += ch;
    }
    appendRun(out, run, style);
    return out;
}

QString AnsiRenderer::hotspot(const QString& actionId, const QString& label) {
    return QStringLiteral("\x1b]MC-HOTSPOT;%1;%2\x07")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(actionId)),
             QString::fromUtf8(QUrl::toPercentEncoding(label)));
}

} // namespace maxchat::ui
