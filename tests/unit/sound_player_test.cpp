#include "ui/SoundPlayer.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using maxchat::ui::notifySoundPath;
using maxchat::ui::resolveSoundPath;

class SoundPlayerTest final : public QObject {
    Q_OBJECT

  private:
    static void touch(const QString& path) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("RIFF");
        f.close();
    }

  private slots:
    void emptyNameResolvesToNothing() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QCOMPARE(resolveSoundPath(dir.path(), QString()), QString());
    }

    void missingFileResolvesToNothing() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Right name, but no such file in the folder.
        QCOMPARE(resolveSoundPath(dir.path(), QStringLiteral("nope.wav")), QString());
    }

    void existingFileResolves() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        touch(QDir(dir.path()).filePath(QStringLiteral("ping.wav")));
        const QString got = resolveSoundPath(dir.path(), QStringLiteral("ping.wav"));
        QCOMPARE(got, QDir(dir.path()).filePath(QStringLiteral("ping.wav")));
    }

    void wavExtensionIsAppended() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        touch(QDir(dir.path()).filePath(QStringLiteral("beep.wav")));
        // Caller passed a bare name; ".wav" is appended before the lookup.
        QCOMPARE(resolveSoundPath(dir.path(), QStringLiteral("beep")),
                 QDir(dir.path()).filePath(QStringLiteral("beep.wav")));
    }

    void pathTraversalIsStrippedToBasename() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // A peer can't escape the sounds folder: only the basename is honoured,
        // so even if the traversal target exists it is never resolved.
        touch(QDir(dir.path()).filePath(QStringLiteral("evil.wav")));
        QCOMPARE(resolveSoundPath(dir.path(), QStringLiteral("../../evil.wav")),
                 QDir(dir.path()).filePath(QStringLiteral("evil.wav")));
        QCOMPARE(resolveSoundPath(dir.path(), QStringLiteral("..\\..\\evil.wav")),
                 QDir(dir.path()).filePath(QStringLiteral("evil.wav")));
        // An absolute path outside the folder collapses to its basename too.
        QCOMPARE(resolveSoundPath(dir.path(), QStringLiteral("/etc/passwd")), QString());
    }

    void notifyPrefersUserOverBundled() {
        QTemporaryDir user;
        QTemporaryDir bundled;
        QVERIFY(user.isValid() && bundled.isValid());
        touch(QDir(bundled.path()).filePath(QStringLiteral("notify.wav")));
        // Only the bundled copy exists → that one is used.
        QCOMPARE(notifySoundPath(user.path(), bundled.path()),
                 QDir(bundled.path()).filePath(QStringLiteral("notify.wav")));
        // Now the user drops their own → it takes precedence.
        touch(QDir(user.path()).filePath(QStringLiteral("notify.wav")));
        QCOMPARE(notifySoundPath(user.path(), bundled.path()),
                 QDir(user.path()).filePath(QStringLiteral("notify.wav")));
    }

    void notifyEmptyWhenNeitherExists() {
        QTemporaryDir user;
        QTemporaryDir bundled;
        QVERIFY(user.isValid() && bundled.isValid());
        QCOMPARE(notifySoundPath(user.path(), bundled.path()), QString());
    }
};

QTEST_MAIN(SoundPlayerTest)

#include "sound_player_test.moc"
