#include "spell/HunspellSpellchecker.h"

#include <QDir>
#include <QFileInfo>
#include <QtTest/QtTest>

using maxchat::spell::HunspellSpellchecker;

class HunspellSpellcheckerTest final : public QObject {
  Q_OBJECT

private slots:
  void loadsBundledEnglishDictionaryAndChecksWords() {
    // The BUNDLED en_US (assets/dictionaries) loaded by the VENDORED engine —
    // end-to-end coverage for third_party/hunspell + the shipped dictionary.
    const QDir dictDir(QStringLiteral(MAXCHAT_TEST_DICTIONARY_DIR));
    const QString affPath = dictDir.filePath(QStringLiteral("en_US.aff"));
    const QString dicPath = dictDir.filePath(QStringLiteral("en_US.dic"));
    QVERIFY2(QFileInfo::exists(affPath) && QFileInfo::exists(dicPath),
             "bundled en_US dictionary is missing from assets/dictionaries");

    HunspellSpellchecker checker;
    QVERIFY(checker.loadDictionary(affPath, dicPath));
    QVERIFY(checker.isLoaded());
    QVERIFY(checker.isCorrect(QStringLiteral("hello")));
    QVERIFY(checker.isCorrect(QStringLiteral("world.")));
    QVERIFY(!checker.isCorrect(QStringLiteral("zzzznotawordzzzz")));
    QVERIFY(!checker.suggestions(QStringLiteral("speling")).isEmpty());
  }

  void loadsBundledNonEnglishDictionaries() {
    // Spot-check a few of the bundled language packs actually load and check
    // words (different FLAG schemes: de default, fr long, tr num, ru default).
    const QDir dictDir(QStringLiteral(MAXCHAT_TEST_DICTIONARY_DIR));
    struct Case {
      QString code;
      QString correct;
      QString wrong;
    };
    const QList<Case> cases = {
        {QStringLiteral("de_DE"), QStringLiteral("Haus"), QStringLiteral("zzqxhaus")},
        {QStringLiteral("fr_FR"), QStringLiteral("bonjour"), QStringLiteral("zzqxbonjour")},
        {QStringLiteral("ru_RU"), QString::fromUtf8("\xD0\xB4\xD0\xBE\xD0\xBC"),
         QStringLiteral("zzqxdom")},
    };
    for (const Case &c : cases) {
      const QString aff = dictDir.filePath(c.code + QStringLiteral(".aff"));
      const QString dic = dictDir.filePath(c.code + QStringLiteral(".dic"));
      if (!QFileInfo::exists(aff) || !QFileInfo::exists(dic)) {
        continue; // language not bundled in this build configuration
      }
      HunspellSpellchecker checker;
      QVERIFY2(checker.loadDictionary(aff, dic), qPrintable(c.code));
      QVERIFY2(checker.isCorrect(c.correct), qPrintable(c.code + ": " + c.correct));
      QVERIFY2(!checker.isCorrect(c.wrong), qPrintable(c.code + ": " + c.wrong));
    }
  }

  void addWordMakesItCorrect() {
    // Personal-dictionary path: a non-word becomes accepted after addWord().
    const QDir dictDir(QStringLiteral(MAXCHAT_TEST_DICTIONARY_DIR));
    HunspellSpellchecker checker;
    QVERIFY(checker.loadDictionary(dictDir.filePath(QStringLiteral("en_US.aff")),
                                   dictDir.filePath(QStringLiteral("en_US.dic"))));
    const QString word = QStringLiteral("zzqxwidget");
    QVERIFY(!checker.isCorrect(word));
    QVERIFY(checker.addWord(word));
    QVERIFY(checker.isCorrect(word));
  }

  void missingDictionaryUnloadsEngine() {
    HunspellSpellchecker checker;
    QVERIFY(!checker.loadDictionary(QStringLiteral("/tmp/missing.aff"),
                                    QStringLiteral("/tmp/missing.dic")));
    QVERIFY(!checker.isLoaded());
    QVERIFY(!checker.isCorrect(QStringLiteral("hello")));
    QVERIFY(checker.suggestions(QStringLiteral("hello")).isEmpty());
  }
};

QTEST_APPLESS_MAIN(HunspellSpellcheckerTest)

#include "hunspell_spellchecker_test.moc"
