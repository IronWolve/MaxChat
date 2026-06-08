"""DCC over Qt sockets — SEND / GET / RESUME / CHAT with **active** and **passive (reverse)** DCC.

Passive/reverse DCC lets the side that *can* accept connections open the listening port, so a transfer
works when one peer is behind NAT / a firewall (the HexChat behaviour). RESUME continues a partial
download from its byte offset (RESUME→ACCEPT handshake); CHAT is a direct newline-delimited text link
(``DCCChat``). Four file-transfer flows, by who listens:

    SEND active   → we listen · peer connects · we write the file
    SEND passive  → peer listens (we send port 0 + token); they reply, we connect, we write
    GET  active   → peer listens (gave us ip:port) · we connect · we read + save
    GET  passive  → peer is NATed (port 0 + token); we listen + reply, they connect, we read + save

So we LISTEN for (SEND active, GET passive) and CONNECT for (SEND passive, GET active). The writer
streams the file; the reader saves it and sends 32-bit big-endian acks (running byte count).

UI-free (Qt signals only) so it can be driven + unit-tested headless (a loopback transfer works).
"""

from __future__ import annotations

import os
import secrets
import shlex
import struct
import time

from PySide6.QtCore import QObject, QTimer, Signal
from PySide6.QtNetwork import QHostAddress, QTcpServer, QTcpSocket

_CHUNK = 64 * 1024
DCC_PROGRESS_INTERVAL = 0.10
DCC_CHAT_MAX_LINE_BYTES = 8192
DCC_CHAT_MAX_PENDING_BYTES = 65536


def ip_to_int(ip: str) -> int:
    parts = ip.split(".")
    if len(parts) == 4:
        try:
            a, b, c, d = (int(p) for p in parts)
            return (a << 24) | (b << 16) | (c << 8) | d
        except ValueError:
            pass
    return 0


def int_to_ip(n) -> str:
    n = int(n)
    return f"{(n >> 24) & 255}.{(n >> 16) & 255}.{(n >> 8) & 255}.{n & 255}"


def _listen(server: QTcpServer, ports=(0, 0)) -> bool:
    first, last = ports
    if first and last:
        for p in range(first, last + 1):
            if server.listen(QHostAddress(QHostAddress.SpecialAddress.Any), p):
                return True
    return server.listen(QHostAddress(QHostAddress.SpecialAddress.Any), 0)


class DCCTransfer(QObject):
    progress = Signal()       # transferred/total changed
    finished = Signal(bool)   # ok

    def __init__(self, *, send: bool, nick: str, filename: str, size: int, path: str,
                 token: str = "") -> None:
        super().__init__()
        self.send = send            # True = we write the file out; False = we receive + save
        self.nick = nick
        self.filename = filename
        self.size = int(size)
        self.path = path            # source (send) or destination (receive)
        self.token = token
        self.transferred = 0
        self.status = "pending"     # pending|offered|connecting|active|done|failed|cancelled
        self.offer = None           # (ip, port) for an incoming offer, until accepted
        self._sock: QTcpSocket | None = None
        self._server: QTcpServer | None = None
        self._f = None
        self._ackbuf = b""          # sender: receiver's running-ack bytes
        self._all_sent = False      # sender: whole file is on the wire
        self.send_ctcp = None       # callable(nick, body) → send a DCC CTCP over this transfer's network
        self.advertised_ip = ""     # network-context IP to advertise for passive replies
        self.start_offset = 0       # resume: bytes already present; start the transfer here
        self.local_port = 0         # our listening port (active SEND), for RESUME matching
        self._last_progress = 0.0

    # ---- open a listener (SEND active / GET passive) ----------------------
    def listen(self, ports=(0, 0)) -> int:
        self._server = QTcpServer(self)
        if not _listen(self._server, ports) or self._server.serverPort() <= 0:
            msg = self._server.errorString() or "DCC listen failed"
            self._server.close()
            self.status = "failed"
            self.finished.emit(False)
            raise OSError(msg)
        self._server.newConnection.connect(self._on_conn)
        self.status = "offered"
        return self._server.serverPort()

    def _on_conn(self) -> None:
        self._sock = self._server.nextPendingConnection()
        self._server.close()
        self._wire()
        self._begin()

    # ---- connect to a peer (SEND passive / GET active) --------------------
    def connect_to(self, ip: str, port: int) -> None:
        self.status = "connecting"
        self._sock = QTcpSocket(self)
        self._wire()
        self._sock.connected.connect(self._begin)
        self._sock.connectToHost(ip, int(port))

    def _wire(self) -> None:
        self._sock.readyRead.connect(self._on_read)
        self._sock.bytesWritten.connect(self._on_written)
        self._sock.errorOccurred.connect(lambda _e: self._fail())
        self._sock.disconnected.connect(self._on_disconnect)

    def _begin(self) -> None:
        self.status = "active"
        try:
            if self.send:
                self._f = open(self.path, "rb")
            else:
                self._f = open(self.path, "r+b" if (self.start_offset and os.path.exists(self.path)) else "wb")
            if self.start_offset:
                self._f.seek(self.start_offset)
                self.transferred = self.start_offset  # resume from here
        except OSError:
            self._fail()
            return
        if self.send:
            self._pump()
        self._emit_progress(force=True)

    def _emit_progress(self, *, force: bool = False) -> None:
        now = time.monotonic()
        if force or now - self._last_progress >= DCC_PROGRESS_INTERVAL:
            self._last_progress = now
            self.progress.emit()

    # ---- sending (writer) -------------------------------------------------
    def _pump(self) -> None:
        if not (self._sock and self._f):
            return
        chunks = 0
        while self._sock.bytesToWrite() < _CHUNK * 4:
            data = self._f.read(_CHUNK)
            if not data:
                break
            self._sock.write(data)
            chunks += 1
            if chunks >= 4:
                QTimer.singleShot(0, self._pump)
                break

    def _on_written(self, n: int) -> None:
        if not self.send:
            return
        self.transferred += n
        self._emit_progress()
        if self.transferred >= self.size:
            self._all_sent = True  # all on the wire — wait for the receiver's ack / close
        else:
            self._pump()

    # ---- receiving (reader) ----------------------------------------------
    def _on_read(self) -> None:
        if not self._sock:
            return
        if self.send:  # receiver's 32-bit running acks — finish once it has acked everything
            self._ackbuf += bytes(self._sock.readAll())
            while len(self._ackbuf) >= 4:
                ack = struct.unpack(">I", self._ackbuf[:4])[0]
                self._ackbuf = self._ackbuf[4:]
                if ack >= (self.size & 0xFFFFFFFF):
                    self._finish(True)
                    return
            return
        if self._f is None:
            return
        data = bytes(self._sock.readAll())
        remaining = self.size - self.transferred
        if remaining <= 0:
            return
        data = data[:remaining]
        self._f.write(data)
        self.transferred += len(data)
        self._sock.write(struct.pack(">I", self.transferred & 0xFFFFFFFF))  # ack
        self._emit_progress()
        if self.transferred >= self.size:
            self._finish(True)

    def _on_disconnect(self) -> None:
        if not self.send and self._sock is not None and self._sock.bytesAvailable():
            self._on_read()  # drain any buffered data the peer sent before closing
        if self.status == "active":
            self._finish(self._all_sent if self.send else self.transferred >= self.size)

    def _finish(self, ok: bool) -> None:
        if self.status in ("done", "failed", "cancelled"):
            return
        if self._f:
            self._f.close()
            self._f = None
        if self._sock:
            self._sock.flush()
            self._sock.disconnectFromHost()
        self.status = "done" if ok else "failed"
        self._emit_progress(force=True)
        self.finished.emit(ok)

    def _fail(self) -> None:
        self._finish(False)

    def cancel(self) -> None:
        if self.status in ("done", "failed", "cancelled"):
            return
        if self._server:
            self._server.close()
        if self._f:
            self._f.close()
            self._f = None
        if self._sock:
            self._sock.abort()
        self.status = "cancelled"
        self._emit_progress(force=True)
        self.finished.emit(False)


class DCCManager(QObject):
    """Orchestrates the CTCP DCC handshake + tracks transfers."""

    added = Signal(object)        # DCCTransfer
    changed = Signal(object)      # DCCTransfer
    chatStarted = Signal(str, object)  # nick, DCCChat

    def __init__(self, *, ip_provider, download_dir, port_range=(0, 0), passive=True) -> None:
        super().__init__()
        self.transfers: list[DCCTransfer] = []
        self._ip = ip_provider            # () -> our advertised IPv4
        self._dl = download_dir           # () -> the downloads folder
        self._ports = port_range          # (first, last) listen range, (0,0) = any
        self.passive = passive
        self._awaiting: dict[str, DCCTransfer] = {}      # token → our passive SEND awaiting a reply
        self._send_by_port: dict[int, DCCTransfer] = {}  # active SEND listen-port → transfer (for RESUME)
        self._resuming: dict[str, DCCTransfer] = {}      # our RECV awaiting a DCC ACCEPT
        self._chat_awaiting: dict[str, object] = {}      # token → our passive CHAT awaiting a reply

    def _ip_for(self, tr: DCCTransfer | None = None, override: str = "") -> str:
        return override or (tr.advertised_ip if tr is not None else "") or self._ip()

    def download_dir(self) -> str:
        return self._dl()

    def _track(self, tr: DCCTransfer) -> None:
        self.transfers.append(tr)
        tr.progress.connect(lambda t=tr: self.changed.emit(t))
        tr.finished.connect(lambda _ok, t=tr: self._on_transfer_finished(t))
        self.added.emit(tr)

    def _on_transfer_finished(self, tr: DCCTransfer) -> None:
        self._forget_transfer(tr)
        self.changed.emit(tr)

    def _forget_transfer(self, tr: DCCTransfer) -> None:
        self._awaiting = {k: v for k, v in self._awaiting.items() if v is not tr}
        self._send_by_port = {k: v for k, v in self._send_by_port.items() if v is not tr}
        self._resuming = {k: v for k, v in self._resuming.items() if v is not tr}

    @staticmethod
    def _inactive(tr: DCCTransfer | None) -> bool:
        return tr is None or tr.status in ("done", "failed", "cancelled")

    # ---- sending ----------------------------------------------------------
    def offer(self, nick: str, path: str, send_ctcp, advertised_ip: str = "") -> DCCTransfer | None:
        if not os.path.isfile(path):
            return None
        size = os.path.getsize(path)
        name = os.path.basename(path)
        tr = DCCTransfer(send=True, nick=nick, filename=name, size=size, path=path)
        tr.send_ctcp = send_ctcp
        tr.advertised_ip = advertised_ip
        self._track(tr)
        if self.passive:
            tr.token = secrets.token_hex(4)
            self._awaiting[tr.token] = tr
            tr.status = "offered"
            send_ctcp(nick, f'DCC SEND "{name}" {ip_to_int(self._ip_for(tr))} 0 {size} {tr.token}')
        else:
            try:
                tr.local_port = tr.listen(self._ports)
            except OSError:
                self.changed.emit(tr)
                return tr
            self._send_by_port[tr.local_port] = tr
            send_ctcp(nick, f'DCC SEND "{name}" {ip_to_int(self._ip_for(tr))} {tr.local_port} {size}')
        self.changed.emit(tr)
        return tr

    def offer_chat(self, nick: str, send_ctcp, advertised_ip: str = ""):
        chat = DCCChat(nick)
        ip = advertised_ip or self._ip()
        if self.passive:
            token = secrets.token_hex(4)
            self._chat_awaiting[token] = chat
            # Drop the pending token if the chat is closed (e.g. the =nick tab is shut) before the peer
            # replies, and expire it if they never reply — so a stale offer can't reactivate or leak.
            chat.stateChanged.connect(
                lambda st, t=token: self._chat_awaiting.pop(t, None) if st == "closed" else None)
            QTimer.singleShot(120_000, lambda t=token: self._chat_awaiting.pop(t, None))
            send_ctcp(nick, f'DCC CHAT chat {ip_to_int(ip)} 0 {token}')
        else:
            try:
                port = chat.listen(self._ports)
            except OSError:
                self.chatStarted.emit(nick, chat)
                return chat
            send_ctcp(nick, f'DCC CHAT chat {ip_to_int(ip)} {port}')
        self.chatStarted.emit(nick, chat)
        return chat

    # ---- incoming CTCP ----------------------------------------------------
    def handle_dcc(self, nick: str, body: str, send_ctcp, advertised_ip: str = "") -> None:
        try:
            toks = shlex.split(body)
        except ValueError:
            toks = body.split()
        if toks and toks[0].upper() == "DCC":
            toks = toks[1:]
        if not toks:
            return
        kind = toks[0].upper()
        if kind == "SEND":
            self._in_send(nick, toks, send_ctcp, advertised_ip)
        elif kind == "RESUME":
            self._in_resume(nick, toks)
        elif kind == "ACCEPT":
            self._in_accept(nick, toks)
        elif kind == "CHAT":
            self._in_chat(nick, toks, send_ctcp, advertised_ip)

    def _in_send(self, nick, toks, send_ctcp, advertised_ip: str = "") -> None:
        try:
            name = os.path.basename(toks[1]) or "file"
            ip, port, size = int_to_ip(int(toks[2])), int(toks[3]), int(toks[4])
            token = toks[5] if len(toks) >= 6 else ""
        except (ValueError, IndexError):
            return
        if token and token in self._awaiting:
            if port == 0:
                return
            tr = self._awaiting.pop(token)
            if self._inactive(tr):
                return
            tr.connect_to(ip, port)
            self.changed.emit(tr)
            return
        if port == 0 and not token:
            return
        tr = DCCTransfer(send=False, nick=nick, filename=name, size=size,
                         path=self._dest(name, size), token=token)
        tr.offer = (ip, port)
        tr.send_ctcp = send_ctcp
        tr.advertised_ip = advertised_ip
        self._track(tr)

    def _in_resume(self, nick, toks) -> None:  # SENDER side: receiver asks to resume from a position
        try:
            port, pos = int(toks[2]), int(toks[3])
            token = toks[4] if len(toks) >= 5 else ""
        except (ValueError, IndexError):
            return
        if port == 0 and not token:
            return
        tr = self._awaiting.get(token) if token else self._send_by_port.get(port)
        if self._inactive(tr):
            return
        tr.start_offset = pos
        tr.send_ctcp(nick, f'DCC ACCEPT "{tr.filename}" {port} {pos}' + (f" {token}" if token else ""))
        self.changed.emit(tr)

    def _in_accept(self, nick, toks) -> None:  # RECEIVER side: sender agreed to resume
        try:
            name, port, pos = os.path.basename(toks[1]), int(toks[2]), int(toks[3])
            token = toks[4] if len(toks) >= 5 else ""
        except (ValueError, IndexError):
            return
        tr = self._resuming.pop(token if token else f"{nick}:{name}:{port}", None)
        if not self._inactive(tr):
            tr.start_offset = pos
            self._do_accept(tr)

    def _in_chat(self, nick, toks, send_ctcp, advertised_ip: str = "") -> None:
        try:
            ip, port = int_to_ip(int(toks[2])), int(toks[3])
            token = toks[4] if len(toks) >= 5 else ""
        except (ValueError, IndexError):
            return
        if token and token in self._chat_awaiting:
            if port == 0:
                return
            chat = self._chat_awaiting.pop(token)
            chat.connect_to(ip, port)  # already surfaced by offer_chat — do NOT emit chatStarted again
            return
        if port == 0 and not token:
            return
        chat = DCCChat(nick)
        if port != 0:
            chat.connect_to(ip, port)
        else:
            try:
                myport = chat.listen(self._ports)
            except OSError:
                self.chatStarted.emit(nick, chat)
                return
            send_ctcp(nick, f"DCC CHAT chat {ip_to_int(self._ip_for(override=advertised_ip))} {myport} {token}")
        self.chatStarted.emit(nick, chat)

    # ---- accepting a file -------------------------------------------------
    def accept(self, tr: DCCTransfer) -> None:
        if tr.send or tr.offer is None or tr.status != "pending" or self._inactive(tr):
            return
        existing = os.path.getsize(tr.path) if os.path.exists(tr.path) else 0
        if 0 < existing < tr.size:  # partial file → ask the sender to RESUME from here
            tr.start_offset = existing
            port = tr.offer[1]
            self._resuming[tr.token if tr.token else f"{tr.nick}:{tr.filename}:{port}"] = tr
            tr.status = "resuming"
            tr.send_ctcp(tr.nick, f'DCC RESUME "{tr.filename}" {port} {existing}'
                         + (f" {tr.token}" if tr.token else ""))
            self.changed.emit(tr)
            return
        self._do_accept(tr)

    def _do_accept(self, tr: DCCTransfer) -> None:
        if self._inactive(tr):
            return
        ip, port = tr.offer
        if port != 0:                       # active offer → we connect and receive
            tr.connect_to(ip, port)
        elif tr.send_ctcp is not None:      # passive offer → we listen and reply
            try:
                myport = tr.listen(self._ports)
            except OSError:
                self.changed.emit(tr)
                return
            tr.send_ctcp(tr.nick, f'DCC SEND "{tr.filename}" {ip_to_int(self._ip_for(tr))} '
                                  f'{myport} {tr.size} {tr.token}')
        self.changed.emit(tr)

    def _dest(self, name: str, size: int) -> str:
        path = os.path.join(self._dl(), os.path.basename(name) or "file")
        if os.path.exists(path):
            on_disk = os.path.getsize(path)
            if 0 < on_disk < size:
                return path  # a partial download of this file — resume it
            root, ext = os.path.splitext(path)  # complete file already there → don't overwrite
            i = 1
            while os.path.exists(path):
                path = f"{root}.{i}{ext}"
                i += 1
        return path


class DCCChat(QObject):
    """A direct DCC CHAT connection — newline-delimited text, active or passive (like DCC SEND)."""

    lineReceived = Signal(str)
    stateChanged = Signal(str)  # connecting|active|closed

    def __init__(self, nick: str) -> None:
        super().__init__()
        self.nick = nick
        self.status = "pending"
        self._sock: QTcpSocket | None = None
        self._server: QTcpServer | None = None
        self._buf = b""

    def listen(self, ports=(0, 0)) -> int:
        self._server = QTcpServer(self)
        if not _listen(self._server, ports) or self._server.serverPort() <= 0:
            msg = self._server.errorString() or "DCC chat listen failed"
            self._server.close()
            self.status = "failed"
            raise OSError(msg)
        self._server.newConnection.connect(self._on_conn)
        self.status = "offered"
        return self._server.serverPort()

    def _on_conn(self) -> None:
        self._sock = self._server.nextPendingConnection()
        self._server.close()
        self._wire()
        self._active()

    def connect_to(self, ip: str, port: int) -> None:
        self.status = "connecting"
        self.stateChanged.emit("connecting")
        self._sock = QTcpSocket(self)
        self._wire()
        self._sock.connected.connect(self._active)
        self._sock.connectToHost(ip, int(port))

    def _wire(self) -> None:
        self._sock.readyRead.connect(self._read)
        self._sock.disconnected.connect(self._on_closed)
        self._sock.errorOccurred.connect(lambda _e: self._on_closed())

    def _active(self) -> None:
        self.status = "active"
        self.stateChanged.emit("active")

    def _read(self) -> None:
        if self._sock is None:
            return
        self._buf += bytes(self._sock.readAll())
        while b"\n" in self._buf:
            line, self._buf = self._buf.split(b"\n", 1)
            if len(line) > DCC_CHAT_MAX_LINE_BYTES:
                self._buf = b""
                self.close()
                return
            self.lineReceived.emit(line.rstrip(b"\r").decode("utf-8", "replace"))
        if len(self._buf) > DCC_CHAT_MAX_PENDING_BYTES:
            self._buf = b""
            self.close()

    def send(self, text: str) -> None:
        if self._sock is not None and self.status == "active":
            self._sock.write((text + "\n").encode("utf-8"))

    def _on_closed(self) -> None:
        if self.status != "closed":
            self.status = "closed"
            self.stateChanged.emit("closed")

    def close(self) -> None:
        if self._server:
            self._server.close()
        if self._sock:
            self._sock.abort()
        self._on_closed()
