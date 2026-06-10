#include "comic/ComicCharacter.h"

#include <QPainter>

namespace maxchat::comic {

namespace {

constexpr int AlphaThreshold = 16;

bool alphaBBox(const QImage& src, int& x0, int& y0, int& x1, int& y1) {
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();
    x0 = w;
    y0 = h;
    x1 = -1;
    y1 = -1;
    for (int y = 0; y < h; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            if (qAlpha(line[x]) > AlphaThreshold) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
        }
    }
    return x1 >= 0;
}

// Neck = topmost row of the body's densest (torso-centre) column.
bool neck(const QImage& src, int& neckY, int& neckCx) {
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();
    QList<int> colCount(w, 0);
    QList<QPair<int, int>> spans(h, {-1, -1});
    bool any = false;
    for (int y = 0; y < h; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(img.scanLine(y));
        int lo = -1;
        int hi = -1;
        for (int x = 0; x < w; ++x) {
            if (qAlpha(line[x]) > AlphaThreshold) {
                ++colCount[x];
                if (lo < 0) {
                    lo = x;
                }
                hi = x;
            }
        }
        spans[y] = {lo, hi};
        if (lo >= 0) {
            any = true;
        }
    }
    if (!any) {
        return false;
    }
    int cx = 0;
    for (int x = 1; x < w; ++x) {
        if (colCount[x] > colCount[cx]) {
            cx = x;
        }
    }
    for (int y = 0; y < h; ++y) {
        if (spans[y].first >= 0 && spans[y].first <= cx && cx <= spans[y].second) {
            neckY = y;
            neckCx = cx;
            return true;
        }
    }
    for (int y = 0; y < h; ++y) {
        if (spans[y].first >= 0) {
            neckY = y;
            neckCx = (spans[y].first + spans[y].second) / 2;
            return true;
        }
    }
    return false;
}

} // namespace

const QStringList& emotions() {
    static const QStringList list = {QStringLiteral("neutral"),  QStringLiteral("happy"),
                                     QStringLiteral("laughing"), QStringLiteral("coy"),
                                     QStringLiteral("bored"),    QStringLiteral("scared"),
                                     QStringLiteral("sad"),      QStringLiteral("angry"),
                                     QStringLiteral("shouting")};
    return list;
}

Character::Character(CharacterCells cells) : cells_(std::move(cells)) {
    ids_ = cells_.faces.keys();
    std::sort(ids_.begin(), ids_.end());
}

QImage Character::faceFor(const QString& emotion) {
    if (ids_.isEmpty()) {
        return {};
    }
    const int idx = std::max(0, static_cast<int>(emotions().indexOf(emotion)));
    const int mapped =
        ids_.size() == 1
            ? 0
            : static_cast<int>(std::lround(static_cast<double>(idx) /
                                           (emotions().size() - 1) * (ids_.size() - 1)));
    return cells_.faces.value(ids_.at(std::clamp<int>(mapped, 0, static_cast<int>(ids_.size()) - 1)));
}

QImage Character::faceCell(const QString& emotion) { return faceFor(emotion); }

QImage Character::compose(const QImage& body, const QImage& face, int poseKey) {
    if (body.isNull()) {
        return face;
    }
    if (face.isNull()) {
        return body;
    }
    if (!neckCache_.contains(poseKey)) {
        int ny = -1;
        int ncx = -1;
        neckCache_.insert(poseKey, neck(body, ny, ncx) ? qMakePair(ny, ncx) : qMakePair(-1, -1));
    }
    const QPair<int, int> geom = neckCache_.value(poseKey);
    if (geom.first < 0) {
        return body;
    }
    int fx0;
    int fy0;
    int fx1;
    int fy1;
    if (!alphaBBox(face, fx0, fy0, fx1, fy1)) {
        return body;
    }
    const int neckY = geom.first;
    const int neckCx = geom.second;
    const int overlap = static_cast<int>((fy1 - fy0) * 0.22);
    const int fdx = neckCx - (fx0 + fx1) / 2;
    const int fdy = neckY + overlap - fy1;
    const int left = std::min(0, fdx);
    const int right = std::max(body.width(), fdx + face.width());
    const int top = std::max(0, -fdy);
    QImage out(right - left, body.height() + top, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.drawImage(-left, top, body);
    p.drawImage(fdx - left, fdy + top, face);
    p.end();
    return out;
}

QImage Character::baseImage(const QString& emotion, int pose) {
    const QString key = QStringLiteral("%1|%2").arg(emotion).arg(pose);
    if (!base_.contains(key)) {
        const QImage body = cells_.bodies.isEmpty() ? QImage() : cells_.bodies.at(pose);
        base_.insert(key, compose(body, faceFor(emotion), pose));
    }
    return base_.value(key);
}

QImage Character::image(const QString& emotion, const QString& facing, int pose) {
    const QString emo = emotions().contains(emotion) ? emotion : QStringLiteral("neutral");
    const int poseIdx = cells_.bodies.isEmpty() ? 0 : (pose % cells_.bodies.size());
    const QString key = QStringLiteral("%1|%2|%3").arg(emo, facing).arg(poseIdx);
    if (composed_.contains(key)) {
        return composed_.value(key);
    }
    QImage img = baseImage(emo, poseIdx);
    if (!img.isNull() && facing == QStringLiteral("left")) {
        img = img.flipped(Qt::Horizontal);
    }
    composed_.insert(key, img);
    return img;
}

QImage Character::imageTrimmed(const QString& emotion, const QString& facing, int pose) {
    const QString emo = emotions().contains(emotion) ? emotion : QStringLiteral("neutral");
    const int poseIdx = cells_.bodies.isEmpty() ? 0 : (pose % cells_.bodies.size());
    const QString key = QStringLiteral("%1|%2|%3").arg(emo, facing).arg(poseIdx);
    if (trimmed_.contains(key)) {
        return trimmed_.value(key);
    }
    QImage img = image(emo, facing, poseIdx);
    if (!img.isNull()) {
        int x0;
        int y0;
        int x1;
        int y1;
        if (alphaBBox(img, x0, y0, x1, y1)) {
            img = img.copy(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
        }
    }
    trimmed_.insert(key, img);
    return img;
}

Character* loadCharacter(const QString& path) {
    static QHash<QString, Character*> cache;
    if (cache.contains(path)) {
        return cache.value(path);
    }
    auto* ch = new Character(loadCharacterCells(path));
    if (!ch->ok()) {
        delete ch;
        cache.insert(path, nullptr);
        return nullptr;
    }
    cache.insert(path, ch);
    return ch;
}

} // namespace maxchat::comic
