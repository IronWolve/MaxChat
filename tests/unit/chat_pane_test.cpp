// Regression net for ChatPane — the chat-render component extracted from
// MainWindow (render-pipeline R1-R3). Drives showBuffer() headless and asserts
// the produced QTextDocument, capturing the load-bearing quirks the deferred
// theming work depends on: line ordering, UTC→local timestamps, dim-replay
// palette, the centered "new" / "Chat ended" dividers and their placement, and
// the marker-off path. This is the safety net RENDER_PIPELINE_DESIGN.md §5 R5
// calls for before the dependent controller phases touch rendering.

#include "core/ChatBufferStore.h"
#include "core/ChatLineFormatter.h"
#include "ui/ChatPane.h"

#include <QDateTime>
#include <QString>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimeZone>
#include <QUrl>
#include <QtTest/QtTest>

using maxchat::core::ChatBufferLine;
using maxchat::core::ChatBufferSnapshot;
using maxchat::core::ChatLineFormatOptions;
using maxchat::ui::ChatPane;
using maxchat::ui::ChatPaneDelegate;

namespace {

// Minimal delegate: ChatPane's gestures + preview hooks are inert for these
// render assertions, but the pure virtuals must be satisfied. Counters let a
// test confirm a hook did (or did not) fire.
class FakeDelegate final : public ChatPaneDelegate {
  public:
    void chatAnchorClicked(const QUrl&) override { ++anchorClicks; }
    void chatSeparatorMoved(int) override { ++separatorMoves; }
    void previewImageNeeded(const QUrl& url) override { imageRequests << url; }
    bool activeBufferReferencesImage(const QString&) override { return false; }
    void rerenderActiveBuffer() override { ++rerenders; }

    int anchorClicks = 0;
    int separatorMoves = 0;
    int rerenders = 0;
    QList<QUrl> imageRequests;
};

ChatBufferLine sourceLine(const QString& text, bool dimmed = false, bool systemLine = false,
                          const QDateTime& timestamp = QDateTime()) {
    ChatBufferLine line;
    line.sourceText = text;
    line.dimmed = dimmed;
    line.systemLine = systemLine;
    line.timestamp = timestamp;
    return line;
}

ChatPane::BufferRenderOptions baseRenderOptions() {
    ChatPane::BufferRenderOptions options;
    options.baseOptions = ChatLineFormatOptions{}; // defaults
    options.timestampFormat = QStringLiteral("HH:mm");
    options.markerLine = true;
    options.markerCount = -1;
    options.indentWrap = true;
    options.alignNicks = true;
    return options;
}

const QString kUnreadMarker = QStringLiteral("──────────  new  ──────────");

} // namespace

class ChatPaneTest : public QObject {
    Q_OBJECT

  private slots:
    void rendersLinesInOrder() {
        ChatPane pane;
        FakeDelegate delegate;
        pane.setDelegate(&delegate);

        ChatBufferSnapshot snapshot;
        snapshot.lines = {sourceLine(QStringLiteral("<alice> first")),
                          sourceLine(QStringLiteral("<bob> second"))};
        pane.showBuffer(snapshot, baseRenderOptions());

        const QString text = pane.view()->document()->toPlainText();
        QVERIFY(text.contains(QStringLiteral("first")));
        QVERIFY(text.contains(QStringLiteral("second")));
        QVERIFY(text.indexOf(QStringLiteral("first")) <
                text.indexOf(QStringLiteral("second")));
    }

    void clearEmptiesDocument() {
        ChatPane pane;
        FakeDelegate delegate;
        pane.setDelegate(&delegate);

        ChatBufferSnapshot snapshot;
        snapshot.lines = {sourceLine(QStringLiteral("<alice> hi"))};
        pane.showBuffer(snapshot, baseRenderOptions());
        QVERIFY(!pane.view()->document()->toPlainText().trimmed().isEmpty());

        pane.clear();
        QVERIFY(pane.view()->document()->toPlainText().trimmed().isEmpty());
    }

    void timestampRendersInLocalTime() {
        ChatPane pane;
        FakeDelegate delegate;
        pane.setDelegate(&delegate);

        // A replayed line stored as UTC must render in LOCAL time (else every
        // re-render shifts the gutter by the UTC offset).
        const QDateTime utc(QDate(2026, 6, 15), QTime(12, 30), QTimeZone::utc());
        ChatBufferSnapshot snapshot;
        snapshot.lines = {sourceLine(QStringLiteral("<alice> hello"), false, false, utc)};

        ChatPane::BufferRenderOptions options = baseRenderOptions();
        options.baseOptions.showTimestamp = true;
        pane.showBuffer(snapshot, options);

        const QString text = pane.view()->document()->toPlainText();
        const QString expectedLocal = utc.toLocalTime().toString(QStringLiteral("HH:mm"));
        QVERIFY2(text.contains(expectedLocal),
                 qPrintable(QStringLiteral("expected local time %1 in: %2")
                                .arg(expectedLocal, text)));
    }

    void unreadMarkerSitsBeforeTheBoundaryLine() {
        ChatPane pane;
        FakeDelegate delegate;
        pane.setDelegate(&delegate);

        ChatBufferSnapshot snapshot;
        snapshot.lines = {sourceLine(QStringLiteral("<alice> before")),
                          sourceLine(QStringLiteral("<bob> after"))};

        ChatPane::BufferRenderOptions options = baseRenderOptions();
        options.markerCount = 1; // boundary before line index 1
        pane.showBuffer(snapshot, options);

        const QString text = pane.view()->document()->toPlainText();
        QVERIFY(text.contains(kUnreadMarker));
        const int beforeIdx = text.indexOf(QStringLiteral("before"));
        const int markerIdx = text.indexOf(kUnreadMarker);
        const int afterIdx = text.indexOf(QStringLiteral("after"));
        QVERIFY(beforeIdx < markerIdx);
        QVERIFY(markerIdx < afterIdx);
    }

    void noMarkerWhenMarkerLineDisabled() {
        ChatPane pane;
        FakeDelegate delegate;
        pane.setDelegate(&delegate);

        ChatBufferSnapshot snapshot;
        snapshot.lines = {sourceLine(QStringLiteral("<alice> a")),
                          sourceLine(QStringLiteral("<bob> b"))};

        ChatPane::BufferRenderOptions options = baseRenderOptions();
        options.markerLine = false;
        options.markerCount = 1;
        pane.showBuffer(snapshot, options);

        QVERIFY(!pane.view()->document()->toPlainText().contains(kUnreadMarker));
    }

    void chatEndedDividerRendersDimmedSystemSource() {
        ChatPane pane;
        FakeDelegate delegate;
        pane.setDelegate(&delegate);

        // A dimmed + systemLine line with source text is the "Chat ended" resume
        // divider — rendered centered, and (default theme) in the dim grey.
        ChatBufferSnapshot snapshot;
        snapshot.lines = {
            sourceLine(QStringLiteral("Chat ended 2026-06-15"), true, true),
            sourceLine(QStringLiteral("<alice> live again"))};
        pane.showBuffer(snapshot, baseRenderOptions());

        const QString text = pane.view()->document()->toPlainText();
        QVERIFY(text.contains(QStringLiteral("Chat ended 2026-06-15")));
        // The default dim colour (#8a8a8a) appears in the rich text for the
        // dimmed content.
        QVERIFY(pane.view()->document()->toHtml().contains(QStringLiteral("8a8a8a"),
                                                           Qt::CaseInsensitive));
    }

    void previewHtmlWithoutImageRendersText() {
        ChatPane pane;
        FakeDelegate delegate;
        pane.setDelegate(&delegate);

        // An htmlText (preview/OG card) line with no <img> renders its text and
        // requests no image fetch.
        ChatBufferLine card;
        card.htmlText = QStringLiteral("<div>example card title</div>");
        ChatBufferSnapshot snapshot;
        snapshot.lines = {card};
        pane.showBuffer(snapshot, baseRenderOptions());

        QVERIFY(pane.view()->document()->toPlainText().contains(
            QStringLiteral("example card title")));
        QCOMPARE(delegate.imageRequests.size(), 0);
    }
};

QTEST_MAIN(ChatPaneTest)
#include "chat_pane_test.moc"
