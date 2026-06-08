"""Ignore list editor — manage the masks whose messages are hidden.

A mask is glob over ``nick!user@host`` (``*`` = any, ``?`` = one char). A bare nick is expanded to
``nick!*@*``. Matching senders' channel/private messages, notices and CTCPs are dropped before they
reach the UI (see ``IRCClient.set_ignores``). Saved on OK via the callback.
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


def normalize_mask(mask: str) -> str:
    """A bare nick becomes ``nick!*@*``; anything with ``!``/``@`` is left as the user typed it."""
    mask = mask.strip()
    if not mask:
        return ""
    return mask if ("!" in mask or "@" in mask) else f"{mask}!*@*"


class IgnoreListDialog(ShadowDialog):
    def __init__(self, parent=None, *, ignores: list[str], save_cb: Callable[[list], None]) -> None:
        super().__init__(parent)
        self.setWindowTitle("Ignore List")
        self.set_autosize(50, 20)  # font-relative sizing
        self._save = save_cb

        v = QVBoxLayout(self.body)
        _lbl = QLabel("Hidden senders — masks over <b>nick!user@host</b> (e.g. "
                      "<tt>spammer!*@*</tt>, <tt>*!*@bad.host</tt>):")
        _lbl.setWordWrap(True)
        v.addWidget(_lbl)
        self.list = QListWidget()
        self.list.addItems(ignores)
        v.addWidget(self.list, 1)

        row = QHBoxLayout()
        self.entry = QLineEdit()
        self.entry.setPlaceholderText("nick  or  nick!user@host")
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
        mask = normalize_mask(self.entry.text())
        if mask and not self._has(mask):
            self.list.addItem(mask)
        self.entry.clear()

    def _has(self, mask: str) -> bool:
        return any(self.list.item(i).text().lower() == mask.lower() for i in range(self.list.count()))

    def _remove(self) -> None:
        for item in self.list.selectedItems() or ([self.list.currentItem()] if self.list.currentItem() else []):
            self.list.takeItem(self.list.row(item))

    def _accept(self) -> None:
        masks = [self.list.item(i).text() for i in range(self.list.count())]
        self._save(masks)
        self.accept()
