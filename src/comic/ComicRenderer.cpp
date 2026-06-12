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
    bool shout = false;
};

// A line is "shouting" if it's all-caps (3+ letters) or piles on "!" — it gets
// a jagged burst balloon. Speech lines only; thoughts/actions never shout.
bool looksLikeShout(const QString& body) {
    if (body.contains(QStringLiteral("!!"))) {
        return true;
    }
    int letters = 0;
    bool allUpper = true;
    for (const QChar c : body) {
        if (c.isLetter()) {
            ++letters;
            if (!c.isUpper()) {
                allUpper = false;
            }
        }
    }
    return letters >= 3 && allUpper;
}

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
    const int gap = std::max(10, size / 24); // room for cloud/burst overhang
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
        const bool shout = !line.action && !line.think && looksLikeShout(body);
        rows.append({line.action, body, pos.first, pos.second, bw, bh, col, line.think,
                     !line.action, -1, shout});
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

constexpr double kPi = 3.14159265358979323846;

// Tail wedge with gently curved sides (the hand-drawn look). Both edges bow
// by the same horizontal offset — back toward the balloon, away from the tip —
// so the tail keeps its width and sweeps toward the speaker like a comma.
// A perfectly vertical tail stays straight (bow is proportional to the lean).
void drawTri(QPainter& p, QPointF b0, QPointF b1, QPointF apex, double penW) {
    const QPointF baseMid((b0.x() + b1.x()) / 2.0, (b0.y() + b1.y()) / 2.0);
    const double bowX = 0.35 * (baseMid.x() - apex.x());
    const auto control = [bowX](const QPointF& from, const QPointF& to) {
        return QPointF((from.x() + to.x()) / 2.0 + bowX, (from.y() + to.y()) / 2.0);
    };

    QPainterPath path;
    path.moveTo(b0);
    path.quadTo(control(b0, apex), apex);
    path.quadTo(control(apex, b1), b1);
    path.closeSubpath();
    p.setPen(Qt::NoPen);
    p.fillPath(path, QColor(255, 255, 255));

    QPainterPath edge0;
    edge0.moveTo(b0);
    edge0.quadTo(control(b0, apex), apex);
    QPainterPath edge1;
    edge1.moveTo(apex);
    edge1.quadTo(control(apex, b1), b1);
    QPen pen(QColor(0, 0, 0), penW);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(edge0);
    p.drawPath(edge1);
}

// A thought-bubble cloud: a ring of overlapping bumps around the text rect,
// unioned via QPainterPath::simplified() so the silhouette is one fluffy
// outline with no internal seams. (The old "think" balloon was just a
// rounded rect with a bigger corner radius — not a cloud at all.)
QPainterPath cloudPath(const QRectF& r) {
    const double bump = std::clamp(r.height() * 0.55, 14.0, 30.0);
    const int nx = std::max(3, static_cast<int>(std::lround(r.width() / bump)));
    const int ny = std::max(1, static_cast<int>(std::lround(r.height() / bump)));
    const double rx = (r.width() / nx) * 0.82;
    const double ry = (r.height() / ny) * 0.82;
    // Boolean-union the bumps with the centre rect (simplified() only removes
    // self-intersections, it does NOT merge subpaths — the seams stayed visible).
    QPainterPath path;
    path.addRoundedRect(r.adjusted(rx * 0.45, ry * 0.45, -rx * 0.45, -ry * 0.45), 6, 6);
    const auto addBump = [&](double cx, double cy) {
        QPainterPath e;
        e.addEllipse(QPointF(cx, cy), rx, ry);
        path = path.united(e);
    };
    // Centre the bumps slightly inside each edge so the cloud only bulges
    // ~0.6×radius beyond the text box (keeps it from eating neighbours).
    const double iy = ry * 0.4, ix = rx * 0.4;
    for (int i = 0; i < nx; ++i) {
        const double cx = r.left() + (i + 0.5) * r.width() / nx;
        addBump(cx, r.top() + iy);
        addBump(cx, r.bottom() - iy);
    }
    for (int j = 0; j < ny; ++j) {
        const double cy = r.top() + (j + 0.5) * r.height() / ny;
        addBump(r.left() + ix, cy);
        addBump(r.right() - ix, cy);
    }
    return path.simplified();
}

// A jagged "burst" balloon for shouting — alternating outer/inner radius
// spikes around the text rect (the classic comic exclamation/impact balloon).
QPainterPath burstPath(const QRectF& r, int spikes) {
    const QPointF c = r.center();
    const double orx = r.width() / 2.0 * 1.20;
    const double ory = r.height() / 2.0 * 1.26;
    const double irx = r.width() / 2.0 * 0.99;
    const double iry = r.height() / 2.0 * 1.02;
    QPainterPath path;
    const int n = spikes * 2;
    for (int i = 0; i <= n; ++i) {
        const double ang = kPi * 2.0 * i / n - kPi / 2.0;
        const bool outer = (i % 2 == 0);
        const double pr = outer ? orx : irx;
        const double pyr = outer ? ory : iry;
        const QPointF pt(c.x() + std::cos(ang) * pr, c.y() + std::sin(ang) * pyr);
        if (i == 0) {
            path.moveTo(pt);
        } else {
            path.lineTo(pt);
        }
    }
    path.closeSubpath();
    return path;
}

// Tail geometry. One set of rules so every tail in a strip looks like family
// (the old code mixed balloon-relative widths and arbitrary lean fractions —
// narrow balloons grew needle tails, stacked balloons grew directionless
// stubs, and lengths differed between the to-head and stacked cases):
//  - the base sits on the balloon's bottom edge, centred on the edge point
//    nearest the speaker's head, inset past the rounded corners;
//  - base width is PANEL-relative (~2.2%, clamped 5..12 px, never > bw/4) —
//    never balloon-relative;
//  - the tip travels along the base→head direction: at least 5% of the panel,
//    at most 16%, and always stops short of the head line, the balloon below,
//    and the panel edge. A tail points at its speaker; it doesn't have to
//    touch them;
//  - lean is clamped to ~50° from vertical so a far-away speaker can't drag
//    the tail near-horizontal.
void drawTail(QPainter& p, int size, int bx, int by, int bw, int bh, double headX, double headY,
              bool think, double limitY) {
    const double baseY = by + bh - 1;
    const double room = limitY - baseY;
    if (room < 6) {
        return;
    }

    const double inset = std::min(bw / 2.0 - 2.0, std::max(10.0, bw * 0.10));
    const double rootX = std::clamp(headX, bx + inset, bx + bw - inset);

    double dx = headX - rootX;
    const double dy = std::max(8.0, headY - baseY);
    const double maxLean = 1.19; // tan(~50°) from vertical
    dx = std::clamp(dx, -dy * maxLean, dy * maxLean);
    const double norm = std::hypot(dx, dy);
    const double ux = dx / norm;
    const double uy = dy / norm;

    if (think) {
        // Shrinking thought puffs along the same direction a speech tail would
        // take, sized by the panel so chains match across balloons.
        const double span = std::min({room - 3.0, size * 0.14, norm * 0.8});
        if (span < 6) {
            return;
        }
        const double big = std::clamp(size * 0.02, 3.0, 8.0);
        const int steps = std::clamp(static_cast<int>(span / (big * 2.2)) + 1, 2, 5);
        p.setPen(QPen(QColor(0, 0, 0), std::max(1, size / 260)));
        p.setBrush(QColor(255, 255, 255));
        // Puffs ride the same quadratic bow a speech tail's edges use, so
        // think- and speech-tails sweep identically.
        const double bowX = -0.35 * ux * span;
        for (int s = 1; s <= steps; ++s) {
            const double t = static_cast<double>(s) / steps;
            const double r = big * (1.0 - 0.4 * t);
            p.drawEllipse(QPointF(rootX + ux * span * t + 2.0 * t * (1.0 - t) * bowX,
                                  baseY + uy * span * t),
                          r, r * 0.85);
        }
        return;
    }

    double len = std::clamp(norm * 0.7, size * 0.05, size * 0.16);
    len = std::min(len, room - 3.0);
    if (len < 5) {
        return;
    }
    const double tipX = std::clamp(rootX + ux * len, 3.0, size - 3.0);
    const double tipY = baseY + uy * len;

    // Thinner than before (narrower base + lighter stroke) for a daintier tail.
    const double half = std::min(std::clamp(size * 0.016, 4.0, 9.0), bw / 4.0);
    const double penW = std::clamp(size / 230.0, 1.2, 1.8);
    drawTri(p, QPointF(std::max<double>(bx + 2, rootX - half), baseY),
            QPointF(std::min<double>(bx + bw - 2, rootX + half), baseY), QPointF(tipX, tipY), penW);
}

void drawBalloons(QPainter& p, const QVector<Row>& rows, const QFont& font, int size, int charTop,
                  const QVector<int>& cx) {
    if (rows.isEmpty()) {
        return;
    }
    for (const Row& r : rows) {
        if (r.action || !r.isTail) {
            continue;
        }
        const double headX = r.col < cx.size() ? cx[r.col] : size / 2.0;
        const double headY = charTop + size * 0.10; // aim at the face, not the hair
        const double limitY = r.tailStop < 0 ? charTop - 2.0 : r.tailStop - 4.0;
        drawTail(p, size, r.bx, r.by, r.bw, r.bh, headX, headY, r.think, limitY);
    }
    for (const Row& r : rows) {
        if (r.action) {
            // /me actions are narration captions (MS Comic Chat style): a
            // tailless cream box, italic text — deliberately NOT a speech or
            // thought balloon.
            QFont ital(font);
            ital.setItalic(true);
            p.setPen(QPen(QColor(120, 100, 40), 1));
            p.setBrush(QColor(255, 251, 224));
            p.drawRoundedRect(QRectF(r.bx, r.by, r.bw, r.bh), 3, 3);
            p.setFont(ital);
            p.setPen(QColor(40, 40, 40));
            p.drawText(QRect(r.bx + 8, r.by + 4, r.bw - 16, r.bh - 8), WrapCentre, r.body);
            continue;
        }
        const double bodyPen = std::clamp(size / 200.0, 1.4, 2.2);
        QPen outline(QColor(0, 0, 0), bodyPen);
        outline.setJoinStyle(Qt::RoundJoin);
        const QRectF box(r.bx, r.by, r.bw, r.bh);
        if (r.think) {
            // Proper thought cloud (scalloped silhouette), not a round rect.
            const QPainterPath cloud = cloudPath(box.adjusted(2, 2, -2, -2));
            p.setPen(Qt::NoPen);
            p.fillPath(cloud, QColor(255, 255, 255));
            p.setPen(outline);
            p.setBrush(Qt::NoBrush);
            p.drawPath(cloud);
        } else if (r.shout) {
            // Jagged exclamation/impact balloon.
            const int spikes = std::clamp(static_cast<int>(std::lround(r.bw / 14.0)), 9, 22);
            const QPainterPath burst = burstPath(box, spikes);
            p.setPen(Qt::NoPen);
            p.fillPath(burst, QColor(255, 255, 255));
            p.setPen(outline);
            p.setBrush(Qt::NoBrush);
            p.drawPath(burst);
        } else {
            p.setPen(outline);
            p.setBrush(QColor(255, 255, 255));
            p.drawRoundedRect(box, 7, 7);
        }
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
