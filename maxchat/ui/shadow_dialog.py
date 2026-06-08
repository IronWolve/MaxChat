"""Frameless dialog base with a themed card + a real drop shadow.

WSLg/Wayland (and some Linux compositors) draw no border/shadow for dialog windows, so we draw our
own: the dialog is frameless + translucent, and the visible content lives in an inner **card** that
carries the ``QGraphicsDropShadowEffect``. (Qt renders graphics effects on child widgets, but not on
top-level windows — hence the card.) A slim title bar (title + ✕) is draggable to move the window;
a corner grip resizes it.

Subclasses just inherit ``ShadowDialog`` and lay their content out on ``self.body`` instead of
``self`` (e.g. ``QVBoxLayout(self.body)``). They can still call ``self.setWindowTitle(...)`` — it
updates the title bar too. Native file/color picker dialogs are still handled by Qt.
"""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QGuiApplication
from PySide6.QtWidgets import (
    QDialog,
    QFrame,
    QGraphicsDropShadowEffect,
    QHBoxLayout,
    QLabel,
    QSizeGrip,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

_MARGIN = 22  # transparent gap around the card for the shadow to paint into


class _TitleBar(QWidget):
    """A draggable title strip with a label + close button."""

    def __init__(self, dialog: QDialog, title: str) -> None:
        super().__init__()
        self.setObjectName("dialogTitleBar")
        self._dialog = dialog
        self._drag = None
        h = QHBoxLayout(self)
        h.setContentsMargins(12, 5, 6, 5)
        self.label = QLabel(title)
        self.label.setObjectName("dialogTitle")
        close = QToolButton()
        close.setObjectName("dialogClose")
        close.setText("✕")
        close.setToolTip("Close")
        close.clicked.connect(dialog.reject)
        h.addWidget(self.label)
        h.addStretch(1)
        h.addWidget(close)

    def mousePressEvent(self, e) -> None:
        if e.button() == Qt.MouseButton.LeftButton:
            # Prefer the compositor-driven move: on Wayland (WSLg) an app can't position its own
            # windows, so a manual ``move()`` is ignored — ``startSystemMove`` asks the WM to drag it.
            wh = self._dialog.windowHandle()
            if wh is not None and wh.startSystemMove():
                e.accept()
                return
            self._drag = e.globalPosition().toPoint() - self._dialog.frameGeometry().topLeft()
            e.accept()

    def mouseMoveEvent(self, e) -> None:
        if self._drag is not None and (e.buttons() & Qt.MouseButton.LeftButton):
            self._dialog.move(e.globalPosition().toPoint() - self._drag)  # X11/Windows fallback
            e.accept()

    def mouseReleaseEvent(self, e) -> None:
        self._drag = None


class ShadowDialog(QDialog):
    def __init__(self, parent=None, title: str = "") -> None:
        super().__init__(parent)
        self.setObjectName("ShadowDialog")
        self.setWindowFlags(self.windowFlags() | Qt.WindowType.FramelessWindowHint)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground, True)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(_MARGIN, _MARGIN, _MARGIN, _MARGIN)
        card = QFrame()
        card.setObjectName("dialogCard")
        shadow = QGraphicsDropShadowEffect(card)
        shadow.setBlurRadius(32)
        shadow.setColor(QColor(0, 0, 0, 200))
        shadow.setOffset(0, 6)
        card.setGraphicsEffect(shadow)
        outer.addWidget(card)

        cl = QVBoxLayout(card)
        cl.setContentsMargins(0, 0, 0, 0)
        cl.setSpacing(0)
        self._title_bar = _TitleBar(self, title)
        cl.addWidget(self._title_bar)
        self.body = QWidget()  # subclasses lay their content out here
        cl.addWidget(self.body, 1)
        grip = QHBoxLayout()
        grip.setContentsMargins(0, 0, 3, 3)
        grip.addStretch(1)
        grip.addWidget(QSizeGrip(self))
        cl.addLayout(grip)

        self._autosize_spec: tuple[int, int] | None = None  # (min_cols, min_rows) once set_autosize() called
        self._autosized = False

        if title:
            super().setWindowTitle(title)

    def setWindowTitle(self, title: str) -> None:  # keep the title bar in sync
        super().setWindowTitle(title)
        if hasattr(self, "_title_bar"):
            self._title_bar.label.setText(title)

    def set_autosize(self, min_cols: int = 0, min_rows: int = 0) -> None:
        """Request font-aware sizing: the dialog will size to its content (with an optional floor of
        ``min_cols`` chars × ``min_rows`` rows) the first time it's shown, instead of a hardcoded pixel
        size. Call this anywhere in ``__init__`` — the actual sizing happens on first show, once the
        layout is built, so it tracks the current UI font."""
        self._autosize_spec = (min_cols, min_rows)

    def showEvent(self, event) -> None:
        super().showEvent(event)
        if self._autosize_spec is not None and not self._autosized:
            self._autosized = True
            self.autosize(*self._autosize_spec)

    def autosize(self, min_cols: int = 0, min_rows: int = 0) -> None:
        """Size to fit the laid-out content, with an optional font-relative floor (``min_cols``
        characters wide × ``min_rows`` text-rows tall) for list/table dialogs whose own content hint is
        tiny when empty. Everything is derived from the current font + content, so the dialog scales
        when the UI font changes instead of using hardcoded pixels. Clamped to the screen.

        Call this at the END of ``__init__`` (after all widgets are added)."""
        self.adjustSize()  # natural content size (the shadow margin + title bar + grip are included)
        hint = self.sizeHint()
        w, h = hint.width(), hint.height()
        if min_cols or min_rows:
            fm = self.fontMetrics()
            char = fm.averageCharWidth() or fm.horizontalAdvance("x") or 8
            chrome_w = 2 * _MARGIN + 28              # shadow margins + card padding
            chrome_h = 2 * _MARGIN + fm.height() + 64  # + title bar + grip
            if min_cols:
                w = max(w, min_cols * char + chrome_w)
            if min_rows:
                h = max(h, min_rows * fm.height() + chrome_h)
        scr = QGuiApplication.primaryScreen()
        if scr is not None:
            avail = scr.availableGeometry()
            w = min(w, int(avail.width() * 0.96))
            h = min(h, int(avail.height() * 0.95))
        self.resize(int(w), int(h))
