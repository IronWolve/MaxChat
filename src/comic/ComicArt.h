#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QString>

namespace maxchat::comic {

// Decoded cells of a Microsoft Comic Chat .avb character bundle.
struct CharacterCells {
    QString name;
    QList<QImage> bodies;          // tall pose cells
    QHash<int, QImage> faces;      // emotion id -> wide face cell
    [[nodiscard]] bool ok() const { return !bodies.isEmpty() || !faces.isEmpty(); }
};

// Decode a .bgb background to a 315x315 ARGB image (null on failure).
[[nodiscard]] QImage loadBackground(const QString& path);
// Decode a .avb character bundle into body + face cells.
[[nodiscard]] CharacterCells loadCharacterCells(const QString& path);

// Scan a Comic Chat art folder: returns (.bgb files, .avb files), sorted.
void scanArtDir(const QString& folder, QStringList& backgrounds, QStringList& characters);
// Dev-only bundled art dir (assets/cc-art) if present, else empty.
[[nodiscard]] QString bundledArtDir();

} // namespace maxchat::comic
