"""Server address book — a saved list of networks (Server ▸ Server List…), each with its own
host/port/SSL, **nick**, **username** (ident), password, and auto-join channels. Stored in config.
"""

from __future__ import annotations

import copy
import re
from urllib.parse import urlparse

from PySide6.QtCore import QElapsedTimer, Qt, QUrl, Signal
from PySide6.QtGui import QDesktopServices, QTextCursor
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialogButtonBox,
    QFormLayout,
    QLabel,
    QHBoxLayout,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QPlainTextEdit,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from maxchat import config
from maxchat.ui.shadow_dialog import ShadowDialog
from maxchat.ui.tooltips import info_tip


class NetworkEditDialog(ShadowDialog):
    def __init__(self, parent=None, net: dict | None = None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Network")
        self.set_autosize(70, 32)
        net = net or {}
        root = QVBoxLayout(self.body)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        page = QWidget()
        form = QFormLayout(page)
        scroll.setWidget(page)
        root.addWidget(scroll, 1)

        def section(title: str) -> None:
            label = QLabel(title)
            label.setStyleSheet("font-weight: 700; margin-top: 8px;")
            form.addRow(label)

        self.name = QLineEdit(net.get("name", ""))
        self.host = QLineEdit(net.get("host", ""))
        self.port = QSpinBox()
        self.port.setRange(1, 65535)
        self.port.setValue(int(net.get("port", 6697)))
        self.tls = QCheckBox("SSL/TLS")
        self.tls.setChecked(bool(net.get("tls", True)))
        self.accept_cert = QCheckBox("Accept unsigned")
        self.accept_cert.setChecked(bool(net.get("accept_invalid_cert", False)))
        self.accept_cert.setEnabled(self.tls.isChecked())
        self.accept_cert.setToolTip(
            info_tip("Allow invalid or self-signed TLS certificates for this network. Leave off unless needed.")
        )
        self.tls.toggled.connect(self.accept_cert.setEnabled)
        tls_row = QWidget()
        tls_layout = QHBoxLayout(tls_row)
        tls_layout.setContentsMargins(0, 0, 0, 0)
        tls_layout.addWidget(self.tls)
        tls_layout.addWidget(self.accept_cert)
        tls_layout.addStretch(1)
        self.nick = QLineEdit(net.get("nick", ""))
        self.realname = QLineEdit(net.get("realname", ""))
        self.realname.setPlaceholderText("optional — shown in /whois (defaults to your nick)")
        self.username = QLineEdit(net.get("username", ""))
        self.username.setPlaceholderText("optional ident / username")
        self.account = QLineEdit(net.get("account", ""))
        self.account.setPlaceholderText("NickServ account — leave blank to use your nick")
        self.password = QLineEdit(net.get("password", ""))
        self.password.setEchoMode(QLineEdit.EchoMode.Password)
        self.password.setPlaceholderText("NickServ password (SASL, with /msg NickServ IDENTIFY fallback)")
        self.server_pass = QLineEdit(net.get("server_pass", ""))
        self.server_pass.setEchoMode(QLineEdit.EchoMode.Password)
        self.server_pass.setPlaceholderText("server PASS — bouncers/private servers (ZNC, soju, etc.)")
        self.server_pass.setToolTip(
            info_tip(
                "Sent as IRC PASS before registration. Use this for ZNC/soju/private-server passwords; "
                "keep NickServ/SASL credentials in the NickServ fields above."
            )
        )
        self.allow_insecure_auth = QCheckBox("Allow plaintext auth")
        self.allow_insecure_auth.setChecked(bool(net.get("allow_insecure_auth", False)))
        self.allow_insecure_auth.setToolTip(
            info_tip(
                "Send PASS/SASL/NickServ passwords without SSL/TLS. Leave off unless this network has "
                "no TLS and you accept the risk."
            )
        )
        self.autostart = QCheckBox("Connect on startup")
        self.autostart.setChecked(bool(net.get("autoconnect", False)))
        self.autostart.setToolTip(
            info_tip(
                "Auto-connect this network at launch — in Server-List order, one network fully connecting "
                "before the next starts. The master on/off switch is in the Server List."
            )
        )
        self.channels = QLineEdit(net.get("channels", ""))
        self.channels.setPlaceholderText("#chan #other  (space/comma)")
        srv = net.get("servers", [])
        if isinstance(srv, list):
            server_lines = [self._server_spec(self.host.text(), self.port.value(), self.tls.isChecked()), *srv]
        else:
            server_lines = [self._server_spec(self.host.text(), self.port.value(), self.tls.isChecked())]
            server_lines.extend(s for s in re.split(r"[\s,]+", str(srv)) if s)
        self.servers = QPlainTextEdit("\n".join(s for s in server_lines if s))
        self.servers.setPlaceholderText("one server per line")
        self.servers.setToolTip(
            "Servers are tried top to bottom. Use host:port or host:+port for SSL/TLS."
        )
        self.servers.setFixedHeight(self.servers.fontMetrics().lineSpacing() * 2 + 18)
        server_box = QWidget()
        server_layout = QHBoxLayout(server_box)
        server_layout.setContentsMargins(0, 0, 0, 0)
        move_buttons = QWidget()
        move_layout = QVBoxLayout(move_buttons)
        move_layout.setContentsMargins(0, 0, 0, 0)
        move_layout.setSpacing(2)
        self.server_up = QToolButton()
        self.server_up.setText("↑")
        self.server_up.setToolTip("Move this server up")
        self.server_up.clicked.connect(lambda: self._move_server_line(-1))
        self.server_down = QToolButton()
        self.server_down.setText("↓")
        self.server_down.setToolTip("Move this server down")
        self.server_down.clicked.connect(lambda: self._move_server_line(1))
        move_layout.addWidget(self.server_up)
        move_layout.addWidget(self.server_down)
        move_layout.addStretch(1)
        server_layout.addWidget(move_buttons)
        server_layout.addWidget(self.servers, 1)
        self.website = QLineEdit(str(net.get("website") or ""))
        self.website.setPlaceholderText("https://network.example/")
        self.website.setToolTip("Network homepage. Used by the Server List homepage button.")
        self.proxy_type = QComboBox()
        self.proxy_type.addItem("No proxy", "none")
        self.proxy_type.addItem("SOCKS5", "socks5")
        self.proxy_type.addItem("HTTP CONNECT", "http")
        proxy_type = str(net.get("proxy_type") or "none").lower()
        pidx = self.proxy_type.findData(proxy_type)
        self.proxy_type.setCurrentIndex(pidx if pidx >= 0 else 0)
        self.proxy_host = QLineEdit(str(net.get("proxy_host") or ""))
        self.proxy_host.setPlaceholderText("127.0.0.1")
        self.proxy_port = QSpinBox()
        self.proxy_port.setRange(1, 65535)
        self.proxy_port.setValue(int(net.get("proxy_port") or 1080))
        proxy_server = QWidget()
        proxy_server_layout = QHBoxLayout(proxy_server)
        proxy_server_layout.setContentsMargins(0, 0, 0, 0)
        proxy_server_layout.addWidget(self.proxy_host, 1)
        proxy_server_layout.addWidget(self.proxy_port)
        self.proxy_user = QLineEdit(str(net.get("proxy_username") or ""))
        self.proxy_user.setPlaceholderText("optional")
        self.proxy_password = QLineEdit(str(net.get("proxy_password") or ""))
        self.proxy_password.setEchoMode(QLineEdit.EchoMode.Password)
        self.proxy_password.setPlaceholderText("optional")
        self.proxy_type.currentIndexChanged.connect(self._sync_proxy_fields)
        perf = net.get("perform", [])
        self.perform = QPlainTextEdit("\n".join(perf) if isinstance(perf, list) else str(perf))
        self.perform.setPlaceholderText("commands run on connect, one per line — e.g. /msg NickServ identify …")
        self.perform.setFixedHeight(64)
        section("Connection")
        for label, w in (
            ("Name:", self.name), ("Server(s):", server_box), ("Port:", self.port), ("", tls_row),
            ("Homepage:", self.website),
        ):
            form.addRow(label, w)
        section("Startup")
        for label, w in (
            ("", self.autostart), ("Channels:", self.channels), ("Perform:", self.perform),
        ):
            form.addRow(label, w)
        section("Identity")
        for label, w in (
            ("Nickname:", self.nick), ("Real name:", self.realname), ("Username:", self.username),
        ):
            form.addRow(label, w)
        section("Authentication / Bouncers")
        for label, w in (
            ("NickServ account:", self.account), ("NickServ password:", self.password),
            ("Server PASS / bouncer:", self.server_pass), ("", self.allow_insecure_auth),
        ):
            form.addRow(label, w)
        section("Proxy")
        for label, w in (
            ("Proxy:", self.proxy_type), ("Proxy server:", proxy_server),
            ("Proxy username:", self.proxy_user), ("Proxy password:", self.proxy_password),
        ):
            form.addRow(label, w)
        self._sync_proxy_fields()
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        root.addWidget(buttons)

    def _sync_proxy_fields(self) -> None:
        enabled = self.proxy_type.currentData() != "none"
        for widget in (self.proxy_host, self.proxy_port, self.proxy_user, self.proxy_password):
            widget.setEnabled(enabled)

    def _move_server_line(self, delta: int) -> None:
        lines = self.servers.toPlainText().splitlines()
        if len(lines) < 2:
            return
        cursor = self.servers.textCursor()
        row = cursor.blockNumber()
        target = row + delta
        if not (0 <= row < len(lines) and 0 <= target < len(lines)):
            return
        lines[row], lines[target] = lines[target], lines[row]
        self.servers.setPlainText("\n".join(lines))
        block = self.servers.document().findBlockByNumber(target)
        cursor.setPosition(block.position())
        cursor.movePosition(QTextCursor.MoveOperation.EndOfBlock, QTextCursor.MoveMode.KeepAnchor)
        self.servers.setTextCursor(cursor)
        self.servers.setFocus()

    @staticmethod
    def _server_spec(host: str, port: int, tls: bool) -> str:
        host = host.strip()
        if not host:
            return ""
        return f"{host}:{'+' if tls else ''}{int(port)}"

    @staticmethod
    def _parse_server_spec(spec: str, fallback_port: int, fallback_tls: bool) -> tuple[str, int, bool]:
        host, port, tls = spec.strip(), int(fallback_port), bool(fallback_tls)
        for sep in (":", "/"):
            if sep in host:
                host, _, raw_port = host.partition(sep)
                raw_port = raw_port.strip()
                tls = raw_port.startswith("+")
                try:
                    port = int(raw_port.lstrip("+"))
                except ValueError:
                    port = int(fallback_port)
                break
        return host.strip(), port, tls

    def values(self) -> dict:
        proxy_type = str(self.proxy_type.currentData() or "none")
        server_specs = [s for s in re.split(r"[\s,]+", self.servers.toPlainText().strip()) if s]
        first_host = self.host.text().strip()
        first_port = self.port.value()
        first_tls = self.tls.isChecked()
        if server_specs:
            first_host, first_port, first_tls = self._parse_server_spec(
                server_specs[0], self.port.value(), self.tls.isChecked()
            )
        return {
            "name": self.name.text().strip() or first_host,
            "host": first_host,
            "port": first_port,
            "tls": first_tls,
            "accept_invalid_cert": self.accept_cert.isChecked(),
            "nick": self.nick.text().strip() or "comicfan",
            "realname": self.realname.text().strip(),
            "username": self.username.text().strip(),
            "account": self.account.text().strip(),
            "password": self.password.text(),
            "server_pass": self.server_pass.text(),
            "allow_insecure_auth": self.allow_insecure_auth.isChecked(),
            "autoconnect": self.autostart.isChecked(),
            "channels": self.channels.text().strip(),
            "website": self.website.text().strip(),
            "servers": server_specs[1:],
            "proxy_type": proxy_type,
            "proxy_host": self.proxy_host.text().strip() if proxy_type != "none" else "",
            "proxy_port": self.proxy_port.value() if proxy_type != "none" else 0,
            "proxy_username": self.proxy_user.text().strip() if proxy_type != "none" else "",
            "proxy_password": self.proxy_password.text() if proxy_type != "none" else "",
            "perform": [ln.rstrip() for ln in self.perform.toPlainText().splitlines() if ln.strip()],
        }


class ServerListDialog(ShadowDialog):
    connectRequested = Signal(dict)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Server List")
        self.set_autosize(78, 24)  # wide enough for network, server, and homepage URL
        self._nets = copy.deepcopy(config.pref("networks") or [])
        self._connect_on_start = bool(config.pref("connect_on_start"))
        self._jump_text = ""
        self._jump_timer = QElapsedTimer()

        root = QVBoxLayout(self.body)
        self.list = QListWidget()
        self.list.keyPressEvent = self._list_key_press
        self.list.itemDoubleClicked.connect(lambda *_: self._connect())
        root.addWidget(self.list)

        row = QHBoxLayout()
        for label, slot, tip in (
            ("Add…", self._add, "Add a network"),
            ("Edit…", self._edit, "Edit the selected network"),
            ("Remove", self._remove, "Remove the selected network"),
            ("↑", self._move_up, "Move up (earlier in the connect order)"),
            ("↓", self._move_down, "Move down (later in the connect order)"),
            ("Startup ⚡", self._toggle_start, "Toggle 'connect this network on startup'"),
        ):
            b = QPushButton(label)
            b.setToolTip(tip)
            b.clicked.connect(slot)
            row.addWidget(b)
        row.addStretch(1)
        root.addLayout(row)

        self.autoconnect = QCheckBox("Auto-connect ⚡ networks on startup — top-to-bottom, one at a time")
        self.autoconnect.setChecked(self._connect_on_start)
        self.autoconnect.setToolTip(
            info_tip(
                "Master switch. On launch, every network marked ⚡ (Startup) connects in this list's order — "
                "each one fully connects before the next starts. If none are marked, the top network connects "
                "(fallback behavior)."
            )
        )
        self.autoconnect.toggled.connect(lambda on: setattr(self, "_connect_on_start", bool(on)))
        root.addWidget(self.autoconnect)

        btns = QHBoxLayout()  # [Connect]      [Homepage]      [OK] [Cancel]
        self.connect_btn = QPushButton("Connect")
        self.connect_btn.setDefault(True)
        self.connect_btn.clicked.connect(self._connect)
        btns.addWidget(self.connect_btn)
        btns.addStretch(1)
        self.home_btn = QPushButton("Homepage")
        self.home_btn.setMinimumWidth(260)
        self.home_btn.setToolTip("Open the selected network's homepage")
        self.home_btn.clicked.connect(self._open_homepage)
        btns.addWidget(self.home_btn)
        btns.addStretch(1)
        ok = QPushButton("OK")
        ok.clicked.connect(self.accept)
        cancel = QPushButton("Cancel")
        cancel.clicked.connect(self.reject)
        btns.addWidget(ok)
        btns.addWidget(cancel)
        root.addLayout(btns)
        self.list.currentRowChanged.connect(lambda *_: self._sync_home_button())
        self._refresh(0 if self._nets else None)  # pre-select the top (last-connected) network

    def _refresh(self, select: int | None = None) -> None:
        self.list.clear()
        for n in self._nets:
            mark = "⚡ " if n.get("autoconnect") else "     "  # flag the startup networks
            extras = n.get("servers") if isinstance(n.get("servers"), list) else []
            extra_count = len(extras)
            suffix = f" + {extra_count} server{'s' if extra_count != 1 else ''}" if extra_count else ""
            url = self._homepage_url(n)
            url_suffix = f"   {self._homepage_label(url)}" if url else ""
            item = QListWidgetItem(
                f"{mark}{n.get('name') or n.get('host')}   "
                f"({n.get('host')}:{n.get('port')}{suffix}){url_suffix}"
            )
            if extra_count:
                item.setToolTip("Servers:\n" + "\n".join(
                    [f"{n.get('host')}:{n.get('port')}"] + [str(s) for s in extras]
                ) + (f"\n\nHomepage:\n{url}" if url else ""))
            elif url:
                item.setToolTip(f"Homepage:\n{url}")
            self.list.addItem(item)
        if select is not None and 0 <= select < len(self._nets):
            self.list.setCurrentRow(select)  # keep the row selected (so Connect works right after Edit)
        self._sync_home_button()

    def accept(self) -> None:
        self._save()
        super().accept()

    def _list_key_press(self, event) -> None:
        text = event.text()
        if len(text) == 1 and text.isalnum() and not event.modifiers() & (
            Qt.KeyboardModifier.ControlModifier
            | Qt.KeyboardModifier.AltModifier
            | Qt.KeyboardModifier.MetaModifier
        ):
            self._jump_to_prefix(text)
            event.accept()
            return
        QListWidget.keyPressEvent(self.list, event)

    def _jump_to_prefix(self, text: str) -> None:
        if not self._jump_timer.isValid() or self._jump_timer.elapsed() > 900:
            self._jump_text = ""
        self._jump_timer.restart()
        self._jump_text += text.lower()
        start = max(self._row(), 0)
        if len(self._jump_text) == 1:
            start = (start + 1) % max(len(self._nets), 1)
        if self._select_prefix(self._jump_text, start):
            return
        if len(self._jump_text) > 1:
            self._jump_text = text.lower()
            self._select_prefix(self._jump_text, start)

    def _select_prefix(self, prefix: str, start: int) -> bool:
        for offset in range(len(self._nets)):
            i = (start + offset) % len(self._nets)
            name = str(self._nets[i].get("name") or self._nets[i].get("host") or "").lower()
            if name.startswith(prefix):
                self.list.setCurrentRow(i)
                self.list.scrollToItem(self.list.item(i))
                return True
        return False

    @staticmethod
    def _homepage_url(net: dict) -> str:
        url = str(net.get("website") or "").strip()
        if url and not url.lower().startswith(("http://", "https://")):
            url = "https://" + url
        return url

    @staticmethod
    def _homepage_label(url: str) -> str:
        if len(url) <= 32:
            return url
        parsed = urlparse(url)
        if parsed.scheme and parsed.netloc:
            return f"{parsed.scheme}://{parsed.netloc}"
        return url

    def _sync_home_button(self) -> None:
        i = self._row()
        url = self._homepage_url(self._nets[i]) if i >= 0 else ""
        self.home_btn.setEnabled(bool(url))
        self.home_btn.setText(self._homepage_label(url) if url else "Homepage")
        self.home_btn.setToolTip(url or "No homepage saved for the selected network")

    def _open_homepage(self) -> None:
        i = self._row()
        if i < 0:
            return
        url = self._homepage_url(self._nets[i])
        if url:
            QDesktopServices.openUrl(QUrl(url))

    def _move(self, delta: int) -> None:
        i = self._row()
        j = i + delta
        if i < 0 or not (0 <= j < len(self._nets)):
            return
        self._nets[i], self._nets[j] = self._nets[j], self._nets[i]  # list order = connect order
        self._refresh(j)

    def _move_up(self) -> None:
        self._move(-1)

    def _move_down(self) -> None:
        self._move(1)

    def _toggle_start(self) -> None:
        i = self._row()
        if i < 0:
            return
        self._nets[i]["autoconnect"] = not self._nets[i].get("autoconnect", False)
        self._refresh(i)

    def _save(self) -> None:
        config.set_setting("networks", self._nets)
        config.set_pref("connect_on_start", self._connect_on_start)

    def _row(self) -> int:
        i = self.list.currentRow()
        return i if 0 <= i < len(self._nets) else -1

    def _add(self) -> None:
        dlg = NetworkEditDialog(self)
        if dlg.exec():
            self._nets.append(dlg.values())
            self._refresh(len(self._nets) - 1)  # select the network you just added

    def _edit(self) -> None:
        i = self._row()
        if i < 0:
            return
        dlg = NetworkEditDialog(self, self._nets[i])
        if dlg.exec():
            self._nets[i] = dlg.values()
            self._refresh(i)  # keep the edited network selected so you can Connect straight away

    def _remove(self) -> None:
        i = self._row()
        if i < 0:
            return
        del self._nets[i]
        self._refresh(min(i, len(self._nets) - 1))  # select a neighbour so the list stays navigable

    def _connect(self) -> None:
        i = self._row()
        if i < 0:
            return
        self.connectRequested.emit(dict(self._nets[i]))
        self.accept()
