"""Channel ban-list editor — view/add/remove +b masks for a channel.

Requests the list with ``MODE #chan +b`` (the server answers with RPL_BANLIST 367 lines, then
RPL_ENDOFBANLIST 368). Add/remove send ``MODE #chan +b mask`` / ``-b mask`` — the server enforces
whether you're allowed (you need channel-op). After a change we re-request so the view reflects reality.
"""

from __future__ import annotations

from PySide6.QtCore import QTimer
from PySide6.QtWidgets import (
    QHBoxLayout,
    QListWidget,
    QListWidgetItem,
    QPushButton,
    QVBoxLayout,
)

from maxchat.ui import shadow_message
from maxchat.ui.shadow_dialog import ShadowDialog


class BanListDialog(ShadowDialog):
    def __init__(self, parent, net, channel: str) -> None:
        super().__init__(parent)
        self.setWindowTitle(f"Ban list — {channel}")
        self.set_autosize(60, 22)  # font-relative sizing
        self._net = net
        self._chan = channel

        v = QVBoxLayout(self.body)
        self.list = QListWidget()
        v.addWidget(self.list, 1)

        row = QHBoxLayout()
        for label, slot in (("Add ban…", self._add), ("Remove", self._remove),
                            ("Refresh", self._refresh)):
            b = QPushButton(label)
            b.clicked.connect(slot)
            row.addWidget(b)
        row.addStretch(1)
        close = QPushButton("Close")
        close.clicked.connect(self.accept)
        row.addWidget(close)
        v.addLayout(row)

        net.client.banList.connect(self._on_ban)
        net.client.banListEnd.connect(self._on_end)
        self.finished.connect(self._cleanup)  # drop the signal links when the dialog closes
        self._refresh()

    def _cleanup(self, *_):
        for sig, slot in ((self._net.client.banList, self._on_ban),
                          (self._net.client.banListEnd, self._on_end)):
            try:
                sig.disconnect(slot)
            except (RuntimeError, TypeError):
                pass

    def _refresh(self) -> None:
        self.list.clear()
        self._net.client.send_raw(f"MODE {self._chan} +b")

    def _on_ban(self, chan: str, mask: str, setter: str) -> None:
        if chan != self._chan:
            return
        item = QListWidgetItem(f"{mask}      (set by {setter})" if setter else mask)
        item.setData(0x0100, mask)  # Qt.UserRole — the bare mask for removal
        self.list.addItem(item)

    def _on_end(self, chan: str) -> None:
        if chan == self._chan and self.list.count() == 0:
            self.list.addItem("(no bans set)")

    def _add(self) -> None:
        mask, ok = shadow_message.get_text(self, "Add ban", "Ban mask (nick!user@host):")
        if ok and mask.strip():
            self._net.client.send_raw(f"MODE {self._chan} +b {mask.strip()}")
            QTimer.singleShot(600, self._refresh)

    def _remove(self) -> None:
        item = self.list.currentItem()
        mask = item.data(0x0100) if item is not None else None
        if mask:
            self._net.client.send_raw(f"MODE {self._chan} -b {mask}")
            QTimer.singleShot(600, self._refresh)
