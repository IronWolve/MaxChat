"""Friends / notify-list editor — nicks the app watches for online/offline.

The window polls each connected network (ISON) and alerts (per the notification setting) when a listed
nick comes online or goes offline. Plain nicks, one per entry. Saved on OK via the callback.
"""

from __future__ import annotations

from collections.abc import Callable

from PySide6.QtWidgets import (
    QDialogButtonBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QPushButton,
    QVBoxLayout,
)

from maxchat.ui.shadow_dialog import ShadowDialog


class FriendsDialog(ShadowDialog):
    def __init__(self, parent=None, *, friends: list[str], save_cb: Callable[[list], None]) -> None:
        super().__init__(parent)
        self.setWindowTitle("Friends / Notify List")
        self.set_autosize(44, 20)  # font-relative sizing
        self._save = save_cb

        v = QVBoxLayout(self.body)
        v.addWidget(QLabel("Nicks to watch — you'll be alerted when they come online or go offline."))
        self.list = QListWidget()
        self.list.addItems(friends)
        v.addWidget(self.list, 1)

        row = QHBoxLayout()
        self.entry = QLineEdit()
        self.entry.setPlaceholderText("nick")
        self.entry.returnPressed.connect(self._add)
        add = QPushButton("Add")
        add.clicked.connect(self._add)
        rm = QPushButton("Remove")
        rm.clicked.connect(self._remove)
        row.addWidget(self.entry, 1)
        row.addWidget(add)
        row.addWidget(rm)
        v.addLayout(row)

        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        bb.accepted.connect(self._accept)
        bb.rejected.connect(self.reject)
        v.addWidget(bb)

    def _add(self) -> None:
        nick = self.entry.text().strip()
        if nick and not any(self.list.item(i).text().lower() == nick.lower()
                            for i in range(self.list.count())):
            self.list.addItem(nick)
        self.entry.clear()

    def _remove(self) -> None:
        for item in self.list.selectedItems() or ([self.list.currentItem()] if self.list.currentItem() else []):
            self.list.takeItem(self.list.row(item))

    def _accept(self) -> None:
        self._save([self.list.item(i).text() for i in range(self.list.count())])
        self.accept()
