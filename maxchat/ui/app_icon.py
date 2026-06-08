"""The application / window / tray icon.

Either a generated **speech bubble** (painted with QPainter, tinted by the active theme accent) or a
chosen **emoji** glyph. No external artwork — everything is drawn at runtime. The stored preference
(``tray_icon``) is the literal value below: ``"bubble"`` or the emoji character itself.
"""

from __future__ import annotations

from PySide6.QtCore import QPointF, Qt
from PySide6.QtGui import QColor, QFont, QIcon, QPainter, QPainterPath, QPixmap

# (stored value, menu label). "bubble" is the default generated speech bubble (theme-tinted).
CHOICES: list[tuple[str, str]] = [
    ("bubble", "Speech bubble (theme color)"),
    ("💬", "Speech balloon"),
    ("💭", "Thought bubble"),
    ("🗨️", "Left speech bubble"),
    ("😀", "Smiley"),
    ("😎", "Cool shades"),
    ("🤖", "Robot"),
    ("👾", "Alien"),
    ("🐧", "Penguin"),
    ("⭐", "Star"),
    ("🔔", "Bell"),
    ("💻", "Computer"),
]

_EMOJI_FAMILIES = ["Noto Color Emoji", "Segoe UI Emoji", "Apple Color Emoji", "Noto Emoji"]


def _bubble_pixmap(accent: QColor, size: int = 64) -> QPixmap:
    """The speech-bubble glyph — bubble body + tail + '...' dots — in ``accent`` (matches the .ico)."""
    pm = QPixmap(size, size)
    pm.fill(Qt.GlobalColor.transparent)
    p = QPainter(pm)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    p.scale(size / 64.0, size / 64.0)
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(accent)
    p.drawRoundedRect(6, 8, 52, 38, 10, 10)
    tail = QPainterPath()
    tail.moveTo(22, 44)
    tail.lineTo(16, 58)
    tail.lineTo(34, 44)
    tail.closeSubpath()
    p.fillPath(tail, accent)
    p.setBrush(QColor("white"))
    for i in range(3):
        p.drawEllipse(QPointF(22 + i * 10, 27), 3.0, 3.0)
    p.end()
    return pm


def _emoji_pixmap(glyph: str, size: int = 64) -> QPixmap:
    """Render an emoji centered on a transparent square (color where a color-emoji font exists)."""
    pm = QPixmap(size, size)
    pm.fill(Qt.GlobalColor.transparent)
    p = QPainter(pm)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    f = QFont()
    f.setFamilies(_EMOJI_FAMILIES)
    f.setPixelSize(int(size * 0.82))
    p.setFont(f)
    p.drawText(pm.rect(), int(Qt.AlignmentFlag.AlignCenter), glyph)
    p.end()
    return pm


def make_icon(choice: str, accent: QColor) -> QIcon:
    """The QIcon for a stored ``tray_icon`` value: the generated bubble (in ``accent``) or an emoji."""
    if not choice or choice == "bubble":
        return QIcon(_bubble_pixmap(accent))
    return QIcon(_emoji_pixmap(choice))
