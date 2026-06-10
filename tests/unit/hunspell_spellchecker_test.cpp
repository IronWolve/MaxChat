#include "spell/HunspellSpellchecker.h"

#include <QFileInfo>
#include <QtTest/QtTest>

using maxchat::spell::HunspellSpellchecker;

class HunspellSpellcheckerTest final : public QObject {
  Q_OBJECT

private slots:
  void loadsEnglishDictionaryAndChecksWords() {
    const QString affPath = QStringLiteral("/usr/share/hunspell/en_US.aff");
    const QString dicPath = QStringLiteral("/usr/share/hunspell/en_US.dic");
    if (!QFileInfo::exists(affPath) || !QFileInfo::exists(dicPath)) {
      QSKIP("en_US Hunspell dictionary is not installed");
    }

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
