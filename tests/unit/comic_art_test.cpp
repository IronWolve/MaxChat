#include "comic/ComicArt.h"

#include <QDir>
#include <QTemporaryFile>
#include <QtTest/QtTest>

using maxchat::comic::loadBackground;
using maxchat::comic::loadCharacterCells;

namespace {

void putU32(QByteArray& b, quint32 v) {
  for (int i = 0; i < 4; ++i) {
    b.append(static_cast<char>((v >> (8 * i)) & 0xFF));
  }
}
void putU16(QByteArray& b, quint16 v) {
  for (int i = 0; i < 2; ++i) {
    b.append(static_cast<char>((v >> (8 * i)) & 0xFF));
  }
}

// A 48-byte BITMAPINFOHEADER (+ origLen/cmprLen) like the decoder expects.
QByteArray dibHeader(qint32 w, qint32 h, quint16 bitCount, quint32 origLen,
                     quint32 cmprLen) {
  QByteArray b;
  putU32(b, 40);                  // +0  header size (0x28 magic)
  putU32(b, static_cast<quint32>(w));   // +4  width
  putU32(b, static_cast<quint32>(h));   // +8  height
  putU16(b, 1);                   // +12 planes
  putU16(b, bitCount);            // +14 bit count
  putU32(b, 0);                   // +16 compression
  putU32(b, 0);                   // +20 image size
  putU32(b, 0);                   // +24 x ppm
  putU32(b, 0);                   // +28 y ppm
  putU32(b, 0);                   // +32 colours used
  putU32(b, 0);                   // +36 colours important
  putU32(b, origLen);             // +40 inflated length (decoder-specific)
  putU32(b, cmprLen);             // +44 compressed length
  return b;
}

// Write bytes to a temp file kept alive by the caller; returns its path.
QString write(QTemporaryFile& f, const QByteArray& data) {
  if (!f.open()) {
    return {};
  }
  f.write(data);
  f.flush();
  return f.fileName();
}

} // namespace

class ComicArtTest final : public QObject {
  Q_OBJECT

private slots:
  void hugeOrigLenDoesNotAllocate() {
    // A crafted header claiming ~4GB inflated size must be rejected, not fed to
    // qUncompress (which would try to allocate it → OOM).
    QByteArray d = dibHeader(10, 10, 8, 0xFFFFFFFFu, 4);
    d.append("\x78\xda\x00\x00", 4);
    QTemporaryFile f(QDir::tempPath() + QStringLiteral("/cc_XXXXXX.bgb"));
    QVERIFY(loadBackground(write(f, d)).isNull());
  }

  void absurdDimensionsRejected() {
    QByteArray d = dibHeader(0x7FFFFFFF, 0x7FFFFFFF, 8, 1024, 4);
    d.append("\x78\xda\x00\x00", 4);
    QTemporaryFile f(QDir::tempPath() + QStringLiteral("/cc_XXXXXX.bgb"));
    QVERIFY(loadBackground(write(f, d)).isNull());
  }

  void garbageInputReturnsEmpty() {
    const QByteArray junk("this is not comic chat art, just some text bytes");
    QTemporaryFile bg(QDir::tempPath() + QStringLiteral("/cc_XXXXXX.bgb"));
    QVERIFY(loadBackground(write(bg, junk)).isNull());
    QTemporaryFile av(QDir::tempPath() + QStringLiteral("/cc_XXXXXX.avb"));
    QVERIFY(!loadCharacterCells(write(av, junk)).ok());
  }

  void truncatedHeaderReturnsNull() {
    QByteArray d = dibHeader(10, 10, 8, 100, 4);
    d.chop(20); // cut into the header
    QTemporaryFile f(QDir::tempPath() + QStringLiteral("/cc_XXXXXX.bgb"));
    QVERIFY(loadBackground(write(f, d)).isNull());
  }
};

QTEST_MAIN(ComicArtTest)

#include "comic_art_test.moc"
