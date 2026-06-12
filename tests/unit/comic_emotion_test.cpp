#include "comic/ComicEmotion.h"

#include <QTest>

// Emotion guessing for comic mode — every one of the 9 emotions must be
// reachable from plain chat text (angry and bored were unreachable before
// 2026-06-11), and emoji must map alongside the classic emoticons.
class ComicEmotionTest final : public QObject {
    Q_OBJECT

  private slots:
    void guesses_data() {
        QTest::addColumn<QString>("body");
        QTest::addColumn<QString>("emotion");

        QTest::newRow("plain") << QStringLiteral("see you at 5") << QStringLiteral("neutral");
        QTest::newRow("smile") << QStringLiteral("sounds good :)") << QStringLiteral("happy");
        QTest::newRow("emoji smile") << QString::fromUtf8("nice \xF0\x9F\x98\x8A")
                                     << QStringLiteral("happy");
        QTest::newRow("frown") << QStringLiteral("that broke :(") << QStringLiteral("sad");
        QTest::newRow("crying emoji") << QString::fromUtf8("\xF0\x9F\x98\xAD")
                                      << QStringLiteral("sad");
        QTest::newRow("lol") << QStringLiteral("lol what") << QStringLiteral("laughing");
        QTest::newRow("joy emoji") << QString::fromUtf8("\xF0\x9F\x98\x82")
                                   << QStringLiteral("laughing");
        QTest::newRow("wink") << QStringLiteral("maybe ;)") << QStringLiteral("coy");
        QTest::newRow("tongue") << QStringLiteral("nope :P") << QStringLiteral("coy");
        QTest::newRow("interrobang") << QStringLiteral("it did what?!")
                                     << QStringLiteral("scared");
        QTest::newRow("omg") << QStringLiteral("omg the build") << QStringLiteral("scared");
        QTest::newRow("caps") << QStringLiteral("STOP DOING THAT")
                              << QStringLiteral("shouting");
        QTest::newRow("bangs") << QStringLiteral("no way!!!") << QStringLiteral("shouting");
        // The two previously-unreachable emotions:
        QTest::newRow("grr") << QStringLiteral("grr the printer again")
                             << QStringLiteral("angry");
        QTest::newRow("angry face") << QStringLiteral(">:( fine") << QStringLiteral("angry");
        QTest::newRow("angry emoji") << QString::fromUtf8("\xF0\x9F\x98\xA1")
                                     << QStringLiteral("angry");
        QTest::newRow("yawn") << QStringLiteral("yawn, meetings") << QStringLiteral("bored");
        QTest::newRow("zzz") << QStringLiteral("zzz this talk") << QStringLiteral("bored");
        // Precedence: angry face must not read as sad via its ":(" suffix.
        QTest::newRow("angry beats sad") << QStringLiteral("ugh >:( whatever")
                                         << QStringLiteral("angry");
        // Laughing beats happy when both are present.
        QTest::newRow("laugh beats smile") << QStringLiteral("haha :)")
                                           << QStringLiteral("laughing");
    }

    void guesses() {
        QFETCH(QString, body);
        QFETCH(QString, emotion);
        QCOMPARE(maxchat::comic::guessEmotion(body), emotion);
    }
};

QTEST_GUILESS_MAIN(ComicEmotionTest)
#include "comic_emotion_test.moc"
