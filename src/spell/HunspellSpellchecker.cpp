#include "spell/HunspellSpellchecker.h"

#include <QByteArray>
#include <QFileInfo>

#include <hunspell.hxx>

#include <string>
#include <vector>

namespace maxchat::spell {
namespace {

std::string toUtf8String(const QString &text) {
  const QByteArray utf8 = text.toUtf8();
  return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

QString normalizedWord(QString word) {
  word = word.trimmed();
  while (!word.isEmpty() && word.front().isPunct() &&
         word.front() != QLatin1Char('\'')) {
    word.remove(0, 1);
  }
  while (!word.isEmpty() && word.back().isPunct() &&
         word.back() != QLatin1Char('\'')) {
    word.chop(1);
  }
  return word;
}

} // namespace

HunspellSpellchecker::HunspellSpellchecker() = default;

HunspellSpellchecker::~HunspellSpellchecker() = default;

HunspellSpellchecker::HunspellSpellchecker(HunspellSpellchecker &&) noexcept =
    default;

HunspellSpellchecker &
HunspellSpellchecker::operator=(HunspellSpellchecker &&) noexcept = default;

bool HunspellSpellchecker::loadDictionary(const QString &affPath,
                                          const QString &dicPath) {
  const QString cleanAffPath = QFileInfo(affPath).absoluteFilePath();
  const QString cleanDicPath = QFileInfo(dicPath).absoluteFilePath();
  if (!QFileInfo::exists(cleanAffPath) || !QFileInfo::exists(cleanDicPath)) {
    unload();
    return false;
  }

  hunspell_ = std::make_unique<Hunspell>(toUtf8String(cleanAffPath).c_str(),
                                         toUtf8String(cleanDicPath).c_str());
  affPath_ = cleanAffPath;
  dicPath_ = cleanDicPath;
  return true;
}

void HunspellSpellchecker::unload() {
  hunspell_.reset();
  affPath_.clear();
  dicPath_.clear();
}

bool HunspellSpellchecker::isLoaded() const { return hunspell_ != nullptr; }

QString HunspellSpellchecker::affPath() const { return affPath_; }

QString HunspellSpellchecker::dicPath() const { return dicPath_; }

bool HunspellSpellchecker::isCorrect(const QString &word) const {
  if (hunspell_ == nullptr) {
    return false;
  }
  const QString cleaned = normalizedWord(word);
  if (cleaned.isEmpty()) {
    return true;
  }
  return hunspell_->spell(toUtf8String(cleaned));
}

QStringList HunspellSpellchecker::suggestions(const QString &word,
                                              int maxSuggestions) const {
  if (hunspell_ == nullptr || maxSuggestions <= 0) {
    return {};
  }

  const QString cleaned = normalizedWord(word);
  if (cleaned.isEmpty()) {
    return {};
  }

  QStringList values;
  const std::vector<std::string> rawSuggestions =
      hunspell_->suggest(toUtf8String(cleaned));
  for (const std::string &suggestion : rawSuggestions) {
    values.append(QString::fromUtf8(suggestion.c_str(),
                                    static_cast<qsizetype>(suggestion.size())));
    if (values.size() >= maxSuggestions) {
      break;
    }
  }
  return values;
}

} // namespace maxchat::spell
