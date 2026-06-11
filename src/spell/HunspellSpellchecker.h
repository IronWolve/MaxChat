#pragma once

#include "spell/Speller.h"

#include <QString>
#include <QStringList>

#include <memory>

class Hunspell;

namespace maxchat::spell {

class HunspellSpellchecker final : public Speller {
public:
  HunspellSpellchecker();
  ~HunspellSpellchecker() override;

  HunspellSpellchecker(const HunspellSpellchecker &) = delete;
  HunspellSpellchecker &operator=(const HunspellSpellchecker &) = delete;
  HunspellSpellchecker(HunspellSpellchecker &&) noexcept;
  HunspellSpellchecker &operator=(HunspellSpellchecker &&) noexcept;

  [[nodiscard]] bool loadDictionary(const QString &affPath,
                                    const QString &dicPath);
  void unload();

  [[nodiscard]] bool isLoaded() const override;
  [[nodiscard]] QString affPath() const;
  [[nodiscard]] QString dicPath() const;
  [[nodiscard]] bool isCorrect(const QString &word) const override;
  [[nodiscard]] QStringList suggestions(const QString &word,
                                        int maxSuggestions = 8) const override;
  bool addWord(const QString &word) override;

private:
  std::unique_ptr<Hunspell> hunspell_;
  QString affPath_;
  QString dicPath_;
};

} // namespace maxchat::spell
