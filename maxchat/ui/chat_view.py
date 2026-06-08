"""The chat text view — a read-only **QTextEdit** (rich text, so it can hold inline images) with:

* **clickable links** — emitted via ``linkClicked``; the window opens them (browser, or the built-in
  image viewer). Hand cursor on hover, right-click Open/Copy.
* **inline images** — ``insert_image`` drops a scaled thumbnail into the document; clicking a thumbnail
  emits ``imageClicked`` (→ big viewer).
* **strip-on-copy** + a **nick separator rule** (as before).

It was a QPlainTextEdit; QTextEdit has no built-in scrollback cap or appendHtml, so we add a small
``appendHtml`` (append + stick-to-bottom + trim) and manual block trimming.
"""

from __future__ import annotations

import re

from PySide6.QtCore import QMimeData, QPoint, Qt, QUrl, Signal
from PySide6.QtGui import (
    QColor,
    QFontMetricsF,
    QGuiApplication,
    QImage,
    QPainter,
    QPen,
    QTextCursor,
    QTextDocument,
    QTextFrameFormat,
    QTextImageFormat,
    QTextTable,
)
from PySide6.QtWidgets import QTextEdit

URL_RE = re.compile(r"(?:https?://|www\.)[^\s<>\"']+", re.IGNORECASE)
_TRIM = ".,);]!?’'\""  # trailing punctuation that isn't part of the link


def _normalise(url: str) -> str:
    return url if url.lower().startswith(("http://", "https://")) else "http://" + url


class ChatView(QTextEdit):
    linkClicked = Signal(str)
    imageClicked = Signal(str)
    separatorMoved = Signal(int)  # the chat line was dragged → new nick-column width (chars)

    NBSP = " "  # the &nbsp; used to pad the nick column

    HRULE = -4  # block tag: a full-width horizontal rule (resume divider) with centred label text

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setReadOnly(True)
        self.setUndoRedoEnabled(False)
        self.viewport().setMouseTracking(True)  # so hover can show the link (hand) cursor
        self.strip_on_copy = True
        self.separator_cols = 0.0                       # char column for the rule (0 = off)
        self.separator_color = QColor(127, 127, 127, 70)
        self.embed_border = QColor(150, 150, 150, 130)   # outline around image / card embeds
        self.ts_cols = 0                                 # timestamp width (chars) — set by the window
        self._drag_sep = False                           # dragging the chat line?
        self._max_blocks = 2000
        self._pending_anchor: QTextCursor | None = None  # where to drop a click-to-load thumbnail
        self._rule_color = QColor(138, 138, 138)         # colour of the resume divider rule

    # ---- append / scrollback (QPlainTextEdit gave these for free) ---------
    def set_scrollback(self, n: int) -> None:
        self._max_blocks = max(50, int(n or 2000))

    def set_wrap(self, on: bool) -> None:
        self.setLineWrapMode(QTextEdit.LineWrapMode.WidgetWidth if on else QTextEdit.LineWrapMode.NoWrap)

    def appendHtml(self, html: str) -> None:
        sb = self.verticalScrollBar()
        at_bottom = sb.value() >= sb.maximum() - 4   # stick to the bottom only if already there
        self.append(html)
        # a new block can inherit the previous block's alignment — e.g. the centred resume-divider rule
        # would centre the next line. Force every appended line back to left-aligned.
        cur = self.textCursor()
        cur.movePosition(QTextCursor.MoveOperation.End)
        bf = cur.blockFormat()
        if bf.alignment() != Qt.AlignmentFlag.AlignLeft:
            bf.setAlignment(Qt.AlignmentFlag.AlignLeft)
            cur.setBlockFormat(bf)
        self._trim()
        if at_bottom:
            sb.setValue(sb.maximum())

    def insert_rule(self, label: str, color: QColor) -> None:
        """A full-width horizontal divider with the label centred in the gap (resume marker). The text
        is a centred paragraph tagged HRULE; paintEvent draws the line on both sides of it, so it spans
        the viewport and re-spans on resize (no fragile dash counting)."""
        from html import escape

        sb = self.verticalScrollBar()
        at_bottom = sb.value() >= sb.maximum() - 4
        cur = QTextCursor(self.document())
        cur.movePosition(QTextCursor.MoveOperation.End)
        if self.document().blockCount() > 1 or self.document().toPlainText():
            cur.insertBlock()
        bf = cur.blockFormat()
        bf.setAlignment(Qt.AlignmentFlag.AlignHCenter)
        bf.setLeftMargin(0)
        bf.setTextIndent(0)
        cur.setBlockFormat(bf)
        cur.insertHtml(f'<span style="color:{color.name()}">{escape(label)}</span>')
        cur.block().setUserState(self.HRULE)
        self._rule_color = color
        self._trim()
        if at_bottom:
            sb.setValue(sb.maximum())

    def _trim(self) -> None:
        over = self.document().blockCount() - self._max_blocks
        if over > 0:
            cur = QTextCursor(self.document())
            cur.movePosition(QTextCursor.MoveOperation.Start)
            cur.movePosition(QTextCursor.MoveOperation.NextBlock,
                             QTextCursor.MoveMode.KeepAnchor, over)
            cur.removeSelectedText()

    # ---- clickable links + inline-image clicks ---------------------------
    @staticmethod
    def _url_in_text(text: str, col: int) -> str | None:
        for m in URL_RE.finditer(text):
            if m.start() <= col <= m.end():
                return m.group().rstrip(_TRIM)
        return None

    def _url_at(self, pos) -> str | None:
        cur = self.cursorForPosition(pos)
        return self._url_in_text(cur.block().text(), cur.positionInBlock())

    def _image_at(self, pos) -> str | None:
        """If an inline image sits under ``pos``, return the URL we stashed in its resource name."""
        cur = self.cursorForPosition(pos)
        fmt = cur.charFormat()
        if fmt.isImageFormat():
            return fmt.toImageFormat().name() or None
        # the click can land just after the image char; peek one to the left too
        cur.movePosition(QTextCursor.MoveOperation.Left, QTextCursor.MoveMode.KeepAnchor)
        fmt = cur.charFormat()
        return fmt.toImageFormat().name() if fmt.isImageFormat() else None

    # ---- draggable chat line (resize the nick column) --------------------
    def _sep_x(self) -> float:
        fm = QFontMetricsF(self.font())
        return self.document().documentMargin() + fm.horizontalAdvance(" ") * self.separator_cols

    def _near_sep(self, x: float) -> bool:
        return self.separator_cols > 0 and abs(x - self._sep_x()) <= 4

    def mousePressEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.LeftButton and self._near_sep(event.pos().x()):
            self._drag_sep = True  # grab the chat line instead of starting a text selection
            event.accept()
            return
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event) -> None:
        if self._drag_sep:  # live-drag the rule; the column reflows on release
            fm = QFontMetricsF(self.font())
            sw = fm.horizontalAdvance(" ") or 1.0
            cols = (event.pos().x() - self.document().documentMargin()) / sw
            self.separator_cols = max(self.ts_cols + 2.5, cols)  # keep a 2-char minimum nick column
            self.viewport().update()
            return
        if self._near_sep(event.pos().x()):
            self.viewport().setCursor(Qt.CursorShape.SplitHCursor)
            return
        super().mouseMoveEvent(event)
        over = self._url_at(event.pos()) is not None or self._image_at(event.pos()) is not None
        self.viewport().setCursor(Qt.CursorShape.PointingHandCursor if over
                                  else Qt.CursorShape.IBeamCursor)

    def mouseReleaseEvent(self, event) -> None:
        if self._drag_sep:  # finished dragging the chat line → commit the new nick width (chars)
            self._drag_sep = False
            self.separatorMoved.emit(max(2, round(self.separator_cols - self.ts_cols - 0.5)))
            return
        super().mouseReleaseEvent(event)
        if event.button() != Qt.MouseButton.LeftButton or self.textCursor().hasSelection():
            return
        img = self._image_at(event.pos())
        if img:
            self.imageClicked.emit(img)
            return
        url = self._url_at(event.pos())
        if url:
            anchor = self.cursorForPosition(event.pos())  # remember where to drop a thumbnail
            anchor.movePosition(QTextCursor.MoveOperation.EndOfBlock)
            self._pending_anchor = anchor
            self.linkClicked.emit(url)

    def end_anchor(self) -> QTextCursor:
        """A live cursor at the current end of the document — capture it right after appending a
        message so an async load can drop media in just below that message (it tracks edits)."""
        c = QTextCursor(self.document())
        c.movePosition(QTextCursor.MoveOperation.End)
        return c

    def _open_insert(self, anchor: QTextCursor | None, left_margin: int = 0):
        """Open a new block after ``anchor`` (or the last click anchor / the doc end) → return a
        positioned cursor + whether we were scrolled to the bottom. ``left_margin`` (px) indents the
        block to the message column so a preview never crosses the gutter into the time/nick area."""
        sb = self.verticalScrollBar()
        at_bottom = sb.value() >= sb.maximum() - 4
        a = anchor if anchor is not None else self._pending_anchor
        cur = QTextCursor(a) if a is not None else self.textCursor()
        if a is None:
            cur.movePosition(QTextCursor.MoveOperation.End)
        cur.movePosition(QTextCursor.MoveOperation.EndOfBlock)
        cur.insertBlock()
        cur.block().setUserState(-2)  # mark as an embed block so reflow keeps it in the message column
        bf = cur.blockFormat()
        bf.setLeftMargin(left_margin)
        bf.setTextIndent(0)  # reset the negative hang-indent inherited from the preceding message,
        cur.setBlockFormat(bf)  # else it cancels the margin and the embed lands back at column 0
        self._pending_anchor = None
        return cur, at_bottom

    def _with_border(self, img: QImage) -> QImage:
        """Return a copy of the image with a 1px outline drawn just inside its edge."""
        out = img.convertToFormat(QImage.Format.Format_ARGB32)
        p = QPainter(out)
        pen = QPen(self.embed_border)
        pen.setWidth(1)
        p.setPen(pen)
        p.drawRect(0, 0, out.width() - 1, out.height() - 1)
        p.end()
        return out

    def insert_thumbnail(self, image: QImage, url: str, anchor=None,
                         max_w: int = 360, max_h: int = 260, left_margin: int = 0,
                         click_url: str | None = None) -> None:
        """Drop a scaled thumbnail just below the relevant message. The image's URL is stashed in its
        format name, so clicking it later emits ``imageClicked``. ``click_url`` can override that target
        for preview images that should open their parent page instead of the image itself."""
        if image.isNull():
            return
        thumb = (image.scaledToWidth(max_w, Qt.TransformationMode.SmoothTransformation)
                 if image.width() > max_w else image)
        if thumb.height() > max_h:
            thumb = thumb.scaledToHeight(max_h, Qt.TransformationMode.SmoothTransformation)
        thumb = self._with_border(thumb)  # draw a subtle outline so the embed reads as a framed picture
        target = click_url or url
        self.document().addResource(QTextDocument.ResourceType.ImageResource, QUrl(target), thumb)
        cur, at_bottom = self._open_insert(anchor, left_margin)
        fmt = QTextImageFormat()
        fmt.setName(target)
        cur.insertImage(fmt)
        self._trim()
        if at_bottom:
            self.verticalScrollBar().setValue(self.verticalScrollBar().maximum())

    def insert_card(self, html: str, anchor=None, left_margin: int = 0) -> QTextCursor:
        """Drop an HTML card (e.g. an X/tweet summary) just below the relevant message. A card is a
        table, which ignores the block left margin — so indent the TABLE's own frame instead."""
        cur, at_bottom = self._open_insert(anchor, left_margin)
        cur.insertHtml(html)
        probe = QTextCursor(cur)
        probe.movePosition(QTextCursor.MoveOperation.Left)
        tbl = probe.currentTable()
        if tbl is not None:
            tf = tbl.format()
            tf.setLeftMargin(left_margin)
            tf.setBorder(1)  # a subtle outline around the card
            tf.setBorderStyle(QTextFrameFormat.BorderStyle.BorderStyle_Solid)
            tf.setBorderBrush(self.embed_border)
            tbl.setFormat(tf)
        self._trim()
        if at_bottom:
            self.verticalScrollBar().setValue(self.verticalScrollBar().maximum())
        out = QTextCursor(cur)
        out.movePosition(QTextCursor.MoveOperation.EndOfBlock)
        return out

    def reflow_nick_column(self, ts_len: int, nick_w: int, margin_px: int) -> None:
        """Re-pad every line to a new nick-column width (after the chat line is dragged), in place so
        inline embeds and formatting survive. Block userState tags the kind: ``>=0`` = a message/action
        line whose value is the label length (``<nick>`` / ``* nick``); ``-3`` = a system/server line
        (text fills the whole nick column); ``-2`` = an embed (margin only); ``-1`` = leave alone."""
        doc = self.document()
        sb = self.verticalScrollBar()
        at_bottom = sb.value() >= sb.maximum() - 4
        for i in range(doc.blockCount()):  # block COUNT is stable (we only edit within blocks)
            block = doc.findBlockByNumber(i)
            st = block.userState()
            if st in (-1, self.HRULE):  # divider / resume rule: centred + full-width, never re-padded
                continue
            cur = QTextCursor(block)
            padded = st >= 0 or st == -3
            if padded:  # re-pad: replace the run of nbsp right after the timestamp
                new_pad = (nick_w + 1) if st == -3 else max(0, nick_w - st)
                text = block.text()
                cnt = 0
                while ts_len + cnt < len(text) and text[ts_len + cnt] == self.NBSP:
                    cnt += 1
                if new_pad != cnt:
                    cur.setPosition(block.position() + ts_len)
                    cur.setPosition(block.position() + ts_len + cnt, QTextCursor.MoveMode.KeepAnchor)
                    cur.insertText(self.NBSP * new_pad)
            bf = cur.blockFormat()  # hang-indent (padded lines) / plain indent (embeds) to the column
            bf.setLeftMargin(margin_px)
            bf.setTextIndent(-margin_px if padded else 0)
            cur.setBlockFormat(bf)
        for frame in doc.rootFrame().childFrames():  # X/tweet cards are tables — indent their frame
            if isinstance(frame, QTextTable):
                tf = frame.format()
                tf.setLeftMargin(margin_px)
                frame.setFormat(tf)
        if at_bottom:
            sb.setValue(sb.maximum())

    def contextMenuEvent(self, event) -> None:
        menu = self.createStandardContextMenu()
        url = self._url_at(event.pos())
        if url:
            menu.addSeparator()
            menu.addAction("Open link", lambda u=url: self.linkClicked.emit(u))
            menu.addAction("Copy link", lambda u=url: QGuiApplication.clipboard().setText(_normalise(u)))
        menu.exec(event.globalPos())

    # ---- copy / separator -------------------------------------------------
    def createMimeDataFromSelection(self) -> QMimeData:  # plain-text-only clipboard when stripping
        if self.strip_on_copy:
            md = QMimeData()
            md.setText(self.textCursor().selection().toPlainText())
            return md
        return super().createMimeDataFromSelection()

    def paintEvent(self, event) -> None:
        super().paintEvent(event)
        p = QPainter(self.viewport())
        self._paint_rules(p)             # full-width resume dividers (behind/around their centred label)
        if self.separator_cols > 0:      # the vertical "chat line" between nicks and messages
            fm = QFontMetricsF(self.font())
            x = (self.document().documentMargin()
                 + fm.horizontalAdvance(" ") * self.separator_cols)
            p.setPen(self.separator_color)
            p.drawLine(int(x), 0, int(x), self.viewport().height())
        p.end()

    def _paint_rules(self, p: QPainter) -> None:
        """Draw a horizontal line across the viewport on both sides of any HRULE block's centred text."""
        doc = self.document()
        layout = doc.documentLayout()
        vp = self.viewport()
        vh, vw = vp.height(), vp.width()
        vscroll = self.verticalScrollBar().value()
        margin = doc.documentMargin()
        fm = QFontMetricsF(self.font())
        block = self.cursorForPosition(QPoint(0, 0)).block()  # first visible block (cheap)
        while block.isValid():
            rect = layout.blockBoundingRect(block)
            top = rect.top() - vscroll
            if top > vh:
                break
            if block.userState() == self.HRULE:
                cy = top + rect.height() / 2.0
                tw = fm.horizontalAdvance(block.text())
                gap = 12.0
                mid = vw / 2.0
                p.setPen(QPen(self._rule_color, 1))
                left_end = mid - tw / 2.0 - gap
                right_start = mid + tw / 2.0 + gap
                if left_end > margin:
                    p.drawLine(int(margin), int(cy), int(left_end), int(cy))
                if right_start < vw - margin:
                    p.drawLine(int(right_start), int(cy), int(vw - margin), int(cy))
            block = block.next()
