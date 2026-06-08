"""mIRC color picker — a small popup (Ctrl+K) for inserting a text-color control code into the
message box, so you don't have to remember the mIRC numbers.

Clicking a swatch inserts ``\\x03NN`` and closes; "Plain" inserts the reset code ``\\x0f`` (cancels
color/bold/etc.). It's a Popup window, so it also closes when you click away.
"""

from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QButtonGroup,
    QFrame,
    QGraphicsDropShadowEffect,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QToolButton,
    QVBoxLayout,
)

# The 16 classic mIRC colors, index 0..15.
MIRC_COLORS = [
    "#FFFFFF", "#000000", "#00007F", "#009300", "#FF0000", "#7F0000", "#9C009C", "#FC7F00",
    "#FFFF00", "#00FC00", "#009393", "#00FFFF", "#0000FC", "#FF00FF", "#7F7F7F", "#D2D2D2",
]
RESET = "\x0f"      # plain (cancel all colors/formatting)
COLOR = "\x03"      # color control code; followed by NN


def _contrast(hex_color: str) -> str:
    h = hex_color.lstrip("#")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return "#000000" if (0.299 * r + 0.587 * g + 0.114 * b) > 140 else "#FFFFFF"


class ColorPicker(QFrame):
    """Emits ``inserted(code)`` with the control code to drop at the cursor."""

    inserted = Signal(str)

    def __init__(self, parent=None) -> None:
        super().__init__(parent, Qt.WindowType.Popup)  # auto-closes on outside click
        self.setObjectName("colorPicker")
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(13, 13, 13, 13)
        card = QFrame(self)
        card.setObjectName("colorPickerCard")
        card.setFrameShape(QFrame.Shape.StyledPanel)
        shadow = QGraphicsDropShadowEffect(self)
        shadow.setBlurRadius(22)
        shadow.setColor(QColor(0, 0, 0, 165))
        shadow.setOffset(0, 3)
        card.setGraphicsEffect(shadow)
        outer.addWidget(card)

        v = QVBoxLayout(card)
        v.setContentsMargins(10, 8, 10, 10)
        v.setSpacing(6)
        v.addWidget(QLabel("Text color"))

        grid = QGridLayout()
        grid.setSpacing(3)
        group = QButtonGroup(self)
        for i, hexc in enumerate(MIRC_COLORS):
            b = QToolButton()
            b.setFixedSize(26, 22)
            b.setToolTip(f"{i}  {hexc}")
            b.setStyleSheet(
                f"QToolButton{{background:{hexc};border:1px solid #555;border-radius:3px;}}"
                f"QToolButton:hover{{border:2px solid {_contrast(hexc)};}}")
            b.clicked.connect(lambda _c=False, n=i: self._emit(f"{COLOR}{n:02d}"))
            group.addButton(b)
            grid.addWidget(b, i // 8, i % 8)
        v.addLayout(grid)

        row = QHBoxLayout()
        plain = QPushButton("Plain (reset)")
        plain.setToolTip("Insert the reset code — cancels color / bold / etc.")
        plain.clicked.connect(lambda: self._emit(RESET))
        row.addWidget(plain)
        row.addStretch(1)
        v.addLayout(row)

    def _emit(self, code: str) -> None:
        self.inserted.emit(code)
        self.close()
