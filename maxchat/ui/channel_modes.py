"""Channel Modes popup — a labelled "settings" panel for the current channel's modes.

Opened from the "Channel Modes…" button above the user list. Each toggle reflects the channel's
current mode and applies live via a callback (which sends the MODE command). Simple flags are
checkboxes; the key (+k) and user-limit (+l) take a value. The server enforces op privileges — a
non-op's changes are simply rejected (the error shows in the channel).
"""

from __future__ import annotations

from collections.abc import Callable

from PySide6.QtWidgets import (
    QCheckBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
)

from maxchat.ui.shadow_dialog import ShadowDialog

# (mode letter, label, tooltip) — the common channel flags, in a sensible order.
SIMPLE_MODES = [
    ("t", "Topic locked", "Only channel operators can change the topic"),
    ("n", "No external messages", "You must be in the channel to send messages to it"),
    ("m", "Moderated", "Only voiced (+v) users and operators may talk"),
    ("i", "Invite only", "Users can only join if invited"),
    ("s", "Secret", "Hide the channel from the public list and from whois"),
    ("p", "Private", "Don't advertise the channel"),
]


class ChannelModesDialog(ShadowDialog):
    def __init__(self, parent=None, *, channel: str, net_name: str,
                 flags: set, key: str, limit: int, apply_cb: Callable[[str], None]) -> None:
        super().__init__(parent)
        self.setWindowTitle(f"Channel Modes — {channel}")
        self.set_autosize(40, 0)  # font-relative width; height follows the form content
        self._apply = apply_cb
        self._key = key

        v = QVBoxLayout(self.body)
        v.addWidget(QLabel(f"<b>{channel}</b>  ·  {net_name}"))

        self._boxes: dict[str, QCheckBox] = {}
        for m, label, tip in SIMPLE_MODES:
            cb = QCheckBox(f"{label}   (+{m})")
            cb.setToolTip(tip)
            cb.setChecked(m in flags)
            cb.toggled.connect(lambda on, mm=m: self._apply(f"+{mm}" if on else f"-{mm}"))
            v.addWidget(cb)
            self._boxes[m] = cb

        # Key (+k) — a password required to join
        krow = QHBoxLayout()
        self.key_on = QCheckBox("Key (+k)")
        self.key_on.setChecked(bool(key))
        self.key_on.toggled.connect(self._toggle_key)
        self.key_edit = QLineEdit(key)
        self.key_edit.setPlaceholderText("password")
        key_set = QPushButton("Set")
        key_set.clicked.connect(self._set_key)
        krow.addWidget(self.key_on)
        krow.addWidget(self.key_edit, 1)
        krow.addWidget(key_set)
        v.addLayout(krow)

        # User limit (+l)
        lrow = QHBoxLayout()
        self.lim_on = QCheckBox("User limit (+l)")
        self.lim_on.setChecked(limit > 0)
        self.lim_on.toggled.connect(self._toggle_limit)
        self.lim_spin = QSpinBox()
        self.lim_spin.setRange(1, 99999)
        self.lim_spin.setValue(limit or 50)
        lim_set = QPushButton("Set")
        lim_set.clicked.connect(self._set_limit)
        lrow.addWidget(self.lim_on)
        lrow.addWidget(self.lim_spin, 1)
        lrow.addWidget(lim_set)
        v.addLayout(lrow)

        note = QLabel("You must be a channel operator (@) for changes to take effect.")
        note.setWordWrap(True)
        v.addWidget(note)

        row = QHBoxLayout()
        row.addStretch(1)
        close = QPushButton("Close")
        close.clicked.connect(self.accept)
        row.addWidget(close)
        v.addLayout(row)

    # ---- key / limit (value modes) ---------------------------------------
    def _toggle_key(self, on: bool) -> None:
        if on:
            k = self.key_edit.text().strip()
            if k:
                self._key = k
                self._apply(f"+k {k}")
            else:
                self.key_on.blockSignals(True)
                self.key_on.setChecked(False)
                self.key_on.blockSignals(False)
                self.key_edit.setFocus()
        else:
            self._apply(f"-k {self._key or '*'}")  # most servers want the key to clear it

    def _set_key(self) -> None:
        k = self.key_edit.text().strip()
        if not k:
            return
        self._key = k
        self.key_on.blockSignals(True)
        self.key_on.setChecked(True)
        self.key_on.blockSignals(False)
        self._apply(f"+k {k}")

    def _toggle_limit(self, on: bool) -> None:
        self._apply(f"+l {self.lim_spin.value()}" if on else "-l")

    def _set_limit(self) -> None:
        self.lim_on.blockSignals(True)
        self.lim_on.setChecked(True)
        self.lim_on.blockSignals(False)
        self._apply(f"+l {self.lim_spin.value()}")
