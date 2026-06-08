"""Command-alias editor — define your own ``/commands``.

Each alias maps a name to a command template that's expanded and run. Placeholders: ``$1 $2`` (args),
``$1-`` (the rest from arg N), ``$*`` (all args), ``$me`` (your nick), ``$chan`` (current channel).
e.g. ``slap`` → ``me slaps $1- around a bit with a large trout`` makes ``/slap bob`` a `/me` action.
"""

from __future__ import annotations

from collections.abc import Callable

from PySide6.QtWidgets import (
    QDialogButtonBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)

from maxchat.ui.shadow_dialog import ShadowDialog


class AliasEditorDialog(ShadowDialog):
    def __init__(self, parent=None, *, aliases: dict, save_cb: Callable[[dict], None]) -> None:
        super().__init__(parent)
        self.setWindowTitle("Command Aliases")
        self.set_autosize(64, 22)  # font-relative sizing
        self._save = save_cb

        v = QVBoxLayout(self.body)
        v.addWidget(QLabel(
            "Type <b>/name args</b> to run an alias. Placeholders: <tt>$1 $2</tt> = args · "
            "<tt>$1-</tt> = the rest · <tt>$*</tt> = all · <tt>$me</tt> = your nick · "
            "<tt>$chan</tt> = current channel."))
        self.table = QTableWidget(0, 2)
        self.table.setHorizontalHeaderLabels(["/alias", "Command (no leading /)"])
        self.table.verticalHeader().setVisible(False)
        hh = self.table.horizontalHeader()
        hh.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        hh.setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        v.addWidget(self.table, 1)
        for name, tmpl in sorted((aliases or {}).items()):
            self._add_row(str(name), str(tmpl))

        row = QHBoxLayout()
        add = QPushButton("Add")
        add.clicked.connect(lambda: self._add_row("", ""))
        rm = QPushButton("Remove")
        rm.clicked.connect(self._remove)
        row.addStretch(1)
        row.addWidget(add)
        row.addWidget(rm)
        v.addLayout(row)

        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        bb.accepted.connect(self._accept)
        bb.rejected.connect(self.reject)
        v.addWidget(bb)

    def _add_row(self, name: str, tmpl: str) -> None:
        r = self.table.rowCount()
        self.table.insertRow(r)
        self.table.setItem(r, 0, QTableWidgetItem(name))
        self.table.setItem(r, 1, QTableWidgetItem(tmpl))

    def _remove(self) -> None:
        r = self.table.currentRow()
        if r >= 0:
            self.table.removeRow(r)

    def _accept(self) -> None:
        out: dict = {}
        for r in range(self.table.rowCount()):
            n, t = self.table.item(r, 0), self.table.item(r, 1)
            name = (n.text().strip().lstrip("/").lower() if n else "")
            tmpl = (t.text().strip() if t else "")
            if name and tmpl:
                out[name] = tmpl
        self._save(out)
        self.accept()
