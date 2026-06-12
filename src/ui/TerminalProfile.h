#pragma once

#include <QColor>
#include <QString>

namespace maxchat::ui {

struct TerminalProfile {
    QString id = QStringLiteral("free");
    int cols = 80;
    int rows = 25;
    QString fontFamily = QStringLiteral("JetBrains Mono");
    int fontPointSize = 11;
    QColor foreground = QColor(QStringLiteral("#eeeeee"));
    QColor background = QColor(QStringLiteral("#101418"));
    QString fitMode = QStringLiteral("none");
    bool fixedGrid = false;
};

[[nodiscard]] TerminalProfile terminalProfile(const QString& id, int cols = 80, int rows = 25);

} // namespace maxchat::ui
