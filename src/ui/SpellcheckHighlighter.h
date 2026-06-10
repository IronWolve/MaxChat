#pragma once

#include <QList>
#include <QString>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <functional>

class QTextDocument;

namespace maxchat::ui {

struct SpellcheckRange {
  int start = 0;
  int length = 0;
  QString word;
};

using SpellcheckWordChecker = std::function<bool(const QString &)>;

[[nodiscard]] QList<SpellcheckRange>
misspelledWordRanges(const QString &text,
                     const SpellcheckWordChecker &wordChecker);

class SpellcheckHighlighter final : public QSyntaxHighlighter {
  Q_OBJECT

public:
  explicit SpellcheckHighlighter(QTextDocument *document);

  void setSpellcheckEnabled(bool enabled);
  void setWordChecker(SpellcheckWordChecker checker);

protected:
  void highlightBlock(const QString &text) override;

private:
  bool enabled_ = false;
  SpellcheckWordChecker wordChecker_;
  QTextCharFormat misspelledFormat_;
};

} // namespace maxchat::ui
