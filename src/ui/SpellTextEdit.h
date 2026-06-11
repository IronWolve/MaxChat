#pragma once

#include <QColor>
#include <QTextEdit>

#include "ui/SpellcheckHighlighter.h" // SpellcheckWordChecker

class QMimeData;
class QPaintEvent;

namespace maxchat::ui {

// A QTextEdit that draws its own misspelling underline. Qt's WaveUnderline text
// format can't be made thicker (the layout draws it from font metrics), so the
// squiggle is painted by hand on top of the text — a heavier, easier-to-see
// red wave than the built-in one.
//
// Also intercepts image paste (Ctrl+V with an image on the clipboard) — since
// acceptRichText is false, pasted images would silently be dropped. Instead
// imageReceived() is emitted so the caller can upload and insert a URL.
class SpellTextEdit final : public QTextEdit {
  Q_OBJECT

public:
  explicit SpellTextEdit(QWidget *parent = nullptr);

  void setSpellcheckEnabled(bool enabled);
  void setWordChecker(SpellcheckWordChecker checker);
  void setMisspelledColor(const QColor &color);

signals:
  void imageReceived(const QImage &image);

protected:
  void paintEvent(QPaintEvent *event) override;
  void insertFromMimeData(const QMimeData *source) override;

private:
  bool spellEnabled_ = false;
  SpellcheckWordChecker wordChecker_;
  QColor misspelledColor_{0xE2, 0x4B, 0x4A};
};

} // namespace maxchat::ui
