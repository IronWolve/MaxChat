#include "ui/ComicView.h"

#include "irc/IrcFormat.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace maxchat::ui {

namespace {

// A stable per-nick character: colour from the shared nick palette, a face
// shape index, and an expression, all derived from the nick hash.
QColor characterColor(const QString& nick) {
    return QColor(maxchat::irc::nickColor(nick));
}

int nickHash(const QString& nick) {
    uint h = 0;
    for (const QChar c : nick.toLower()) {
        h = h * 31u + c.unicode();
    }
    return static_cast<int>(h);
}

void drawCharacter(QPainter& painter, const QRectF& box, const QString& nick, bool action) {
    const QColor base = characterColor(nick);
    const int h = nickHash(nick);

    // Head: rounded square or circle depending on the hash.
    QRectF head(box.center().x() - box.width() * 0.32, box.top() + box.height() * 0.08,
                box.width() * 0.64, box.height() * 0.64);
    painter.setPen(QPen(base.darker(150), 2));
    painter.setBrush(base.lighter(115));
    if (h & 1) {
        painter.drawRoundedRect(head, 8, 8);
    } else {
        painter.drawEllipse(head);
    }

    // Eyes.
    const double eyeY = head.top() + head.height() * 0.38;
    const double eyeDx = head.width() * 0.20;
    const double eyeR = qMax(2.0, head.width() * 0.06);
    painter.setBrush(QColor(20, 20, 20));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(head.center().x() - eyeDx, eyeY), eyeR, eyeR);
    painter.drawEllipse(QPointF(head.center().x() + eyeDx, eyeY), eyeR, eyeR);

    // Mouth: expression from the hash (and a wide grin for /me actions).
    const double mouthY = head.top() + head.height() * 0.68;
    const double mouthW = head.width() * 0.42;
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(20, 20, 20), 2));
    QPainterPath mouth;
    const int expr = action ? 0 : (h / 2) % 3;
    if (expr == 0) { // smile
        mouth.moveTo(head.center().x() - mouthW / 2, mouthY);
        mouth.quadTo(head.center().x(), mouthY + head.height() * 0.12, head.center().x() + mouthW / 2,
                     mouthY);
    } else if (expr == 1) { // neutral
        mouth.moveTo(head.center().x() - mouthW / 2, mouthY + 3);
        mouth.lineTo(head.center().x() + mouthW / 2, mouthY + 3);
    } else { // frown
        mouth.moveTo(head.center().x() - mouthW / 2, mouthY + head.height() * 0.08);
        mouth.quadTo(head.center().x(), mouthY - head.height() * 0.04,
                     head.center().x() + mouthW / 2, mouthY + head.height() * 0.08);
    }
    painter.drawPath(mouth);
}

void drawSpeechBubble(QPainter& painter, const QRectF& box, const QString& text,
                      const QFont& font) {
    painter.setFont(font);
    const QRectF inner = box.adjusted(8, 8, -8, -8);
    QPainterPath bubble;
    bubble.addRoundedRect(box, 10, 10);
    // Tail pointing up toward the character.
    bubble.moveTo(box.center().x() - 8, box.top());
    bubble.lineTo(box.center().x() + 2, box.top() - 9);
    bubble.lineTo(box.center().x() + 10, box.top());
    painter.setPen(QPen(QColor(40, 40, 40), 2));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawPath(bubble.simplified());
    painter.setPen(QColor(20, 20, 20));
    painter.drawText(inner, Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, text);
}

} // namespace

ComicView::ComicView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("comicView"));
    setMinimumHeight(120);
}

void ComicView::setLines(const QVector<ComicLine>& lines) {
    lines_ = lines;
    update();
}

void ComicView::setShowNames(bool show) {
    showNames_ = show;
    update();
}

void ComicView::setPanelCount(int count) {
    panelCount_ = qBound(1, count, 12);
    update();
}

int ComicView::columnsForWidth() const {
    return width() >= 720 ? 2 : 1;
}

void ComicView::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (lines_.isEmpty()) {
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignCenter,
                         QStringLiteral("Comic Mode - recent chat appears as panels here."));
        return;
    }

    // Show the most recent panelCount_ lines.
    QVector<ComicLine> shown = lines_;
    if (shown.size() > panelCount_) {
        shown = shown.mid(shown.size() - panelCount_);
    }

    const int cols = columnsForWidth();
    const int rows = (shown.size() + cols - 1) / cols;
    const int gap = 8;
    const double panelW = (width() - gap * (cols + 1)) / static_cast<double>(cols);
    const double panelH =
        qMax(140.0, (height() - gap * (rows + 1)) / static_cast<double>(qMax(1, rows)));

    QFont nameFont(QStringLiteral("Comic Relief"));
    nameFont.setBold(true);
    nameFont.setPointSize(10);
    QFont bubbleFont(QStringLiteral("Comic Relief"));
    bubbleFont.setPointSize(11);

    for (int i = 0; i < shown.size(); ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const QRectF panel(gap + col * (panelW + gap), gap + row * (panelH + gap), panelW, panelH);

        // Panel frame with a soft per-speaker background tint.
        QColor tint = characterColor(shown[i].nick);
        tint.setAlpha(28);
        painter.setPen(QPen(QColor(60, 60, 60), 2));
        painter.setBrush(tint);
        painter.drawRoundedRect(panel, 6, 6);

        // Character on the left, bubble filling the rest.
        const QRectF charBox(panel.left() + 8, panel.top() + 8, panel.height() * 0.5,
                             panel.height() * 0.6);
        drawCharacter(painter, charBox, shown[i].nick, shown[i].action);

        if (showNames_) {
            painter.setFont(nameFont);
            painter.setPen(characterColor(shown[i].nick).darker(140));
            painter.drawText(QRectF(charBox.left(), charBox.bottom() + 2, charBox.width(), 18),
                             Qt::AlignHCenter | Qt::AlignTop, shown[i].nick);
        }

        const QRectF bubbleBox(charBox.right() + 14, panel.top() + 16, panel.right() - charBox.right() - 24,
                               panel.height() - 28);
        const QString text = shown[i].action
                                 ? QStringLiteral("* %1 %2").arg(shown[i].nick, shown[i].text)
                                 : shown[i].text;
        drawSpeechBubble(painter, bubbleBox, text, bubbleFont);
    }
}

} // namespace maxchat::ui
