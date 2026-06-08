"""Assign-comic-character dialog with a live avatar preview — the figure updates as you scroll the
list, so you can see who you're picking. Returns the chosen character stem ("" = default / random)."""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import (
    QComboBox,
    QDialogButtonBox,
    QHBoxLayout,
    QLabel,
    QVBoxLayout,
)

from maxchat.ui.shadow_dialog import ShadowDialog

_PREVIEW_H = 150


class AssignCharacterDialog(ShadowDialog):
    def __init__(self, parent=None, *, prompt: str, stems: list[str], current: str, image_for) -> None:
        """stems = character names; current = the one already assigned (""=default); image_for(stem)->QImage."""
        super().__init__(parent)
        self.setWindowTitle("Assign comic character")
        self._image_for = image_for
        self.result_stem: str | None = None

        v = QVBoxLayout(self.body)
        v.addWidget(QLabel(prompt))

        row = QHBoxLayout()
        self._preview = QLabel()  # the figure, on the LEFT
        self._preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._preview.setFixedSize(150, _PREVIEW_H)
        self._preview.setStyleSheet("background:#f4f4f4; border-radius:6px;")  # light → dark line-art reads
        row.addWidget(self._preview)

        self.combo = QComboBox()  # the picker, on the RIGHT (not stacked on top)
        self.combo.addItem("(default / random)", "")
        for s in stems:
            self.combo.addItem(s, s)
        i = self.combo.findData(current)
        self.combo.setCurrentIndex(i if i >= 0 else 0)
        right = QVBoxLayout()
        right.addWidget(self.combo)
        right.addStretch(1)
        row.addLayout(right, 1)
        v.addLayout(row)

        self.combo.currentIndexChanged.connect(self._show)
        self.combo.highlighted.connect(self._show)  # update the preview as you SCROLL the dropdown
        self._show(self.combo.currentIndex())

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self._accept)
        buttons.rejected.connect(self.reject)
        v.addWidget(buttons)
        self.set_autosize(48, 0)

    def _show(self, index: int) -> None:
        stem = self.combo.itemData(index) if index >= 0 else ""
        img = self._image_for(stem) if stem else None
        if img is not None and not img.isNull():
            self._preview.setPixmap(QPixmap.fromImage(img).scaledToHeight(
                _PREVIEW_H - 8, Qt.TransformationMode.SmoothTransformation))
        else:
            self._preview.setPixmap(QPixmap())
            self._preview.setText("random character" if not stem else "(no preview)")

    def _accept(self) -> None:
        self.result_stem = self.combo.currentData() or ""
        self.accept()
