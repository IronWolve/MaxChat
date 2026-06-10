#include "ui/SpellcheckHighlighter.h"

#include <QSet>
#include <QtTest/QtTest>

using maxchat::ui::misspelledWordRanges;
using maxchat::ui::SpellcheckRange;

namespace {

QSet<QString> correctWords() {
  return {QStringLiteral("hello"),   QStringLiteral("world"),
          QStringLiteral("message"), QStringLiteral("friend"),
          QStringLiteral("testing"), QStringLiteral("topic"),
          QStringLiteral("reason"),  QStringLiteral("action"),
          QStringLiteral("line")};
}

bool isCorrect(const QString &word) {
  return correctWords().contains(word.toLower());
}

QStringList wordsFromRanges(const QList<SpellcheckRange> &ranges) {
  QStringList words;
  for (const SpellcheckRange &range : ranges) {
    words.append(range.word);
  }
  return words;
}

} // namespace

class SpellcheckHighlighterTest final : public QObject {
  Q_OBJECT

private slots:
  void findsMisspelledWordsInPlainMessages() {
    const QList<SpellcheckRange> ranges = misspelledWordRanges(
        QStringLiteral("hello wurld from freind"), isCorrect);

    QCOMPARE(wordsFromRanges(ranges),
             QStringList({QStringLiteral("wurld"), QStringLiteral("from"),
                          QStringLiteral("freind")}));
    QCOMPARE(ranges.first().start, 6);
  }

  void ignoresUrlsChannelsAndShortTokens() {
    const QList<SpellcheckRange> ranges = misspelledWordRanges(
        QStringLiteral("#maxchat x https://example.net/wurld badword"),
        isCorrect);

    QCOMPARE(wordsFromRanges(ranges), QStringList({QStringLiteral("badword")}));
  }

  void skipsCommandSyntaxButChecksFreeTextArguments() {
    QCOMPARE(wordsFromRanges(misspelledWordRanges(
                 QStringLiteral("/join #maxchat"), isCorrect)),
             QStringList());
    QCOMPARE(wordsFromRanges(misspelledWordRanges(
                 QStringLiteral("/msg nick hello wurld"), isCorrect)),
             QStringList({QStringLiteral("wurld")}));
    QCOMPARE(wordsFromRanges(misspelledWordRanges(
                 QStringLiteral("/kick #chan nick reason wurld"), isCorrect)),
             QStringList({QStringLiteral("wurld")}));
  }
};

QTEST_APPLESS_MAIN(SpellcheckHighlighterTest)

#include "spellcheck_highlighter_test.moc"
