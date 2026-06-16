#pragma once

#include <QColor>
#include <QObject>
#include <QString>

#include "core/ChatLineFormatter.h" // maxchat::core::FormattedChatLine

class QImage;
class QTextBrowser;
class QUrl;
class QWidget;

namespace maxchat::ui {

// Callbacks from ChatPane back up to its owner (MainWindow). Narrow seam: only
// the user gestures on the chat view that the owner must react to. Grows as the
// render-pipeline refactor (RENDER_PIPELINE_DESIGN.md) moves more in.
class ChatPaneDelegate {
  public:
    virtual ~ChatPaneDelegate() = default;
    virtual void chatAnchorClicked(const QUrl& url) = 0;  // → MediaController
    virtual void chatSeparatorMoved(int nickWidth) = 0;   // → persist nick column
};

// Owns the chat QTextBrowser (a ChatTextView with the separator guide +
// strip-colours-on-copy behaviour) and the low-level "append one line/block to
// the document" primitives extracted from MainWindow (render-pipeline refactor
// R1, see RENDER_PIPELINE_DESIGN.md §3).
//
// MainWindow still owns the buffer model and the render orchestration
// (renderActiveBuffer, chatLineFormatOptions, the preview cache); it builds each
// line's format/indent and calls these primitives. Anchor-click and
// separator-drag route back through ChatPaneDelegate.
//
// Note: an R1-stage QObject that owns the view widget rather than the QWidget
// composite the design's end-state describes — deliberately, so the splitter +
// comic view + audio bar stay untouched in MainWindow until R4. MainWindow adds
// view() to its existing splitter exactly where the raw view used to sit, so the
// layout is pixel-identical.
class ChatPane : public QObject {
    Q_OBJECT

  public:
    explicit ChatPane(QWidget* parent = nullptr);

    void setDelegate(ChatPaneDelegate* delegate) { delegate_ = delegate; }

    // The chat view widget. MainWindow inserts it into its layout and the
    // find/copy/scroll/render code that still lives there borrows it. (The
    // surface narrows as later R-steps move that code in.)
    [[nodiscard]] QTextBrowser* view() const { return view_; }

    void clear();

    // Forwarded to the underlying ChatTextView.
    void setStripColorsOnCopy(bool strip);
    void setSeparatorGuide(double timestampColumns, int nickWidth, bool visible, QColor color,
                           double pixelX);

    // Register an already-decoded preview image as a document resource so a
    // referenced <img src> resolves (QTextBrowser never fetches over the net).
    void addImageResource(const QUrl& url, const QImage& image);

    // --- Low-level append primitives (one line / block into the document) ---
    void appendPlain(const QString& line);
    void appendHtml(const QString& html);
    void appendFormatted(const maxchat::core::FormattedChatLine& line, bool indentWrap);
    // prefixPlain: rendered timestamp+nick prefix used to indent wrapped /
    // multi-block preview content (empty = no indent).
    void appendPreviewHtml(const QString& html, const QString& prefixPlain);
    // prefixPlain: when aligning nicks, indent the centred divider to the content
    // column (empty = full-width centre).
    void appendCenteredDivider(const QString& text, const QString& color,
                               const QString& prefixPlain);

  private:
    QTextBrowser* view_ = nullptr;        // a ChatTextView (private impl detail)
    ChatPaneDelegate* delegate_ = nullptr;
};

} // namespace maxchat::ui
