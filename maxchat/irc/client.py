"""A real IRC client on Qt sockets.

`QSslSocket` handles both plain and TLS (one code path). The client is UI-free and talks to the
window through Qt signals, so it can be driven headlessly. Covers: connect + register, CAP
negotiation + **SASL PLAIN** (with NickServ fallback), PING/PONG keepalive, channels, PRIVMSG/
NOTICE, NAMES/TOPIC, membership (JOIN/PART/QUIT/NICK), MODE, WHOIS, and **CTCP auto-reply**
(VERSION/PING/TIME/CLIENTINFO, rate-limited). Auto-reconnect is driven by the window.
"""

from __future__ import annotations

import base64
import fnmatch
import re
import time
from collections import deque
from datetime import datetime

from PySide6.QtCore import QObject, QTimer, Signal
from PySide6.QtNetwork import QAbstractSocket, QNetworkProxy, QSslSocket

from maxchat import __app_name__, __version__
from maxchat.irc.message import parse

IRC_MAX_LINE_BYTES = 8192
IRC_MAX_PENDING_BYTES = 65536
IRC_MAX_LINES_PER_TICK = 100
IRC_MAX_WIRE_BYTES = 512
_PROXY_TYPES = {
    "socks5": QNetworkProxy.ProxyType.Socks5Proxy,
    "http": QNetworkProxy.ProxyType.HttpProxy,
}


def _redact(line: str) -> str:
    """Mask secrets before they reach the raw log: server PASS, the SASL AUTHENTICATE payload, and
    NickServ/services identify-style passwords. Only the LOGGED copy is masked — the wire is unchanged."""
    up = line.upper()
    if up.startswith("PASS "):
        return "PASS ****"
    if up.startswith("AUTHENTICATE ") and line[13:].strip() not in ("+", "PLAIN", "EXTERNAL"):
        return "AUTHENTICATE ****"  # the base64 authzid\0authcid\0password blob
    m = re.match(r"((?:PRIVMSG|NOTICE) \S+ :(?:IDENTIFY|REGISTER|GHOST|RECOVER|RELEASE|SIDENTIFY|LOGIN)\s+)",
                 line, re.IGNORECASE)
    if m:
        return m.group(1) + "****"
    return line


def _proxy_from_config(proxy: dict | None) -> QNetworkProxy | None:
    """Build a Qt proxy from a saved network config, or return None for direct connections."""
    if not isinstance(proxy, dict):
        return None
    kind = str(proxy.get("type") or "none").strip().lower()
    if kind in ("", "none"):
        return None
    if kind not in _PROXY_TYPES:
        raise ValueError(f"Unsupported proxy type: {kind}")
    host = str(proxy.get("host") or "").strip()
    try:
        port = int(proxy.get("port") or 0)
    except (TypeError, ValueError) as exc:
        raise ValueError("Proxy port must be a number") from exc
    if not host or not (1 <= port <= 65535):
        raise ValueError("Proxy host and port are required")
    qproxy = QNetworkProxy(_PROXY_TYPES[kind], host, port)
    user = str(proxy.get("username") or "")
    password = str(proxy.get("password") or "")
    if user:
        qproxy.setUser(user)
        qproxy.setPassword(password)
    return qproxy


class IRCClient(QObject):
    # Connection lifecycle
    connected = Signal()
    registered = Signal()  # received 001
    disconnected = Signal(str)
    errorOccurred = Signal(str)

    # Traffic
    rawLine = Signal(str, str)  # direction ("<<"/">>"), line
    systemText = Signal(str)
    message = Signal(str, str, str)  # target, nick, text
    notice = Signal(str, str, str)
    joined = Signal(str, str)
    parted = Signal(str, str)
    kicked = Signal(str, str, str)
    quitUser = Signal(str)
    nickChanged = Signal(str, str)
    namesReply = Signal(str, list)
    namesEnd = Signal(str)
    topicChanged = Signal(str, str)
    modeChanged = Signal(str, str, str)
    whois = Signal(str, str)
    awayChanged = Signal(str, bool)  # nick, is_away (via away-notify)
    listReply = Signal(str, int, str)  # channel, users, topic (RPL_LIST 322)
    listEnd = Signal()  # RPL_LISTEND 323
    channelText = Signal(str, str)  # channel, text — a server reply/error about a channel
    replyText = Signal(str)  # formatted WHO / WHOWAS reply line — the UI prints it to the active chat
    banList = Signal(str, str, str)  # channel, mask, setter (RPL_BANLIST 367)
    banListEnd = Signal(str)  # channel (RPL_ENDOFBANLIST 368)
    ctcpReply = Signal(str, str, str)  # nick, type, info — a reply to a CTCP we sent (e.g. VERSION/PING)
    channelModeIs = Signal(str, str)  # channel, "modes [params]" (RPL_CHANNELMODEIS 324)
    isonReply = Signal(list)  # online nicks from an ISON poll (RPL_ISON 303) — for the notify list
    invited = Signal(str, str)  # nick, channel — someone invited you (INVITE)
    dccRequest = Signal(str, str)  # nick, "DCC …" body — a DCC CTCP (handled by the UI's DCC manager)
    ctcpSound = Signal(str, str, str, str)  # nick, target, file, text — a CTCP SOUND (mIRC/Comic Chat)
    lag = Signal(float)  # round-trip seconds from a /lag PING → PONG

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._sock: QSslSocket | None = None
        self._buf = b""
        self._nick = ""
        self._username = ""
        self._realname = ""
        self._password: str | None = None
        self._sasl_password: str | None = None
        self._sasl_account: str | None = None
        self._hide_version = False   # don't answer CTCP VERSION
        self._ctcp_version = ""       # custom VERSION reply ("" = the default app/version string)
        self.isupport: dict = {}      # ISUPPORT (005) tokens, e.g. NETWORK, CHANTYPES, PREFIX
        self._lag_token: str | None = None
        self._lag_sent = 0.0
        self._tls = False
        self._allow_insecure_auth = False
        self._insecure_auth_warned = False
        self._accept_invalid_cert = False
        self._autojoin: list[str] = []
        self._whois: dict[str, list[str]] = {}
        self._server_caps: set[str] = set()
        self._authed = False
        self._last_ctcp: datetime | None = None
        self._ignore_res: list[re.Pattern] = []  # compiled ignore masks (match a nick!user@host)
        self._line_queue = deque()
        self._draining_lines = False

    # ---- public API -------------------------------------------------------
    def connect_to(
        self,
        host: str,
        port: int,
        *,
        tls: bool = False,
        nick: str,
        username: str | None = None,
        realname: str | None = None,
        password: str | None = None,
        sasl_password: str | None = None,
        sasl_account: str | None = None,
        accept_invalid_cert: bool = False,
        allow_insecure_auth: bool = False,
        autojoin: list[str] | None = None,
        proxy: dict | None = None,
    ) -> None:
        try:
            qproxy = _proxy_from_config(proxy)
        except ValueError as e:
            self.errorOccurred.emit(str(e))
            self.disconnected.emit(str(e))
            return

        self._nick = nick
        self._base_nick = nick      # the requested nick; alternates derive from it on 433
        self._nick_try = 0
        self._registered = False
        self._username = username or nick
        self._realname = realname or nick
        self._password = password
        self._sasl_password = sasl_password
        self._sasl_account = sasl_account or nick  # NickServ/SASL login name (defaults to the nick)
        self._tls = tls
        self._allow_insecure_auth = allow_insecure_auth
        self._insecure_auth_warned = False
        self._accept_invalid_cert = accept_invalid_cert
        self._autojoin = list(autojoin or [])
        self._buf = b""
        self._line_queue.clear()
        self._draining_lines = False
        self._server_caps = set()
        self._authed = False

        self._retire_socket()

        sock = QSslSocket(self)
        if qproxy is not None:
            sock.setProxy(qproxy)
        self._sock = sock
        sock.readyRead.connect(lambda s=sock: self._on_ready_read(s))
        sock.disconnected.connect(lambda s=sock: self._on_sock_disconnected(s))
        sock.errorOccurred.connect(lambda _e, s=sock: self._on_sock_error(s))
        sock.sslErrors.connect(lambda errs, s=sock: self._on_ssl_errors(errs, s))
        if tls:
            sock.encrypted.connect(lambda s=sock: self._on_connected(s))
            sock.connectToHostEncrypted(host, port)
        else:
            sock.connected.connect(lambda s=sock: self._on_connected(s))
            sock.connectToHost(host, port)

    def send_raw(self, line: str) -> bool:
        if self._sock and self._sock.state() == QAbstractSocket.SocketState.ConnectedState:
            payload = (line + "\r\n").encode("utf-8")
            if len(payload) > IRC_MAX_WIRE_BYTES:
                self.errorOccurred.emit("IRC line is too long; message not sent")
                return False
            written = self._sock.write(payload)
            if written != len(payload):
                self.errorOccurred.emit("Socket write failed; message not sent")
                return False
            self.rawLine.emit(">>", _redact(line))
            return True
        return False

    def privmsg(self, target: str, text: str) -> bool:
        return self.send_raw(f"PRIVMSG {target} :{text}")

    def action(self, target: str, text: str) -> bool:
        return self.privmsg(target, f"\x01ACTION {text}\x01")

    def ctcp(self, target: str, command: str, arg: str = "") -> bool:
        """Send a CTCP request. PING carries a timestamp so the reply can be timed (round-trip)."""
        command = command.upper()
        if command == "PING" and not arg:
            arg = f"{datetime.now().timestamp():.3f}"
        body = f"{command} {arg}".strip()
        return self.send_raw(f"PRIVMSG {target} :\x01{body}\x01")

    def set_ctcp_version(self, hide: bool, custom: str = "") -> None:
        """Configure the CTCP VERSION auto-reply: hide it entirely, or use a custom string."""
        self._hide_version = bool(hide)
        self._ctcp_version = (custom or "").strip()

    def measure_lag(self) -> bool:
        """Send a PING with a unique token; the matching PONG emits ``lag`` with the round-trip secs."""
        self._lag_token = f"MAXLAG{int(time.monotonic() * 1000)}"
        self._lag_sent = time.monotonic()
        return self.send_raw(f"PING :{self._lag_token}")

    def set_ignores(self, masks: list[str]) -> None:
        """Compile ignore masks (glob over ``nick!user@host``). Matching senders' PRIVMSG/NOTICE/CTCP
        are dropped silently. ``*`` = any run, ``?`` = one char; a bare nick should arrive as ``nick!*@*``."""
        self._ignore_res = [
            re.compile(fnmatch.translate(m.strip().lower())) for m in masks if m.strip()
        ]

    def _is_ignored(self, prefix: str) -> bool:
        return bool(prefix) and any(rx.match(prefix.lower()) for rx in self._ignore_res)

    def join(self, channel: str) -> bool:
        return self.send_raw(f"JOIN {channel}")

    def list_channels(self, query: str = "") -> bool:
        return self.send_raw(f"LIST {query}".strip())

    def ison(self, nicks: list[str]) -> bool:
        """Ask which of these nicks are online (RPL_ISON 303 → isonReply)."""
        nicks = [n for n in nicks if n]
        if nicks:
            return self.send_raw("ISON " + " ".join(nicks))
        return False

    def quit(self, message: str = "MaxChat") -> bool:
        if self._sock:
            sent = self.send_raw(f"QUIT :{message}")
            self._sock.disconnectFromHost()
            return sent
        return False

    def disconnect(self) -> None:
        """Drop the socket immediately — also aborts an in-progress connect attempt (disconnectFromHost
        does nothing while still connecting, so a reconnect loop couldn't be stopped without this)."""
        if self._sock is not None:
            self._sock.abort()

    @property
    def nick(self) -> str:
        return self._nick

    def is_connected(self) -> bool:
        return (
            self._sock is not None
            and self._sock.state() == QAbstractSocket.SocketState.ConnectedState
        )

    def local_address(self) -> str:
        """Our local socket IP (used as a DCC fallback when no public DCC IP is configured)."""
        return self._sock.localAddress().toString() if self._sock is not None else ""

    # ---- internals --------------------------------------------------------
    def _password_auth_allowed(self) -> bool:
        if self._tls or self._allow_insecure_auth:
            return True
        if not self._insecure_auth_warned:
            self._insecure_auth_warned = True
            self.systemText.emit(
                "Password authentication skipped on plaintext IRC. Enable SSL/TLS or opt in to "
                "plaintext auth for this saved network."
            )
        return False

    def _retire_socket(self) -> None:
        old = self._sock
        if old is None:
            return
        self._sock = None
        self._buf = b""
        self._line_queue.clear()
        self._draining_lines = False
        old.abort()
        old.deleteLater()

    def _on_connected(self, sock: QSslSocket | None = None) -> None:
        if sock is not None and sock is not self._sock:
            return
        self.connected.emit()
        self.send_raw("CAP LS 302")  # start capability negotiation (servers without CAP ignore it)
        if self._password and self._password_auth_allowed():
            self.send_raw(f"PASS {self._password}")
        self.send_raw(f"NICK {self._nick}")
        self.send_raw(f"USER {self._username} 0 * :{self._realname}")

    def _on_ssl_errors(self, errs, sock: QSslSocket | None = None) -> None:
        """Handle TLS certificate failures for the current socket only."""
        if sock is not None and sock is not self._sock:
            return
        detail = "; ".join(e.errorString() for e in errs)
        if self._accept_invalid_cert and self._sock is not None:
            self._sock.ignoreSslErrors()
            self.systemText.emit(f"Accepted the server's certificate despite: {detail}")
        else:
            self.systemText.emit(
                "TLS certificate rejected: "
                f"{detail} — tick 'Accept unsigned' in Server List > Edit to connect anyway"
            )

    def _on_sock_disconnected(self, sock: QSslSocket) -> None:
        if sock is self._sock:
            self.disconnected.emit("connection closed")

    def _on_sock_error(self, sock: QSslSocket | None = None) -> None:
        """Surface the socket error; if it failed *before* connecting, signal a disconnect too so the
        window's reconnect/failover can try the next server (a failed connect emits no `disconnected`)."""
        if sock is not None and sock is not self._sock:
            return
        sock = self._sock
        msg = sock.errorString() if sock is not None else "socket error"
        self.errorOccurred.emit(msg)
        if sock is not None and sock.state() != QAbstractSocket.SocketState.ConnectedState:
            self.disconnected.emit(msg)

    def _alt_nick(self, attempt: int) -> str:
        """A fresh nick to try after ERR_NICKNAMEINUSE: base_, base__, base___, then base + a number."""
        base = self._base_nick or "guest"
        suffixes = ("_", "__", "___", "^", "`")
        return base + suffixes[attempt - 1] if attempt <= len(suffixes) else f"{base}{attempt}"

    def _on_ready_read(self, sock: QSslSocket | None = None) -> None:
        if sock is not None and sock is not self._sock:
            return
        if sock is None:
            sock = self._sock
        if sock is None:
            return
        self._buf += bytes(sock.readAll())
        self._queue_lines(sock)
        if self._line_queue and not self._draining_lines:
            self._draining_lines = True
            QTimer.singleShot(0, self._drain_lines)
        if len(self._buf) > IRC_MAX_PENDING_BYTES:
            self._buf = b""
            self.errorOccurred.emit("IRC server sent an oversized line; disconnecting")
            sock.abort()

    def _queue_lines(self, sock: QSslSocket) -> None:
        while b"\r\n" in self._buf:
            chunk, self._buf = self._buf.split(b"\r\n", 1)
            if not chunk:
                continue
            if len(chunk) > IRC_MAX_LINE_BYTES:
                self.systemText.emit("Dropped oversized IRC line from server")
                continue
            try:
                text = chunk.decode("utf-8")
            except UnicodeDecodeError:
                text = chunk.decode("latin-1", "replace")
            self._line_queue.append((sock, text))

    def _drain_lines(self) -> None:
        handled = 0
        while self._line_queue and handled < IRC_MAX_LINES_PER_TICK:
            sock, text = self._line_queue.popleft()
            if sock is self._sock:
                self._handle(text)
            handled += 1
        if self._line_queue:
            QTimer.singleShot(0, self._drain_lines)
        else:
            self._draining_lines = False

    def _handle(self, line: str) -> None:
        self.rawLine.emit("<<", _redact(line))
        msg = parse(line)
        cmd = msg.command
        p = msg.params

        if cmd == "PING":
            self.send_raw("PONG :" + msg.trailing)
            return
        if cmd == "PONG":
            if self._lag_token and msg.trailing == self._lag_token:
                self.lag.emit(max(0.0, time.monotonic() - self._lag_sent))
                self._lag_token = None
            return
        if cmd == "005":  # ISUPPORT — collect tokens (NETWORK, CHANTYPES, PREFIX, …)
            for tok in p[1:]:
                key, _, val = tok.partition("=")
                if key and key.upper() == key:
                    self.isupport[key] = val
        if cmd == "CAP":
            self._handle_cap(p, msg.trailing)
            return
        if cmd == "AUTHENTICATE" and p and p[0] == "+":
            if not self._password_auth_allowed():
                self.send_raw("CAP END")
                return
            acct = self._sasl_account or self._nick  # authzid \0 authcid (account) \0 password
            payload = f"{acct}\0{acct}\0{self._sasl_password or ''}".encode()
            self.send_raw("AUTHENTICATE " + base64.b64encode(payload).decode())
            return
        if cmd in ("903", "900"):  # SASL success / logged in
            self._authed = True
            self.send_raw("CAP END")
            return
        if cmd in ("902", "904", "905", "906", "907"):  # SASL failed/aborted
            self.systemText.emit("SASL authentication failed — continuing without it")
            self.send_raw("CAP END")
            return
        if cmd == "001":
            self._registered = True
            if p:
                self._nick = p[0]  # the nick the server actually gave us (may be an alternate)
            self.registered.emit()
            if (self._sasl_password and not self._authed
                    and self._password_auth_allowed()):  # NickServ fallback (SASL absent/failed)
                acct = self._sasl_account or self._nick
                ident = (f"{acct} {self._sasl_password}" if acct and acct != self._nick
                         else self._sasl_password)
                self.send_raw(f"PRIVMSG NickServ :IDENTIFY {ident}")
            for channel in self._autojoin:
                self.join(channel)
            return
        if cmd in ("433", "436"):  # ERR_NICKNAMEINUSE / ERR_NICKCOLLISION
            if not self._registered:  # still registering → try the next candidate nick
                self._nick_try += 1
                self._nick = self._alt_nick(self._nick_try)
                self.send_raw(f"NICK {self._nick}")
                self.systemText.emit(f"Nick in use — trying {self._nick}")
            else:
                self.systemText.emit("That nickname is already in use.")
            return
        if cmd == "PRIVMSG":
            if self._is_ignored(msg.prefix):
                return  # ignored sender — drop their message/CTCP entirely
            target, text = (p[0] if p else ""), msg.trailing
            if text.startswith("\x01") and text.endswith("\x01") and not text.startswith("\x01ACTION"):
                self._handle_ctcp(msg.nick, text.strip("\x01"), target)
                return
            self.message.emit(target, msg.nick, text)
            return
        if cmd == "NOTICE":
            if self._is_ignored(msg.prefix):
                return  # ignored sender — drop their notice / CTCP reply
            text = msg.trailing
            if len(text) >= 2 and text.startswith("\x01") and text.endswith("\x01"):
                self._handle_ctcp_reply(msg.nick, text.strip("\x01"))  # reply to a CTCP we sent
                return
            self.notice.emit(p[0] if p else "", msg.nick, text)
            return
        if cmd == "JOIN":
            self.joined.emit(p[0] if p else msg.trailing, msg.nick)
            return
        if cmd == "PART":
            self.parted.emit(p[0] if p else "", msg.nick)
            return
        if cmd == "KICK":
            if len(p) >= 2:
                self.kicked.emit(p[0], p[1], msg.trailing)
            return
        if cmd == "QUIT":
            self.quitUser.emit(msg.nick)
            return
        if cmd == "NICK":
            new = p[0] if p else msg.trailing
            if msg.nick == self._nick:  # our OWN nick changed → keep self._nick current
                self._nick = new
            self.nickChanged.emit(msg.nick, new)
            return
        if cmd == "MODE":
            self.modeChanged.emit(p[0] if p else "", msg.nick, " ".join(p[1:]))
            return
        if cmd == "AWAY":  # away-notify: trailing present = now away, absent = back
            self.awayChanged.emit(msg.nick, bool(msg.trailing))
            return
        if cmd == "INVITE":  # :nick INVITE you :#chan  (or as a param)
            chan = msg.trailing or (p[1] if len(p) >= 2 else "")
            self.invited.emit(msg.nick, chan)
            return
        if cmd == "353":
            self.namesReply.emit(p[2] if len(p) >= 3 else "", msg.trailing.split())
            return
        if cmd == "366":  # RPL_ENDOFNAMES
            if len(p) >= 2:
                self.namesEnd.emit(p[1])
            return
        if cmd == "332":
            if len(p) >= 2:
                self.topicChanged.emit(p[1], msg.trailing)
            return
        if cmd == "TOPIC":
            self.topicChanged.emit(p[0] if p else "", msg.trailing)
            return
        if cmd == "322":  # RPL_LIST: <me> <channel> <users> :<topic>
            if len(p) >= 3:
                try:
                    users = int(p[2])
                except ValueError:
                    users = 0
                self.listReply.emit(p[1], users, msg.trailing)
            return
        if cmd == "323":  # RPL_LISTEND
            self.listEnd.emit()
            return
        if cmd == "324":  # RPL_CHANNELMODEIS: <me> <#chan> <+modes> [params]
            if len(p) >= 3:
                self.channelModeIs.emit(p[1], " ".join(p[2:]))
            return
        if cmd == "329":  # RPL_CREATIONTIME — channel creation timestamp; noise, swallow it
            return
        if cmd == "303":  # RPL_ISON: trailing = space-separated online nicks
            self.isonReply.emit(msg.trailing.split())
            return
        if cmd == "311" and len(p) >= 4:
            self._whois[p[1]] = [f"{p[1]} is {p[2]}@{p[3]}  ({msg.trailing})"]
            return
        if cmd == "319" and len(p) >= 2:
            self._whois.setdefault(p[1], []).append(f"channels: {msg.trailing}")
            return
        if cmd == "312" and len(p) >= 3:
            self._whois.setdefault(p[1], []).append(f"server: {p[2]} ({msg.trailing})")
            return
        if cmd == "317" and len(p) >= 3:
            self._whois.setdefault(p[1], []).append(f"idle: {p[2]}s")
            return
        if cmd == "330" and len(p) >= 3:
            self._whois.setdefault(p[1], []).append(f"account: {p[2]}")
            return
        if cmd == "318" and len(p) >= 2:
            nick = p[1]
            self.whois.emit(nick, "\n".join(self._whois.pop(nick, [f"{nick}: no info"])))
            return
        if cmd == "352" and len(p) >= 7:  # RPL_WHOREPLY: me chan user host server nick flags :hops real
            real = msg.trailing.split(" ", 1)[1] if " " in msg.trailing else msg.trailing
            self.replyText.emit(f"[who] {p[5]} ({p[2]}@{p[3]}) {p[6]} on {p[1]} — {real}")
            return
        if cmd == "315":  # RPL_ENDOFWHO
            self.replyText.emit("[who] End of /WHO.")
            return
        if cmd == "314" and len(p) >= 4:  # RPL_WHOWASUSER: me nick user host * :realname
            self.replyText.emit(f"[whowas] {p[1]} was {p[2]}@{p[3]} ({msg.trailing})")
            return
        if cmd == "369":  # RPL_ENDOFWHOWAS
            self.replyText.emit("[whowas] End of /WHOWAS.")
            return
        if cmd == "367" and len(p) >= 3:  # RPL_BANLIST: me chan mask [setter] [ts]
            self.banList.emit(p[1], p[2], p[3] if len(p) >= 4 else "")
            return
        if cmd == "368" and len(p) >= 2:  # RPL_ENDOFBANLIST
            self.banListEnd.emit(p[1])
            return
        if cmd.isdigit():
            chan = next((x for x in p[1:] if x.startswith(("#", "&"))), "")
            if chan and cmd >= "400":  # an error about a channel (e.g. 482 not-op) → show it there
                self.channelText.emit(chan, msg.trailing or line)
            else:
                self.systemText.emit(msg.trailing or line)

    def _handle_cap(self, p: list[str], trailing: str) -> None:
        sub = p[1] if len(p) >= 2 else ""
        if sub == "LS":
            self._server_caps |= {c.split("=")[0] for c in trailing.split()}
            if len(p) >= 3 and p[2] == "*":  # more LS lines coming
                return
            want = [c for c in ("multi-prefix", "server-time", "away-notify") if c in self._server_caps]
            if (self._sasl_password and "sasl" in self._server_caps
                    and self._password_auth_allowed()):
                want.append("sasl")
            if want:
                self.send_raw("CAP REQ :" + " ".join(want))
            else:
                self.send_raw("CAP END")
        elif sub == "ACK":
            if ("sasl" in trailing.split() and self._sasl_password
                    and self._password_auth_allowed()):
                self.send_raw("AUTHENTICATE PLAIN")
            else:
                self.send_raw("CAP END")
        elif sub == "NAK":
            self.send_raw("CAP END")

    def _handle_ctcp_reply(self, nick: str, body: str) -> None:
        """A NOTICE wrapped in \\x01 is a reply to a CTCP we sent — surface it to the UI."""
        kind, _, arg = body.partition(" ")
        kind = kind.upper()
        info = arg.strip()
        if kind == "PING":
            try:
                info = f"{datetime.now().timestamp() - float(arg):.3f}s"  # round-trip time
            except ValueError:
                pass  # non-timestamp payload — show it verbatim
        self.ctcpReply.emit(nick, kind, info)

    def _handle_ctcp(self, nick: str, body: str, target: str = "") -> None:
        if body.upper().startswith("DCC "):  # file transfer / reverse DCC — hand to the DCC manager
            self.dccRequest.emit(nick, body)
            return
        now = datetime.now()
        if self._last_ctcp and (now - self._last_ctcp).total_seconds() < 1.0:
            return  # rate-limit to avoid CTCP-flood amplification (also throttles SOUND spam)
        self._last_ctcp = now
        ctcp, _, arg = body.partition(" ")
        ctcp = ctcp.upper()
        if ctcp == "SOUND":  # mIRC/Comic Chat: "SOUND <file> [text]" — the UI plays it if enabled
            parts = arg.strip().split(None, 1)  # split on any whitespace → file, rest (robust to extra spaces)
            fname = parts[0] if parts else ""
            snd_text = parts[1].strip() if len(parts) > 1 else ""
            self.ctcpSound.emit(nick, target, fname, snd_text)
            return
        reply = None
        if ctcp == "VERSION":
            reply = None if self._hide_version else \
                "VERSION " + (self._ctcp_version or f"{__app_name__} {__version__}")
        elif ctcp == "PING":
            reply = f"PING {arg}"
        elif ctcp == "TIME":
            reply = "TIME " + now.strftime("%a %b %d %H:%M:%S %Y")
        elif ctcp == "CLIENTINFO":
            reply = "CLIENTINFO ACTION CLIENTINFO PING TIME VERSION"
        if reply:
            self.send_raw(f"NOTICE {nick} :\x01{reply}\x01")
        self.systemText.emit(f"[CTCP {ctcp} from {nick}]")
