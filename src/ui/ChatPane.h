#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>

#include "core/ChatBufferStore.h"  // maxchat::core::ChatBufferSnapshot
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
    // ChatPane owns the preview image cache (R3) but not the network fetch: it
    // asks the owner to fetch a missing image (owner runs it through ImageFetcher
    // and reports back via onPreviewImageReady/onPreviewImageFailed).
    virtual void previewImageNeeded(const QUrl& url) = 0;
    // Model query: does the active buffer reference this image URL? (Skip the
    // re-render if a now-arrived image isn't even on screen.) The buffer model
    // lives in MainWindow.
    [[nodiscard]] virtual bool activeBufferReferencesImage(const QString& url) = 0;
    // Rebuild the active buffer's document (owner calls back into showBuffer).
    // ChatPane wraps this with scroll preservation when an image lands.
    virtual void rerenderActiveBuffer() = 0;
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

    // Resolved inputs for rendering one buffer's whole line model into the view
    // (render-pipeline R2). MainWindow still owns the model + theme decisions and
    // hands these down; ChatPane does the unread-marker / dim-replay / per-line
    // formatting loop.
    struct BufferRenderOptions {
        maxchat::core::ChatLineFormatOptions baseOptions; // = chatLineFormatOptions()
        QString timestampFormat;   // resolved Qt format for per-line (replayed) timestamps
        bool markerLine = true;    // show the unread / replay→live "new" marker
        int markerCount = -1;      // line index of the unread boundary (-1 = none)
        bool indentWrap = true;    // hanging-indent wrapped lines
        bool alignNicks = true;    // indent centred dividers to the content column
    };

    void setDelegate(ChatPaneDelegate* delegate) { delegate_ = delegate; }

    // The chat view widget. MainWindow inserts it into its layout and the
    // find/copy/scroll/render code that still lives there borrows it. (The
    // surface narrows as later R-steps move that code in.)
    [[nodiscard]] QTextBrowser* view() const { return view_; }

    void clear();

    // Clear the view and render a buffer's entire line model (R2). Does not touch
    // the comic view — MainWindow refreshes that after, until R4 folds it in.
    void showBuffer(const maxchat::core::ChatBufferSnapshot& snapshot,
                    const BufferRenderOptions& options);

    // Forwarded to the underlying ChatTextView.
    void setStripColorsOnCopy(bool strip);
    void setSeparatorGuide(double timestampColumns, int nickWidth, bool visible, QColor color,
                           double pixelX);

    // Register an already-decoded preview image as a document resource so a
    // referenced <img src> resolves (QTextBrowser never fetches over the net).
    void addImageResource(const QUrl& url, const QImage& image);

    // A requested preview image arrived (already scaled by the owner) — cache it
    // and, if the active buffer references it, re-render preserving scroll.
    void onPreviewImageReady(const QUrl& url, const QImage& scaledImage);
    // A requested preview image failed — stop retrying it this session.
    void onPreviewImageFailed(const QUrl& url);

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
    // Register cached <img src> images into the document (before insert); request
    // the rest from the owner (after insert). Owned preview cache (R3).
    void registerCachedImages(const QString& html);
    void requestPreviewImages(const QString& html);

    QTextBrowser* view_ = nullptr;        // a ChatTextView (private impl detail)
    ChatPaneDelegate* delegate_ = nullptr;
    QHash<QString, QImage> previewImageCache_; // url -> decoded, scaled image
    QSet<QString> previewImagePending_;        // in-flight image fetches
    QSet<QString> previewImageFailed_;         // gave up — don't retry this session
};

} // namespace maxchat::ui
