#pragma once

#include <QString>
#include <QStringList>

#include <memory>

class Hunspell;

namespace maxchat::spell {

class HunspellSpellchecker final {
public:
  HunspellSpellchecker();
  ~HunspellSpellchecker();

  HunspellSpellchecker(const HunspellSpellchecker &) = delete;
  HunspellSpellchecker &operator=(const HunspellSpellchecker &) = delete;
  HunspellSpellchecker(HunspellSpellchecker &&) noexcept;
  HunspellSpellchecker &operator=(HunspellSpellchecker &&) noexcept;

  [[nodiscard]] bool loadDictionary(const QString &affPath,
                                    const QString &dicPath);
  void unload();

  [[nodiscard]] bool isLoaded() const;
  [[nodiscard]] QString affPath() const;
  [[nodiscard]] QString dicPath() const;
  [[nodiscard]] bool isCorrect(const QString &word) const;
  [[nodiscard]] QStringList suggestions(const QString &word,
                                        int maxSuggestions = 8) const;

private:
  std::unique_ptr<Hunspell> hunspell_;
  QString affPath_;
  QString dicPath_;
};

} // namespace maxchat::spell
