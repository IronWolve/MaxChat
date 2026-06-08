"""DCC transfers window — accept/cancel file transfers and watch live progress."""

from __future__ import annotations

import time

from PySide6.QtCore import QTimer, QUrl
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import (
    QAbstractItemView,
    QHBoxLayout,
    QHeaderView,
    QProgressBar,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)

from maxchat.ui.shadow_dialog import ShadowDialog


def _human(n: float) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{int(n)} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} GB"


def _eta(secs: float) -> str:
    if secs <= 0 or secs != secs or secs == float("inf"):  # 0, NaN, inf
        return "—"
    secs = int(secs)
    if secs < 60:
        return f"{secs}s"
    if secs < 3600:
        return f"{secs // 60}m {secs % 60:02d}s"
    return f"{secs // 3600}h {(secs % 3600) // 60:02d}m"


_COLS = ["File", "Peer", "Dir", "Progress", "Rate", "ETA", "Status"]
_DONE = {"done", "failed", "cancelled"}


class DCCDialog(ShadowDialog):
    def __init__(self, parent=None, *, manager) -> None:
        super().__init__(parent)
        self.setWindowTitle("File Transfers")
        self.set_autosize(76, 18)  # font-relative sizing
        self._m = manager
        self._rows: dict = {}     # transfer → row
        self._samples: dict = {}  # transfer → (last_transferred, last_time, ema_rate)

        v = QVBoxLayout(self.body)
        self.table = QTableWidget(0, len(_COLS))
        self.table.setHorizontalHeaderLabels(_COLS)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.verticalHeader().setVisible(False)
        hh = self.table.horizontalHeader()
        hh.setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)        # File
        hh.setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)        # Progress
        for c in (1, 2, 4, 5, 6):
            hh.setSectionResizeMode(c, QHeaderView.ResizeMode.ResizeToContents)
        v.addWidget(self.table, 1)

        row = QHBoxLayout()
        for label, slot in (("Accept", self._accept), ("Cancel", self._cancel),
                            ("Open folder", self._open_dir)):
            b = QPushButton(label)
            b.clicked.connect(slot)
            row.addWidget(b)
        row.addStretch(1)
        close = QPushButton("Close")
        close.clicked.connect(self.accept)
        row.addWidget(close)
        v.addLayout(row)

        for tr in manager.transfers:
            self._add(tr)
        manager.added.connect(self._add)
        manager.changed.connect(self._update)

        # live rate/ETA recompute once a second
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._tick)
        self._timer.start(1000)

    def _add(self, tr) -> None:
        r = self.table.rowCount()
        self.table.insertRow(r)
        self._rows[tr] = r
        for c in range(len(_COLS)):
            self.table.setItem(r, c, QTableWidgetItem(""))
        bar = QProgressBar()
        bar.setRange(0, 100)
        self.table.setCellWidget(r, 3, bar)
        self._update(tr)

    def _update(self, tr) -> None:
        r = self._rows.get(tr)
        if r is None:
            return
        pct = int(tr.transferred * 100 / tr.size) if tr.size else 0
        self.table.item(r, 0).setText(tr.filename)
        self.table.item(r, 1).setText(tr.nick)
        self.table.item(r, 2).setText("send ↑" if tr.send else "recv ↓")
        bar = self.table.cellWidget(r, 3)
        if bar is not None:
            bar.setValue(pct)
            bar.setFormat(f"{pct}%  ({_human(tr.transferred)} / {_human(tr.size)})")
        self.table.item(r, 6).setText(tr.status)
        if tr.status in _DONE:  # freeze rate/ETA once the transfer ends
            self.table.item(r, 4).setText("")
            self.table.item(r, 5).setText("—")

    def _tick(self) -> None:
        now = time.monotonic()
        for tr, r in self._rows.items():
            cur = tr.transferred
            prev = self._samples.get(tr)
            ema = None
            if tr.status == "active" and prev is not None:
                dt = now - prev[1]
                if dt > 0:
                    inst = max(0.0, (cur - prev[0]) / dt)
                    ema = inst if prev[2] is None else prev[2] * 0.4 + inst * 0.6
            self._samples[tr] = (cur, now, ema)
            if tr.status == "active" and ema and ema > 1:
                self.table.item(r, 4).setText(_human(ema) + "/s")
                self.table.item(r, 5).setText(_eta(max(0, tr.size - cur) / ema))
            elif tr.status not in _DONE:
                self.table.item(r, 4).setText("")
                self.table.item(r, 5).setText("—")

    def _selected(self):
        r = self.table.currentRow()
        for tr, row in self._rows.items():
            if row == r:
                return tr
        return None

    def _accept(self) -> None:
        tr = self._selected()
        if tr is not None:
            self._m.accept(tr)

    def _cancel(self) -> None:
        tr = self._selected()
        if tr is not None:
            tr.cancel()

    def _open_dir(self) -> None:
        QDesktopServices.openUrl(QUrl.fromLocalFile(self._m.download_dir()))
