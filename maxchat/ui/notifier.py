"""Custom toast notifications — small themed popups that slide into a screen corner for PMs / highlights.

A frameless popup MaxChat draws itself, so it looks identical on every OS and can be themed apart from
the app. ``Notifier`` stacks several toasts from the chosen corner instead of overlapping them. The
*System* notification style (OS-native, via the tray) is handled in the main window, not here.
"""

from __future__ import annotations

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QColor, QGuiApplication
from PySide6.QtWidgets import (
    QFrame,
    QGraphicsDropShadowEffect,
    QHBoxLayout,
    QLabel,
    QVBoxLayout,
    QWidget,
)

# Where a toast appears. Keys are the stored ``notify_corner`` values.
CORNERS = {"tl": "Top-left", "tr": "Top-right", "bl": "Bottom-left", "br": "Bottom-right"}

# notify_theme -> (bg, fg, accent). "follow" is resolved from the live app theme by the caller.
PALETTES = {
    "dark": ("#2b2b2b", "#e8e8e8", "#4a9eff"),
    "light": ("#f5f5f5", "#1a1a1a", "#2563eb"),
    "solar-dark": ("#073642", "#93a1a1", "#268bd2"),
    "solar-light": ("#fdf6e3", "#586e75", "#cb4b16"),
}
THEME_LABELS = {
    "follow": "Follow app theme",
    "dark": "Dark",
    "light": "Light",
    "solar-dark": "Solarized dark",
    "solar-light": "Solarized light",
}


def palette(notify_theme: str, follow: tuple[str, str, str]) -> tuple[str, str, str]:
    """(bg, fg, accent) for a toast. ``follow`` is used when ``notify_theme == 'follow'``."""
    return PALETTES.get(notify_theme, follow)


class _Toast(QWidget):
    WIDTH = 340

    def __init__(self, title, body, colors, duration_ms, icon, on_click, on_done):
        super().__init__(
            None,
            Qt.WindowType.FramelessWindowHint | Qt.WindowType.Tool | Qt.WindowType.WindowStaysOnTopHint,
        )
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)  # never steal focus
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)  # don't accumulate dismissed toasts
        self._on_click, self._on_done = on_click, on_done
        bg, fg, accent = colors

        card = QFrame(self)
        card.setObjectName("toastCard")
        card.setStyleSheet(
            f"#toastCard{{background:{bg};border-radius:10px;border-left:4px solid {accent};}}"
            f"QLabel{{background:transparent;color:{fg};}}"
        )
        shadow = QGraphicsDropShadowEffect(self)
        shadow.setBlurRadius(26)
        shadow.setColor(QColor(0, 0, 0, 170))
        shadow.setOffset(0, 3)
        card.setGraphicsEffect(shadow)

        row = QHBoxLayout(card)
        row.setContentsMargins(14, 11, 13, 11)
        row.setSpacing(11)
        if icon is not None and not icon.isNull():
            ic = QLabel()
            ic.setPixmap(icon.pixmap(28, 28))
            ic.setFixedSize(30, 30)
            ic.setAlignment(Qt.AlignmentFlag.AlignTop)
            row.addWidget(ic)
        text_col = QVBoxLayout()
        text_col.setSpacing(2)
        tl = QLabel(title)
        tf = tl.font()
        tf.setBold(True)
        tf.setPointSize(tf.pointSize() + 1)
        tl.setFont(tf)
        tl.setStyleSheet(f"color:{accent};background:transparent;")
        bl = QLabel(body)
        bl.setWordWrap(True)
        text_col.addWidget(tl)
        text_col.addWidget(bl)
        row.addLayout(text_col, 1)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(13, 13, 13, 13)  # room for the drop shadow to render
        outer.addWidget(card)
        self.setFixedWidth(self.WIDTH)
        self.adjustSize()
        QTimer.singleShot(max(1000, duration_ms), self.close)

    def mousePressEvent(self, _event) -> None:
        cb = self._on_click
        self.close()
        if cb:
            cb()

    def closeEvent(self, event) -> None:
        done, self._on_done = self._on_done, None
        if done:
            done(self)
        super().closeEvent(event)


class Notifier:
    """Shows + stacks toasts. One instance lives on the main window."""

    MARGIN = 18
    GAP = 9
    MAX_VISIBLE = 4

    def __init__(self) -> None:
        self._toasts: list[_Toast] = []
        self._corner = "br"

    def show(self, title, body, colors, corner="br", duration_ms=6000, icon=None, on_click=None) -> None:
        self._corner = corner if corner in CORNERS else "br"
        while len(self._toasts) >= self.MAX_VISIBLE:
            self._toasts[0].close()  # drop the oldest (its closeEvent removes it from the list)
        t = _Toast(title, body, colors, duration_ms, icon, on_click, self._remove)
        self._toasts.append(t)
        t.show()
        self._relayout()

    def _remove(self, toast: _Toast) -> None:
        if toast in self._toasts:
            self._toasts.remove(toast)
        self._relayout()

    def _relayout(self) -> None:
        screen = QGuiApplication.primaryScreen()
        if screen is None:
            return
        g = screen.availableGeometry()
        at_right = self._corner in ("tr", "br")
        at_bottom = self._corner in ("bl", "br")
        # bottom corners stack upward (newest nearest the corner) → iterate newest-first
        order = list(reversed(self._toasts)) if at_bottom else list(self._toasts)
        y = (g.bottom() - self.MARGIN) if at_bottom else (g.top() + self.MARGIN)
        for t in order:
            h = t.height()
            x = (g.right() - self.MARGIN - t.width()) if at_right else (g.left() + self.MARGIN)
            if at_bottom:
                y -= h
                t.move(x, y)
                y -= self.GAP
            else:
                t.move(x, y)
                y += h + self.GAP
