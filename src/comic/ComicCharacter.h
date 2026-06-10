#pragma once

#include "comic/ComicArt.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QString>

namespace maxchat::comic {

// The 9 Comic Chat emotions: neutral centre + 8 ring emotions.
[[nodiscard]] const QStringList& emotions();

// A loaded comic character: body poses + emotion faces, composing a head onto a
// body at the neck, mirroring for facing, and trimming to content. Port of the
// Python characters.py Character class (with the same caches).
class Character {
  public:
    explicit Character(CharacterCells cells);

    [[nodiscard]] bool ok() const { return cells_.ok(); }
    [[nodiscard]] QString name() const { return cells_.name; }
    [[nodiscard]] int bodyCount() const { return cells_.bodies.size(); }

    // Composite cropped to its non-transparent content (feet at the bottom).
    [[nodiscard]] QImage imageTrimmed(const QString& emotion, const QString& facing, int pose);
    // The face cell for an emotion (for the emotion picker preview).
    [[nodiscard]] QImage faceCell(const QString& emotion);

  private:
    [[nodiscard]] QImage faceFor(const QString& emotion);
    [[nodiscard]] QImage compose(const QImage& body, const QImage& face, int poseKey);
    [[nodiscard]] QImage baseImage(const QString& emotion, int pose);
    [[nodiscard]] QImage image(const QString& emotion, const QString& facing, int pose);

    CharacterCells cells_;
    QList<int> ids_; // sorted face emotion ids
    QHash<QString, QImage> base_;
    QHash<QString, QImage> composed_;
    QHash<QString, QImage> trimmed_;
    QHash<int, QPair<int, int>> neckCache_; // pose index -> (neck_y, neck_cx); (-1,-1) = none
};

// Cache-backed loader by path. Returns nullptr if the file has no usable cells.
[[nodiscard]] Character* loadCharacter(const QString& path);

} // namespace maxchat::comic
