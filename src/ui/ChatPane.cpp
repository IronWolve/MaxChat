#include "ui/ChatPane.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

#include <QColor>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QImage>
#include <QLatin1Char>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <QVariant>
#include <QWidget>

namespace maxchat::ui {

namespace {

// The chat QTextBrowser subclass: draws the draggable nick-column separator
// guide and (optionally) strips colour runs on copy. Moved verbatim out of
// MainWindow.cpp (render-pipeline refactor R1) — an implementation detail of
// ChatPane, so it stays in this TU's anonymous namespace.
class ChatTextView final : public QTextBrowser {
  public:
    using SeparatorMovedHandler = std::function<void(int)>;

    explicit ChatTextView(QWidget* parent = nullptr) : QTextBrowser(parent) {}

    void setSeparatorMovedHandler(SeparatorMovedHandler handler) {
        separatorMovedHandler_ = std::move(handler);
    }

    // When true, copying yields plain text only (no rich-text colour runs),
    // matching the Python "strip colors on copy" option.
    void setStripColorsOnCopy(bool strip) { stripColorsOnCopy_ = strip; }

    void setSeparatorGuide(double timestampColumns, int nickWidth, bool visible, QColor color,
                           double pixelX = -1.0) {
        timestampColumns_ = std::max(0.0, timestampColumns);
        nickWidth_ = std::clamp(nickWidth, 4, 40);
        separatorColumns_ =
            visible ? timestampColumns_ + static_cast<double>(nickWidth_) + 1.0 : 0.0;
        // Exact pixel position measured from the real rendered prefix: the
        // space-width × columns estimate drifts with proportional fonts and
        // digit widths, putting the line inside the nick column.
        separatorPixelX_ = visible ? pixelX : -1.0;
        color.setAlpha(130);
        separatorColor_ = color;
        viewport()->update();
    }

  protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && nearSeparator(event->position().x())) {
            draggingSeparator_ = true;
            event->accept();
            return;
        }
        QTextBrowser::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (draggingSeparator_) {
            const double spaceWidth =
                std::max(1.0, QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')));
            const double columns =
                (event->position().x() - document()->documentMargin()) / spaceWidth;
            separatorColumns_ = std::max(timestampColumns_ + 5.0, columns);
            viewport()->update();
            event->accept();
            return;
        }
        if (nearSeparator(event->position().x())) {
            viewport()->setCursor(Qt::SplitHCursor);
            event->accept();
            return;
        }
        viewport()->setCursor(Qt::IBeamCursor);
        QTextBrowser::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (draggingSeparator_) {
            draggingSeparator_ = false;
            const int nextWidth = std::clamp(
                static_cast<int>(std::lround(separatorColumns_ - timestampColumns_ - 1.0)), 4, 40);
            if (separatorMovedHandler_) {
                separatorMovedHandler_(nextWidth);
            }
            event->accept();
            return;
        }
        QTextBrowser::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        if (!draggingSeparator_) {
            viewport()->unsetCursor();
        }
        QTextBrowser::leaveEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        QTextBrowser::paintEvent(event);
        if (separatorColumns_ <= 0.0) {
            return;
        }
        QPainter painter(viewport());
        painter.setPen(separatorColor_);
        const int x = static_cast<int>(std::lround(separatorX()));
        painter.drawLine(x, 0, x, viewport()->height());
    }

    QMimeData* createMimeDataFromSelection() const override {
        QMimeData* mime = QTextBrowser::createMimeDataFromSelection();
        if (stripColorsOnCopy_ && mime != nullptr) {
            // Drop the HTML/colour payload; keep the plain text only. The
            // aligned-nick padding is &nbsp; (U+00A0) — normalise to plain
            // spaces or pastes into terminals/IRC carry invisible junk.
            QString plain = mime->text();
            plain.replace(QChar(QChar::Nbsp), QLatin1Char(' '));
            mime->clear();
            mime->setText(plain);
        }
        return mime;
    }

  private:
    [[nodiscard]] double separatorX() const {
        if (separatorPixelX_ > 0.0) {
            return document()->documentMargin() + separatorPixelX_;
        }
        return document()->documentMargin() +
               QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')) * separatorColumns_;
    }

    [[nodiscard]] bool nearSeparator(double x) const {
        return separatorColumns_ > 0.0 && std::abs(x - separatorX()) <= 5.0;
    }

    bool stripColorsOnCopy_ = true;
    SeparatorMovedHandler separatorMovedHandler_;
    double timestampColumns_ = 0.0;
    double separatorColumns_ = 0.0;
    double separatorPixelX_ = -1.0; // measured prefix width; -1 = column estimate
    int nickWidth_ = 16;
    QColor separatorColor_ = QColor(127, 127, 127, 130);
    bool draggingSeparator_ = false;
};

[[nodiscard]] ChatTextView* asChatTextView(QTextBrowser* view) {
    return static_cast<ChatTextView*>(view);
}

} // namespace

ChatPane::ChatPane(QWidget* parent) : QObject(parent) {
    auto* view = new ChatTextView(parent);
    view->setObjectName(QStringLiteral("chatView"));
    view->setReadOnly(true);
    // Anchor clicks route through the delegate (→ MediaController) so image/
    // audio/video links open the inline viewers instead of an external browser.
    view->setOpenExternalLinks(false);
    view->setOpenLinks(false);
    view->setSeparatorMovedHandler([this](const int nickWidth) {
        if (delegate_ != nullptr) {
            delegate_->chatSeparatorMoved(nickWidth);
        }
    });
    connect(view, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (delegate_ != nullptr) {
            delegate_->chatAnchorClicked(url);
        }
    });
    view_ = view;
}

void ChatPane::clear() {
    if (view_ != nullptr) {
        view_->clear();
    }
}

void ChatPane::setStripColorsOnCopy(bool strip) {
    asChatTextView(view_)->setStripColorsOnCopy(strip);
}

void ChatPane::setSeparatorGuide(double timestampColumns, int nickWidth, bool visible, QColor color,
                                 double pixelX) {
    asChatTextView(view_)->setSeparatorGuide(timestampColumns, nickWidth, visible, color, pixelX);
}

void ChatPane::addImageResource(const QUrl& url, const QImage& image) {
    if (view_ == nullptr) {
        return;
    }
    view_->document()->addResource(QTextDocument::ImageResource, url, QVariant(image));
}

void ChatPane::appendPlain(const QString& line) {
    if (view_ == nullptr) {
        return;
    }
    QTextCursor cursor = view_->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat blockFormat;
    if (!view_->document()->isEmpty()) {
        cursor.insertBlock(blockFormat);
    } else {
        cursor.setBlockFormat(blockFormat);
    }
    cursor.insertText(line);
    view_->setTextCursor(cursor);
    view_->ensureCursorVisible();
}

void ChatPane::appendHtml(const QString& html) {
    if (view_ == nullptr) {
        return;
    }
    QTextCursor cursor = view_->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat blockFormat;
    if (!view_->document()->isEmpty()) {
        cursor.insertBlock(blockFormat);
    } else {
        cursor.setBlockFormat(blockFormat);
    }
    cursor.insertHtml(html);
    view_->setTextCursor(cursor);
    view_->ensureCursorVisible();
}

void ChatPane::appendFormatted(const maxchat::core::FormattedChatLine& line, bool indentWrap) {
    if (view_ == nullptr) {
        return;
    }
    QTextCursor cursor = view_->textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextBlockFormat blockFormat;
    if (indentWrap && line.hangingIndent && !line.prefixPlain.isEmpty()) {
        const int indent = QFontMetrics(view_->font()).horizontalAdvance(line.prefixPlain);
        if (indent > 0) {
            blockFormat.setLeftMargin(indent);
            blockFormat.setTextIndent(-indent);
        }
    }

    if (!view_->document()->isEmpty()) {
        cursor.insertBlock(blockFormat);
    } else {
        cursor.setBlockFormat(blockFormat);
    }
    cursor.insertHtml(line.html);
    cursor.mergeBlockFormat(blockFormat);
    view_->setTextCursor(cursor);
    view_->ensureCursorVisible();
}

void ChatPane::appendPreviewHtml(const QString& html, const QString& prefixPlain) {
    if (view_ == nullptr) {
        return;
    }
    QTextCursor cursor = view_->textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextBlockFormat blockFormat;
    if (!prefixPlain.isEmpty()) {
        const int indent = QFontMetrics(view_->font()).horizontalAdvance(prefixPlain);
        if (indent > 0) {
            blockFormat.setLeftMargin(indent);
        }
    }

    if (!view_->document()->isEmpty()) {
        cursor.insertBlock(blockFormat);
    } else {
        cursor.setBlockFormat(blockFormat);
    }

    // insertHtml with block-level elements (<div>, <p>) creates multiple
    // QTextBlocks. The blockFormat set above only applies to the first one;
    // subsequent blocks get default formatting (leftMargin=0), so the card
    // text renders at the left edge instead of aligned with chat text.
    // Fix: walk every block that insertHtml created and apply the format.
    const int insertStart = cursor.position();
    cursor.insertHtml(html);
    const int insertEnd = cursor.position();

    if (blockFormat.leftMargin() > 0.0) {
        QTextDocument* doc = view_->document();
        QTextBlock block = doc->findBlock(insertStart);
        while (block.isValid() && block.position() <= insertEnd) {
            QTextCursor bc(block);
            bc.mergeBlockFormat(blockFormat);
            block = block.next();
        }
    }

    view_->setTextCursor(cursor);
    view_->ensureCursorVisible();
}

void ChatPane::appendCenteredDivider(const QString& text, const QString& color,
                                     const QString& prefixPlain) {
    if (view_ == nullptr) {
        return;
    }
    QTextCursor cursor = view_->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat blockFormat; // fresh format: centered, no inherited margins
    blockFormat.setAlignment(Qt::AlignHCenter);
    blockFormat.setTopMargin(8);
    blockFormat.setBottomMargin(6);
    // With aligned nicks, content lives right of the nick column / separator
    // bar. Centre the divider within THAT area — a full-width divider crosses
    // the bar and visually collides with the nick column.
    if (!prefixPlain.isEmpty()) {
        const int indent = QFontMetrics(view_->font()).horizontalAdvance(prefixPlain);
        if (indent > 0) {
            blockFormat.setLeftMargin(indent);
        }
    }
    if (!view_->document()->isEmpty()) {
        cursor.insertBlock(blockFormat);
    } else {
        cursor.setBlockFormat(blockFormat);
    }
    QTextCharFormat charFormat;
    charFormat.setForeground(QColor(color));
    cursor.setCharFormat(charFormat);
    cursor.insertText(text);
    view_->setTextCursor(cursor);
}

} // namespace maxchat::ui
