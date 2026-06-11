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
