"""Raw IRC traffic window — the in/out protocol lines for this session (HexChat's Raw Log).

Live-updates as traffic flows. ``<<`` = received from the server, ``>>`` = sent by us. Read-only,
ring-buffered by the caller; handy for debugging CAP/SASL/mode issues.
"""

from __future__ import annotations

from PySide6.QtGui import QFont
from PySide6.QtWidgets import QHBoxLayout, QPlainTextEdit, QPushButton, QVBoxLayout

from maxchat.ui import fonts
from maxchat.ui.shadow_dialog import ShadowDialog

_COLORS = {"<<": "#7f9fbf", ">>": "#98c379"}  # in = blue-grey, out = green


class RawLogDialog(ShadowDialog):
    def __init__(self, parent, history, on_clear) -> None:
        super().__init__(parent)
        self.setWindowTitle("Raw Log")
        self.set_autosize(94, 26)  # font-relative sizing
        self._on_clear = on_clear

        v = QVBoxLayout(self.body)
        self.view = QPlainTextEdit()
        self.view.setReadOnly(True)
        self.view.setMaximumBlockCount(5000)
        self.view.setFont(QFont(fonts.default_mono(), 14))
        self.view.setStyleSheet("QPlainTextEdit{background:#11141a;color:#cdd6e0;border:none;}")
        v.addWidget(self.view, 1)

        row = QHBoxLayout()
        clear = QPushButton("Clear")
        clear.clicked.connect(self._clear)
        row.addWidget(clear)
        row.addStretch(1)
        close = QPushButton("Close")
        close.clicked.connect(self.accept)
        row.addWidget(close)
        v.addLayout(row)

        for entry in history:
            self.add_line(*entry)

    def add_line(self, net_name: str, direction: str, line: str) -> None:
        from html import escape
        color = _COLORS.get(direction, "#cdd6e0")
        net = f"[{net_name}] " if net_name else ""
        self.view.appendHtml(
            f'<span style="color:{color}">{escape(net + direction)}</span> {escape(line)}')

    def _clear(self) -> None:
        self.view.clear()
        self._on_clear()
