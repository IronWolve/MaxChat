#include "spell/SpellcheckDictionaryCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace maxchat::spell {
namespace {

struct LanguageSpec {
  QString code;
  QString label;
  QStringList dictionaryCodes;
};

QList<LanguageSpec> languageSpecs() {
  return {
      {QStringLiteral("en"),
       QStringLiteral("English"),
       {QStringLiteral("en"), QStringLiteral("en_US"),
        QStringLiteral("en_GB")}},
      {QStringLiteral("en_US"),
       QStringLiteral("English (United States)"),
       {QStringLiteral("en_US"), QStringLiteral("en")}},
      {QStringLiteral("en_GB"),
       QStringLiteral("English (United Kingdom)"),
       {QStringLiteral("en_GB"), QStringLiteral("en")}},
      {QStringLiteral("es"),
       QStringLiteral("Spanish"),
       {QStringLiteral("es"), QStringLiteral("es_ES"),
        QStringLiteral("es_MX")}},
      {QStringLiteral("fr"),
       QStringLiteral("French"),
       {QStringLiteral("fr"), QStringLiteral("fr_FR")}},
      {QStringLiteral("de"),
       QStringLiteral("German"),
       {QStringLiteral("de"), QStringLiteral("de_DE")}},
      {QStringLiteral("pt"),
       QStringLiteral("Portuguese"),
       {QStringLiteral("pt"), QStringLiteral("pt_PT"),
        QStringLiteral("pt_BR")}},
      {QStringLiteral("pt_BR"),
       QStringLiteral("Portuguese (Brazil)"),
       {QStringLiteral("pt_BR"), QStringLiteral("pt")}},
      {QStringLiteral("it"),
       QStringLiteral("Italian"),
       {QStringLiteral("it"), QStringLiteral("it_IT")}},
      {QStringLiteral("nl"),
       QStringLiteral("Dutch"),
       {QStringLiteral("nl"), QStringLiteral("nl_NL")}},
      {QStringLiteral("pl"),
       QStringLiteral("Polish"),
       {QStringLiteral("pl"), QStringLiteral("pl_PL")}},
      {QStringLiteral("ru"),
       QStringLiteral("Russian"),
       {QStringLiteral("ru"), QStringLiteral("ru_RU")}},
      {QStringLiteral("uk"),
       QStringLiteral("Ukrainian"),
       {QStringLiteral("uk"), QStringLiteral("uk_UA")}},
      {QStringLiteral("sv"),
       QStringLiteral("Swedish"),
       {QStringLiteral("sv"), QStringLiteral("sv_SE")}},
      {QStringLiteral("nb"),
       QStringLiteral("Norwegian Bokmal"),
       {QStringLiteral("nb"), QStringLiteral("nb_NO")}},
      {QStringLiteral("da"),
       QStringLiteral("Danish"),
       {QStringLiteral("da"), QStringLiteral("da_DK")}},
      {QStringLiteral("fi"),
       QStringLiteral("Finnish"),
       {QStringLiteral("fi"), QStringLiteral("fi_FI")}},
      {QStringLiteral("tr"),
       QStringLiteral("Turkish"),
       {QStringLiteral("tr"), QStringLiteral("tr_TR")}},
  };
}

void addUniquePath(QStringList &paths, const QString &path) {
  if (path.isEmpty()) {
    return;
  }

  const QString clean = QDir::cleanPath(path);
  if (!paths.contains(clean)) {
    paths.append(clean);
  }
}

SpellcheckLanguage languageFromSpec(const LanguageSpec &spec,
                                    const QStringList &searchPaths) {
  SpellcheckLanguage language;
  language.code = spec.code;
  language.label = spec.label;

  for (const QString &dictionaryCode : spec.dictionaryCodes) {
    for (const QString &path : searchPaths) {
      const QDir dir(path);
      const QString affPath =
          dir.filePath(dictionaryCode + QStringLiteral(".aff"));
      const QString dicPath =
          dir.filePath(dictionaryCode + QStringLiteral(".dic"));
      if (QFileInfo::exists(affPath) && QFileInfo::exists(dicPath)) {
        language.dictionaryCode = dictionaryCode;
        language.affPath = affPath;
        language.dicPath = dicPath;
        return language;
      }
    }
  }

  return language;
}

} // namespace

bool SpellcheckLanguage::dictionaryAvailable() const {
  return !dictionaryCode.isEmpty() && !affPath.isEmpty() && !dicPath.isEmpty();
}

QString SpellcheckLanguage::displayLabel() const {
  if (dictionaryAvailable()) {
    return label;
  }
  return QStringLiteral("%1 [dictionary missing]").arg(label);
}

QStringList defaultDictionarySearchPaths() {
  QStringList paths;

  const QString appDir = QCoreApplication::applicationDirPath();
  addUniquePath(paths, QDir(appDir).filePath(QStringLiteral("dictionaries")));
  addUniquePath(paths,
                QDir(appDir).filePath(QStringLiteral("assets/dictionaries")));
  addUniquePath(paths, QStringLiteral("/usr/share/hunspell"));
  addUniquePath(paths, QStringLiteral("/usr/share/myspell/dicts"));

  return paths;
}

QList<SpellcheckLanguage> spellcheckLanguages() {
  return spellcheckLanguages(defaultDictionarySearchPaths());
}

QList<SpellcheckLanguage>
spellcheckLanguages(const QStringList &dictionarySearchPaths) {
  QList<SpellcheckLanguage> languages;
  const QList<LanguageSpec> specs = languageSpecs();
  languages.reserve(specs.size());
  for (const LanguageSpec &spec : specs) {
    languages.append(languageFromSpec(spec, dictionarySearchPaths));
  }
  return languages;
}

} // namespace maxchat::spell
