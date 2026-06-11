#include "ui/SpellcheckHighlighter.h"

#include <QColor>
#include <QRegularExpression>

#include <algorithm>

namespace maxchat::ui {
namespace {

struct TextRange {
  int start = 0;
  int end = 0;
};

bool isWordLetter(const QChar ch) { return ch.isLetter(); }

bool isWordJoiner(const QString &text, const int index) {
  if (index <= 0 || index >= text.size() - 1) {
    return false;
  }
  const QChar ch = text.at(index);
  if (ch != QLatin1Char('\'') && ch != QLatin1Char('-')) {
    return false;
  }
  return isWordLetter(text.at(index - 1)) && isWordLetter(text.at(index + 1));
}

bool shouldSkipByPrefix(const QString &text, const int start) {
  if (start <= 0) {
    return false;
  }
  const QChar prefix = text.at(start - 1);
  return prefix == QLatin1Char('#') || prefix == QLatin1Char('@') ||
         prefix == QLatin1Char('+') || prefix == QLatin1Char('%') ||
         prefix == QLatin1Char('&') || prefix == QLatin1Char('/');
}

int tokenEnd(const QString &text, int index) {
  while (index < text.size() && !text.at(index).isSpace()) {
    ++index;
  }
  return index;
}

int nextTokenStart(const QString &text, int index) {
  while (index < text.size() && text.at(index).isSpace()) {
    ++index;
  }
  return index;
}

int skipTokens(const QString &text, int index, const int count) {
  for (int skipped = 0; skipped < count && index < text.size(); ++skipped) {
    index = tokenEnd(text, nextTokenStart(text, index));
  }
  return nextTokenStart(text, index);
}

int commandScanStart(const QString &text) {
  int start = nextTokenStart(text, 0);
  if (start >= text.size() || text.at(start) != QLatin1Char('/')) {
    return 0;
  }

  const int commandEnd = tokenEnd(text, start);
  const QString command = text.mid(start + 1, commandEnd - start - 1).toLower();
  if (command == QStringLiteral("me") || command == QStringLiteral("away")) {
    return nextTokenStart(text, commandEnd);
  }
  if (command == QStringLiteral("msg") || command == QStringLiteral("query") ||
      command == QStringLiteral("notice") ||
      command == QStringLiteral("topic") || command == QStringLiteral("part")) {
    return skipTokens(text, commandEnd, 1);
  }
  if (command == QStringLiteral("kick")) {
    return skipTokens(text, commandEnd, 2);
  }
  return text.size();
}

QList<TextRange> urlRanges(const QString &text) {
  static const QRegularExpression urlPattern(
      QStringLiteral(R"(\b(?:https?://|www\.)\S+)"),
      QRegularExpression::CaseInsensitiveOption);

  QList<TextRange> ranges;
  QRegularExpressionMatchIterator matches = urlPattern.globalMatch(text);
  while (matches.hasNext()) {
    const QRegularExpressionMatch match = matches.next();
    const int start = static_cast<int>(match.capturedStart());
    const int end =
        static_cast<int>(match.capturedStart() + match.capturedLength());
    ranges.append({start, end});
  }
  return ranges;
}

bool intersectsAnyRange(const int start, const int end,
                        const QList<TextRange> &ranges) {
  return std::any_of(ranges.cbegin(), ranges.cend(),
                     [start, end](const TextRange &range) {
                       return start < range.end && end > range.start;
                     });
}

bool shouldCheckWord(const QString &text, const int start, const int end,
                     const QList<TextRange> &urls) {
  if (end - start <= 1) {
    return false;
  }
  if (start < commandScanStart(text)) {
    return false;
  }
  if (shouldSkipByPrefix(text, start)) {
    return false;
  }
  return !intersectsAnyRange(start, end, urls);
}

} // namespace

QList<SpellcheckRange>
misspelledWordRanges(const QString &text,
                     const SpellcheckWordChecker &wordChecker) {
  if (!wordChecker) {
    return {};
  }

  QList<SpellcheckRange> ranges;
  const QList<TextRange> urls = urlRanges(text);
  int index = 0;
  while (index < text.size()) {
    if (!isWordLetter(text.at(index))) {
      ++index;
      continue;
    }

    const int start = index;
    while (index < text.size() &&
           (isWordLetter(text.at(index)) || isWordJoiner(text, index))) {
      ++index;
    }
    const int end = index;
    if (!shouldCheckWord(text, start, end, urls)) {
      continue;
    }

    const QString word = text.mid(start, end - start);
    if (!wordChecker(word)) {
      ranges.append({start, end - start, word});
    }
  }
  return ranges;
}

SpellcheckHighlighter::SpellcheckHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {
  // Explicit red wavy underline. SpellCheckUnderline delegates to the platform
  // style, which renders inconsistently (sometimes a box / dotted line); a plain
  // WaveUnderline is reliably the red squiggle people expect.
  misspelledFormat_.setUnderlineStyle(QTextCharFormat::WaveUnderline);
  misspelledFormat_.setUnderlineColor(QColor(0xE2, 0x4B, 0x4A));
}

void SpellcheckHighlighter::setSpellcheckEnabled(const bool enabled) {
  if (enabled_ == enabled) {
    return;
  }
  enabled_ = enabled;
  rehighlight();
}

void SpellcheckHighlighter::setWordChecker(SpellcheckWordChecker checker) {
  wordChecker_ = std::move(checker);
  rehighlight();
}

void SpellcheckHighlighter::highlightBlock(const QString &text) {
  if (!enabled_ || !wordChecker_) {
    return;
  }

  for (const SpellcheckRange &range :
       misspelledWordRanges(text, wordChecker_)) {
    setFormat(range.start, range.length, misspelledFormat_);
  }
}

} // namespace maxchat::ui
