"""Scripts manager — load / unload / reload Python scripts, and open the scripts folder."""

from __future__ import annotations

from PySide6.QtCore import QUrl
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import (
    QDialogButtonBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QPushButton,
    QVBoxLayout,
)

from maxchat.ui.shadow_dialog import ShadowDialog


class ScriptsDialog(ShadowDialog):
    def __init__(self, parent=None, *, manager) -> None:
        super().__init__(parent)
        self.setWindowTitle("Scripts")
        self.set_autosize(54, 22)  # font-relative sizing
        self._m = manager

        v = QVBoxLayout(self.body)
        v.addWidget(QLabel(
            f"Python scripts in <tt>{manager.dir()}</tt>. Hooks: <tt>on_message · on_join · "
            "on_command · on_load</tt>. <b>Only run scripts you trust</b> — they have full Python."))
        self.list = QListWidget()
        v.addWidget(self.list, 1)
        self._refresh()

        row = QHBoxLayout()
        for label, slot in (("Load file…", self._load), ("Reload", self._reload),
                            ("Unload", self._unload), ("Open folder", self._open_folder)):
            b = QPushButton(label)
            b.clicked.connect(slot)
            row.addWidget(b)
        v.addLayout(row)

        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Close)
        bb.rejected.connect(self.reject)
        bb.accepted.connect(self.accept)
        v.addWidget(bb)

    def _refresh(self) -> None:
        self.list.clear()
        self.list.addItems(sorted(self._m.scripts))

    def _selected(self) -> str:
        it = self.list.currentItem()
        return it.text() if it else ""

    def _load(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Load script", self._m.dir(), "Python (*.py)")
        if path:
            self._m.load(path)
            self._refresh()

    def _reload(self) -> None:
        name = self._selected()
        if name:
            self._m.reload(name)
            self._refresh()

    def _unload(self) -> None:
        name = self._selected()
        if name:
            self._m.unload(name)
            self._refresh()

    def _open_folder(self) -> None:
        QDesktopServices.openUrl(QUrl.fromLocalFile(self._m.dir()))
