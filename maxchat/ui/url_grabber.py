"""URL list — every link seen in chat, with open-in-browser + copy (HexChat's URL grabber)."""

from __future__ import annotations

from PySide6.QtCore import QUrl
from PySide6.QtGui import QDesktopServices, QGuiApplication
from PySide6.QtWidgets import (
    QAbstractItemView,
    QHBoxLayout,
    QHeaderView,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)

from maxchat.ui.shadow_dialog import ShadowDialog

_COLS = ["Time", "From", "Where", "URL"]


class UrlGrabberDialog(ShadowDialog):
    def __init__(self, parent, entries, on_clear) -> None:
        super().__init__(parent)
        self.setWindowTitle("URL List")
        self.set_autosize(92, 24)  # font-relative sizing
        self._on_clear = on_clear

        v = QVBoxLayout(self.body)
        self.table = QTableWidget(0, len(_COLS))
        self.table.setHorizontalHeaderLabels(_COLS)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.verticalHeader().setVisible(False)
        hh = self.table.horizontalHeader()
        hh.setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
        for c in (0, 1, 2):
            hh.setSectionResizeMode(c, QHeaderView.ResizeMode.ResizeToContents)
        self.table.doubleClicked.connect(lambda _i: self._open())
        v.addWidget(self.table, 1)

        row = QHBoxLayout()
        for label, slot in (("Open", self._open), ("Copy", self._copy), ("Clear", self._clear)):
            b = QPushButton(label)
            b.clicked.connect(slot)
            row.addWidget(b)
        row.addStretch(1)
        close = QPushButton("Close")
        close.clicked.connect(self.accept)
        row.addWidget(close)
        v.addLayout(row)

        for e in entries:
            self.add_row(e)

    def add_row(self, e) -> None:
        r = self.table.rowCount()
        self.table.insertRow(r)
        for c, val in enumerate(e):
            self.table.setItem(r, c, QTableWidgetItem(val))
        self.table.scrollToBottom()

    def _selected_url(self) -> str:
        r = self.table.currentRow()
        item = self.table.item(r, 3) if r >= 0 else None
        return item.text() if item is not None else ""

    def _open(self) -> None:
        url = self._selected_url()
        if url:
            QDesktopServices.openUrl(QUrl(url))

    def _copy(self) -> None:
        url = self._selected_url()
        if url:
            QGuiApplication.clipboard().setText(url)

    def _clear(self) -> None:
        self.table.setRowCount(0)
        self._on_clear()
