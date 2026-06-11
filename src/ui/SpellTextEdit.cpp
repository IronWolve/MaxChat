#include "ui/SpellTextEdit.h"

#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>
#include <utility>

namespace maxchat::ui {

SpellTextEdit::SpellTextEdit(QWidget *parent) : QTextEdit(parent) {}

void SpellTextEdit::setSpellcheckEnabled(const bool enabled) {
  if (spellEnabled_ == enabled) {
    return;
  }
  spellEnabled_ = enabled;
  viewport()->update();
}

void SpellTextEdit::setWordChecker(SpellcheckWordChecker checker) {
  wordChecker_ = std::move(checker);
  viewport()->update();
}

void SpellTextEdit::setMisspelledColor(const QColor &color) {
  if (color.isValid()) {
    misspelledColor_ = color;
    viewport()->update();
  }
}

void SpellTextEdit::paintEvent(QPaintEvent *event) {
  QTextEdit::paintEvent(event);
  if (!spellEnabled_ || !wordChecker_) {
    return;
  }

  QPainter painter(viewport());
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPen pen(misspelledColor_);
  pen.setWidthF(1.8);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);

  QTextDocument *doc = document();
  for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
    const QString text = block.text();
    if (text.isEmpty()) {
      continue;
    }
    for (const SpellcheckRange &range : misspelledWordRanges(text, wordChecker_)) {
      QTextCursor startCursor(doc);
      startCursor.setPosition(block.position() + range.start);
      QTextCursor endCursor = startCursor;
      endCursor.setPosition(block.position() + range.start + range.length);
      const QRect startRect = cursorRect(startCursor);
      const QRect endRect = cursorRect(endCursor);
      // Single visual line only (the input is NoWrap); skip anything that spans.
      if (startRect.top() != endRect.top()) {
        continue;
      }
      const qreal left = startRect.left();
      const qreal right = endRect.left();
      if (right <= left) {
        continue;
      }
      const qreal baseY = startRect.bottom() - 1.0;
      const qreal amplitude = 2.0;
      const qreal step = 2.5;
      QPainterPath wave;
      wave.moveTo(left, baseY);
      bool up = true;
      for (qreal x = left; x < right; x += step) {
        const qreal nextX = std::min(x + step, right);
        wave.lineTo(nextX, up ? baseY - amplitude : baseY);
        up = !up;
      }
      painter.drawPath(wave);
    }
  }
}

void SpellTextEdit::insertFromMimeData(const QMimeData *source) {
    if (source != nullptr && source->hasImage()) {
        const QImage img = qvariant_cast<QImage>(source->imageData());
        if (!img.isNull()) {
            emit imageReceived(img);
            return; // don't let QTextEdit try to paste the raw image bytes
        }
    }
    QTextEdit::insertFromMimeData(source);
}

} // namespace maxchat::ui
