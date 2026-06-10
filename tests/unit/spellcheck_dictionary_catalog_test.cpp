#include "spell/SpellcheckDictionaryCatalog.h"

#include <QFile>
#include <QIODevice>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using maxchat::spell::SpellcheckLanguage;
using maxchat::spell::spellcheckLanguages;

namespace {

bool writeEmptyFile(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly);
}

const SpellcheckLanguage *
findLanguage(const QList<SpellcheckLanguage> &languages, const QString &code) {
  for (const SpellcheckLanguage &language : languages) {
    if (language.code == code) {
      return &language;
    }
  }
  return nullptr;
}

} // namespace

class SpellcheckDictionaryCatalogTest final : public QObject {
  Q_OBJECT

private slots:
  void marksLanguagesAvailableWhenAffAndDicExist() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeEmptyFile(dir.filePath(QStringLiteral("en_US.aff"))));
    QVERIFY(writeEmptyFile(dir.filePath(QStringLiteral("en_US.dic"))));
    QVERIFY(writeEmptyFile(dir.filePath(QStringLiteral("pt_BR.aff"))));
    QVERIFY(writeEmptyFile(dir.filePath(QStringLiteral("pt_BR.dic"))));

    const QList<SpellcheckLanguage> languages =
        spellcheckLanguages({dir.path()});
    const SpellcheckLanguage *english =
        findLanguage(languages, QStringLiteral("en"));
    const SpellcheckLanguage *portugueseBrazil =
        findLanguage(languages, QStringLiteral("pt_BR"));
    QVERIFY(english != nullptr);
    QVERIFY(portugueseBrazil != nullptr);

    QVERIFY(english->dictionaryAvailable());
    QCOMPARE(english->dictionaryCode, QStringLiteral("en_US"));
    QCOMPARE(english->displayLabel(), QStringLiteral("English"));
    QVERIFY(portugueseBrazil->dictionaryAvailable());
    QCOMPARE(portugueseBrazil->dictionaryCode, QStringLiteral("pt_BR"));
  }

  void marksMissingLanguagesWithPlaceholderLabels() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QList<SpellcheckLanguage> languages =
        spellcheckLanguages({dir.path()});
    const SpellcheckLanguage *french =
        findLanguage(languages, QStringLiteral("fr"));
    QVERIFY(french != nullptr);

    QVERIFY(!french->dictionaryAvailable());
    QCOMPARE(french->displayLabel(),
             QStringLiteral("French [dictionary missing]"));
  }
};

QTEST_APPLESS_MAIN(SpellcheckDictionaryCatalogTest)

#include "spellcheck_dictionary_catalog_test.moc"
