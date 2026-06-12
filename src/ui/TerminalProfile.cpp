#include "ui/TerminalProfile.h"

namespace maxchat::ui {

TerminalProfile terminalProfile(const QString& id, const int cols, const int rows) {
    const QString key = id.trimmed().toLower();
    if (key == QStringLiteral("ibm-vga") || key.isEmpty()) {
        TerminalProfile profile;
        profile.id = QStringLiteral("ibm-vga");
        profile.cols = 80;
        profile.rows = 25;
        profile.fontFamily = QStringLiteral("JetBrains Mono");
        profile.fontPointSize = 11;
        profile.foreground = QColor(QStringLiteral("#d8d8d8"));
        profile.background = QColor(QStringLiteral("#000000"));
        profile.fitMode = QStringLiteral("fit");
        profile.fixedGrid = true;
        return profile;
    }
    if (key == QStringLiteral("c64")) {
        TerminalProfile profile;
        profile.id = QStringLiteral("c64");
        profile.cols = 40;
        profile.rows = 25;
        profile.fontFamily = QStringLiteral("JetBrains Mono");
        profile.fontPointSize = 13;
        profile.foreground = QColor(QStringLiteral("#9cc7ff"));
        profile.background = QColor(QStringLiteral("#352879"));
        profile.fitMode = QStringLiteral("integer");
        profile.fixedGrid = true;
        return profile;
    }

    TerminalProfile profile;
    profile.id = QStringLiteral("free");
    profile.cols = cols > 0 ? cols : 80;
    profile.rows = rows > 0 ? rows : 25;
    profile.fontFamily = QStringLiteral("JetBrains Mono");
    profile.fontPointSize = 12;
    profile.fitMode = QStringLiteral("none");
    profile.fixedGrid = false;
    return profile;
}

} // namespace maxchat::ui
