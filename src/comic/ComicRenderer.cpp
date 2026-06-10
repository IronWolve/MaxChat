#include "comic/ComicRenderer.h"

#include "comic/ComicCharacter.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

// Port of comic/renderer.py. Balloons hang above their speaker; the lowest in a
// column grows a tail down to the head; /me is a tailless narration box.

namespace maxchat::comic {

namespace {

constexpr int Wrap = Qt::TextWordWrap;
constexpr int WrapCentre = Qt::TextWordWrap | Qt::AlignCenter;

QFont comicFont(int pointSize, bool bold = false) {
    QFont f(QStringLiteral("Comic Relief"));
    f.setPointSize(std::max(1, pointSize));
    f.setBold(bold);
    return f;
}

int floorLine(int size) {
    return size - std::max(3, static_cast<int>(size * 0.02)) - static_cast<int>(size * 0.085);
}

int balloonMaxW(int size, int n) {
    const double f = n <= 1 ? 0.66 : n == 2 ? 0.58 : n == 3 ? 0.5 : 0.42;
    return static_cast<int>(size * f);
}

struct Placed {
    int x, y, w, h;
};

// One laid-out balloon row.
struct Row {
    bool action;
    QString body;
    int bx, by, bw, bh, col;
    bool think;
    bool isTail;
    int tailStop; // -1 = none (runs to the head)
};

QVector<int> placeCharsWidths(int size, const QVector<ComicActor>& actors, int& targetH,
                              QVector<int>& xs) {
    const int n = actors.size();
    QVector<int> widths(n);
    for (int i = 0; i < n; ++i) {
        const QImage im = actors[i].character
                              ? actors[i].character->imageTrimmed(actors[i].emotion,
                                                                  QStringLiteral("right"),
                                                                  actors[i].pose)
                              : QImage();
        widths[i] = (!im.isNull() && im.height() > 0)
                        ? static_cast<int>(im.width() * static_cast<double>(targetH) / im.height())
                        : static_cast<int>(targetH * 0.6);
    }
    const int gap = std::max(2, static_cast<int>(size * 0.012));
    auto total = [&]() {
        int t = 0;
        for (int w : widths) {
            t += w;
        }
        return t + gap * (n - 1);
    };
    int tot = total();
    if (tot > size * 0.96 && tot > 0) {
        const double f = size * 0.96 / tot;
        targetH = std::max(20, static_cast<int>(targetH * f));
        for (int& w : widths) {
            w = std::max(1, static_cast<int>(w * f));
        }
        tot = total();
    }
    xs.resize(n);
    if (n == 1) {
        xs[0] = (size - widths[0]) / 2;
    } else {
        const double factor = n == 2 ? 0.58 : n == 3 ? 0.76 : 0.9;
        const double span = std::max<double>(tot, size * factor);
        const double left = std::max<double>(static_cast<int>(size * 0.02), (size - span) / 2);
        const double step = std::max(0.0, (span - tot) / (n - 1));
        double xacc = left;
        for (int i = 0; i < n; ++i) {
            xs[i] = static_cast<int>(std::lround(xacc));
            xacc += widths[i] + gap + step;
        }
    }
    return widths;
}

// Initial cx for the balloon pass.
QVector<int> initialCx(int size, const QVector<ComicActor>& actors, int nLines) {
    const int n = actors.size();
    const int nb = std::max(1, nLines);
    const double crowd = n <= 2 ? 1.0 : n == 3 ? 0.86 : n <= 4 ? 0.74 : 0.64;
    int targetH = static_cast<int>(size * (nb <= 1 ? 0.62 : nb <= 3 ? 0.48 : 0.4) * crowd);
    QVector<int> xs;
    const QVector<int> widths = placeCharsWidths(size, actors, targetH, xs);
    QVector<int> cx(n);
    for (int i = 0; i < n; ++i) {
        cx[i] = xs[i] + widths[i] / 2;
    }
    return cx;
}

int fitTop(int size, bool hasActors) {
    if (!hasActors) {
        return size - 6;
    }
    return floorLine(size) - static_cast<int>(size * 0.30) - std::max(6, static_cast<int>(size * 0.02));
}

QVector<Row> layout(QPainter& p, const QFont& font, int size, const QVector<ComicLineItem>& lines,
                    const QVector<int>& cx, const QVector<ComicActor>& actors, int maxw,
                    int& overflow) {
    const int n = std::max<int>(1, static_cast<int>(cx.size()));
    const int gap = std::max(6, size / 32);
    const int top = std::max(6, size / 26);
    QVector<int> colY(n, top);
    QVector<Placed> placed;
    QVector<Row> rows;

    auto collides = [&](int bx, int by, int bw, int bh) {
        for (const Placed& o : placed) {
            if (bx < o.x + o.w && bx + bw > o.x && by < o.y + o.h + gap && by + bh + gap > o.y) {
                return true;
            }
        }
        return false;
    };
    auto place = [&](double center, int bw, int bh, int startY) {
        const int target = static_cast<int>(std::min<double>(std::max(4.0, center - bw / 2.0),
                                                             size - 4 - bw));
        int by = startY;
        while (true) {
            QList<int> cands = {target, 4, size - 4 - bw};
            for (const Placed& o : placed) {
                if (by < o.y + o.h + gap && by + bh + gap > o.y) {
                    cands.append(std::min(std::max(4, o.x + o.w + gap), size - 4 - bw));
                    cands.append(std::min(std::max(4, o.x - bw - gap), size - 4 - bw));
                }
            }
            QList<int> valid;
            for (int c : cands) {
                if (!collides(c, by, bw, bh)) {
                    valid.append(c);
                }
            }
            if (!valid.isEmpty()) {
                int best = valid.first();
                for (int c : valid) {
                    if (std::abs(c - target) < std::abs(best - target)) {
                        best = c;
                    }
                }
                return qMakePair(best, by);
            }
            by += std::max(6, bh / 3);
            if (by + bh > size - 4) {
                return qMakePair(target, by);
            }
        }
    };

    for (const ComicLineItem& line : lines) {
        const int col = (line.actorIndex >= 0 && line.actorIndex < n) ? line.actorIndex : 0;
        const double center = col < cx.size() ? cx[col] : size / 2.0;
        QString body;
        int bw, bh;
        if (line.action) {
            const QString nick =
                (line.actorIndex >= 0 && line.actorIndex < actors.size())
                    ? actors[line.actorIndex].nick
                    : QString();
            body = QStringLiteral("%1 %2").arg(nick, line.text).trimmed();
            QFont ital(font);
            ital.setItalic(true);
            p.setFont(ital);
            const QRect r = p.boundingRect(QRect(0, 0, maxw - 16, size), Wrap, body);
            bw = std::min(maxw, r.width() + 16);
            bh = r.height() + 8;
        } else {
            body = line.text;
            p.setFont(font);
            const QRect r = p.boundingRect(QRect(0, 0, maxw - 18, size), Wrap, body);
            bw = std::min(maxw, r.width() + 20);
            bh = r.height() + 10;
        }
        const QPair<int, int> pos = place(center, bw, bh, colY[col]);
        placed.append({pos.first, pos.second, bw, bh});
        colY[col] = pos.second + bh + gap;
        rows.append({line.action, body, pos.first, pos.second, bw, bh, col, line.think,
                     !line.action, -1});
    }
    for (Row& r : rows) {
        int below = -1;
        for (const Row& o : rows) {
            if (&o != &r && o.col == r.col && o.by > r.by + 1) {
                below = below < 0 ? o.by : std::min(below, o.by);
            }
        }
        r.tailStop = below;
    }
    overflow = top;
    for (const Placed& o : placed) {
        overflow = std::max(overflow, o.y + o.h);
    }
    return rows;
}

QFont fitBalloons(QPainter& p, int size, const QVector<ComicLineItem>& lines,
                  const QVector<int>& cx, const QVector<ComicActor>& actors, int bottom,
                  QVector<Row>& rowsOut) {
    if (lines.isEmpty()) {
        rowsOut.clear();
        return comicFont(std::max(8, size / 26));
    }
    const int maxw = balloonMaxW(size, std::max<int>(1, static_cast<int>(cx.size())));
    QFont font;
    for (int fs = std::max(8, size / 26); fs > 6; --fs) {
        font = comicFont(fs);
        int overflow = 0;
        rowsOut = layout(p, font, size, lines, cx, actors, maxw, overflow);
        if (overflow <= bottom) {
            break;
        }
    }
    return font;
}

void drawTri(QPainter& p, QPointF b0, QPointF b1, QPointF apex) {
    QPainterPath path;
    path.moveTo(b0);
    path.lineTo(apex);
    path.lineTo(b1);
    path.closeSubpath();
    p.setPen(Qt::NoPen);
    p.fillPath(path, QColor(255, 255, 255));
    p.setPen(QPen(QColor(0, 0, 0), 2));
    p.drawLine(b0, apex);
    p.drawLine(apex, b1);
}

void drawTail(QPainter& p, int size, int bx, int by, int bw, int bh, double tailX, bool think,
              bool longTail, int headY, bool toHead) {
    const double cy = by + bh - 1;
    const double room = headY - (by + bh);
    double root;
    if (tailX < bx + bw * 0.33) {
        root = bx + std::max(6, static_cast<int>(bw * 0.10));
    } else if (tailX > bx + bw * 0.67) {
        root = bx + bw - std::max(6, static_cast<int>(bw * 0.10));
    } else {
        root = std::max<double>(bx + 10, std::min<double>(tailX, bx + bw - 10));
    }
    if (think) {
        const double span = std::max(5.0, room - 3);
        const double endX = root + (tailX - root) * 0.6;
        const double big = std::min(std::max(3.0, size * 0.019), span * 0.5);
        const int steps = std::max(2, std::min(7, static_cast<int>(span / std::max(4.0, big))));
        p.setPen(QPen(QColor(0, 0, 0), std::max(1, size / 260)));
        p.setBrush(QColor(255, 255, 255));
        for (int s = 1; s <= steps; ++s) {
            const double t = static_cast<double>(s) / steps;
            const double r = big * (1.0 - 0.45 * t);
            p.drawEllipse(QPointF(root + (endX - root) * t, (by + bh) + span * t), r, r * 0.85);
        }
        return;
    }
    if (room < 8) {
        return;
    }
    if (!toHead) {
        const int cap = longTail ? static_cast<int>(size * 0.09) : static_cast<int>(size * 0.045);
        const double tipY = by + bh + std::min<double>(room - 3, std::max(8, cap));
        const double tipX = root + (tailX - root) * 0.55;
        const double half = std::max(4.0, bw * 0.05);
        drawTri(p, QPointF(std::max<double>(bx + 2, root - half), cy),
                QPointF(std::min<double>(bx + bw - 2, root + half), cy), QPointF(tipX, tipY));
        return;
    }
    const double tipY = by + bh + std::max<double>(8, room - std::max(4.0, size * 0.02));
    const double margin = bw * 0.12;
    if (tailX < bx - margin || tailX > bx + bw + margin) {
        const double ex = tailX < bx ? bx + 1 : bx + bw - 1;
        const double rooty = by + bh * 0.5;
        const double halfv = std::max(5.0, bh * 0.24);
        drawTri(p, QPointF(ex, rooty - halfv), QPointF(ex, rooty + halfv), QPointF(tailX, tipY));
    } else {
        const double tipX = root + (tailX - root) * 0.82;
        const double half = std::max(4.0, bw * 0.05);
        drawTri(p, QPointF(std::max<double>(bx + 2, root - half), cy),
                QPointF(std::min<double>(bx + bw - 2, root + half), cy), QPointF(tipX, tipY));
    }
}

void drawBalloons(QPainter& p, const QVector<Row>& rows, const QFont& font, int size, int charTop,
                  const QVector<int>& cx) {
    if (rows.isEmpty()) {
        return;
    }
    const bool multi = std::max<int>(1, static_cast<int>(cx.size())) >= 2;
    for (const Row& r : rows) {
        if (r.action || !r.isTail) {
            continue;
        }
        const int floorY = r.tailStop < 0 ? charTop : r.tailStop;
        drawTail(p, size, r.bx, r.by, r.bw, r.bh, r.col < cx.size() ? cx[r.col] : size / 2.0,
                 r.think, multi, floorY, r.tailStop < 0);
    }
    for (const Row& r : rows) {
        if (r.action) {
            QFont ital(font);
            ital.setItalic(true);
            p.setPen(QPen(QColor(0, 0, 0), 1));
            p.setBrush(QColor(255, 251, 224));
            p.drawRect(r.bx, r.by, r.bw, r.bh);
            p.setFont(ital);
            p.setPen(QColor(40, 40, 40));
            p.drawText(QRect(r.bx + 8, r.by + 4, r.bw - 16, r.bh - 8), WrapCentre, r.body);
            continue;
        }
        p.setPen(QPen(QColor(0, 0, 0), 2));
        p.setBrush(QColor(255, 255, 255));
        p.drawRoundedRect(r.bx, r.by, r.bw, r.bh, r.think ? 18 : 6, r.think ? 18 : 6);
        p.setPen(QColor(0, 0, 0));
        p.setFont(font);
        p.drawText(QRect(r.bx + 9, r.by + 5, r.bw - 18, r.bh - 10), WrapCentre, r.body);
    }
}

void caption(QPainter& p, int size, const QString& nick, int x, int w, int feet,
             const QString& boxColor, double scale) {
    QFont f = comicFont(std::max(6, static_cast<int>((size / 30) * std::max(0.5, scale))), true);
    p.setFont(f);
    const QFontMetrics fm = p.fontMetrics();
    const int maxw = std::max(static_cast<int>(w * 1.25), static_cast<int>(size * 0.34));
    const QString text = fm.elidedText(nick, Qt::ElideRight, maxw);
    const int tw = fm.horizontalAdvance(text) + 12;
    const int th = fm.height() + 4;
    const int bx = std::max(2, std::min(static_cast<int>(x + w / 2 - tw / 2), size - tw - 2));
    const int by = std::min(feet + 2, size - th - 2);
    QColor bg = boxColor.isEmpty() ? QColor(54, 54, 54) : QColor(boxColor);
    bg.setAlpha(236);
    const double lum = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue();
    const QColor txt = lum > 140 ? QColor(20, 20, 20) : QColor(255, 255, 255);
    p.setPen(QPen(lum <= 140 ? QColor(255, 255, 255, 150) : QColor(0, 0, 0, 110), 1));
    p.setBrush(bg);
    p.drawRoundedRect(bx, by, tw, th, 4, 4);
    p.setPen(txt);
    p.drawText(QRect(bx, by, tw, th), Qt::AlignCenter, text);
}

} // namespace

int panelMinFont(int size, const QVector<ComicActor>& actors,
                 const QVector<ComicLineItem>& lines) {
    QVector<int> cx;
    if (!actors.isEmpty()) {
        cx = initialCx(size, actors, lines.size());
    }
    const int bottom = fitTop(size, !actors.isEmpty());
    const int maxw = balloonMaxW(size, std::max<int>(1, static_cast<int>(cx.size())));
    QImage img(1, 1, QImage::Format_ARGB32);
    QPainter p(&img);
    int chosen = std::max(7, size / 26);
    for (int fs = std::max(8, size / 26); fs > 6; --fs) {
        chosen = fs;
        int overflow = 0;
        layout(p, comicFont(fs), size, lines, cx, actors, maxw, overflow);
        if (overflow <= bottom) {
            break;
        }
    }
    p.end();
    return chosen;
}

QPixmap renderComicPanel(int size, const QImage& background, const QVector<ComicActor>& actors,
                         const QVector<ComicLineItem>& lines, bool captions, double captionScale,
                         const QHash<QString, QString>& captionColors) {
    QPixmap pm(size, size);
    pm.fill(QColor(255, 255, 255));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!background.isNull()) {
        p.drawImage(QRect(0, 0, size, size), background, background.rect());
    }

    const int feet = floorLine(size);
    if (!actors.isEmpty()) {
        const int n = actors.size();
        const int gap = std::max(6, static_cast<int>(size * 0.02));
        const int tailRoom = std::max(gap, static_cast<int>(size * 0.06));
        const int minH = static_cast<int>(size * 0.30);
        const int maxH = static_cast<int>(size * 0.62);
        QVector<int> cx = initialCx(size, actors, lines.size());
        QVector<Row> rows;
        const QFont font =
            fitBalloons(p, size, lines, cx, actors, feet - minH - gap, rows);
        int stackBottom = std::max(6, size / 26);
        for (const Row& r : rows) {
            stackBottom = std::max(stackBottom, r.by + r.bh);
        }
        const int charTop =
            std::max(feet - maxH, std::min(stackBottom + tailRoom, feet - minH));
        int targetH = feet - charTop;
        QVector<int> xs;
        const QVector<int> widths = placeCharsWidths(size, actors, targetH, xs);
        cx.resize(n);
        for (int i = 0; i < n; ++i) {
            cx[i] = xs[i] + widths[i] / 2;
        }
        for (int i = 0; i < n; ++i) {
            const QString facing =
                cx[i] < size / 2.0 ? QStringLiteral("right") : QStringLiteral("left");
            const QImage im = actors[i].character
                                  ? actors[i].character->imageTrimmed(actors[i].emotion, facing,
                                                                      actors[i].pose)
                                  : QImage();
            if (!im.isNull() && im.height() > 0) {
                p.drawImage(QRect(xs[i], feet - targetH, widths[i], targetH), im);
                if (captions) {
                    caption(p, size, actors[i].nick, xs[i], widths[i], feet,
                            captionColors.value(actors[i].nick.toLower()), captionScale);
                }
            }
        }
        drawBalloons(p, rows, font, size, charTop, cx);
    } else if (!lines.isEmpty()) {
        QVector<int> cx;
        QVector<Row> rows;
        const QFont font = fitBalloons(p, size, lines, cx, actors,
                                       static_cast<int>(size * 0.72), rows);
        drawBalloons(p, rows, font, size, size, cx);
    }

    p.setPen(QPen(QColor(0, 0, 0), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(1, 1, size - 2, size - 2);
    p.end();
    return pm;
}

} // namespace maxchat::comic
