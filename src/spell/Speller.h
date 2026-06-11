#pragma once

#include <QString>
#include <QStringList>

#include <memory>

namespace maxchat::spell {

// Backend-neutral spellchecker the UI talks to (internal Hunspell or the native
// OS engine). The highlighter's word-check and the right-click suggestions go
// through this, so swapping engines doesn't touch the UI.
class Speller {
public:
  virtual ~Speller() = default;

  [[nodiscard]] virtual bool isLoaded() const = 0;
  [[nodiscard]] virtual bool isCorrect(const QString &word) const = 0;
  [[nodiscard]] virtual QStringList suggestions(const QString &word,
                                                int maxSuggestions = 8) const = 0;
};

// Build the native OS speller for a BCP-47-ish language code (e.g. "en" or
// "en_US"). Returns nullptr when no native engine is compiled in for this
// platform (then the caller falls back to the internal engine). The real
// implementation lives in a platform TU compiled only with MAXCHAT_OS_SPELL;
// otherwise a stub in SpellerFactory.cpp returns nullptr.
[[nodiscard]] std::unique_ptr<Speller> createOsSpeller(const QString &languageCode);

} // namespace maxchat::spell
