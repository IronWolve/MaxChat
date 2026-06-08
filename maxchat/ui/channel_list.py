"""Channel list browser — runs ``/LIST`` and shows the result in a sortable table.

HexChat/mIRC-style: a popup with **Channel · Users · Topic** columns, a **min-users** filter (sent
server-side via ELIST ``>N`` so big networks don't dump everything) and a name filter, **join by
double-click**, plus **Export CSV** and **Copy**. Wired to the active network's IRC client.
"""

from __future__ import annotations

import csv

from PySide6.QtCore import Qt
from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import (
    QAbstractItemView,
    QFileDialog,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QPushButton,
    QSpinBox,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)

from maxchat.ui.shadow_dialog import ShadowDialog

_MAX_ROWS = 6000  # don't try to render more than this many rows (all are still kept for export)


class ChannelListDialog(ShadowDialog):
    def __init__(self, parent, net, join_cb) -> None:
        super().__init__(parent)
        self._net = net
        self._join_cb = join_cb
        self._rows: list[tuple[str, int, str]] = []
        self._cleaned = False
        self.setWindowTitle(f"Channel List — {net.name}")
        self.set_autosize(66, 26)  # font-relative sizing
        root = QVBoxLayout(self.body)

        flt = QHBoxLayout()
        flt.addWidget(QLabel("Min users:"))
        self.min_users = QSpinBox()
        self.min_users.setRange(0, 1000000)
        self.min_users.setValue(3)
        flt.addWidget(self.min_users)
        flt.addWidget(QLabel("Name:"))
        self.name_filter = QLineEdit()
        self.name_filter.setPlaceholderText("contains…")
        self.name_filter.textChanged.connect(self._apply_filter)
        flt.addWidget(self.name_filter, 1)
        self.refresh_btn = QPushButton("Refresh /list")
        self.refresh_btn.clicked.connect(self._request)
        flt.addWidget(self.refresh_btn)
        root.addLayout(flt)

        self.table = QTableWidget(0, 3)
        self.table.setHorizontalHeaderLabels(["Channel", "Users", "Topic"])
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.setSortingEnabled(True)
        self.table.verticalHeader().setVisible(False)
        self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
        self.table.setColumnWidth(0, 210)
        self.table.setColumnWidth(1, 70)
        self.table.itemDoubleClicked.connect(self._join_selected)
        root.addWidget(self.table, 1)

        self.status = QLabel("")
        root.addWidget(self.status)

        bar = QHBoxLayout()
        join = QPushButton("Join")
        join.clicked.connect(self._join_selected)
        bar.addWidget(join)
        bar.addStretch(1)
        csv_btn = QPushButton("Export CSV…")
        csv_btn.clicked.connect(self._export_csv)
        bar.addWidget(csv_btn)
        copy_btn = QPushButton("Copy")
        copy_btn.clicked.connect(self._copy)
        bar.addWidget(copy_btn)
        close = QPushButton("Close")
        close.clicked.connect(self.reject)
        bar.addWidget(close)
        root.addLayout(bar)

        net.client.listReply.connect(self._on_reply)
        net.client.listEnd.connect(self._on_end)
        self.finished.connect(self._cleanup)
        if net.connected:
            self._request()
        else:
            self.status.setText("Not connected.")

    # ---- fetch ------------------------------------------------------------
    def _request(self) -> None:
        self._rows = []
        self.table.setSortingEnabled(False)
        self.table.setRowCount(0)
        self.refresh_btn.setEnabled(False)
        self.status.setText("Fetching channel list…")
        mn = self.min_users.value()
        if not self._net.connected or not self._net.client.list_channels(f">{mn - 1}" if mn > 0 else ""):
            self.status.setText("Not connected.")
            self.refresh_btn.setEnabled(True)
            self.table.setSortingEnabled(True)

    def _on_reply(self, channel: str, users: int, topic: str) -> None:
        self._rows.append((channel, users, topic))
        if self.table.rowCount() < _MAX_ROWS:
            self._add_row(channel, users, topic)
        if len(self._rows) % 200 == 0:
            self.status.setText(f"Fetching… {len(self._rows)} channels")

    def _on_end(self) -> None:
        self.table.setSortingEnabled(True)
        self.refresh_btn.setEnabled(True)
        self._set_status()

    def _set_status(self) -> None:
        shown = self.table.rowCount()
        extra = f" — showing {shown} of {len(self._rows)}" if shown < len(self._rows) else ""
        self.status.setText(f"{len(self._rows)} channels{extra}. Double-click to join.")

    # ---- table ------------------------------------------------------------
    def _add_row(self, channel: str, users: int, topic: str) -> None:
        nf = self.name_filter.text().strip().lower()
        if nf and nf not in channel.lower():
            return
        r = self.table.rowCount()
        self.table.insertRow(r)
        self.table.setItem(r, 0, QTableWidgetItem(channel))
        u = QTableWidgetItem()
        u.setData(Qt.ItemDataRole.DisplayRole, int(users))  # integer → numeric sort
        self.table.setItem(r, 1, u)
        self.table.setItem(r, 2, QTableWidgetItem(topic))

    def _apply_filter(self) -> None:
        self.table.setSortingEnabled(False)
        self.table.setRowCount(0)
        for ch, u, t in self._rows:
            if self.table.rowCount() >= _MAX_ROWS:
                break
            self._add_row(ch, u, t)
        self.table.setSortingEnabled(True)
        self._set_status()

    def _selected_channel(self):
        r = self.table.currentRow()
        it = self.table.item(r, 0) if r >= 0 else None
        return it.text() if it else None

    def _join_selected(self, *_) -> None:
        ch = self._selected_channel()
        if ch:
            self._join_cb(ch)
            self.accept()

    # ---- export -----------------------------------------------------------
    def _export_csv(self) -> None:
        path, _ = QFileDialog.getSaveFileName(self, "Export channel list",
                                              f"{self._net.name}-channels.csv", "CSV (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                w.writerow(["Channel", "Users", "Topic"])
                w.writerows(self._rows)
            self.status.setText(f"Saved {len(self._rows)} channels to {path}")
        except OSError as e:
            self.status.setText(f"Save failed: {e}")

    def _copy(self) -> None:
        text = "\n".join(["Channel\tUsers\tTopic"] + [f"{c}\t{u}\t{t}" for c, u, t in self._rows])
        QGuiApplication.clipboard().setText(text)
        self.status.setText(f"Copied {len(self._rows)} channels to the clipboard")

    def _cleanup(self, *_args) -> None:
        if self._cleaned:
            return
        self._cleaned = True
        for sig, slot in ((self._net.client.listReply, self._on_reply),
                          (self._net.client.listEnd, self._on_end)):
            try:
                sig.disconnect(slot)
            except (RuntimeError, TypeError):
                pass

    def closeEvent(self, event) -> None:
        self._cleanup()
        super().closeEvent(event)
