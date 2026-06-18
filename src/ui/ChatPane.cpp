#include "ui/ChatPane.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

#include <QColor>
#include <QDateTime>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QImage>
#include <QLatin1Char>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QList>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QStringList>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include "services/LinkPreviewClassifier.h" // maxchat::services::canFetchPreviewUrl

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

// Pull the src URLs out of <img ...> tags in a preview HTML snippet.
[[nodiscard]] QStringList imageSourcesInHtml(const QString& html) {
    static const QRegularExpression imgSrc(
        QStringLiteral(R"RX(<img\b[^>]*\bsrc\s*=\s*"([^"]+)")RX"),
        QRegularExpression::CaseInsensitiveOption);
    QStringList sources;
    auto it = imgSrc.globalMatch(html);
    while (it.hasNext()) {
        const QString src = it.next().captured(1).trimmed();
        if (!src.isEmpty() && !sources.contains(src)) {
            sources.append(src);
        }
    }
    return sources;
}

} // namespace

ChatPane::ChatPane(QWidget* parent) : QWidget(parent) {
    auto* view = new ChatTextView(this);
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

    // Vertical splitter, chat as the (only, for now) bottom pane; the comic pane
    // is inserted above it via setComicWidget. Zero-margin layout so dropping this
    // composite into MainWindow's column is pixel-identical to the old splitter.
    splitter_ = new QSplitter(Qt::Vertical, this);
    splitter_->setObjectName(QStringLiteral("chatSplitter"));
    splitter_->addWidget(view_);
    splitter_->setCollapsible(0, false);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter_);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ChatPane::setComicWidget(QWidget* comic) {
    comicWidget_ = comic;
    if (comic == nullptr || splitter_ == nullptr) {
        return;
    }
    comic->setVisible(false);
    // Comic above chat (classic comic-strip style); chat stays visible beneath.
    splitter_->insertWidget(0, comic);
    splitter_->setCollapsible(0, false);
    splitter_->setCollapsible(1, false);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 1);
}

void ChatPane::setComicVisible(bool visible) {
    if (comicWidget_ != nullptr) {
        comicWidget_->setVisible(visible);
    }
}

void ChatPane::ensureComicSplit() {
    if (splitter_ == nullptr) {
        return;
    }
    const QList<int> sizes = splitter_->sizes();
    const int total = sizes.value(0) + sizes.value(1);
    if (total > 0 && sizes.value(0) <= 0) {
        splitter_->setSizes({total / 2, total - total / 2});
    }
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
    // QTextBrowser never fetches remote <img src> over the network: a referenced
    // image only renders if it's a registered document resource. Add any already
    // cached images now (before the insert), and kick off downloads for the rest
    // (which re-render this buffer on arrival).
    registerCachedImages(html);

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
    requestPreviewImages(html);
}

void ChatPane::registerCachedImages(const QString& html) {
    if (view_ == nullptr) {
        return;
    }
    const QStringList sources = imageSourcesInHtml(html);
    for (const QString& src : sources) {
        const auto cached = previewImageCache_.constFind(src);
        if (cached != previewImageCache_.constEnd()) {
            addImageResource(QUrl(src), cached.value());
        }
    }
}

void ChatPane::requestPreviewImages(const QString& html) {
    const QStringList sources = imageSourcesInHtml(html);
    for (const QString& src : sources) {
        if (previewImageCache_.contains(src) || previewImagePending_.contains(src) ||
            previewImageFailed_.contains(src)) {
            continue;
        }
        const QUrl url(src);
        if (!maxchat::services::canFetchPreviewUrl(url)) {
            continue;
        }
        previewImagePending_.insert(src);
        if (delegate_ != nullptr) {
            delegate_->previewImageNeeded(url); // owner runs the actual fetch
        }
    }
}

void ChatPane::onPreviewImageReady(const QUrl& url, const QImage& scaledImage) {
    const QString key = url.toString();
    previewImagePending_.remove(key);
    if (scaledImage.isNull()) {
        return;
    }
    if (previewImageCache_.size() > 64) {
        // Decoded images are big; cheap full flush beats LRU bookkeeping.
        // Evicted images simply re-fetch if their line scrolls back into view.
        previewImageCache_.clear();
    }
    previewImageCache_.insert(key, scaledImage);
    // The image landed after the line was already laid out with a broken <img>.
    // Re-render the active buffer (preserving scroll position) so it now shows.
    if (view_ != nullptr && delegate_ != nullptr && delegate_->activeBufferReferencesImage(key)) {
        QScrollBar* bar = view_->verticalScrollBar();
        const bool atBottom = bar != nullptr && bar->value() >= bar->maximum() - 4;
        const int previous = bar != nullptr ? bar->value() : 0;
        delegate_->rerenderActiveBuffer();
        if (bar != nullptr) {
            bar->setValue(atBottom ? bar->maximum() : qMin(previous, bar->maximum()));
        }
    }
}

void ChatPane::onPreviewImageFailed(const QUrl& url) {
    const QString key = url.toString();
    previewImagePending_.remove(key);
    if (previewImageFailed_.size() > 512) {
        previewImageFailed_.clear(); // add-only set; cap it for week-long sessions
    }
    previewImageFailed_.insert(key); // don't hammer a 404/blocked URL on every render
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

void ChatPane::showBuffer(const maxchat::core::ChatBufferSnapshot& snapshot,
                          const BufferRenderOptions& options) {
    if (view_ == nullptr) {
        return;
    }
    clear();

    const maxchat::core::ChatLineFormatOptions& baseOptions = options.baseOptions;
    // The `──── new ────` marker sits before the first line that arrived since we
    // last left this buffer; markerCount is that line's index (-1 = none).
    const qsizetype markerIndex = options.markerLine && options.markerCount > 0 &&
                                          options.markerCount < snapshot.lines.size()
                                      ? options.markerCount
                                      : -1;
    // Dimmed palette for replayed history (matches seedReplayForBuffer): whole
    // line in the timestamp grey, no nick colours, formatting stripped.
    const QString dim = baseOptions.timestampColor.isEmpty() ? QStringLiteral("#8a8a8a")
                                                             : baseOptions.timestampColor;
    maxchat::core::ChatLineFormatOptions dimOptions = baseOptions;
    dimOptions.defaultForeground = dim;
    dimOptions.systemColor = dim;
    dimOptions.bracketColor = dim;
    dimOptions.bodyColor = dim; // actually dims the message text, not just trim
    // Nick colors stay ON in replayed history (inherited from baseOptions): the
    // user reads nick identity by color, and history nicks must match the member
    // list / live lines. The dim text + dividers still mark it as replay.
    dimOptions.renderFormatting = false;

    // The empty-line timestamp+nick prefix. Preview cards always indent to it;
    // centred dividers do so only when aligning nicks (else they full-width
    // centre, which would cross the nick gutter / separator bar).
    const QString contentPrefix =
        maxchat::core::formatChatLine(QString(), baseOptions).prefixPlain;
    const QString alignPrefix = options.alignNicks ? contentPrefix : QString();
    static const QString kUnreadMarker = QStringLiteral("──────────  new  ──────────");

    // Structural replay→live boundary: "new" always appears right after the
    // "Chat ended" divider, even when an explicit unread marker exists further
    // down. The structural path fires at the first live line after any dimmed
    // content; the explicit unread marker is suppressed once replay has been seen.
    qsizetype lineIndex = 0;
    bool seenDimmed = false;
    bool replayMarkerInserted = false;
    for (const maxchat::core::ChatBufferLine& line : snapshot.lines) {
        // Explicit unread marker — only when there is no replay content ahead
        // of it; once we've passed dimmed lines the structural marker takes over.
        if (lineIndex++ == markerIndex && !seenDimmed) {
            appendCenteredDivider(kUnreadMarker, dim, alignPrefix);
            replayMarkerInserted = true;
        }
        // Structural replay→live boundary: first live line after dimmed content.
        if (options.markerLine && !line.dimmed && seenDimmed && !replayMarkerInserted) {
            appendCenteredDivider(kUnreadMarker, dim, alignPrefix);
            replayMarkerInserted = true;
        }
        if (line.dimmed) {
            seenDimmed = true;
        }
        if (line.dimmed && line.systemLine && !line.sourceText.isEmpty()) {
            // The "Chat ended" resume rule is a centered divider (like "new").
            appendCenteredDivider(line.sourceText, dim, alignPrefix);
        } else if (!line.sourceText.isEmpty()) {
            maxchat::core::ChatLineFormatOptions opts = line.dimmed ? dimOptions : baseOptions;
            opts.systemLine = line.systemLine;
            // Replayed lines keep their original time; live lines use the default
            // (current) timestamp already baked into baseOptions. A dimmed line
            // with no timestamp (the "Chat ended" divider, whose date+time is in
            // its text) must NOT fall back to the current time.
            if (line.timestamp.isValid()) {
                // Stored as UTC (ChatBufferStore) — format in LOCAL time or every
                // re-render shifts all gutters by the UTC offset.
                opts.timestamp = line.timestamp.toLocalTime().toString(options.timestampFormat);
            } else if (line.dimmed) {
                opts.timestamp.clear();
            }
            const maxchat::core::FormattedChatLine display =
                maxchat::core::formatChatLine(line.sourceText, opts);
            appendFormatted(display, options.indentWrap);
        } else if (!line.htmlText.isEmpty()) {
            // Preview/OG card: ChatPane owns the image cache + fetch request (R3).
            appendPreviewHtml(line.htmlText, contentPrefix);
        } else if (!line.plainText.isEmpty()) {
            appendPlain(line.plainText);
        }
    }
    // NB: no refreshComic() here — MainWindow drives the comic view until R4.
}

} // namespace maxchat::ui
