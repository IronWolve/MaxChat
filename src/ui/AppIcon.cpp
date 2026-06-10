#include "AppIcon.h"

#include <QPainter>
#include <QPainterPath>
#include <QFont>

namespace maxchat::ui {

static QStringList emojiFamilies() {
    return {
        QStringLiteral("Noto Color Emoji"),
        QStringLiteral("Segoe UI Emoji"),
        QStringLiteral("Apple Color Emoji"),
        QStringLiteral("Noto Emoji")
    };
}

QPixmap AppIcon::bubblePixmap(const QColor& accent, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 64.0, size / 64.0);
    p.setPen(Qt::NoPen);
    p.setBrush(accent);

    // Rounded rectangle body
    p.drawRoundedRect(6, 8, 52, 38, 10, 10);

    // Tail
    QPainterPath tail;
    tail.moveTo(22, 44);
    tail.lineTo(16, 58);
    tail.lineTo(34, 44);
    tail.closeSubpath();
    p.fillPath(tail, accent);

    // Three dots
    p.setBrush(Qt::white);
    for (int i = 0; i < 3; ++i) {
        p.drawEllipse(QPointF(22 + i * 10, 27), 3.0, 3.0);
    }
    p.end();
    return pm;
}

QPixmap AppIcon::emojiPixmap(const QString& glyph, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    QFont f;
    f.setFamilies(emojiFamilies());
    f.setPixelSize(static_cast<int>(size * 0.82));
    p.setFont(f);

    p.drawText(pm.rect(), Qt::AlignCenter, glyph);
    p.end();
    return pm;
}

QIcon AppIcon::makeIcon(const QString& choice, const QColor& accent) {
    if (choice.isEmpty() || choice == QLatin1String("bubble")) {
        return QIcon(bubblePixmap(accent));
    }
    return QIcon(emojiPixmap(choice));
}

} // namespace maxchat::ui