#include "comic/ComicArt.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <array>

// Port of the Python comic/assets.py decoder. .avb/.bgb wrap zlib-deflated
// Windows DIBs in a TLV chunk stream. We inflate with qUncompress by prefixing
// the big-endian original length (qUncompress's expected-size header) onto the
// raw zlib stream the file already carries.

namespace maxchat::comic {

namespace {

quint16 u16(const QByteArray& d, int o) {
    return o + 2 <= d.size() ? qFromLittleEndian<quint16>(
                                   reinterpret_cast<const uchar*>(d.constData() + o))
                             : 0;
}
quint32 u32(const QByteArray& d, int o) {
    return o + 4 <= d.size() ? qFromLittleEndian<quint32>(
                                   reinterpret_cast<const uchar*>(d.constData() + o))
                             : 0;
}
qint16 i16(const QByteArray& d, int o) {
    return static_cast<qint16>(u16(d, o));
}

// 2-bpp character cell LUT -> ARGB (B,G,R,A): 0 transparent, 1 ink, 2 fill, 3 grey.
const std::array<QRgb, 4>& cellLut() {
    static const std::array<QRgb, 4> lut = {qRgba(0, 0, 0, 0), qRgba(140, 140, 140, 255),
                                            qRgba(255, 255, 255, 255), qRgba(150, 150, 150, 255)};
    return lut;
}

struct Dib {
    int w = 0;
    int h = 0;
    int bitCount = 0;
    QByteArray pixels;
    bool ok = false;
};

// Hostile-file guards: real Comic Chat art is tiny (315x315 backgrounds, cells
// well under 512px), so these caps never reject legitimate art but stop a
// crafted .avb/.bgb from forcing a giant allocation.
constexpr qint64 MaxDibDim = 4096;                        // bounds stride + QImage size
constexpr quint32 MaxInflateBytes = 32u * 1024 * 1024;   // bounds qUncompress output alloc

// Inflate the zlib DIB whose BITMAPINFOHEADER is at offset j.
Dib inflateDib(const QByteArray& data, int j) {
    Dib out;
    if (j < 0 || j + 48 > data.size()) {
        return out;
    }
    // Use qint64 for the height magnitude — std::abs(INT_MIN) would be UB.
    const qint32 wRaw = static_cast<qint32>(u32(data, j + 4));
    const qint32 hRaw = static_cast<qint32>(u32(data, j + 8));
    const qint64 w = wRaw;
    const qint64 h = hRaw < 0 ? -static_cast<qint64>(hRaw) : static_cast<qint64>(hRaw);
    if (w <= 0 || w > MaxDibDim || h <= 0 || h > MaxDibDim) {
        return out; // reject absurd/hostile dimensions
    }
    out.w = static_cast<int>(w);
    out.h = static_cast<int>(h);
    out.bitCount = u16(data, j + 14);
    const quint32 origLen = u32(data, j + 40);
    const quint32 cmprLen = u32(data, j + 44);
    // 64-bit arithmetic throughout: cmprLen is an attacker-controlled quint32, so
    // `static_cast<int>(cmprLen)` could wrap NEGATIVE and slip past the bounds
    // check (and then feed a negative length to append). Compare/append in qint64.
    const qint64 dataLen = data.size();
    const qint64 cmprLen64 = static_cast<qint64>(cmprLen);
    if (cmprLen64 == 0 || static_cast<qint64>(j) + 48 + cmprLen64 > dataLen) {
        return out;
    }
    // qUncompress allocates the prefixed length up front, so an attacker-set
    // origLen (~4GB) would OOM us. Reject anything beyond a sane ceiling.
    if (origLen == 0 || origLen > MaxInflateBytes) {
        return out;
    }
    QByteArray packed;
    packed.resize(4);
    qToBigEndian<quint32>(origLen, reinterpret_cast<uchar*>(packed.data()));
    packed.append(data.constData() + j + 48, cmprLen64);
    out.pixels = qUncompress(packed);
    out.ok = !out.pixels.isEmpty();
    return out;
}

// RGB palette from the 0x0101 chunk preceding a DIB header -> BGRA bytes.
QByteArray paletteBefore(const QByteArray& data, int j) {
    const int from = std::max(0, j - 8000);
    int pj = data.lastIndexOf(QByteArray("\x01\x01", 2), j);
    if (pj < from || pj < 0) {
        return {};
    }
    const int num = u16(data, pj + 4);
    if (num <= 0 || num > 256) {
        return {};
    }
    const int base = pj + 6;
    if (base + num * 3 > data.size()) {
        return {};
    }
    QByteArray bgra;
    bgra.reserve(num * 4);
    for (int k = 0; k < num; ++k) {
        const uchar r = static_cast<uchar>(data[base + k * 3]);
        const uchar g = static_cast<uchar>(data[base + k * 3 + 1]);
        const uchar b = static_cast<uchar>(data[base + k * 3 + 2]);
        bgra.append(static_cast<char>(b));
        bgra.append(static_cast<char>(g));
        bgra.append(static_cast<char>(r));
        bgra.append('\0');
    }
    return bgra;
}

// Decode a 4/8-bpp paletted DIB (backgrounds) by synthesising a BMP and loading it.
QImage palettedImage(const QByteArray& data, int j) {
    const Dib dib = inflateDib(data, j);
    if (!dib.ok) {
        return {};
    }
    const QByteArray pal = paletteBefore(data, j);
    const int nc = pal.size() / 4;
    QByteArray ih(40, '\0');
    qToLittleEndian<quint32>(40u, reinterpret_cast<uchar*>(ih.data()));
    qToLittleEndian<qint32>(dib.w, reinterpret_cast<uchar*>(ih.data() + 4));
    qToLittleEndian<qint32>(dib.h, reinterpret_cast<uchar*>(ih.data() + 8));
    qToLittleEndian<quint16>(1, reinterpret_cast<uchar*>(ih.data() + 12));
    qToLittleEndian<quint16>(static_cast<quint16>(dib.bitCount),
                             reinterpret_cast<uchar*>(ih.data() + 14));
    qToLittleEndian<quint32>(static_cast<quint32>(dib.pixels.size()),
                             reinterpret_cast<uchar*>(ih.data() + 20));
    qToLittleEndian<quint32>(static_cast<quint32>(nc), reinterpret_cast<uchar*>(ih.data() + 32));
    QByteArray dibBlob = ih + pal + dib.pixels;
    QByteArray bmp("BM");
    QByteArray hdr(12, '\0');
    qToLittleEndian<quint32>(static_cast<quint32>(14 + dibBlob.size()),
                             reinterpret_cast<uchar*>(hdr.data()));
    qToLittleEndian<quint32>(static_cast<quint32>(14 + 40 + pal.size()),
                             reinterpret_cast<uchar*>(hdr.data() + 8));
    bmp.append(hdr);
    bmp.append(dibBlob);
    return QImage::fromData(bmp, "BMP").convertToFormat(QImage::Format_ARGB32);
}

// Decode a 2-bpp character cell -> transparent ARGB image (index 0 transparent).
QImage cellImage(const QByteArray& data, int j) {
    const Dib dib = inflateDib(data, j);
    if (!dib.ok || dib.w <= 0 || dib.h <= 0) {
        return {};
    }
    const int stride = ((dib.w * 2 + 31) / 32) * 4;
    QImage img(dib.w, dib.h, QImage::Format_ARGB32);
    const auto& lut = cellLut();
    for (int y = 0; y < dib.h; ++y) {
        const int sr = dib.h - 1 - y; // DIB rows are bottom-up
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < dib.w; ++x) {
            const int byteIdx = sr * stride + (x / 4);
            int v = 0;
            if (byteIdx < dib.pixels.size()) {
                const uchar b = static_cast<uchar>(dib.pixels[byteIdx]);
                v = (b >> (6 - 2 * (x % 4))) & 3;
            }
            line[x] = lut[v];
        }
    }
    return img;
}

int headerOffsetAfterPalette(const QByteArray& data, int off) {
    if (off >= 0 && off + 2 <= data.size() && static_cast<uchar>(data[off]) == 0x01 &&
        static_cast<uchar>(data[off + 1]) == 0x01) {
        const int num = u16(data, off + 4);
        const int nxt = off + 6 + num * 3;
        if (nxt + 4 <= data.size() && static_cast<uchar>(data[nxt]) == 0x28 &&
            data[nxt + 1] == 0 && data[nxt + 2] == 0 && data[nxt + 3] == 0) {
            return nxt;
        }
    }
    return off;
}

QImage tryCell(const QByteArray& data, int off) {
    if (off < 0 || off + 4 > data.size() || static_cast<uchar>(data[off]) != 0x28) {
        return {};
    }
    return cellImage(data, off);
}

QList<int> zlibDibOffsets(const QByteArray& data) {
    QList<int> out;
    int i = 0;
    while (true) {
        const int z = data.indexOf(QByteArray("\x78\xda", 2), i);
        if (z < 0) {
            break;
        }
        out.append(z - 48);
        i = z + 1;
    }
    return out;
}

// Drop "face" cells that are really full-body / self-preview cells. The
// aspect-ratio classifier (h > w*1.4 = body) lets near-square oversized cells
// through as faces (e.g. a 262x332 self cell next to 166x190 heads); the
// emotion→face stretch then maps "shouting" (the last emotion) onto that junk
// cell, garbling the avatar. A real face is head-sized — much smaller area
// than these outliers.
void dropOutlierFaces(QHash<int, QImage>& faces) {
    if (faces.size() < 3) {
        return; // too few to establish a median
    }
    QList<qint64> areas;
    for (const QImage& f : faces) {
        areas.append(static_cast<qint64>(f.width()) * f.height());
    }
    std::sort(areas.begin(), areas.end());
    const qint64 median = areas.at(areas.size() / 2);
    const qint64 cap = median * 9 / 5; // 1.8x the median face area
    QList<int> drop;
    for (auto it = faces.constBegin(); it != faces.constEnd(); ++it) {
        if (static_cast<qint64>(it.value().width()) * it.value().height() > cap) {
            drop.append(it.key());
        }
    }
    // Never drop them all (a degenerate set where every face looks "large").
    if (drop.size() < faces.size()) {
        for (const int id : drop) {
            faces.remove(id);
        }
    }
}

QList<QImage> cleanBodies(QList<QImage> bodies, const QHash<int, QImage>& faces) {
    if (bodies.size() < 2 || faces.isEmpty()) {
        return bodies;
    }
    QList<int> bh;
    for (const QImage& b : bodies) {
        bh.append(b.height());
    }
    std::sort(bh.begin(), bh.end());
    const int bmed = bh.at(bh.size() / 2);
    QList<int> fh;
    for (const QImage& f : faces) {
        fh.append(f.height());
    }
    std::sort(fh.begin(), fh.end());
    const int fmed = fh.isEmpty() ? 0 : fh.at(fh.size() / 2);
    QList<QImage> real;
    for (const QImage& b : bodies) {
        if (!(b.height() < bmed * 0.85 && b.height() <= fmed * 1.4)) {
            real.append(b);
        }
    }
    return real.isEmpty() ? bodies : real;
}

// Cell tables: tag -> fixed record size.
int cellTableRecordSize(int tag) {
    switch (tag) {
    case 0x0004: return 43;
    case 0x0005:
    case 0x0009: return 35;
    case 0x000a: return 33;
    case 0x000b:
    case 0x000c: return 25;
    default: return 0;
    }
}

} // namespace

QImage loadBackground(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray data = file.readAll();
    const int j = data.indexOf(QByteArray("\x28\x00\x00\x00", 4));
    return j >= 0 ? palettedImage(data, j) : QImage();
}

CharacterCells loadCharacterCells(const QString& path) {
    CharacterCells cells;
    cells.name = QFileInfo(path).completeBaseName();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return cells;
    }
    const QByteArray data = file.readAll();

    // Chunk-table parse (magic 0x8181).
    if (data.size() > 6 && static_cast<uchar>(data[0]) == 0x81 &&
        static_cast<uchar>(data[1]) == 0x81) {
        int i = 6;
        quint32 bias = 0;
        QString name;
        QList<int> bodyOffsets;
        QHash<int, int> faceOffsets;
        bool parseOk = true;
        while (i < data.size() - 2) {
            const int tag = u16(data, i);
            if (tag == 0x0006 || tag == 0x0007) {
                break;
            }
            if (tag >= 0x0100) {
                if (tag == 0x0107) {
                    bias = u32(data, i + 4);
                }
                i += 4 + u16(data, i + 2);
            } else if (tag == 0x0001) {
                const int end = data.indexOf('\0', i + 2);
                if (end < 0) {
                    parseOk = false;
                    break;
                }
                name = QString::fromLatin1(data.mid(i + 2, end - (i + 2)));
                i = end + 1;
            } else if (tag == 0x0002 || tag == 0x0008) {
                i += 4;
            } else if (tag == 0x0003) {
                i += 6;
            } else if (const int rs = cellTableRecordSize(tag); rs > 0) {
                const int cnt = u16(data, i + 2);
                const int recBase = i + 4;
                for (int r = 0; r < cnt; ++r) {
                    const int rec = recBase + r * rs;
                    if (rec + 14 > data.size()) {
                        break;
                    }
                    const int off =
                        headerOffsetAfterPalette(data, static_cast<int>(u32(data, rec) + bias));
                    const int eid = i16(data, rec + 12);
                    if (off < 0 || off + 40 > data.size() ||
                        static_cast<uchar>(data[off]) != 0x28) {
                        continue;
                    }
                    const int w = static_cast<qint32>(u32(data, off + 4));
                    const int h = static_cast<qint32>(u32(data, off + 8));
                    if (w <= 0 || h == 0) {
                        continue;
                    }
                    if (std::abs(h) > w * 1.4) {
                        bodyOffsets.append(off);
                    } else if (!faceOffsets.contains(eid)) {
                        faceOffsets.insert(eid, off);
                    }
                }
                i = recBase + cnt * rs;
            } else {
                break;
            }
        }
        if (parseOk && (!bodyOffsets.isEmpty() || !faceOffsets.isEmpty())) {
            for (const int off : bodyOffsets) {
                const QImage img = tryCell(data, off);
                if (!img.isNull()) {
                    cells.bodies.append(img);
                }
            }
            for (auto it = faceOffsets.constBegin(); it != faceOffsets.constEnd(); ++it) {
                const QImage img = tryCell(data, it.value());
                if (!img.isNull()) {
                    cells.faces.insert(it.key(), img);
                }
            }
            dropOutlierFaces(cells.faces);
            if (cells.ok()) {
                if (!name.isEmpty()) {
                    cells.name = name;
                }
                cells.bodies = cleanBodies(cells.bodies, cells.faces);
                return cells;
            }
        }
    }

    // Fallback: scan every zlib image, classify by aspect, key faces by order.
    QList<QImage> faceList;
    for (const int j : zlibDibOffsets(data)) {
        if (j < 0 || j + 16 > data.size()) {
            continue;
        }
        const int sz = static_cast<int>(u32(data, j));
        const int w = static_cast<qint32>(u32(data, j + 4));
        const int h = static_cast<qint32>(u32(data, j + 8));
        const int bc = u16(data, j + 14);
        if (sz != 40 || bc != 2 || w < 40 || std::abs(h) < 40) {
            continue;
        }
        const QImage img = tryCell(data, j);
        if (img.isNull()) {
            continue;
        }
        if (std::abs(h) > w * 1.4) {
            cells.bodies.append(img);
        } else {
            faceList.append(img);
        }
    }
    for (int k = 0; k < faceList.size(); ++k) {
        cells.faces.insert(k + 1, faceList.at(k));
    }
    dropOutlierFaces(cells.faces);
    cells.bodies = cleanBodies(cells.bodies, cells.faces);
    return cells;
}

void scanArtDir(const QString& folder, QStringList& backgrounds, QStringList& characters) {
    backgrounds.clear();
    characters.clear();
    QDir dir(folder);
    if (!dir.exists()) {
        return;
    }
    QMap<QString, QString> bg;
    QMap<QString, QString> ch;
    for (const QFileInfo& info : dir.entryInfoList(QDir::Files)) {
        const QString ext = info.suffix().toLower();
        if (ext == QStringLiteral("bgb")) {
            bg.insert(info.fileName().toLower(), info.absoluteFilePath());
        } else if (ext == QStringLiteral("avb")) {
            ch.insert(info.fileName().toLower(), info.absoluteFilePath());
        }
    }
    backgrounds = bg.values();
    characters = ch.values();
}

QString bundledArtDir() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {QDir(appDir).filePath(QStringLiteral("assets/cc-art")),
                                    QDir(appDir).filePath(QStringLiteral("../assets/cc-art")),
                                    QDir::current().filePath(QStringLiteral("assets/cc-art"))};
    for (const QString& dir : candidates) {
        if (QDir(dir).exists()) {
            return QDir(dir).absolutePath();
        }
    }
    return {};
}

} // namespace maxchat::comic
