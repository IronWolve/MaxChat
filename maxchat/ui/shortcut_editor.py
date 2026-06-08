"""Keyboard-shortcut editor — rebind the chat navigation keys.

Shows each rebindable action with a ``QKeySequenceEdit`` to capture a new combo. Save writes the
overrides to the ``shortcuts`` pref and calls back so the live bindings update; Reset clears an
override back to its default.
"""

from __future__ import annotations

from PySide6.QtGui import QKeySequence
from PySide6.QtWidgets import (
    QHBoxLayout,
    QKeySequenceEdit,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from maxchat import config
from maxchat.ui import shadow_message
from maxchat.ui.shadow_dialog import ShadowDialog


class ShortcutEditorDialog(ShadowDialog):
    def __init__(self, parent, actions, on_apply) -> None:
        super().__init__(parent)
        self.setWindowTitle("Keyboard Shortcuts")
        self.set_autosize(46, 24)  # font-relative sizing
        self._actions = actions          # [(id, label, default), …]
        self._on_apply = on_apply
        self._edits: dict = {}

        v = QVBoxLayout(self.body)
        overrides = dict(config.pref("shortcuts") or {})
        for aid, label, default in actions:
            row = QHBoxLayout()
            row.addWidget(QLabel(label), 1)
            edit = QKeySequenceEdit(overrides.get(aid) or default)
            self._edits[aid] = edit
            row.addWidget(edit)
            reset = QPushButton("Reset")
            reset.setToolTip(f"Back to {default}")
            reset.clicked.connect(lambda _c=False, e=edit, d=default: e.setKeySequence(d))
            row.addWidget(reset)
            holder = QWidget()
            holder.setLayout(row)
            v.addWidget(holder)

        v.addWidget(QLabel("Alt+1–9 (chat N) and Alt+` (activity) are fixed."))
        buttons = QHBoxLayout()
        buttons.addStretch(1)
        save = QPushButton("Save")
        save.clicked.connect(self._save)
        cancel = QPushButton("Cancel")
        cancel.clicked.connect(self.reject)
        buttons.addWidget(save)
        buttons.addWidget(cancel)
        v.addLayout(buttons)

    def _save(self) -> None:
        overrides = {}
        seen: dict[str, str] = {}
        reserved = {f"Alt+{i}" for i in range(1, 10)}
        reserved.update({"Alt+`", "Ctrl+S", "Ctrl+J", "Ctrl+P", "Ctrl+M", "F1", "F11"})
        for aid, _label, default in self._actions:
            label = next((lbl for a, lbl, _d in self._actions if a == aid), aid)
            seq = self._edits[aid].keySequence().toString(QKeySequence.SequenceFormat.PortableText)
            if not seq:
                continue
            if seq in reserved:
                shadow_message.warning(self, "Keyboard Shortcuts", f"{seq} is reserved and cannot be rebound.")
                return
            if seq in seen:
                shadow_message.warning(
                    self, "Keyboard Shortcuts",
                    f"{seq} is already assigned to {seen[seq]} and {label}.",
                )
                return
            seen[seq] = label
            if seq and seq != default:  # only store real overrides
                overrides[aid] = seq
        config.set_pref("shortcuts", overrides)
        self._on_apply()
        self.accept()
