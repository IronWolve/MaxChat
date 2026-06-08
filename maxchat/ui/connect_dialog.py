"""Quick Connect dialog — one-off server/identity settings, opened from Server ▸ Quick Connect…

Connection details live here, not in a toolbar. Channels accepts several (space/comma separated)
and is optional. Password (optional) is used for SASL login, falling back to NickServ IDENTIFY.
"""

from __future__ import annotations

from PySide6.QtWidgets import (
    QCheckBox,
    QDialogButtonBox,
    QFormLayout,
    QLabel,
    QLineEdit,
    QSpinBox,
)

from maxchat.ui.shadow_dialog import ShadowDialog


class ConnectDialog(ShadowDialog):
    def __init__(
        self,
        parent=None,
        *,
        host: str = "irc.libera.chat",
        port: int = 6697,
        tls: bool = True,
        nick: str = "comicfan",
        channels: str = "#maxchat",
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle("Connect to IRC")
        form = QFormLayout(self.body)

        self.host = QLineEdit(host)
        self.port = QSpinBox()
        self.port.setRange(1, 65535)
        self.port.setValue(port)
        self.tls = QCheckBox("Use SSL/TLS")
        self.tls.setChecked(tls)
        self.nick = QLineEdit(nick)
        self.password = QLineEdit()
        self.password.setEchoMode(QLineEdit.EchoMode.Password)
        self.password.setPlaceholderText("optional — SASL / NickServ")
        self.server_pass = QLineEdit()
        self.server_pass.setEchoMode(QLineEdit.EchoMode.Password)
        self.server_pass.setPlaceholderText("optional — bouncer/private-server PASS")
        self.server_pass.setToolTip(
            "Sent as IRC PASS before registration. Use this for ZNC/soju/private-server passwords."
        )
        self.channels = QLineEdit(channels)
        self.channels.setPlaceholderText("#chan #other  (space/comma separated; optional)")
        note = QLabel("SOCKS5 / HTTP CONNECT proxy settings are in Server ▸ Server List ▸ Add/Edit.")
        note.setWordWrap(True)

        form.addRow("Server:", self.host)
        form.addRow("Port:", self.port)
        form.addRow("", self.tls)
        form.addRow("Nickname:", self.nick)
        form.addRow("NickServ password:", self.password)
        form.addRow("Server PASS / bouncer:", self.server_pass)
        form.addRow("Channels:", self.channels)
        form.addRow(note)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        form.addRow(buttons)

    def values(self) -> dict:
        raw = self.channels.text().replace(",", " ").split()
        channels = [c if c.startswith(("#", "&")) else "#" + c for c in raw]
        return {
            "host": self.host.text().strip(),
            "port": self.port.value(),
            "tls": self.tls.isChecked(),
            "nick": self.nick.text().strip() or "comicfan",
            "password": self.password.text() or None,
            "server_pass": self.server_pass.text() or None,
            "channels": channels,
        }
