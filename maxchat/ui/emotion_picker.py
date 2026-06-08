"""MS Comic Chat-style emotion selector — a popup with your avatar in the centre and the emotions
arranged around it, plus **Auto** on top (the default).

Opened from a button on the topic bar when Comic Mode is on. Picking an emotion sets the expression
used for YOUR outgoing comic panels (overriding the text guess); **Auto** returns to guessing from
your message text. Each emotion button shows your character's face at that expression; the centre
shows the whole figure at the current pick (a live self-view).
"""

from __future__ import annotations

from PySide6.QtCore import QSize, Qt, Signal
from PySide6.QtGui import QIcon, QPixmap
from PySide6.QtWidgets import (
    QButtonGroup,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QToolButton,
    QVBoxLayout,
)

from maxchat.ui.shadow_dialog import ShadowDialog

# the 8 non-neutral emotions placed around the centre avatar (3×3 grid; (1,1) is the avatar)
_RING = [
    (0, 0, "happy"), (0, 1, "laughing"), (0, 2, "coy"),
    (1, 0, "scared"),                    (1, 2, "bored"),
    (2, 0, "angry"), (2, 1, "shouting"), (2, 2, "sad"),
]


class EmotionWheel(ShadowDialog):
    emotionChanged = Signal(str)  # "auto" or an emotion name

    def __init__(self, parent=None) -> None:
        super().__init__(parent, title="Emotion")
        self._char = None
        self._group = QButtonGroup(self)
        self._group.setExclusive(True)
        self._buttons: dict[str, QToolButton] = {}

        v = QVBoxLayout(self.body)
        v.setContentsMargins(10, 8, 10, 10)
        v.setSpacing(6)

        top = QHBoxLayout()
        self.auto_btn = self._btn("auto", "Auto", "Guess the emotion from your message text")
        top.addWidget(self.auto_btn, 1)
        top.addWidget(self._btn("neutral", "Neutral", "Neutral expression"), 1)
        v.addLayout(top)

        grid = QGridLayout()
        grid.setSpacing(4)
        self.avatar = QLabel()
        self.avatar.setObjectName("emotionAvatar")
        self.avatar.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.avatar.setFixedSize(120, 132)
        grid.addWidget(self.avatar, 1, 1)
        for r, c, emo in _RING:
            grid.addWidget(self._btn(emo, emo[:3], emo.capitalize()), r, c)
        v.addLayout(grid)
        self.auto_btn.setChecked(True)  # default ON (guess from text) — after all widgets exist

    def _btn(self, key: str, text: str, tip: str) -> QToolButton:
        b = QToolButton()
        b.setCheckable(True)
        b.setText(text)
        b.setToolTip(tip)
        b.setIconSize(QSize(30, 30))
        b.setProperty("emo", key)
        b.toggled.connect(lambda on, k=key: on and self._pick(k))
        self._group.addButton(b)
        self._buttons[key] = b
        return b

    def _pick(self, key: str) -> None:
        self.emotionChanged.emit(key)
        self._update_preview()

    def set_character(self, ch) -> None:
        """Point the emotion buttons' faces + the centre preview at your character."""
        self._char = ch
        for key, b in self._buttons.items():
            if key in ("auto", "neutral") or ch is None:
                continue
            face = ch.face_cell(key)
            if face is not None and not face.isNull():
                b.setIcon(QIcon(QPixmap.fromImage(face)))
                b.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonIconOnly)
        self._update_preview()

    def _current(self) -> str:
        btn = self._group.checkedButton()
        return btn.property("emo") if btn else "auto"

    def _update_preview(self) -> None:
        ch = self._char
        if ch is None:
            self.avatar.setText("(set a comic\nart folder)")
            return
        emo = "neutral" if self._current() == "auto" else self._current()
        img = ch.image(emo, "right", 0)
        if img is not None and not img.isNull():
            self.avatar.setPixmap(QPixmap.fromImage(img).scaled(
                self.avatar.width() - 6, self.avatar.height() - 6,
                Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))
