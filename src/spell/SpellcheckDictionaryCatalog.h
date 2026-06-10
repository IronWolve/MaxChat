#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace maxchat::spell {

struct SpellcheckLanguage {
  QString code;
  QString label;
  QString dictionaryCode;
  QString affPath;
  QString dicPath;

  [[nodiscard]] bool dictionaryAvailable() const;
  [[nodiscard]] QString displayLabel() const;
};

[[nodiscard]] QStringList defaultDictionarySearchPaths();
[[nodiscard]] QList<SpellcheckLanguage> spellcheckLanguages();
[[nodiscard]] QList<SpellcheckLanguage>
spellcheckLanguages(const QStringList &dictionarySearchPaths);

} // namespace maxchat::spell
