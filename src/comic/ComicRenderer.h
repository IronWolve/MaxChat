#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QVector>

namespace maxchat::comic {

class Character;

struct ComicActor {
    Character* character = nullptr;
    QString emotion;
    QString nick;
    int pose = 0;
};

struct ComicLineItem {
    int actorIndex = 0;
    QString text;
    bool think = false;
    bool action = false;
};

// Render one square comic panel: background + characters facing each other +
// speech balloons over each speaker. Port of renderer.py render_panel().
[[nodiscard]] QPixmap renderComicPanel(int size, const QImage& background,
                                       const QVector<ComicActor>& actors,
                                       const QVector<ComicLineItem>& lines, bool captions,
                                       double captionScale,
                                       const QHash<QString, QString>& captionColors);

// The font point-size a panel would actually use (for the spill check).
[[nodiscard]] int panelMinFont(int size, const QVector<ComicActor>& actors,
                               const QVector<ComicLineItem>& lines);

} // namespace maxchat::comic
