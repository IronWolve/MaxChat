"""Comic ▸ Browse Characters — a gallery of the available comic figures so you can SEE who's who
before assigning a character to a nick (Comic Settings). Each tile shows the figure + its name."""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import (
    QGridLayout,
    QLabel,
    QScrollArea,
    QVBoxLayout,
    QWidget,
)

from maxchat.ui.shadow_dialog import ShadowDialog

_COLS = 4
_THUMB = 132  # figure height in px


class CharacterGallery(ShadowDialog):
    def __init__(self, parent=None, items: list[tuple[str, object]] | None = None) -> None:
        """items = [(name, QImage|None), …] — the figure image for each character."""
        super().__init__(parent)
        self.setWindowTitle("Comic Characters")
        self.set_autosize(64, 30)
        items = items or []

        root = QVBoxLayout(self.body)
        root.addWidget(QLabel(f"{len(items)} characters — assign them to nicks in Comic Settings ▸ "
                              "Characters / Channels."))
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        host = QWidget()
        # a light card so the dark line-art figures read well AND the names are dark-on-light (legible)
        host.setStyleSheet("background:#f4f4f4; color:#1a1a1a; font-weight:600;")
        grid = QGridLayout(host)
        grid.setSpacing(10)
        for i, (name, img) in enumerate(items):
            grid.addWidget(self._tile(name, img), i // _COLS, i % _COLS)
        scroll.setWidget(host)
        root.addWidget(scroll, 1)

    @staticmethod
    def _tile(name: str, img) -> QWidget:
        w = QWidget()
        v = QVBoxLayout(w)
        v.setContentsMargins(4, 4, 4, 4)
        v.setSpacing(3)
        pic = QLabel()
        pic.setAlignment(Qt.AlignmentFlag.AlignCenter)
        pic.setFixedHeight(_THUMB)
        if img is not None and not img.isNull():
            pic.setPixmap(QPixmap.fromImage(img).scaledToHeight(
                _THUMB, Qt.TransformationMode.SmoothTransformation))
        else:
            pic.setText("(no preview)")
        v.addWidget(pic)
        lbl = QLabel(name)
        lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lbl.setWordWrap(True)
        v.addWidget(lbl)
        return w
