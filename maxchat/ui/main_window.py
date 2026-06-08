"""The main window — a typical IRC client (HexChat-style), with optional comic mode.

Supports **multiple simultaneous network connections**. The left **tree** shows each network as a
clickable header bar (the network name from the server/bookmark manager) with its channels/queries
nested underneath — click a network bar to switch to it. Per-chat **text area** (middle), **member
list** (right), **topic bar** (top, centred), **input** (bottom). Sending and /commands act on the
currently-active network; incoming events are routed to the network they came from. Chat renders mIRC
formatting + per-nick colours + timestamps; highlights beep + flag the tab. Many options live in
Preferences (Settings ▸ Preferences…, Ctrl+P).
"""

from __future__ import annotations

import hashlib
import os
import platform
import re
import time
from collections import deque
from datetime import datetime
from html import escape

from PySide6.QtCore import QByteArray, QEvent, QEventLoop, QObject, QSize, Qt, QTimer
from PySide6.QtGui import (
    QAction, QActionGroup, QBrush, QColor, QFont, QIcon, QKeySequence,
    QPainter, QPixmap, QShortcut, QTextCursor,
)
from PySide6.QtWidgets import (
    QApplication,
    QFrame,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMenu,
    QMenuBar,
    QPushButton,
    QSizePolicy,
    QSplitter,
    QStackedWidget,
    QTabBar,
    QSystemTrayIcon,
    QToolButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from maxchat import __app_name__, __version__, config
from maxchat.default_networks import DEFAULT_NETWORKS
from maxchat.irc.client import IRCClient
from maxchat.irc.dcc import DCCManager
from maxchat.ui.dcc_dialog import DCCDialog
from maxchat.ui.chat_view import ChatView
from maxchat.ui import app_icon, fonts, notifier, sounds, theme
from maxchat.ui import shadow_message
from maxchat.ui.connect_dialog import ConnectDialog
from maxchat.ui.input_bar import InputBar
from maxchat.ui.emotion_picker import EmotionWheel
from maxchat.ui.server_list import ServerListDialog
from maxchat.ui.irc_format import nick_color, strip_formatting, to_html
from maxchat.ui.prefs_dialog import PreferencesDialog
from maxchat.ui.about_dialog import AboutDialog
from maxchat.ui.comic_settings import ComicSettingsDialog
from maxchat.ui.channel_modes import ChannelModesDialog
from maxchat.ui.ignore_list import IgnoreListDialog, normalize_mask
from maxchat.ui.alias_editor import AliasEditorDialog
from maxchat.ui.friends_list import FriendsDialog
from maxchat.ui.scripts_dialog import ScriptsDialog
from maxchat.scripting import ScriptManager
from maxchat.comic import assets as comic_assets
from maxchat.comic.characters import load_character
from maxchat.comic.renderer import panel_min_font, render_panel

SERVER_BUFFER = "(server)"
PLACEHOLDER_SERVER_ITEM = "__placeholder_server__"

# Rebindable chat navigation shortcuts: (id, label, default key). Edited in Settings ▸ Keyboard
# Shortcuts; user overrides live in the `shortcuts` pref. Alt+1–9 and Alt+` are fixed extras.
NAV_SHORTCUTS = [
    ("buffer_prev", "Previous chat", "Ctrl+PgUp"),
    ("buffer_next", "Next chat", "Ctrl+PgDown"),
    ("buffer_activity", "Jump to active chat", "Alt+A"),
    ("buffer_close", "Close chat", "Ctrl+W"),
    ("buffer_clear", "Clear chat", "Ctrl+L"),
    ("find", "Find in chat", "Ctrl+F"),
    ("color_picker", "Color picker", "Ctrl+K"),
]

# User-list role groups (IRCCloud-style headers), highest status first. "" = plain members.
MEMBER_GROUPS = [
    ("~", "Owners"), ("&", "Admins"), ("@", "Operators"),
    ("%", "Half-Ops"), ("+", "Voiced"), ("", "Members"),
]

# Comic panels. The reference Comic Chat background art is 315×315 (square), so panels are SQUARE.
# We scale the box down a touch so the 2×2 grid still leaves room for the chat below ("not too big").
BG_SIZE = 315          # native background art size (square) — drives the panel aspect
MAX_COMIC_PANELS = 6   # panels cached per channel; the strip shows the last N that fit (older rotate off)
NETWORKS_MERGE_VERSION = 4  # bump when DEFAULT_NETWORKS gains networks/servers to merge into existing configs
CONNECT_ATTEMPT_TIMEOUT_MS = 20_000  # fail a stuck TCP/TLS/register attempt so failover can continue
SERVER_RETRY_LIMIT = 3  # try each server this many times before moving to the next failover server
LOOK_KEYS = (  # the prefs that make up a saved "look" — the whole visual appearance
    "theme", "chat_theme", "wallpaper",
    "chat_font_family", "chat_font_size", "chat_font_bold", "chat_text_color",
    "app_font_family", "app_font_size", "app_font_bold",
    "list_font_family", "list_font_size", "list_font_bold",
    "nick_label_color", "status_text_color", "topic_color",
    "tree_color", "userlist_color", "event_color", "separator_line",
    "nick_font_family", "nick_font_size", "nick_font_bold",
    "status_font_family", "status_font_size", "status_font_bold",
    "topic_font_family", "topic_font_size", "topic_font_bold",
)


def _apply_mode_delta(state: dict, modestr: str, params: list) -> None:
    """Fold a MODE change (e.g. '+nt-k', ['key']) into a tracked {flags,key,limit} channel state.
    Per-user / list modes (o v h a q b e I) are skipped — they're not channel-wide flags."""
    adding, pi = True, 0
    for ch in modestr:
        if ch in "+-":
            adding = ch == "+"
            continue
        takes_param = ch in "kovhaqbeI" or (ch == "l" and adding)
        param = ""
        if takes_param and pi < len(params):
            param, pi = params[pi], pi + 1
        if ch == "k":
            state["key"] = param if adding else ""
        elif ch == "l":
            state["limit"] = (int(param) if param.isdigit() else 0) if adding else 0
        elif ch in "ovhaqbeI":
            continue  # nick/mask modes, tracked elsewhere (user list / ban editor)
        else:
            (state["flags"].add if adding else state["flags"].discard)(ch)


class _ElidingLabel(QLabel):
    """A single-line label that trims its text with '…' to fit its width (re-eliding on resize), so a long
    channel topic can't force the window wider. Keeps the full text available for the tooltip/caller."""

    def __init__(self, text: str = "") -> None:
        super().__init__()
        self._full = text

    def setFullText(self, text: str) -> None:
        self._full = text
        self._elide()

    def fullText(self) -> str:
        return self._full

    def resizeEvent(self, e) -> None:  # noqa: N802 (Qt override) — re-elide as the bar width changes
        super().resizeEvent(e)
        self._elide()

    def _elide(self) -> None:
        fm = self.fontMetrics()
        super().setText(fm.elidedText(self._full, Qt.TextElideMode.ElideRight, max(0, self.width() - 8)))


class _ClosingMenuBar(QMenuBar):
    """Menu bar that actively closes old popups before opening another top-level menu.

    WSLg can leave QMenu popup windows alive when switching top-level menus. Closing them here catches
    the user action before Qt starts opening the next menu. This deliberately only touches the menu
    bar's own top-level pull-down menus; context menus, combos, dialogs, and other popups stay native.
    """

    def mousePressEvent(self, event) -> None:
        if self._close_existing_for(event.pos(), close_same=True):
            event.accept()
            return
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event) -> None:
        self._close_existing_for(event.pos(), close_same=False)
        super().mouseMoveEvent(event)

    def _close_existing_for(self, pos, close_same: bool) -> bool:
        action = self.actionAt(pos)
        visible = self._visible_menus()
        if not visible:
            return False
        same_menu = action is not None and any(menu.menuAction() is action for menu in visible)
        if same_menu and not close_same:
            return False
        self._close_visible_menus()
        QApplication.processEvents(QEventLoop.ProcessEventsFlag.ExcludeUserInputEvents)
        return same_menu

    def close_other_top_menus(self, opening: QMenu) -> None:
        for menu in self._visible_menus():
            if menu is not opening:
                self._force_close(menu)

    def _visible_menus(self) -> list[QMenu]:
        menus: list[QMenu] = []
        for action in self.actions():
            menu = action.menu()
            if isinstance(menu, QMenu) and menu.isVisible():
                menus.append(menu)
        return menus

    def _close_visible_menus(self) -> None:
        for menu in self._visible_menus():
            self._force_close(menu)

    @staticmethod
    def _force_close(menu: QMenu) -> None:
        menu.setActiveAction(None)
        menu.close()
        if menu.isVisible():
            menu.hide()
        handle = menu.windowHandle()
        if handle is not None:
            handle.hide()
            handle.close()
        menu.destroy(True, True)


class Network:
    """One server connection: its IRC client + all per-connection UI state."""

    def __init__(self, net_id: int, name: str, client: IRCClient) -> None:
        self.id = net_id
        self.name = name
        self.client = client
        self.buffers: dict[str, ChatView] = {}
        self.tree_items: dict[str, QTreeWidgetItem] = {}
        self.root_item: QTreeWidgetItem | None = None  # the network "bar" in the tree
        self.spacer_item: QTreeWidgetItem | None = None  # slim gap above this network (after the 1st)
        self.names: dict[str, list] = {}
        self.pending_names: dict[str, list] = {}
        self.joined_channels: set[str] = set()
        self.topics: dict[str, str] = {}
        self.away: set[str] = set()
        self.channel_modes: dict[str, dict] = {}  # chan → {"flags": set, "key": str, "limit": int}
        self.online_friends: set | None = None    # notify-list nicks seen online (None until first poll)
        self.unread_marked: set[str] = set()
        self.autojoin: set[str] = set()
        self.autojoin_focus: str | None = None
        self.reconnect_params: dict | None = None
        self.reconnect_attempt = 0
        self.intentional = False
        self.connected = False
        self.servers: list = []        # [(host, port, tls), …] — failover cycles through these
        self.server_index = 0
        self.server_attempt = 0        # tries on the current server before failover advances
        self.connect_generation = 0    # increments for each socket attempt; invalidates old timers
        self.connection_key = ""       # stable live-connection identity; name stays display-only
        self.reconnect_pending = False  # debounce: a reconnect is already scheduled for this net
        self.perform: list = []        # commands run after registering (perform-on-connect)


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle(f"{__app_name__} {__version__}")
        self.resize(1000, 680)
        self._networks: dict[int, Network] = {}
        self._net_seq = 0
        self._active_net: Network | None = None
        self._active_buf: str | None = None
        self._placeholder_tree_item: QTreeWidgetItem | None = None
        self._theme = config.get_setting("theme", "dark")
        self._merge_default_networks()  # add any newly-bundled networks to an existing address book
        self._load_prefs()
        self._migrate_comic_patterns()  # ensure newer default bot-patterns (e.g. "s/") reach old configs
        self._migrate_logging_for_replay()  # turn logging on if replay-on-open is set but logging was off
        self._migrate_flood_off()  # the old default-on flood auto-ignore silently muted bots (globally)
        self._apply_chat_appearance()
        # comic state
        self._comic: dict = {}                # (net_id, buffer) → {"strip": [pixmaps], "lines": [tuples]}
        self._comic_bg_img = None             # global default background image
        self._comic_bg_cache: dict = {}       # bg filename → loaded image (per-channel backgrounds)
        self._comic_blank_cache: dict = {}    # bg key → background-only placeholder pixmap
        self._comic_backgrounds: list = []
        self._comic_characters: list = []
        self._nick_char: dict = {}            # nick → character file path (deterministic fallback)
        self._my_emotion = "auto"             # your chosen emotion ("auto" = guess from text)
        self._emotion_wheel = None            # the MS-Chat-style emotion popup (lazy)
        self._flood_times: dict = {}          # (net_id, nick) → recent message timestamps (flood guard)
        self._invite_times: dict = {}         # (net_id, nick) → recent INVITE timestamps
        self._pending_channel_closes: set[tuple[int, str]] = set()
        self._paste_timers: dict[tuple[int, str], list[QTimer]] = {}
        self._tray_menu = None
        self._cached_app_icon = None
        self._sound = sounds.SoundPlayer()  # CTCP SOUND playback (default off; bring-your-own .wav)
        self._notifier = notifier.Notifier()  # custom corner toasts for PM / highlight alerts
        self._started = time.monotonic()  # client uptime baseline (for /uptime)
        self._unread: set = set()      # (net.id, buf) with activity — drives the title/tray count
        self._unread_hi: set = set()   # …of which these carry a highlight (shown as ⚑)

        self._build_menu()
        self._build_toolbar()
        self._build_ui()
        self._apply_status_colors()  # nick-label + status-bar text colours (after the widgets exist)
        self._show_placeholder()
        self._restore_geometry()
        self._build_tray()
        if config.pref("update_check"):  # quiet GitHub Releases check shortly after launch
            QTimer.singleShot(3500, lambda: self._check_updates(manual=False))
        for _seq, _d in (("Ctrl+=", 1), ("Ctrl++", 1), ("Ctrl+-", -1), ("Ctrl+0", 0)):  # zoom chat font
            QShortcut(QKeySequence(_seq), self).activated.connect(lambda dd=_d: self._zoom_chat(dd))
        self._ison_timer = QTimer(self)  # poll the notify list for online/offline changes
        self._ison_timer.timeout.connect(self._poll_friends)
        self._ison_timer.start(45000)
        self._scripts = ScriptManager(self)  # Python plugin system
        self._scripts.load_all()             # loads user scripts (seeds the examples on first run)
        self._dcc = DCCManager(              # DCC file transfer (active + passive/reverse)
            ip_provider=self._dcc_ip, download_dir=self._dcc_dir,
            port_range=(int(config.pref("dcc_port_first") or 0), int(config.pref("dcc_port_last") or 0)),
            passive=bool(config.pref("dcc_passive")),
        )
        self._dcc_window = None
        self._dcc_chats: dict = {}      # (net.id, "=nick") → DCCChat
        self._pending_dcc_net = None    # net context for the in-flight DCC handshake
        self._urls: list = []           # (time, nick, where, url) seen in chat — the URL grabber
        self._url_window = None
        self._rawlog: list = []         # (net_name, "<<"/">>", line) — raw IRC traffic (capped)
        self._rawlog_window = None
        self._dcc.added.connect(self._on_dcc_added)
        self._dcc.chatStarted.connect(self._on_dcc_chat)
        self.input.setFocus()  # land in the message box so you can type straight away
        app = QApplication.instance()
        if app is not None:  # type anywhere (not in a field/menu/dialog) → jump to the message box; Esc too
            app.installEventFilter(self)
        QTimer.singleShot(0, self._maybe_autoconnect)  # auto-connect after the window is shown

    def _maybe_autoconnect(self) -> None:
        """On launch, connect the networks flagged 'connect on startup' — IN LIST ORDER, one at a time
        (each fully registers, or times out, before the next starts). If none are flagged, fall back to
        the top network (the fallback behaviour)."""
        self.input.setFocus()
        if not config.pref("connect_on_start"):
            return
        nets = config.pref("networks") or []
        queue = [dict(n) for n in nets if n.get("autoconnect")]
        if not queue and nets:
            queue = [dict(nets[0])]
        self._autoconnect_queue = queue
        self._autoconnect_pending = None
        self._autoconnect_step()

    def _autoconnect_step(self) -> None:
        queue = getattr(self, "_autoconnect_queue", None)
        if not queue:
            return
        net = queue.pop(0)
        self._autoconnect_pending = (net.get("name") or net.get("host") or "").lower()
        self._connect_network(net)
        # advance when it registers (see _on_registered) — or after a timeout if it never connects
        QTimer.singleShot(25000, lambda nm=self._autoconnect_pending: self._autoconnect_advance(nm))

    def _autoconnect_advance(self, name: str) -> None:
        """Move to the next queued network — but only once per network (registration vs the stale timer
        vs a failed-connect disconnect all funnel here)."""
        if (getattr(self, "_autoconnect_pending", None) or "") != (name or ""):
            return
        self._autoconnect_pending = None
        self._autoconnect_step()

    # ---- preferences ------------------------------------------------------
    def _load_prefs(self) -> None:
        self._show_ts = bool(config.pref("show_timestamps"))
        self._ts_format = str(config.pref("timestamp_format") or "%I:%M %p")
        self._colored_nicks = bool(config.pref("colored_nicks"))
        self._show_fmt = bool(config.pref("show_formatting"))
        self._hide_jp = bool(config.pref("hide_joinpart"))
        self._show_mode = bool(config.pref("show_mode"))
        self._logging = bool(config.pref("logging"))
        self._pm_echo = bool(config.pref("pm_echo"))
        self._notify_pm = bool(config.pref("notify_pm"))            # toast/flash on private messages
        self._notify_highlight = bool(config.pref("notify_highlight"))  # …on nick / highlight-word hits
        if getattr(self, "act_dnd", None) is not None:  # keep the DnD toggle in sync after a prefs change
            self.act_dnd.blockSignals(True)
            self.act_dnd.setChecked(bool(config.pref("dnd")))
            self.act_dnd.blockSignals(False)
        if getattr(self, "_networks", None):  # re-push CTCP VERSION reply prefs to live clients
            self._apply_ctcp_settings()
        self._minimize_to_tray = bool(config.pref("minimize_to_tray"))
        self._friends = [str(f) for f in (config.pref("friends") or [])]
        self._aliases = {str(k): str(v) for k, v in (config.pref("aliases") or {}).items()}
        self._ignores = list(config.pref("ignores") or [])
        # comic ignore set + command/regex filters are loaded in _load_comic_prefs() (called just below)
        self._flood_protect = bool(config.pref("flood_protect"))
        self._flood_msgs = int(config.pref("flood_msgs") or 6)
        self._flood_secs = float(config.pref("flood_secs") or 4)
        self._paste_guard = bool(config.pref("paste_guard"))
        self._paste_lines = int(config.pref("paste_lines") or 4)
        self._ignore_invites = bool(config.pref("ignore_invites"))
        self._invite_protect = bool(config.pref("invite_protect"))
        self._replay_log_on = bool(config.pref("replay_log"))
        self._replay_lines = int(config.pref("replay_lines") or 0)
        self._beep_highlight = bool(config.pref("beep_highlight"))
        self._highlight_words = [
            w for w in str(config.pref("highlight_words") or "").lower().replace(",", " ").split() if w
        ]
        self._font_family = str(config.pref("chat_font_family") or "")
        self._font_size = int(config.pref("chat_font_size") or 0)
        self._chat_font_bold = bool(config.pref("chat_font_bold"))
        self._chat_text_color = str(config.pref("chat_text_color") or "")
        self._list_font_bold = bool(config.pref("list_font_bold"))
        self._scrollback = int(config.pref("scrollback") or 2000)
        self._auto_reconnect = bool(config.pref("auto_reconnect"))
        self._indent_wrap = bool(config.pref("indent_wrap"))
        self._marker_line = bool(config.pref("marker_line"))
        self._align_nicks = bool(config.pref("align_nicks"))
        # One-time lift: the old default (10) was too narrow for common nicks → bump a saved 10 to 16.
        if config.get_setting("nick_width", None) == 10:
            config.set_pref("nick_width", 16)
        self._nick_width = int(config.pref("nick_width") or 16)
        # One-time: the "chat line" separator now defaults on — flip a stale saved-off once (then the
        # user's own choice sticks). Old configs saved separator_line=False from the previous default.
        if not config.get_setting("sep_line_v2", False):
            config.set_pref("separator_line", True)
            config.set_pref("sep_line_v2", True)
        self._word_wrap = bool(config.pref("word_wrap"))
        self._sort_status = bool(config.pref("sort_status"))
        self._strip_copy = bool(config.pref("strip_color_copy"))
        self._separator_line = bool(config.pref("separator_line"))
        self._content_services = self._load_content_services()
        self._use_tabs = bool(config.pref("buffer_tabs"))
        self._chat_theme = str(config.pref("chat_theme") or "follow")
        self._list_font_size = int(config.pref("list_font_size") or 0)
        self._list_font_family = str(config.pref("list_font_family") or "")
        self._event_color = str(config.pref("event_color") or "")
        self._load_comic_prefs()

    def _load_content_services(self) -> dict:
        """Per-service link-preview toggles (Preferences ▸ Services), merged over the defaults so a new
        service is on until switched off. Migrates the old single ``load_images`` toggle once."""
        from maxchat.content_services import DEFAULT_CONTENT_SERVICES
        svc = dict(DEFAULT_CONTENT_SERVICES)
        saved = config.get_setting("content_services", None)
        if isinstance(saved, dict):
            svc.update({k: bool(v) for k, v in saved.items()})
        elif config.get_setting("load_images", True) is False:  # legacy: previews were globally off
            svc = {k: False for k in svc}
        return svc

    def _service_on(self, key: str) -> bool:
        return bool(self._content_services.get(key, True))

    def _migrate_logging_for_replay(self) -> None:
        """One-time: the resume divider + history replay read from the logs, but ``logging`` used to
        default off — so on an existing config replay had nothing to show. If you have replay-on-open
        enabled but logging was off, turn logging on once (you can turn it back off in Preferences ▸
        Messages). Runs a single time, so a deliberate later 'logging off' sticks."""
        if config.get_setting("logging_for_replay_migrated", False):
            return
        if bool(config.pref("replay_log")) and not bool(config.get_setting("logging", False)):
            config.set_pref("logging", True)
            self._logging = True
            QTimer.singleShot(0, lambda: self._note(  # after the UI exists
                "* Logging enabled so chat history can resume on reopen (Preferences ▸ Messages to "
                "change). New history appears next time you open a channel."))
        config.set_setting("logging_for_replay_migrated", True)

    def _migrate_flood_off(self) -> None:
        """One-time: turn OFF flood auto-ignore for existing configs. It was default-on and silently
        ignored chatty BOTS with a GLOBAL nick mask, hiding them on every network. Re-enable in
        Preferences ▸ Protection if you actually want it. (Doesn't remove ignores it already added —
        use /unignore or the Ignore list editor for those.)"""
        if config.get_setting("flood_off_migrated", False):
            return
        config.set_pref("flood_protect", False)
        self._flood_protect = False
        config.set_setting("flood_off_migrated", True)

    def _migrate_comic_patterns(self) -> None:
        """One-time: make sure the bot-command patterns added since a config was first saved (notably the
        ``s/…`` correction prefix) are present, so old configs filter them out of the comic. Runs once;
        if you later delete a pattern on purpose it stays deleted."""
        if config.get_setting("comic_patterns_migrated", False):
            return
        pats = [str(p) for p in (config.get_setting("comic_bot_patterns", None) or [])]
        if pats:  # only touch a list the user actually saved; a fresh config already has the defaults
            low = {p.lower() for p in pats}
            for need in ("s/",):
                if need not in low:
                    pats.append(need)
            config.set_pref("comic_bot_patterns", pats)
            self._comic_bot_patterns = [p.lower() for p in pats if p]
        config.set_setting("comic_patterns_migrated", True)

    @staticmethod
    def _is_dark_hex(h: str) -> bool:
        h = h.lstrip("#")
        r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
        return (0.299 * r + 0.587 * g + 0.114 * b) < 128

    @staticmethod
    def _rgb_hex(c) -> str:
        return "#%02x%02x%02x" % (int(c[0]), int(c[1]), int(c[2]))

    def _all_views(self):
        for net in self._networks.values():
            yield from net.buffers.values()

    def _chat_appearance(self) -> tuple[str, str, QFont]:
        """(bg_hex, fg_hex, font) for the chat area — from the chat theme, independent of the app theme."""
        d = theme.CHAT_THEMES.get(self._chat_theme)
        if d and "bg" in d:
            bg = "#%02x%02x%02x" % d["bg"]
            fg = "#%02x%02x%02x" % d["fg"]
            fixed = d["fixed"]
        else:  # "follow" → match the app theme's chat colours, fixed font
            fg, bg = theme.chat_colors(self._theme)
            fixed = True
        if self._chat_text_color:  # explicit chat-text colour override (Fonts ▸ Chat ▸ Colour)
            fg = self._chat_text_color
        if self._font_family:
            font = QFont(self._font_family)
        elif fixed:
            font = QFont(fonts.default_mono())  # bundled JetBrains Mono (else the system fixed font)
        else:
            app = QApplication.instance()
            font = QFont(app.font()) if app is not None else QFont()
        font.setPointSize(self._font_size or 14)  # default 14pt for the bundled JetBrains Mono
        font.setBold(self._chat_font_bold)
        # fallback: chat font → nerd symbols → colour emoji, so glyphs/icons/emoji all resolve
        primary = font.family() or font.defaultFamily()
        font.setFamilies(fonts.fallback_chain(primary))
        return bg, fg, font

    def _chat_bg_css(self, bg: str) -> str:
        """Chat background as QSS — translucent (so a theme wallpaper glows through) when the chat
        follows an app theme that has a wallpaper; otherwise the solid colour."""
        if self._chat_theme == "follow" and bg.startswith("#") and theme.has_wallpaper(self._theme):
            h = bg.lstrip("#")
            return f"rgba({int(h[0:2], 16)},{int(h[2:4], 16)},{int(h[4:6], 16)},0.74)"
        return bg

    def _style_view(self, v: ChatView) -> None:
        bg, fg, font = self._chat_appearance()
        v.setStyleSheet(f"QTextEdit{{background:{self._chat_bg_css(bg)};color:{fg};border:none;}}")
        v.setFont(font)
        self._apply_view_options(v, fg)

    def _apply_view_options(self, v, fg: str) -> None:
        """ChatView extras (strip-on-copy + nick separator rule) — kept in sync with the prefs."""
        if isinstance(v, ChatView):
            v.strip_on_copy = self._strip_copy
            # The chat line sits just right of the nick column, which starts AFTER the timestamp —
            # so the column offset is len(timestamp) + nick_width (+0.5 to centre it in the gap).
            v.ts_cols = len(self._ts_plain())  # so a drag can convert the line x back to a nick width
            v.separator_cols = (
                (v.ts_cols + self._nick_width + 0.5)
                if (self._separator_line and self._align_nicks) else 0.0
            )
            line = QColor(fg)
            line.setAlpha(130)  # a visible rule in the chat foreground colour (reads over a wallpaper)
            v.separator_color = line
            v.embed_border = QColor(0, 0, 0, 160)  # a darker outline around image / X-card embeds
            v.viewport().update()

    def _reflow_all_buffers(self) -> None:
        """Re-pad every buffer's lines to the current nick width + timestamp so existing nicks, text and
        embeds line up to the chat line. Safe only when the timestamp width is unchanged (the per-block
        pad offset assumes it) — callers guard that."""
        ts_len = len(self._ts_plain())
        nw = self._nick_width
        for v in self._all_views():
            margin = (round(v.fontMetrics().horizontalAdvance(self._ts_plain() + " " * (nw + 1)))
                      if self._align_nicks else 0)  # the column follows align-nicks, not wrap
            v.reflow_nick_column(ts_len, nw, margin)

    def _set_nick_width(self, nick_w: int) -> None:
        """Apply a new nick-column width: persist it + reflow every buffer in place (existing lines +
        embeds re-align to the new column), then snap the rule to the exact column."""
        nick_w = max(4, min(40, int(nick_w)))
        if nick_w != self._nick_width:
            self._nick_width = nick_w
            config.set_pref("nick_width", nick_w)
            self._reflow_all_buffers()
        self._apply_chat_appearance()  # snap the rule to the exact column (+ refresh the spin in prefs)

    def _on_separator_moved(self, nick_w: int) -> None:
        """The chat line was dragged → resize the nick column to it."""
        self._set_nick_width(nick_w)

    def _autoset_nick_width(self, nick: str) -> None:
        """First connect only: widen the nick column to fit YOUR nick (a 30-char nick gets a wide
        column instead of overflowing the chat line). Only ever EXPANDS — never shrinks a column you've
        already widened by hand — and runs once (then you drag it)."""
        if config.get_setting("nick_width_autoset", False) or not nick or not self._align_nicks:
            return
        config.set_pref("nick_width_autoset", True)
        self._set_nick_width(max(self._nick_width, len(nick) + 2))  # +2 for the < > around it

    def _apply_list_font(self) -> None:
        app = QApplication.instance()
        default = app.font().pointSize() if app is not None else 10
        size = self._list_font_size or default
        for widget in (self.tree, self.members):
            f = widget.font()
            if self._list_font_family:  # per-section family for the tree + user list (else the app font)
                f.setFamily(self._list_font_family)
            else:
                f.setFamily(app.font().family() if app is not None else f.defaultFamily())
            f.setPointSize(size)
            f.setBold(self._list_font_bold)
            widget.setFont(f)

    def _apply_app_font(self) -> None:
        app = QApplication.instance()
        if app is None:
            return
        fam = str(config.pref("app_font_family") or "") or fonts.default_mono()  # default: JetBrains Mono
        size = int(config.pref("app_font_size") or 0) or 14
        f = QFont(fam, size)
        f.setBold(bool(config.pref("app_font_bold")))
        app.setFont(f)
        app.setStyleSheet(theme.stylesheet(self._theme))  # repolish so widgets adopt the new font

    def _apply_prefs_to_views(self) -> None:
        self._apply_chat_appearance()  # chat-theme bg/fg + font for every buffer view
        for view in self._all_views():
            view.set_scrollback(self._scrollback)
            view.set_wrap(self._word_wrap)
        self.input.paste_guard = self._paste_guard  # re-apply protection settings
        self.input.paste_lines = self._paste_lines
        self.input.set_spellcheck(bool(config.pref("spellcheck_enabled")), str(config.pref("spell_language") or "en"))
        self._apply_input_hint()  # show/hide the message-box hint text
        self._apply_button_bar()  # show/hide the button bar + sync the View checkmark
        self._apply_app_icon()  # window/tray icon choice (bubble vs emoji)
        self._apply_status_colors()  # nick-label + status-bar text colours
        self._dcc.passive = bool(config.pref("dcc_passive"))  # re-apply DCC settings
        self._dcc._ports = (int(config.pref("dcc_port_first") or 0), int(config.pref("dcc_port_last") or 0))

    def _open_prefs(self, initial_tab: str | None = None) -> None:
        app = QApplication.instance()
        app_font = app.font() if app is not None else QFont()
        chat_font = self._chat_appearance()[2]  # the live chat font, so the picker pre-fills it
        if not PreferencesDialog(self, initial_tab, app_font=app_font, chat_font=chat_font,
                                 browse_chars=self._open_character_gallery,
                                 open_comic_settings=self._open_comic_settings,
                                 test_notify=self._test_notification).exec():
            return
        prev = (self._nick_width, self._align_nicks, self._show_ts, self._ts_format)  # column inputs
        self._load_prefs()
        new_theme = config.get_setting("theme", "dark")
        if new_theme != self._theme:
            self._set_theme(new_theme)
        else:
            self._rebuild_theme_menu()  # reflect any newly-saved custom app theme
        self._apply_app_font()
        self._apply_prefs_to_views()
        self._apply_list_font()
        self._refresh_active_members()  # colour/group the user list per the new colour-nicks setting
        self._load_comic_art()          # pick up a changed art folder / character / background
        self._build_comic_panels(self._comic_panels)
        self._rebuild_chat_theme_menu()
        # Re-align existing messages if the nick column changed — but only when the timestamp width is
        # the SAME (reflow re-pads at a fixed offset; a changed clock format would mis-pad old lines).
        if ((self._show_ts, self._ts_format) == (prev[2], prev[3])
                and (self._nick_width, self._align_nicks) != (prev[0], prev[1])):
            self._reflow_all_buffers()

    def _open_character_gallery(self) -> None:
        """Comic ▸ Browse Characters — show every available comic figure so you can see who's who."""
        if not self._comic_characters:
            self._load_comic_art()
        if not self._comic_characters:
            shadow_message.information(
                self, "Browse Characters",
                "No comic art found.\n\nSet your comic art folder in Comic ▸ Comic Settings first.",
            )
            return
        items = []
        for path in self._comic_characters:
            ch = load_character(path)
            img = ch.image("neutral", "right", 0) if ch is not None else None
            items.append((path.stem, img))
        from maxchat.ui.character_gallery import CharacterGallery
        CharacterGallery(self, items).exec()

    def _char_preview_image(self, stem: str):
        """A character's standing-figure image (cached), for the assign / gallery previews."""
        if not stem:
            return None
        cache = getattr(self, "_char_preview_cache", None)
        if cache is None:
            cache = self._char_preview_cache = {}
        if stem not in cache:
            path = self._char_path_by_stem(stem)
            ch = load_character(path) if path is not None else None
            cache[stem] = ch.image("neutral", "right", 0) if ch is not None else None
        return cache[stem]

    def _open_comic_settings(self) -> None:
        """Comic Settings: art folder + Global defaults + Characters + per-channel + Bots."""
        if not self._comic_characters and not self._comic_backgrounds:
            self._load_comic_art()  # load art on first use so the pickers are populated
        # every open channel across all networks → (key, "label", members); the active one first
        channels = []
        active = (self._active_net, self._active_buf)
        for n in self._networks.values():
            for name in n.buffers:
                if name.startswith(("#", "&")):
                    mem = [self._strip(m) for m in n.names.get(name, [])]
                    entry = (self._chan_key(n, name), f"{name}  ·  {n.name}", mem)
                    channels.insert(0, entry) if (n, name) == active else channels.append(entry)
        dlg = ComicSettingsDialog(
            self,
            characters=[p.stem for p in self._comic_characters],
            backgrounds=[(p.stem, p.name) for p in self._comic_backgrounds],
            channels=channels, art_dir=self._comic_dir,
        )
        if not dlg.exec():
            return
        self._load_comic_prefs()  # re-read globals + assignments + per-channel overrides
        if len(self._comic_panel_widgets) != self._comic_panels:
            self._build_comic_panels(self._comic_panels)
        self._load_comic_art()    # rebuild caches; the default background may have changed
        self._rerender_channel()  # redraw the visible strip with the new characters/background

    def _comic_sheet(self):
        """Compose the active channel's cached panels into one QImage (None if there are no panels)."""
        from PySide6.QtGui import QImage

        state = self._comic_cur()
        if state is not None and state.get("dirty") and self._comic_ready:
            self._rebuild_strip(state)  # buffer composed in the background — draw before exporting
        strip = list(state["strip"]) if state else []
        if not strip:
            return None
        cols = len(strip) if len(strip) <= 4 else (len(strip) + 1) // 2  # 1 row up to 4, else 2 rows
        rows = (len(strip) + cols - 1) // cols
        pw = max(p.width() for p in strip)
        ph = max(p.height() for p in strip)
        gap = 8
        sheet = QImage(cols * pw + (cols + 1) * gap, rows * ph + (rows + 1) * gap,
                       QImage.Format.Format_RGB32)
        sheet.fill(QColor("white"))
        painter = QPainter(sheet)
        for i, pm in enumerate(strip):
            r, c = divmod(i, cols)
            painter.drawPixmap(gap + c * (pw + gap), gap + r * (ph + gap), pm)
        painter.end()
        return sheet

    def _save_comic(self) -> None:
        """Save the active channel's comic strip (the cached panels) as a single PNG."""
        from PySide6.QtWidgets import QFileDialog

        sheet = self._comic_sheet()
        if sheet is None:
            shadow_message.information(
                self, "Save Comic",
                "No comic panels yet — turn on Comic Mode and let some messages come in first.")
            return
        chan = (self._active_buf or "comic").lstrip("#&").replace("/", "_") or "comic"
        path, _ = QFileDialog.getSaveFileName(self, "Save Comic", f"comic-{chan}.png", "PNG image (*.png)")
        if not path:
            return
        if not path.lower().endswith(".png"):
            path += ".png"
        if sheet.save(path):
            self._note(f"* Saved comic → {path}")
        else:
            shadow_message.warning(self, "Save Comic", f"Could not write {path}")

    def _copy_comic(self) -> None:
        """Copy the active channel's comic strip to the clipboard as an image."""
        sheet = self._comic_sheet()
        if sheet is None:
            shadow_message.information(self, "Copy Comic", "No comic panels yet.")
            return
        from PySide6.QtGui import QGuiApplication
        QGuiApplication.clipboard().setImage(sheet)
        self._note("* Comic image copied to the clipboard.")

    def _toggle_comic_captions(self, on: bool) -> None:
        config.set_pref("comic_captions", bool(on))
        self._comic_blank_cache.clear()
        self._rerender_channel()  # redraw the visible strip with/without name labels

    def _copy_panel(self, panel) -> None:
        pm = panel.pixmap() if panel is not None else None
        if pm is not None and not pm.isNull():
            from PySide6.QtGui import QGuiApplication
            QGuiApplication.clipboard().setImage(pm.toImage())
            self._note("* Panel copied to the clipboard.")

    def _comic_menu(self, panel=None) -> None:
        """Right-click menu over the comic area: settings · emotion · turn off · copy / save image."""
        from PySide6.QtGui import QCursor
        menu = QMenu(self)
        menu.addAction(self.act_comic_settings)
        menu.addAction(self.act_emotion)
        menu.addSeparator()
        menu.addAction("Turn comic off", lambda: self.act_comic.setChecked(False))
        menu.addSeparator()
        if panel is not None and panel.pixmap() is not None and not panel.pixmap().isNull():
            menu.addAction("Copy this panel", lambda p=panel: self._copy_panel(p))
        menu.addAction("Copy comic image", self._copy_comic)
        menu.addAction("Save comic image…", self._save_comic)
        menu.exec(QCursor.pos())

    # ---- menu -------------------------------------------------------------
    def _build_menu(self) -> None:
        menu_workaround = theme.needs_stale_menu_workaround()
        if menu_workaround and not isinstance(self.menuBar(), _ClosingMenuBar):
            self.setMenuBar(_ClosingMenuBar(self))
        mb = self.menuBar()
        mb.setNativeMenuBar(False)
        mb.setMouseTracking(menu_workaround)

        server = mb.addMenu(self.tr("&Server"))
        self.act_servers = QAction(self.tr("Server List…"), self)
        self.act_servers.setShortcut("Ctrl+S")
        self.act_servers.triggered.connect(self._open_server_list)
        self.act_connect = QAction(self.tr("Quick Connect…"), self)
        self.act_connect.triggered.connect(self._open_connect)
        self.act_disconnect = QAction(self.tr("Disconnect"), self)
        self.act_disconnect.triggered.connect(self._disconnect)
        self.act_join = QAction(self.tr("Join…"), self)
        self.act_join.setShortcut("Ctrl+J")
        self.act_join.triggered.connect(self._join_channel)
        self.act_leave = QAction(self.tr("Leave Channel"), self)
        self.act_leave.triggered.connect(self._leave_channel)
        self.act_list = QAction(self.tr("Channels…"), self)
        self.act_list.triggered.connect(self._open_channel_list)
        act_quit = QAction(self.tr("Quit"), self)
        act_quit.triggered.connect(self.close)
        server.addAction(self.act_servers)
        server.addAction(self.act_connect)
        server.addAction(self.act_disconnect)
        self.act_disconnect_all = QAction(self.tr("Disconnect All"), self)
        self.act_disconnect_all.triggered.connect(self._disconnect_all)
        server.addAction(self.act_disconnect_all)
        self.act_reconnect_all = QAction(self.tr("Reconnect All"), self)
        self.act_reconnect_all.triggered.connect(self._reconnect_all)
        server.addAction(self.act_reconnect_all)
        server.addSeparator()
        server.addAction(self.act_join)
        server.addAction(self.act_leave)
        server.addSeparator()
        server.addAction(self.act_list)
        server.addSeparator()
        server.addAction(act_quit)

        view = mb.addMenu(self.tr("&View"))
        self.act_button_bar = QAction(self.tr("Button Bar"), self)  # first item under View
        self.act_button_bar.setCheckable(True)
        self.act_button_bar.setChecked(bool(config.pref("show_button_bar")))
        self.act_button_bar.toggled.connect(self._toggle_button_bar)
        view.addAction(self.act_button_bar)
        view.addSeparator()
        self.act_tree_panel = QAction(self.tr("Server List"), self)  # slides the left server/channel panel shut
        self.act_tree_panel.setCheckable(True)
        self.act_tree_panel.setChecked(True)
        self.act_tree_panel.toggled.connect(lambda on: self._set_panel_visible(0, on))
        self.act_members = QAction(self.tr("Member List"), self)  # slides the right user-list panel shut
        self.act_members.setCheckable(True)
        self.act_members.setChecked(True)
        self.act_members.toggled.connect(lambda on: self._set_panel_visible(2, on))
        self.act_tabs = QAction(self.tr("Buttons as Tabs"), self)
        self.act_tabs.setCheckable(True)
        self.act_tabs.setChecked(bool(config.pref("buffer_tabs")))
        self.act_tabs.toggled.connect(self._toggle_buffer_tabs)
        view.addAction(self.act_tree_panel)
        view.addAction(self.act_members)
        view.addAction(self.act_tabs)
        act_mark_read = QAction(self.tr("Mark All Read"), self)
        act_mark_read.triggered.connect(self._mark_all_read)
        view.addAction(act_mark_read)
        view.addSeparator()
        self._theme_menu = view.addMenu(self.tr("Theme"))
        self._rebuild_theme_menu()
        self._chat_theme_menu = view.addMenu(self.tr("Chat Theme"))
        self._rebuild_chat_theme_menu()
        self._wallpaper_menu = view.addMenu(self.tr("Wallpaper"))
        self._rebuild_wallpaper_menu()
        self._looks_menu = view.addMenu(self.tr("Saved Looks"))
        self._rebuild_looks_menu()

        tools = mb.addMenu(self.tr("&Tools"))
        self.act_urls = act_urls = QAction(self.tr("URL List…"), self)
        act_urls.triggered.connect(self._open_url_grabber)
        tools.addAction(act_urls)
        act_rawlog = QAction(self.tr("Raw Log…"), self)
        act_rawlog.triggered.connect(self._open_raw_log)
        tools.addAction(act_rawlog)
        tools.addSeparator()
        self.act_dnd = QAction(self.tr("Do Not Disturb"), self, checkable=True)
        self.act_dnd.setChecked(bool(config.pref("dnd")))
        self.act_dnd.setToolTip(self.tr("Suppress all notifications — toast, flash, tray, beep and sound"))
        self.act_dnd.toggled.connect(self._set_dnd)
        tools.addAction(self.act_dnd)  # same QAction is reused in the tray menu, so they stay in sync

        settings = mb.addMenu(self.tr("&Settings"))
        self.act_prefs = act_prefs = QAction(self.tr("Preferences…"), self)
        act_prefs.setShortcut("Ctrl+P")
        act_prefs.triggered.connect(lambda: self._open_prefs())
        settings.addAction(act_prefs)
        act_ignore = QAction(self.tr("Ignore List…"), self)
        act_ignore.triggered.connect(self._open_ignore_list)
        settings.addAction(act_ignore)
        act_aliases = QAction(self.tr("Aliases…"), self)
        act_aliases.triggered.connect(self._open_aliases)
        settings.addAction(act_aliases)
        act_keys = QAction(self.tr("Keyboard Shortcuts…"), self)
        act_keys.triggered.connect(self._open_shortcuts)
        settings.addAction(act_keys)
        act_friends = QAction(self.tr("Friends / Notify…"), self)
        act_friends.triggered.connect(self._open_friends)
        settings.addAction(act_friends)
        act_scripts = QAction(self.tr("Scripts…"), self)
        act_scripts.triggered.connect(self._open_scripts)
        settings.addAction(act_scripts)
        self.act_dcc = act_dcc = QAction(self.tr("File Transfers…"), self)
        act_dcc.triggered.connect(self._open_dcc)
        settings.addAction(act_dcc)

        # Comic — everything comic in one pull-down (sits just before Help): on/off, emotion, settings, save
        self.act_comic = QAction(self.tr("Comic Mode"), self)
        self.act_comic.setCheckable(True)
        self.act_comic.setShortcut("Ctrl+M")
        self.act_comic.toggled.connect(self._toggle_comic)
        self.act_emotion = QAction(self.tr("Emotion…"), self)
        self.act_emotion.setToolTip(self.tr("Choose your comic expression (used for your comic panels)"))
        self.act_emotion.triggered.connect(self._toggle_emotion_wheel)  # always available
        self.act_comic_settings = QAction(self.tr("Comic Settings…"), self)
        self.act_comic_settings.triggered.connect(self._open_comic_settings)
        self.act_browse_chars = QAction(self.tr("Browse Characters…"), self)
        self.act_browse_chars.setToolTip(self.tr("See what the comic avatars look like"))
        self.act_browse_chars.triggered.connect(self._open_character_gallery)
        self.act_save_comic = QAction(self.tr("Save Comic…"), self)
        self.act_save_comic.triggered.connect(self._save_comic)
        comic = mb.addMenu(self.tr("&Comic"))
        comic.addAction(self.act_comic)
        comic.addAction(self.act_emotion)
        comic.addSeparator()
        comic.addAction(self.act_comic_settings)
        comic.addAction(self.act_browse_chars)
        comic.addAction(self.act_save_comic)
        comic.addSeparator()
        self.act_comic_captions = QAction(self.tr("Character Names"), self, checkable=True)
        self.act_comic_captions.setChecked(bool(config.pref("comic_captions")))
        self.act_comic_captions.toggled.connect(self._toggle_comic_captions)
        comic.addAction(self.act_comic_captions)

        helpm = mb.addMenu(self.tr("&Help"))
        act_help = QAction(self.tr("Commands && Shortcuts…"), self)
        act_help.setShortcut("F1")
        act_help.triggered.connect(self._open_help)
        helpm.addAction(act_help)
        helpm.addSeparator()
        act_update = QAction(self.tr("Check for Updates…"), self)
        act_update.triggered.connect(lambda: self._check_updates(manual=True))
        helpm.addAction(act_update)
        about = QAction(self.tr("About"), self)
        about.triggered.connect(self._about)
        helpm.addAction(about)

        if menu_workaround:
            self._install_top_menu_cleanup(server, view, tools, settings, comic, helpm)

    def _install_top_menu_cleanup(self, *menus: QMenu) -> None:
        for menu in menus:
            menu.aboutToShow.connect(lambda m=menu: self._close_menus_before_open(m))

    def _close_menus_before_open(self, opening: QMenu) -> None:
        mb = self.menuBar()
        if isinstance(mb, _ClosingMenuBar):
            mb.close_other_top_menus(opening)

    def _build_toolbar(self) -> None:
        tb = self.addToolBar("Main")
        tb.setObjectName("mainToolbar")  # scoped QSS: its buttons don't light up when checked
        self._toolbar = tb
        tb.setMovable(False)
        tb.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextOnly)
        # connect / navigate  ·  comic (the signature feature)  ·  view / tools
        # the checkable ones (Comic Mode, Member List) show a lit "on" state via the QSS :checked rule
        groups = (
            (self.act_tree_panel, self.act_members),  # slide the left tree / right user-list panes shut
            (self.act_list, self.act_join),           # Channel List dialog · Join
            (self.act_comic, self.act_emotion),
            (self.act_urls, self.act_dcc, self.act_prefs),
        )
        for i, group in enumerate(groups):
            if i:
                tb.addSeparator()
            for act in group:
                tb.addAction(act)
        # short labels on the BAR only (the menu actions keep their full "…" text) so it fits one row.
        # act_list/act_join already read "Channels…"/"Join…" → the bar auto-strips the "…".
        self._bar_short = {
            self.act_tree_panel: "Servers", self.act_members: "Members", self.act_comic: "Comic",
            self.act_emotion: "Emotion", self.act_urls: "URLs", self.act_dcc: "Transfers",
            self.act_prefs: "Prefs",
        }
        self._reapply_bar_labels()
        # A checkable button re-syncs its text from the action when toggled (reverting our short label) —
        # re-apply after any change so it doesn't flip between "Servers"/"Server List" on click.
        for act in (self.act_tree_panel, self.act_members, self.act_comic):
            act.changed.connect(self._reapply_bar_labels)
        tb.setVisible(bool(config.pref("show_button_bar")))  # hide option (View ▸ Button Bar)

    def _reapply_bar_labels(self) -> None:
        tb = getattr(self, "_toolbar", None)
        if tb is None:
            return
        for act, label in getattr(self, "_bar_short", {}).items():
            btn = tb.widgetForAction(act)
            if btn is not None and btn.text() != label:
                btn.setText(label)

    def _toggle_button_bar(self, on: bool) -> None:
        """Show/hide the top button bar; persisted and kept in sync with the Appearance checkbox."""
        if getattr(self, "_toolbar", None) is not None:
            self._toolbar.setVisible(on)
        config.set_pref("show_button_bar", on)

    def _apply_button_bar(self) -> None:
        """Re-apply the button-bar pref (after the Appearance tab saves) + sync the View checkmark."""
        on = bool(config.pref("show_button_bar"))
        if getattr(self, "_toolbar", None) is not None:
            self._toolbar.setVisible(on)
        self.act_button_bar.blockSignals(True)  # don't re-enter _toggle_button_bar
        self.act_button_bar.setChecked(on)
        self.act_button_bar.blockSignals(False)

    def _rebuild_theme_menu(self) -> None:
        self._theme_menu.clear()
        group = QActionGroup(self)
        group.setExclusive(True)
        for tid, label in sorted(theme.THEME_LABELS.items(), key=lambda kv: kv[1].lower()):
            a = QAction(label, self)
            a.setCheckable(True)
            a.setChecked(tid == self._theme)
            a.triggered.connect(lambda _checked=False, t=tid: self._set_theme(t))
            group.addAction(a)
            self._theme_menu.addAction(a)
        self._theme_group = group  # keep a reference alive

    def _set_theme(self, mode: str) -> None:
        self._cached_app_icon = None
        app = QApplication.instance()
        if app is not None:
            app.setPalette(theme.palette(mode))
            app.setStyleSheet(theme.stylesheet(mode))  # restyles the chat area too
        config.set_setting("theme", mode)
        self._theme = mode
        self._apply_app_font()  # re-assert the UI font: a new app-wide QSS shadows QApplication.setFont()
        self._apply_chat_appearance()
        self._apply_status_colors()  # keep the custom nick/status colours after a theme restyle
        self._rebuild_theme_menu()
        for net in self._networks.values():  # re-tint the network bars for the new theme
            self._style_network_item(net)
        self._refresh_active_members()  # header-box colours follow the app theme
        icon = self._app_icon()
        self.setWindowIcon(icon)
        if getattr(self, "_tray", None) is not None:
            self._tray.setIcon(icon)

    def _apply_chat_appearance(self) -> None:
        bg, fg, font = self._chat_appearance()
        self._fg, self._bg = fg, bg
        d = theme.CHAT_THEMES.get(self._chat_theme) or {}
        self._ts_color = (
            self._rgb_hex(d["ts"]) if "ts" in d
            else ("#8a8a8a" if self._is_dark_hex(bg) else "#6f6f6f")
        )
        self._sys_color = self._rgb_hex(d["system"]) if "system" in d else None
        self._bracket_color = self._rgb_hex(d["bracket"]) if "bracket" in d else None
        nk = d.get("nicks")
        if nk == "mono":
            self._nick_mode, self._nick_palette = "mono", None
        elif isinstance(nk, (list, tuple)) and nk:
            self._nick_mode, self._nick_palette = "palette", [self._rgb_hex(c) for c in nk]
        else:
            self._nick_mode, self._nick_palette = "default", None
        cbg = self._chat_bg_css(bg)  # translucent over a theme wallpaper, else solid
        qss = f"QTextEdit{{background:{cbg};color:{fg};border:none;}}"
        for v in self._all_views():
            v.setStyleSheet(qss)
            v.setFont(font)
            self._apply_view_options(v, fg)
        if hasattr(self, "input"):  # the input bar matches the chat (darker terminal look + chat fg)
            self.input.setStyleSheet(
                f"QLineEdit{{background:{cbg};color:{fg};border:1px solid {cbg};"
                "border-radius:4px;padding:4px 6px;}"
                'QLineEdit[spellError="true"]{border:1px solid #e5484d;}'
            )
            self.input.setFont(font)

    def _rebuild_chat_theme_menu(self) -> None:
        self._chat_theme_menu.clear()
        group = QActionGroup(self)
        group.setExclusive(True)
        for tid, label in theme.CHAT_THEME_LABELS.items():
            a = QAction(label, self)
            a.setCheckable(True)
            a.setChecked(tid == self._chat_theme)
            a.triggered.connect(lambda _checked=False, t=tid: self._set_chat_theme(t))
            group.addAction(a)
            self._chat_theme_menu.addAction(a)
        self._chat_theme_group = group  # keep a reference alive

    def _set_chat_theme(self, ct: str) -> None:
        self._chat_theme = ct
        config.set_setting("chat_theme", ct)
        self._apply_chat_appearance()
        self._rebuild_chat_theme_menu()
        self._refresh_active_members()  # member-nick colours follow the chat theme's palette

    # ---- wallpaper (window background image) ------------------------------
    def _rebuild_wallpaper_menu(self) -> None:
        self._wallpaper_menu.clear()
        group = QActionGroup(self)
        group.setExclusive(True)
        cur = str(config.pref("wallpaper") or "")

        def opt(label, checked, value):
            a = QAction(label, self)
            a.setCheckable(True)
            a.setChecked(checked)
            a.triggered.connect(lambda _c=False, v=value: self._set_wallpaper(v))
            group.addAction(a)
            self._wallpaper_menu.addAction(a)

        opt("Theme default", cur == "", "")
        opt("None", cur == "none", "none")
        bundled = self._bundled_wallpapers()
        if bundled:
            self._wallpaper_menu.addSeparator()
            for fn in bundled:  # named bundled wallpapers — Synthwave, Vaporwave… (apply to any theme)
                label = os.path.splitext(fn)[0].replace("-", " ").replace("_", " ").title()
                opt(label, cur == fn, fn)
        if cur and cur != "none" and cur not in bundled:  # a loaded custom image
            opt(f"Custom: {os.path.basename(cur)}", True, cur)
        self._wallpaper_group = group  # keep a ref alive
        self._wallpaper_menu.addSeparator()
        load = QAction("Load image…", self)
        load.triggered.connect(self._load_wallpaper)
        self._wallpaper_menu.addAction(load)

    # ---- saved looks (the whole appearance: theme + wallpaper + fonts + colours) ----
    def _rebuild_looks_menu(self) -> None:
        self._looks_menu.clear()
        looks = config.get_setting("looks", {}) or {}
        for name in sorted(looks, key=str.lower):
            self._looks_menu.addAction(name, lambda _c=False, n=name: self._apply_look(n))
        if looks:
            self._looks_menu.addSeparator()
        self._looks_menu.addAction("Save current look…", self._save_look)
        if looks:
            self._looks_menu.addAction("Delete look…", self._delete_look)

    def _save_look(self) -> None:
        name, ok = shadow_message.get_text(
            self, "Save look", "Name this look (theme + wallpaper + fonts + colors):"
        )
        name = (name or "").strip()
        if not ok or not name:
            return
        looks = dict(config.get_setting("looks", {}) or {})
        looks[name] = {k: config.pref(k) for k in LOOK_KEYS}
        config.set_setting("looks", looks)
        self._rebuild_looks_menu()
        self._note(f"* Saved look “{name}”.")

    def _apply_look(self, name: str) -> None:
        look = (config.get_setting("looks", {}) or {}).get(name)
        if not look:
            return
        for k, v in look.items():
            config.set_pref(k, v)
        self._reapply_appearance()
        self._note(f"* Applied look “{name}”.")

    def _delete_look(self) -> None:
        looks = dict(config.get_setting("looks", {}) or {})
        if not looks:
            return
        name, ok = shadow_message.get_item(self, "Delete look", "Remove which look?",
                                           sorted(looks, key=str.lower), 0)
        if ok and name in looks:
            del looks[name]
            config.set_setting("looks", looks)
            self._rebuild_looks_menu()

    def _reapply_appearance(self) -> None:
        """Re-read + apply every appearance pref (after loading a saved look or external change)."""
        self._load_prefs()
        self._set_theme(config.get_setting("theme", "dark"))  # palette/stylesheet/wallpaper + restyle
        self._apply_app_font()
        self._apply_prefs_to_views()   # chat appearance + per-section colours + input hint + button bar
        self._apply_list_font()
        self._rebuild_wallpaper_menu()
        self._rebuild_chat_theme_menu()

    @staticmethod
    def _bundled_wallpapers() -> list:
        try:
            return sorted(p.name for p in theme._WP_DIR.iterdir()
                          if p.suffix.lower() in (".png", ".jpg", ".jpeg", ".webp", ".bmp"))
        except OSError:
            return []

    def _set_wallpaper(self, value: str) -> None:
        config.set_pref("wallpaper", value)
        self._set_theme(self._theme)  # re-apply palette + stylesheet (picks up the new wallpaper)
        self._apply_chat_appearance()  # chat translucency tracks the wallpaper
        self._rebuild_wallpaper_menu()

    def _load_wallpaper(self) -> None:
        from PySide6.QtWidgets import QFileDialog
        path, _ = QFileDialog.getOpenFileName(
            self, "Choose a wallpaper image", "",
            "Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)")
        if path:
            self._set_wallpaper(path)

    # ---- layout -----------------------------------------------------------
    def _build_ui(self) -> None:
        central = QWidget(self)
        root = QVBoxLayout(central)
        root.setContentsMargins(4, 4, 4, 4)

        self.topic = _ElidingLabel("")
        self.topic.setObjectName("topicBar")
        self.topic.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        self.topic.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self.topic.setToolTip("Double-click to set the channel topic")
        # a long topic is elided with '…' and never forces the window wider than it can shrink
        self.topic.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        self.topic.setMinimumWidth(0)
        root.addWidget(self.topic)

        self._tabbar = QTabBar()  # the alternative to the tree (View ▸ Buttons as Tabs)
        self._tabbar.setDrawBase(False)  # no base line under the buttons — they're outlined chips
        self._tabbar.setExpanding(False)
        self._tabbar.setUsesScrollButtons(True)
        self._tabbar.setTabsClosable(True)
        self._tabbar.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self._tabbar.setVisible(False)
        self._rebuilding_tabs = False
        self._tabbar.currentChanged.connect(self._on_tab_changed)
        self._tabbar.tabCloseRequested.connect(self._close_tab_index)
        self._tabbar.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self._tabbar.customContextMenuRequested.connect(self._tab_menu)
        root.addWidget(self._tabbar)

        self.tree = QTreeWidget()
        self.tree.setHeaderHidden(True)
        self.tree.setMinimumWidth(0)  # draggable via the splitter: no max cap, collapsible to nothing
        self.tree.itemSelectionChanged.connect(self._on_tree_select)
        self.tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.tree.customContextMenuRequested.connect(self._tree_menu)

        self.comic_area = QFrame()
        self.comic_area.setObjectName("comicArea")
        self.comic_area.setMinimumHeight(80)
        self.comic_area.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.comic_area.customContextMenuRequested.connect(self._comic_menu)
        self._comic_panel_widgets: list[QLabel] = []
        self._build_comic_panels(self._comic_panels)
        self.comic_area.setVisible(False)

        self.stack = QStackedWidget()
        self._placeholder = QLabel("Not connected — Server ▸ Server List… or Quick Connect…")
        self._placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.stack.addWidget(self._placeholder)
        self._comic_split = QSplitter(Qt.Orientation.Vertical)  # draggable divider: comic over chat
        self._comic_split.addWidget(self.comic_area)
        self._comic_split.addWidget(self.stack)
        self._comic_split.setStretchFactor(1, 1)  # the chat takes the extra space
        self._comic_split.setCollapsible(0, False)
        self._comic_split.setSizes([300, 380])

        self.members = QListWidget()
        self.members.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.members.customContextMenuRequested.connect(self._members_menu)
        self.members.itemDoubleClicked.connect(self._member_double)
        self.members_header = QLabel("Members")
        self.members_header.setObjectName("topicBar")
        self.chan_modes_btn = QPushButton("Channel Modes…")
        self.chan_modes_btn.setEnabled(False)
        self.chan_modes_btn.clicked.connect(self._open_channel_modes)
        self._members_pane = QWidget()
        self._members_pane.setMinimumWidth(0)  # draggable: no max cap (fits long nicks), collapsible
        mpv = QVBoxLayout(self._members_pane)
        mpv.setContentsMargins(0, 0, 0, 0)
        mpv.setSpacing(2)
        mpv.addWidget(self.chan_modes_btn)
        mpv.addWidget(self.members_header)
        mpv.addWidget(self.members)

        # the chat COLUMN: comic strip / chat, then the find bar + input — so the input box lines up
        # with the chat box (it used to span the whole window, hanging over the tree + member list).
        self._find_bar = self._build_find_bar()  # Ctrl+F search bar (hidden until invoked)
        irow = QHBoxLayout()
        self.nick_label = QLabel("—")
        self.input = InputBar(completions=self._completion_candidates)
        self._apply_input_hint()
        self.input.returnPressed.connect(self._send)
        self.input.multiLinePaste.connect(self._on_paste)
        self.input.imagePasted.connect(self._on_image_pasted)
        self.input.paste_guard = self._paste_guard
        self.input.paste_lines = self._paste_lines
        self.input.set_spellcheck(bool(config.pref("spellcheck_enabled")), str(config.pref("spell_language") or "en"))
        irow.addWidget(self.nick_label)
        irow.addWidget(self.input, 1)
        from maxchat.ui.audio_bar import AudioBar
        self._audio_bar = AudioBar()  # inline mp3/audio transport (hidden until you play something)
        chat_col = QWidget()
        cv = QVBoxLayout(chat_col)
        cv.setContentsMargins(0, 0, 0, 0)
        cv.setSpacing(3)
        cv.addWidget(self._comic_split, 1)
        cv.addWidget(self._find_bar)
        cv.addWidget(self._audio_bar)
        cv.addLayout(irow)

        self._split = QSplitter(Qt.Orientation.Horizontal)
        self._split.addWidget(self.tree)
        self._split.addWidget(chat_col)
        self._split.addWidget(self._members_pane)
        self._split.setStretchFactor(1, 1)
        self._split.setSizes([180, 640, 180])
        self._panel_widths = {0: 180, 2: 180}  # remembered widths so a collapsed pane restores its size
        self._split.splitterMoved.connect(lambda *_: self._sync_panel_actions())  # keep menu ✓ in sync
        root.addWidget(self._split, 1)

        self.setCentralWidget(central)
        self.statusBar().showMessage("Not connected — Server ▸ Server List… or Quick Connect…")
        self._apply_list_font()
        self._apply_chat_appearance()  # also styles the input bar to match the chat
        self._build_nav_shortcuts()
        self._apply_buffer_layout()  # tree (default) or button bar, per the buffer_tabs pref

    def _build_nav_shortcuts(self) -> None:
        """HexChat-style keyboard navigation (see fkeys.c). The NAV_SHORTCUTS set is rebindable
        (Settings ▸ Keyboard Shortcuts); Alt+1–9 and Alt+` are fixed extras."""
        self._shortcut_slots = {
            "buffer_prev": lambda: self._switch_relative(-1),
            "buffer_next": lambda: self._switch_relative(1),
            "buffer_activity": self._switch_next_activity,
            "buffer_close": self._close_active_buffer,
            "buffer_clear": self._clear_active_buffer,
            "find": self._show_find,
            "color_picker": self._open_color_picker,
        }
        overrides = config.pref("shortcuts") or {}
        self._shortcuts = {}
        for aid, _label, default in NAV_SHORTCUTS:
            sc = QShortcut(QKeySequence(overrides.get(aid) or default), self, self._shortcut_slots[aid])
            sc.activatedAmbiguously.connect(
                lambda a=aid: self.statusBar().showMessage(f"Shortcut for {a} is ambiguous", 4000)
            )
            self._shortcuts[aid] = sc
        QShortcut(QKeySequence(Qt.Modifier.ALT | Qt.Key.Key_QuoteLeft), self, self._switch_next_activity)
        for i in range(1, 10):                                # Alt+1 … Alt+9 → chat N (fixed)
            QShortcut(QKeySequence(f"Alt+{i}"), self, lambda n=i: self._switch_by_index(n - 1))
        QShortcut(QKeySequence("F11"), self, self._toggle_maximize)  # maximise/restore toggle

    def _toggle_maximize(self) -> None:
        self.showNormal() if self.isMaximized() else self.showMaximized()

    def _open_help(self) -> None:
        """Help ▸ Commands & Shortcuts — a reference window with the live keybindings."""
        from maxchat.ui.help_dialog import HelpDialog
        overrides = config.pref("shortcuts") or {}
        rows = [(label, overrides.get(aid) or default) for aid, label, default in NAV_SHORTCUTS]
        rows += [("Switch to chat 1–9", "Alt+1 … Alt+9"),
                 ("Jump to active chat", "Alt+`"),
                 ("Zoom chat font", "Ctrl++ / Ctrl+- / Ctrl+0"),
                 ("Maximise / restore", "F11"),
                 ("This help", "F1")]
        HelpDialog(self, rows).exec()  # rows are (label, keys) — what HelpDialog expects

    def _open_color_picker(self) -> None:
        """Ctrl+K — pop the mIRC colour picker just above the message box; a pick inserts the code."""
        from maxchat.ui.color_picker import ColorPicker
        picker = getattr(self, "_color_picker", None)
        if picker is None:
            picker = self._color_picker = ColorPicker(self)
            picker.inserted.connect(self._insert_color_code)
        picker.adjustSize()
        # anchor it just above the input's caret, clamped on-screen
        gp = self.input.mapToGlobal(self.input.rect().topLeft())
        picker.move(gp.x(), gp.y() - picker.height() - 4)
        picker.show()
        self.input.setFocus()

    def _insert_color_code(self, code: str) -> None:
        self.input.insert(code)
        self.input.setFocus()

    @staticmethod
    def _section_qss(color: str, family: str, size, bold: bool = False) -> str:
        """A QSS body ('color:…;font-family:…;font-size:…pt;font-weight:bold;') from the parts that are
        set ('' / 0 / False skip)."""
        parts = []
        if color:
            parts.append(f"color:{color}")
        if family:
            parts.append(f"font-family:'{family}'")
        if size:
            parts.append(f"font-size:{int(size)}pt")
        if bold:
            parts.append("font-weight:bold")
        return (";".join(parts) + ";") if parts else ""

    def _apply_status_colors(self) -> None:
        """Recolour + re-font the per-section text per the Appearance/Fonts prefs ("" = the theme's
        own): YOUR nick (beside the input), the status bar, the topic bar, the channel tree, the user
        list. Re-applied on prefs/theme change so it survives a restyle."""
        nick = self._section_qss(config.pref("nick_label_color"), config.pref("nick_font_family"),
                                 config.pref("nick_font_size"), bool(config.pref("nick_font_bold")))
        status = self._section_qss(config.pref("status_text_color"), config.pref("status_font_family"),
                                   config.pref("status_font_size"), bool(config.pref("status_font_bold")))
        topic = self._section_qss(config.pref("topic_color"), config.pref("topic_font_family"),
                                  config.pref("topic_font_size"), bool(config.pref("topic_font_bold")))
        bold = ";font-weight:bold" if config.pref("list_font_bold") else ""
        trc = str(config.pref("tree_color") or "")
        uc = str(config.pref("userlist_color") or "")
        self.nick_label.setStyleSheet(nick)
        self.statusBar().setStyleSheet(f"QStatusBar{{{status}}}" if status else "")
        # objectName is shared with the members header, so scope by it to style only the topic
        self.topic.setStyleSheet(f"QLabel#topicBar{{{topic}}}" if topic else "")
        self.tree.setStyleSheet(f"QTreeWidget{{color:{trc or 'palette(text)'}{bold};}}" if (trc or bold) else "")
        self.members.setStyleSheet(f"QListWidget{{color:{uc or 'palette(text)'}{bold};}}" if (uc or bold) else "")

    def _apply_input_hint(self) -> None:
        """Show or hide the grey hint inside the message box (Appearance pref)."""
        if config.pref("show_input_hint"):
            self.input.setPlaceholderText(
                "Message · Tab completes · ↑/↓ history · Ctrl+B/I/U/K format"
            )
        else:
            self.input.setPlaceholderText("")

    def _apply_shortcuts(self) -> None:
        """Re-apply key bindings after the shortcut editor saves new ones."""
        overrides = config.pref("shortcuts") or {}
        for aid, _label, default in NAV_SHORTCUTS:
            sc = self._shortcuts.get(aid)
            if sc is not None:
                sc.setKey(QKeySequence(overrides.get(aid) or default))

    def _open_shortcuts(self) -> None:
        from maxchat.ui.shortcut_editor import ShortcutEditorDialog
        ShortcutEditorDialog(self, NAV_SHORTCUTS, self._apply_shortcuts).exec()

    # ---- networks ---------------------------------------------------------
    def _new_network(self, name: str) -> Network:
        self._remove_placeholder_tree_item()
        self._net_seq += 1
        net = Network(self._net_seq, name, IRCClient(self))
        self._networks[net.id] = net
        if len(self._networks) > 1:  # a visible divider line above each network after the first
            spacer = QTreeWidgetItem([""])
            spacer.setFlags(Qt.ItemFlag.ItemIsEnabled)  # non-selectable, renders its divider widget
            spacer.setSizeHint(0, QSize(0, 11))
            self.tree.addTopLevelItem(spacer)
            line = QFrame()
            line.setFrameShape(QFrame.Shape.HLine)
            line.setFrameShadow(QFrame.Shadow.Sunken)
            self.tree.setItemWidget(spacer, 0, line)
            net.spacer_item = spacer
        root = QTreeWidgetItem([name])  # the clickable network "bar"
        root.setData(0, Qt.ItemDataRole.UserRole, (net.id, SERVER_BUFFER))
        self.tree.addTopLevelItem(root)
        root.setExpanded(True)
        net.root_item = root
        net.tree_items[SERVER_BUFFER] = root
        self._style_network_item(net)
        view = self._make_view()
        net.buffers[SERVER_BUFFER] = view
        self.stack.addWidget(view)
        self._wire_client(net)
        net.client.set_ignores(self._ignores)  # apply the saved ignore masks to this connection
        self._apply_buffer_layout()
        return net

    def _style_network_item(self, net: Network) -> None:
        item = net.root_item
        if item is None:
            return
        f = item.font(0)
        f.setBold(True)
        item.setFont(0, f)
        item.setBackground(0, QBrush(QColor(theme.ui_color(self._theme, "panel2"))))
        text = theme.ui_color(self._theme, "text") if net.connected else "#9aa0a6"
        item.setForeground(0, QBrush(QColor(text)))
        item.setText(0, net.name if net.connected else f"{net.name} (offline)")

    def _find_network(self, key: str) -> Network | None:
        for net in self._networks.values():
            if (net.connection_key or net.name).lower() == key.lower():
                return net
        return None

    def _network_live(self, net: Network | None) -> bool:
        return net is not None and self._networks.get(net.id) is net

    def _call_if_network_live(self, net: Network, fn, *args):
        if self._network_live(net):
            return fn(net, *args)
        return None

    @staticmethod
    def _channel_key(name: str | None) -> str:
        return (name or "").lower()

    @staticmethod
    def _is_channel_name(name: str | None) -> bool:
        return bool(name and name.startswith(("#", "&")))

    def _is_joined_channel(self, net: Network | None, name: str | None) -> bool:
        return bool(
            self._network_live(net)
            and self._is_channel_name(name)
            and self._channel_key(name) in net.joined_channels
        )

    def _media_loader(self):
        if getattr(self, "_media", None) is None:
            from maxchat.ui.media import MediaLoader
            self._media = MediaLoader(self)
        return self._media

    def _on_link_clicked(self, view, url: str) -> None:
        from maxchat.ui import media
        from maxchat.ui.chat_view import _normalise
        full = _normalise(url)
        if media.is_image_url(full):  # open the full-size viewer (same as clicking the inline preview)
            self._show_image(full)
        elif media.is_audio_url(full) and self._audio_bar.available():  # mp3 → inline player
            self._audio_bar.play(full)
        elif media.is_video_url(full) and self._audio_bar.available():  # mp4/webm → inline + video popup
            self._audio_bar.play(full, video=True)
        # X links auto-load a summary card already; a CLICK opens the post in the browser
        else:
            from PySide6.QtCore import QUrl
            from PySide6.QtGui import QDesktopServices
            QDesktopServices.openUrl(QUrl(full))

    def _x_card(self, view, link: str, data, anchor=None, fallback_open: bool = True) -> None:
        import json
        from html import escape
        try:
            tw = (json.loads(bytes(data).decode("utf-8", "replace")) or {}).get("tweet") or {}
            author = tw.get("author") or {}
            name, screen, text = author.get("name") or "?", author.get("screen_name") or "", tw.get("text") or ""
            if not text and not author:
                raise ValueError("empty")
        except Exception:  # service down / not a tweet
            if fallback_open:  # a click → open it in the browser; an auto-preview → just skip quietly
                from PySide6.QtCore import QUrl
                from PySide6.QtGui import QDesktopServices
                QDesktopServices.openUrl(QUrl(link))
            return
        # Size the card to fit the whole status URL on the bottom line (avg ≤ ~65 chars), and cap the
        # tweet to ~3 lines so the box stays a tidy 5-row preview (handle / 3 lines / URL).
        fm = view.fontMetrics()
        cw = max(1, fm.horizontalAdvance("0"))            # monospace cell width
        pad = 26                                          # cellpadding (7×2) + breathing room
        width = max(420, min(fm.horizontalAdvance("𝕏 " + link) + pad, cw * 92 + pad))
        cpl = max(20, (width - pad) // cw)                # chars per line at this width
        maxchars = cpl * 3                                # keep the tweet to three lines
        plain = text if len(text) <= maxchars else text[: maxchars - 1].rstrip() + "…"
        body = escape(plain).replace("\n", "<br>")
        card = (  # fixed-width card sized to the URL (was 92% of the pane — far too wide)
            f'<table cellpadding="7" cellspacing="0" width="{width}" '
            f'style="background:rgba(120,140,200,0.16);"><tr><td width="{width}">'
            f'<b>{escape(name)}</b> <span style="color:#8a8f98">@{escape(screen)}</span><br>'
            f'{body}<br><span style="color:#8a8f98">𝕏 {escape(link)}</span></td></tr></table>'
        )
        return view.insert_card(card, anchor=anchor, left_margin=self._gutter_px(view))

    def _live_media_view(self, net_id: int, name: str) -> ChatView | None:
        net = self._networks.get(net_id)
        if not self._network_live(net):
            return None
        view = net.buffers.get(name)
        if view is None:
            return None
        try:
            from shiboken6 import isValid
            if not isValid(view):
                return None
        except Exception:
            pass
        return view

    def _insert_autoload_image(self, net_id: int, name: str, anchor, url: str, data: bytes,
                               click_url: str | None = None) -> None:
        if not data:
            return
        view = self._live_media_view(net_id, name)
        if view is None:
            return
        from PySide6.QtGui import QImage
        img = QImage.fromData(data)
        if img.isNull():
            return
        view.insert_thumbnail(img, url, anchor=anchor, left_margin=self._gutter_px(view), click_url=click_url)

    def _insert_autoload_card(self, net_id: int, name: str, anchor, link: str, data: bytes) -> None:
        if not data:
            return
        view = self._live_media_view(net_id, name)
        if view is not None:
            card_anchor = self._x_card(view, link, data, anchor=anchor, fallback_open=False)
            if card_anchor is not None and self._service_on("images"):
                from maxchat.ui import media
                photos = media.x_photo_urls(data)
                if photos:
                    self._media_loader().fetch(
                        photos[0],
                        lambda img_data, nid=net_id, nm=name, a=card_anchor, img_url=photos[0], tweet=link:
                        self._insert_autoload_image(nid, nm, a, img_url, img_data, click_url=tweet))

    def _web_card(self, view, link: str, data: bytes, anchor=None):
        from html import escape
        from urllib.parse import urlparse

        from maxchat.ui import media

        card_data = media.opengraph_card(data, link)
        if not card_data:
            return None, {}
        host = urlparse(link).netloc.lower() or "Website"
        title = card_data.get("title") or host
        desc = card_data.get("description") or ""
        fm = view.fontMetrics()
        cw = max(1, fm.horizontalAdvance("0"))
        pad = 26
        footer = f"{host} {link}"
        width = max(420, min(max(fm.horizontalAdvance(title), fm.horizontalAdvance(footer)) + pad, cw * 92 + pad))
        cpl = max(20, (width - pad) // cw)
        desc = desc if len(desc) <= cpl * 3 else desc[: cpl * 3 - 1].rstrip() + "…"
        body = escape(desc).replace("\n", "<br>")
        card = (
            f'<table cellpadding="7" cellspacing="0" width="{width}" '
            f'style="background:rgba(120,170,150,0.16);"><tr><td width="{width}">'
            f'<b>{escape(title)}</b><br>'
            f'{body}<br><span style="color:#8a8f98">{escape(footer)}</span></td></tr></table>'
        )
        card_anchor = view.insert_card(card, anchor=anchor, left_margin=self._gutter_px(view))
        return card_anchor, card_data

    def _insert_autoload_web_card(self, net_id: int, name: str, anchor, link: str, data: bytes) -> None:
        if not data:
            return
        view = self._live_media_view(net_id, name)
        if view is None:
            return
        card_anchor, card_data = self._web_card(view, link, data, anchor=anchor)
        if card_anchor is None or not self._service_on("images"):
            return
        image_url = card_data.get("image") or ""
        if image_url:
            self._media_loader().fetch(
                image_url,
                lambda img_data, nid=net_id, nm=name, a=card_anchor, img=image_url, page=link:
                self._insert_autoload_image(nid, nm, a, img, img_data, click_url=page))

    def _on_image_clicked(self, url: str) -> None:
        from maxchat.ui import media
        if media.is_x_url(url) or media.is_web_card_url(url):
            from PySide6.QtCore import QUrl
            from PySide6.QtGui import QDesktopServices
            QDesktopServices.openUrl(QUrl(url))
            return
        self._show_image(url)  # a normal inline thumbnail was clicked → big view

    def _show_image(self, url: str) -> None:
        from maxchat.ui.media import ImageViewer
        ImageViewer(self, self._media_loader(), url).show()

    def _make_view(self) -> ChatView:
        v = ChatView()
        v.set_scrollback(self._scrollback)
        v.set_wrap(self._word_wrap)
        v.linkClicked.connect(lambda url, vv=v: self._on_link_clicked(vv, url))
        v.imageClicked.connect(self._on_image_clicked)
        v.separatorMoved.connect(self._on_separator_moved)  # drag the chat line → resize nick column
        self._style_view(v)  # chat-theme bg/fg + font + copy/separator options
        return v

    def _ensure_buffer(self, net: Network, name: str) -> ChatView:
        if name in net.buffers:
            return net.buffers[name]
        if not self._network_live(net) or net.root_item is None:
            raise RuntimeError("cannot create a buffer for a closed network")
        view = self._make_view()
        net.buffers[name] = view
        self._replay_log(net, name, view)  # resume: show the tail of last session's log + "Ended" divider
        self.stack.addWidget(view)
        item = QTreeWidgetItem([name])
        item.setData(0, Qt.ItemDataRole.UserRole, (net.id, name))
        net.root_item.addChild(item)
        net.root_item.setExpanded(True)
        net.tree_items[name] = item
        self._tabs_changed()  # mirror into the tab bar when in tabs mode
        return view

    def _close_buffer(self, net: Network, name: str) -> None:
        if name == SERVER_BUFFER:
            return
        self._cancel_paste_timers(net.id, name)
        if self._is_channel_name(name):
            key = self._channel_key(name)
            self._pending_channel_closes.discard((net.id, key))
            net.pending_names.pop(key, None)
        chat = self._dcc_chats.pop((net.id, name), None)  # close any DCC-chat socket
        if chat is not None:
            chat.close()
        view = net.buffers.pop(name, None)
        if view is not None:
            self.stack.removeWidget(view)
            view.deleteLater()
        item = net.tree_items.pop(name, None)
        if item is not None:
            (item.parent() or self.tree.invisibleRootItem()).removeChild(item)
        net.names.pop(name, None)
        net.topics.pop(name, None)
        net.channel_modes.pop(name, None)
        self._tabs_changed()
        if self._active_net is net and self._active_buf == name:
            self._switch_to(net, SERVER_BUFFER)

    def _close_network(self, net: Network) -> None:
        if not self._network_live(net):
            return
        net.intentional = True
        net.reconnect_pending = False
        if hasattr(self, "_cancel_paste_timers"):
            self._cancel_paste_timers(net.id)
        if hasattr(self, "_pending_channel_closes"):
            self._pending_channel_closes = {
                key for key in self._pending_channel_closes if key[0] != net.id
            }
        if self._pending_dcc_net is net:
            self._pending_dcc_net = None
        for key, chat in list(self._dcc_chats.items()):
            if key[0] == net.id:
                self._dcc_chats.pop(key, None)
                try:
                    chat.close()
                except Exception:
                    pass
        try:
            if net.client.is_connected():
                net.client.quit()
            else:
                net.client.disconnect()
        except Exception:
            pass
        try:
            QObject.disconnect(net.client)
        except (RuntimeError, TypeError):
            pass
        net.client.deleteLater()
        for view in net.buffers.values():
            self.stack.removeWidget(view)
            view.deleteLater()
        idx = self.tree.indexOfTopLevelItem(net.root_item)
        if idx >= 0:
            self.tree.takeTopLevelItem(idx)
        if net.spacer_item is not None:
            sidx = self.tree.indexOfTopLevelItem(net.spacer_item)
            if sidx >= 0:
                self.tree.takeTopLevelItem(sidx)
        self._networks.pop(net.id, None)
        net.buffers.clear()
        net.tree_items.clear()
        net.names.clear()
        if hasattr(net, "pending_names"):
            net.pending_names.clear()
        if hasattr(net, "joined_channels"):
            net.joined_channels.clear()
        net.topics.clear()
        net.channel_modes.clear()
        net.root_item = None
        net.spacer_item = None
        net.unread_marked.clear()
        self._tabs_changed()
        if self._active_net is net:
            other = next(iter(self._networks.values()), None)
            if other is not None:
                self._switch_to(other, SERVER_BUFFER)
            else:
                self._show_placeholder()

    def _show_placeholder(self) -> None:
        self._active_net, self._active_buf = None, None
        self.stack.setCurrentWidget(self._placeholder)
        if not self._networks:
            self._ensure_placeholder_tree_item()
            self._apply_buffer_layout()
        self.members.clear()
        self.members_header.setText("Members")
        self.topic.setText("")
        self._set_topic("")
        self.nick_label.setText("—")
        self.setWindowTitle(f"{__app_name__} {__version__}")
        self.statusBar().showMessage("Not connected — Server ▸ Server List… or Quick Connect…")
        self._set_connected_ui()

    def _ensure_placeholder_tree_item(self) -> None:
        item = self._placeholder_tree_item
        if self._networks:
            return
        self.tree.setHeaderHidden(True)
        if item is None:
            item = QTreeWidgetItem(["Server"])
            item.setData(0, Qt.ItemDataRole.UserRole, PLACEHOLDER_SERVER_ITEM)
            f = item.font(0)
            f.setBold(True)
            item.setFont(0, f)
            item.setSizeHint(0, QSize(0, 28))
            theme_name = getattr(self, "_theme", config.get_setting("theme", "dark"))
            item.setBackground(0, QBrush(QColor(theme.ui_color(theme_name, "panel2"))))
            item.setForeground(0, QBrush(QColor(theme.ui_color(theme_name, "text"))))
            self.tree.addTopLevelItem(item)
            self._placeholder_tree_item = item
        if self.tree.currentItem() is not item:
            self.tree.setCurrentItem(item)

    def _remove_placeholder_tree_item(self) -> None:
        item = self._placeholder_tree_item
        if item is None:
            return
        idx = self.tree.indexOfTopLevelItem(item)
        if idx >= 0:
            self.tree.takeTopLevelItem(idx)
        self._placeholder_tree_item = None
        self.tree.setHeaderHidden(True)

    # ---- buffers + tree ---------------------------------------------------
    def _clear_mark(self, item: QTreeWidgetItem, net: Network | None = None) -> None:
        if net is not None and item is net.root_item:
            self._style_network_item(net)  # restore the network-bar look
            return
        f = item.font(0)
        f.setBold(False)
        item.setFont(0, f)
        item.setForeground(0, QBrush())

    def _mark_activity(self, net: Network, name: str, highlight: bool) -> None:
        if not self._network_live(net):
            return
        if name == SERVER_BUFFER:
            return
        if self._active_net is net and self._active_buf == name:
            return
        item = net.tree_items.get(name)
        if item is None:
            return
        f = item.font(0)
        f.setBold(True)
        item.setFont(0, f)
        color = QColor("#e5c07b" if highlight else "#7f9fbf")
        item.setForeground(0, QBrush(color))
        if self._use_tabs:  # mirror the activity tint onto the tab
            i = self._tab_index_for(net, name)
            if i >= 0:
                self._tabbar.setTabTextColor(i, color)
        self._unread.add((net.id, name))
        if highlight:
            self._unread_hi.add((net.id, name))
        self._refresh_title()

    def _switch_to(self, net: Network, name: str) -> None:
        if not self._network_live(net):
            return
        view = net.buffers.get(name)
        if view is None:
            return
        self._active_net, self._active_buf = net, name  # comic panels are kept per channel (self._comic)
        net.unread_marked.discard(name)
        self.stack.setCurrentWidget(view)
        self._populate_members(net.names.get(name, []))
        self._set_topic(net.topics.get(name, ""))
        self._unread.discard((net.id, name))      # viewing it clears its unread/highlight
        self._unread_hi.discard((net.id, name))
        self._refresh_title()
        self.nick_label.setText(net.client.nick or "—")
        self._set_connected_ui()
        item = net.tree_items.get(name)
        if item is not None:
            self._clear_mark(item, net)
            if not item.isSelected():
                self.tree.blockSignals(True)
                self.tree.setCurrentItem(item)
                self.tree.blockSignals(False)
        if self._use_tabs:
            self._sync_tab_current()
            i = self._tab_index_for(net, name)
            if i >= 0:
                self._tabbar.setTabTextColor(i, QColor())  # clear the activity tint on the active tab
        self._render_strip()

    def _set_topic(self, text: str) -> None:
        """Show the channel topic in the bar — elided with '…' to fit (never forcing the window wider);
        the FULL topic goes in the tooltip."""
        self.topic.setFullText(text)
        hint = "Double-click to set the channel topic"
        self.topic.setToolTip(f"{text}\n\n{hint}" if text else hint)

    def _on_tree_select(self) -> None:
        items = self.tree.selectedItems()
        if not items:
            return
        data = items[0].data(0, Qt.ItemDataRole.UserRole)
        if not data:
            return
        if data == PLACEHOLDER_SERVER_ITEM:
            self._show_placeholder()
            return
        if not isinstance(data, tuple) or len(data) != 2:
            return
        net_id, name = data
        net = self._networks.get(net_id)
        if net is not None:
            self._switch_to(net, name)

    # ---- keyboard navigation (Alt+N / Ctrl+PgUp·Dn / Alt+` ) -------------
    def _ordered_buffers(self) -> list:
        """Every buffer in tree (visual) order as (net, name) — the list Alt+1…9 indexes into."""
        out = []
        for i in range(self.tree.topLevelItemCount()):
            top = self.tree.topLevelItem(i)
            for item in [top] + [top.child(j) for j in range(top.childCount())]:
                data = item.data(0, Qt.ItemDataRole.UserRole)
                if isinstance(data, tuple) and len(data) == 2:
                    net = self._networks.get(data[0])
                    if net is not None:
                        out.append((net, data[1]))
        return out

    def _switch_by_index(self, idx: int) -> None:
        ob = self._ordered_buffers()
        if 0 <= idx < len(ob):
            self._switch_to(*ob[idx])

    def _switch_relative(self, delta: int) -> None:
        ob = self._ordered_buffers()
        if not ob:
            return
        cur = (self._active_net, self._active_buf)
        idx = ob.index(cur) if cur in ob else 0
        self._switch_to(*ob[(idx + delta) % len(ob)])

    def _switch_next_activity(self) -> None:
        """Jump to the next buffer flagged with activity (bold tree item), wrapping around."""
        ob = self._ordered_buffers()
        if not ob:
            return
        cur = (self._active_net, self._active_buf)
        start = (ob.index(cur) + 1) if cur in ob else 0
        for k in range(len(ob)):
            net, name = ob[(start + k) % len(ob)]
            if name == SERVER_BUFFER:
                continue  # server/network bars use a bold header font — not an activity mark
            item = net.tree_items.get(name)
            if item is not None and item.font(0).bold():
                self._switch_to(net, name)
                return

    def _clear_active_buffer(self) -> None:
        if self._active_net is not None and self._active_buf is not None:
            view = self._active_net.buffers.get(self._active_buf)
            if view is not None:
                view.clear()

    def _close_active_buffer(self) -> None:
        net, name = self._active_net, self._active_buf
        if net is None or name is None or name == SERVER_BUFFER:
            return
        if self._is_channel_name(name):
            self._leave_and_close_channel(net, name)
        else:
            self._close_buffer(net, name)

    def _leave_and_close_channel(self, net: Network, channel: str) -> None:
        key = self._channel_key(channel)
        if net.connected and key in net.joined_channels:
            self._pending_channel_closes.add((net.id, key))
            if net.client.send_raw(f"PART {channel}"):
                net.joined_channels.discard(key)
                self._cancel_paste_timers(net.id, channel)
            else:
                self._pending_channel_closes.discard((net.id, key))
                self._append_send_failure(net, channel)
                return
        self._close_buffer(net, channel)

    # ---- tabs vs tree buffer switcher ------------------------------------
    def _apply_buffer_layout(self) -> None:
        show_tabs = bool(self._use_tabs and self._networks)
        self.tree.setVisible(not show_tabs)
        self._tabbar.setVisible(show_tabs)
        if show_tabs:
            self._rebuild_tabs()

    def _tabs_changed(self) -> None:
        if self._use_tabs:
            self._rebuild_tabs()

    def _toggle_buffer_tabs(self, on: bool) -> None:
        self._use_tabs = bool(on)
        config.set_pref("buffer_tabs", self._use_tabs)
        self._apply_buffer_layout()

    @staticmethod
    def _panel_visible_pref_key(index: int) -> str:
        if index == 0:
            return "server_list_visible"
        if index == 2:
            return "member_list_visible"
        return ""

    def _set_panel_visible(self, index: int, show: bool, default: int = 180, persist: bool = True) -> None:
        """Slide a side pane open/shut by collapsing its splitter size to 0 (the handle stays, so you
        can still drag it back) — index 0 = the left server/channel tree, 2 = the right user list."""
        sizes = self._split.sizes()
        if index < 0 or index >= len(sizes):
            return
        if persist and (pref_key := self._panel_visible_pref_key(index)):
            config.set_pref(pref_key, bool(show))
        if show and sizes[index] == 0:  # restore the remembered width, taking it from the centre pane
            w = self._panel_widths.get(index) or default
            sizes[1] = max(160, sizes[1] - w)
            sizes[index] = w
            self._split.setSizes(sizes)
        elif not show and sizes[index] > 0:  # remember + collapse, giving the width to the centre pane
            self._panel_widths[index] = sizes[index]
            sizes[1] += sizes[index]
            sizes[index] = 0
            self._split.setSizes(sizes)

    def _apply_saved_side_panel_visibility(self) -> None:
        for index in (0, 2):
            self._set_panel_visible(index, bool(config.pref(self._panel_visible_pref_key(index))), persist=False)
        self._sync_panel_actions()

    def _sync_panel_actions(self) -> None:
        """Keep the View ▸ menu checkmarks (and bar buttons) in step with a manual splitter drag."""
        sizes = self._split.sizes()
        for act, idx in ((self.act_tree_panel, 0), (self.act_members, 2)):
            act.blockSignals(True)
            act.setChecked(sizes[idx] > 0)
            act.blockSignals(False)

    def _rebuild_tabs(self) -> None:
        if not self._use_tabs:
            return
        self._rebuilding_tabs = True
        self._tabbar.blockSignals(True)
        while self._tabbar.count():
            self._tabbar.removeTab(0)
        multi = len(self._networks) > 1
        for net, name in self._ordered_buffers():
            label = net.name if name == SERVER_BUFFER else (f"{net.name}/{name}" if multi else name)
            idx = self._tabbar.addTab(label)
            self._tabbar.setTabData(idx, (net.id, name))
            if name == SERVER_BUFFER:
                self._tabbar.setTabButton(idx, QTabBar.ButtonPosition.RightSide, None)
            item = net.tree_items.get(name)
            if item is not None and item.font(0).bold():  # carry over activity colour
                self._tabbar.setTabTextColor(idx, item.foreground(0).color())
        self._tabbar.blockSignals(False)
        self._rebuilding_tabs = False
        self._sync_tab_current()

    def _tab_index_for(self, net: Network, name: str) -> int:
        for i in range(self._tabbar.count()):
            if self._tabbar.tabData(i) == (net.id, name):
                return i
        return -1

    def _sync_tab_current(self) -> None:
        if not self._use_tabs or self._active_net is None:
            return
        i = self._tab_index_for(self._active_net, self._active_buf)
        if i >= 0 and i != self._tabbar.currentIndex():
            self._tabbar.blockSignals(True)
            self._tabbar.setCurrentIndex(i)
            self._tabbar.blockSignals(False)

    def _on_tab_changed(self, idx: int) -> None:
        if self._rebuilding_tabs or idx < 0:
            return
        data = self._tabbar.tabData(idx)
        if data:
            net = self._networks.get(data[0])
            if net is not None:
                self._switch_to(net, data[1])

    def _close_tab_index(self, idx: int) -> None:
        data = self._tabbar.tabData(idx)
        if not data:
            return
        net = self._networks.get(data[0])
        name = data[1]
        if net is None or name == SERVER_BUFFER:
            return
        if self._is_channel_name(name):
            self._leave_and_close_channel(net, name)
        else:
            self._close_buffer(net, name)

    def _tab_menu(self, pos) -> None:
        idx = self._tabbar.tabAt(pos)
        if idx < 0:
            return
        data = self._tabbar.tabData(idx)
        if not data:
            return
        net = self._networks.get(data[0])
        if net is None:
            return
        menu = QMenu(self)
        self._add_buffer_menu_actions(menu, net, data[1])
        if not menu.isEmpty():
            menu.exec(self._tabbar.mapToGlobal(pos))

    def _add_buffer_menu_actions(self, menu: QMenu, net: Network, name: str) -> None:
        if name == SERVER_BUFFER:
            reconnecting = net.reconnect_pending or (
                not net.connected and net.reconnect_params is not None and not net.intentional)
            if net.connected:
                menu.addAction("Disconnect", lambda: self._disconnect_net(net))
            elif reconnecting:
                menu.addAction("Stop reconnecting", lambda: self._disconnect_net(net))
                menu.addAction("Reconnect now", lambda: self._reconnect_net(net))
            else:
                menu.addAction("Reconnect", lambda: self._reconnect_net(net))
            menu.addAction("Close network", lambda: self._close_network(net))
            return
        if self._is_channel_name(name):
            if self._is_joined_channel(net, name):
                menu.addAction("Set topic…", lambda: self._edit_topic_for(net, name))
                menu.addAction("Ban list…", lambda: self._open_ban_list(net, name))
                menu.addAction("Leave", lambda: self._leave_channel_for(net, name))
            menu.addAction("Close", lambda: self._leave_and_close_channel(net, name))
        else:
            menu.addAction("Close", lambda: self._close_buffer(net, name))

    def _tree_menu(self, pos) -> None:
        item = self.tree.itemAt(pos)
        if item is None:
            return
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if not data:
            return
        if data == PLACEHOLDER_SERVER_ITEM:
            return
        if not isinstance(data, tuple) or len(data) != 2:
            return
        net_id, name = data
        net = self._networks.get(net_id)
        if net is None:
            return
        menu = QMenu(self)
        self._add_buffer_menu_actions(menu, net, name)
        if name != SERVER_BUFFER:
            menu.addSeparator()
            menu.addAction("Unmute" if self._is_muted(net, name) else "Mute (no highlights)",
                           lambda: self._toggle_mute(net, name))
        menu.exec(self.tree.viewport().mapToGlobal(pos))

    def _open_ban_list(self, net: Network, channel: str) -> None:
        from maxchat.ui.ban_list import BanListDialog
        BanListDialog(self, net, channel).exec()

    def _edit_topic_for(self, net: Network, channel: str) -> None:
        new, ok = shadow_message.get_text(self, f"Set topic for {channel}", "Topic:",
                                          text=net.topics.get(channel, ""))
        if ok:
            net.client.send_raw(f"TOPIC {channel} :{new}")

    def _member_double(self, item) -> None:
        nick = item.data(Qt.ItemDataRole.UserRole)
        if nick and self._active_net is not None:
            self._open_query(self._active_net, nick)

    def _set_nick_color(self, nick: str, reset: bool = False) -> None:
        """Per-user nick colour override, saved to config so it persists (just like avatar assignments)."""
        from PySide6.QtWidgets import QColorDialog
        key = self._strip(nick).lower()
        colors = dict(config.pref("nick_colors") or {})
        if reset:
            colors.pop(key, None)
        else:
            cur = colors.get(key) or self._nick_color(nick) or self._fg
            chosen = QColorDialog.getColor(QColor(cur), self, f"Color for {nick}")
            if not chosen.isValid():
                return
            colors[key] = chosen.name()
        config.set_pref("nick_colors", colors)
        self._refresh_active_members()  # recolour the user list now (new messages pick it up too)
        self._note(f"* Color {'reset' if reset else 'set'} for {nick}.")

    @staticmethod
    def _mkey(net: "Network", buf: str) -> str:
        return f"{net.name}/{buf}"

    def _is_muted(self, net: "Network", buf: str) -> bool:
        return self._mkey(net, buf) in set(config.pref("muted_buffers") or [])

    def _toggle_mute(self, net: "Network", buf: str) -> None:
        """Mute/unmute a buffer — muted buffers never highlight / beep / notify (still show activity)."""
        key = self._mkey(net, buf)
        muted = list(config.pref("muted_buffers") or [])
        if key in muted:
            muted.remove(key)
            state = "unmuted"
        else:
            muted.append(key)
            state = "muted"
        config.set_pref("muted_buffers", muted)
        self._append(net, buf, f"* {buf} {state}.", event=True)

    def _on_lag(self, net: "Network", secs: float) -> None:
        self._append(net, self._active_target(net), f"* Lag to {net.name}: {secs * 1000:.0f} ms")

    @staticmethod
    def _fmt_duration(secs: float) -> str:
        secs = int(secs)
        d, secs = divmod(secs, 86400)
        h, secs = divmod(secs, 3600)
        m, s = divmod(secs, 60)
        return " ".join(p for p in (f"{d}d" if d else "", f"{h}h" if h else "",
                                    f"{m}m" if m else "", f"{s}s") if p)

    @staticmethod
    def _sysinfo_str() -> str:
        from PySide6 import __version__ as pyside_ver
        from PySide6.QtCore import qVersion
        return (f"MaxChat {__version__} · {platform.system()} {platform.release()} "
                f"({platform.machine()}) · Python {platform.python_version()} · "
                f"Qt {qVersion()} / PySide6 {pyside_ver} · {os.cpu_count() or '?'} cores")

    def _netinfo_str(self, net: "Network") -> str:
        su = getattr(net.client, "isupport", {}) or {}
        keys = ("NETWORK", "CHANTYPES", "PREFIX", "CHANMODES", "NICKLEN", "TOPICLEN", "CASEMAPPING")
        bits = [f"{k}={su[k]}" for k in keys if su.get(k)]
        host = net.servers[net.server_index][0] if getattr(net, "servers", None) else "?"
        return f"{net.name} ({host}): " + (" · ".join(bits) if bits else "no ISUPPORT received yet")

    def _members_menu(self, pos) -> None:
        item = self.members.itemAt(pos)
        if item is None or self._active_net is None:
            return
        nick = item.data(Qt.ItemDataRole.UserRole)
        if not nick:  # a group header, not a nick
            return
        net = self._active_net
        cl = net.client
        chan = self._active_buf if (self._active_buf or "").startswith(("#", "&")) else ""
        menu = QMenu(self)
        menu.addAction(f"WhoIs {nick}", lambda: self._do_whois(net, nick))
        menu.addAction(f"Message {nick}", lambda: self._open_query(net, nick))
        menu.addAction("Copy nick", lambda: QApplication.clipboard().setText(nick))
        menu.addAction("Set color…", lambda: self._set_nick_color(nick))
        if (config.pref("nick_colors") or {}).get(self._strip(nick).lower()):
            menu.addAction("Reset color", lambda: self._set_nick_color(nick, reset=True))
        menu.addSeparator()
        menu.addAction(f"Send File to {nick}…", lambda: self._send_file(net, nick))
        menu.addAction(f"DCC Chat with {nick}", lambda: self._start_dcc_chat(net, nick))
        mask = f"{nick}!*@*"
        if self._ignored(mask):
            menu.addAction(f"Unignore {nick}", lambda: self._remove_ignore(mask))
        else:
            menu.addAction(f"Ignore {nick}", lambda: self._add_ignore(mask))
        if self._friend(nick):
            menu.addAction(f"Remove {nick} from notify", lambda: self._remove_friend(nick))
        else:
            menu.addAction(f"Add {nick} to notify list", lambda: self._add_friend(nick))
        if chan:  # channel-op actions (HexChat/mIRC style) — the server enforces who may use them
            menu.addSeparator()
            ops = menu.addMenu("Operator")
            for label, m in (("Owner", "q"), ("Admin", "a"), ("Op", "o"), ("Half-Op", "h"), ("Voice", "v")):
                ops.addAction(f"Give {label} (+{m})",
                              lambda _c=False, mm=m: cl.send_raw(f"MODE {chan} +{mm} {nick}"))
                ops.addAction(f"Take {label} (-{m})",
                              lambda _c=False, mm=m: cl.send_raw(f"MODE {chan} -{mm} {nick}"))
                ops.addSeparator()
            kb = menu.addMenu("Kick / Ban")
            kb.addAction("Kick", lambda: self._kick(net, chan, nick))
            kb.addAction("Kick (reason)…", lambda: self._kick(net, chan, nick, prompt=True))
            kb.addAction("Ban", lambda: cl.send_raw(f"MODE {chan} +b {nick}!*@*"))
            kb.addAction("Kick + Ban", lambda: self._kickban(net, chan, nick))
            menu.addSeparator()
        menu.addSeparator()
        low = self._strip(nick).lower()
        comic_m = menu.addMenu("Comic")  # comic-only controls (separate from IRC ignore above)
        if chan:
            comic_m.addAction("Assign character…", lambda: self._assign_char(net, chan, nick))
            comic_m.addSeparator()
            here = comic_m.addAction("Hide from comic — here")  # this channel only, remembered on rejoin
            here.setCheckable(True)
            here.setChecked(low in self._chan_comic_ignore(net, chan))
            here.toggled.connect(lambda on, n=nick, c=chan: self._set_comic_ignore_chan(net, c, n, on))
        everywhere = comic_m.addAction("Hide from comic — everywhere")  # bots: all channels (still in chat)
        everywhere.setCheckable(True)
        everywhere.setChecked(low in self._comic_ignore)
        everywhere.toggled.connect(lambda on, n=nick: self._set_comic_ignore(n, on))
        menu.addSeparator()
        ctcp = menu.addMenu("CTCP")
        for kind in ("PING", "VERSION", "TIME", "CLIENTINFO"):
            ctcp.addAction(kind.title(),
                           lambda _c=False, k=kind: self._send_ctcp(net, nick, k))
        menu.exec(self.members.viewport().mapToGlobal(pos))

    def _set_comic_ignore(self, nick: str, ignore: bool) -> None:
        """Keep a nick's messages out of the comic EVERYWHERE (e.g. a URL-info bot) — still shown in chat.
        This is the comic-only ignore; it's separate from the IRC ignore list (which hides them entirely)."""
        low = self._strip(nick).lower()
        if ignore:
            self._comic_ignore.add(low)
        else:
            self._comic_ignore.discard(low)
        config.set_pref("comic_ignore", sorted(self._comic_ignore))
        self._note(f"* {self._strip(nick)} {'hidden from' if ignore else 'shown in'} comic (everywhere).")

    def _chan_comic_ignore(self, net: Network, chan: str) -> set[str]:
        """The set of nicks (lowercased) kept out of the comic in THIS channel only (remembered on rejoin)."""
        key = self._chan_key(net, chan)
        return {str(n).lower() for n in (self._comic_channels.get(key, {}) or {}).get("ignore", [])}

    def _set_comic_ignore_chan(self, net: Network, chan: str, nick: str, hide: bool) -> None:
        """Hide/show a nick in the comic for THIS channel only, persisted in the per-channel comic map."""
        low = self._strip(nick).lower()
        key = self._chan_key(net, chan)
        chans = dict(config.pref("comic_channels") or {})
        entry = dict(chans.get(key, {}) or {})
        ign = [n for n in (entry.get("ignore") or []) if str(n).lower() != low]
        if hide:
            ign.append(low)
        if ign:
            entry["ignore"] = ign
        else:
            entry.pop("ignore", None)
        if entry.get("bg") or entry.get("chars") or entry.get("ignore"):
            chans[key] = entry
        else:
            chans.pop(key, None)  # nothing left set → drop the channel override
        config.set_pref("comic_channels", chans)
        self._load_comic_prefs()
        self._note(f"* {self._strip(nick)} {'hidden from' if hide else 'shown in'} comic in {chan}.")

    def _set_comic_cmd_regex(self, pattern: str) -> None:
        """Compile the user's custom comic-exclude regex (blank = none); ignore an invalid pattern."""
        self._comic_exclude_regex = str(pattern or "")
        self._comic_exclude_re = None
        if self._comic_exclude_regex:
            try:
                self._comic_exclude_re = re.compile(self._comic_exclude_regex, re.IGNORECASE)
            except re.error:
                self._comic_exclude_re = None

    def _comic_text_filtered(self, text: str) -> bool:
        """True if this message should be kept OUT of the comic — a bot command/correction (it starts with
        one of the configured prefixes, e.g. ! . ~ @ ? s/) or a custom-regex match. A single-character
        sigil must be followed by a letter OR DIGIT, so a real command (".8ball", "!mlb") is filtered while
        "...", "!!!" and ":)" stay in the comic."""
        plain = strip_formatting(text).strip()
        if not plain:
            return False
        if self._comic_ignore_cmds:
            low = plain.lower()
            for pat in self._comic_bot_patterns:
                if pat and low.startswith(pat):
                    if len(pat) == 1 and not (len(low) > 1 and low[1].isalnum()):
                        continue  # a lone sigil ("...", "!!!", ":)") — not a command
                    return True
        if self._comic_exclude_re is not None and self._comic_exclude_re.search(plain):
            return True
        return False

    def _assign_char(self, net: Network, chan: str, nick: str) -> None:
        """Assign a comic character to a nick for THIS channel (writes the per-channel comic map)."""
        if not self._comic_characters:
            self._load_comic_art()  # populate the character list on first use
        if not self._comic_characters:
            shadow_message.information(
                self, "Assign character",
                "No comic art found.\n\nSet your comic art folder in Comic ▸ Comic Settings first.",
            )
            return
        bare = self._strip(nick)
        low = bare.lower()
        key = self._chan_key(net, chan)
        current = (self._comic_channels.get(key, {}) or {}).get("chars", {}).get(low, "")
        from maxchat.ui.assign_character import AssignCharacterDialog
        dlg = AssignCharacterDialog(
            self, prompt=f"Character for {bare} in {chan}:",
            stems=[p.stem for p in self._comic_characters], current=current,
            image_for=self._char_preview_image)
        if not dlg.exec():
            return
        choice = dlg.result_stem or ""
        chans = dict(config.pref("comic_channels") or {})
        entry = dict(chans.get(key, {}) or {})
        charmap = dict(entry.get("chars") or {})
        if not choice:
            charmap.pop(low, None)  # back to the default/random character
        else:
            charmap[low] = choice
        entry["chars"] = charmap
        if entry.get("bg") or entry.get("chars") or entry.get("ignore"):
            chans[key] = entry
        else:
            chans.pop(key, None)  # nothing left set → drop the channel override
        config.set_pref("comic_channels", chans)
        self._load_comic_prefs()
        self._nick_char.pop((net.id, bare), None)  # forget any cached auto choice for this nick (this net)
        self._rerender_channel()

    def _kick(self, net: Network, chan: str, nick: str, prompt: bool = False) -> None:
        reason = nick
        if prompt:
            text, ok = shadow_message.get_text(self, f"Kick {nick}", "Reason:")
            if not ok:
                return
            reason = text.strip() or nick
        net.client.send_raw(f"KICK {chan} {nick} :{reason}")

    def _kickban(self, net: Network, chan: str, nick: str) -> None:
        net.client.send_raw(f"MODE {chan} +b {nick}!*@*")
        net.client.send_raw(f"KICK {chan} {nick} :{nick}")

    def _do_whois(self, net: Network, nick: str) -> None:
        net.client.send_raw(f"WHOIS {nick}")  # reply prints to the active chat (see _on_whois)

    def _open_query(self, net: Network, nick: str) -> None:
        if nick:
            self._ensure_buffer(net, nick)
            self._switch_to(net, nick)

    @staticmethod
    def _strip(nick: str) -> str:
        return nick.lstrip("~&@%+")

    def _sorted_names(self, nicks: list) -> list:
        if self._sort_status:
            rank = {"~": 0, "&": 1, "@": 2, "%": 3, "+": 4}
            return sorted(nicks, key=lambda n: (rank.get(n[:1], 5), self._strip(n).lower()))
        return sorted(nicks, key=lambda n: self._strip(n).lower())

    def _names_add(self, net: Network, channel: str, nick: str) -> None:
        lst = net.names.setdefault(channel, [])
        if not any(self._strip(x) == self._strip(nick) for x in lst):
            lst.append(nick)

    def _names_remove(self, net: Network, channel: str, nick: str) -> None:
        lst = net.names.get(channel)
        if lst:
            s = self._strip(nick)
            net.names[channel] = [x for x in lst if self._strip(x) != s]

    def _refresh_members(self, net: Network, channel: str) -> None:
        if self._active_net is net and self._active_buf == channel:
            self._populate_members(net.names.get(channel, []))

    def _populate_members(self, nicks: list) -> None:
        """Rebuild the user list — coloured nicks (matching the chat) when 'colour nicks' is on, else
        grouped by role with IRCCloud-style section header boxes. Away users are dimmed."""
        away = self._active_net.away if self._active_net else set()
        self.members.clear()
        self.members_header.setText(f"{len(nicks)} users")
        if self._colored_nicks:  # flat, status-sorted, coloured like the chat
            for n in self._sorted_names(nicks):
                stripped = self._strip(n)
                it = QListWidgetItem(n)
                it.setData(Qt.ItemDataRole.UserRole, stripped)
                color = "#808080" if stripped.lower() in away else self._nick_color(stripped)
                it.setForeground(QBrush(QColor(color)))
                self.members.addItem(it)
            return
        groups: dict[str, list] = {sym: [] for sym, _ in MEMBER_GROUPS}
        for n in nicks:
            groups[n[:1] if n[:1] in "~&@%+" else ""].append(n)
        hdr_bg = QColor(theme.ui_color(self._theme, "panel2"))
        hdr_fg = QColor(theme.ui_color(self._theme, "text"))
        for sym, title in MEMBER_GROUPS:
            members = sorted(groups[sym], key=lambda x: self._strip(x).lower())
            if not members:
                continue
            h = QListWidgetItem(f"{title.upper()}   {len(members)}")
            h.setFlags(Qt.ItemFlag.ItemIsEnabled)  # a visible title box, not selectable
            hf = h.font()
            hf.setBold(True)
            h.setFont(hf)
            h.setForeground(QBrush(hdr_fg))
            h.setBackground(QBrush(hdr_bg))
            self.members.addItem(h)
            for n in members:
                stripped = self._strip(n)
                it = QListWidgetItem(stripped)
                it.setData(Qt.ItemDataRole.UserRole, stripped)
                if stripped.lower() in away:
                    it.setForeground(QBrush(QColor("#808080")))
                self.members.addItem(it)

    def _refresh_active_members(self) -> None:
        if self._active_net is not None and self._active_buf is not None:
            self._populate_members(self._active_net.names.get(self._active_buf, []))

    def _completion_candidates(self) -> list:
        cmds = [
            "/join", "/part", "/cycle", "/topic", "/me", "/msg", "/query", "/notice", "/nick",
            "/whois", "/whowas", "/kick", "/op", "/deop", "/voice", "/devoice", "/ban", "/kickban",
            "/mode", "/names", "/invite", "/ctcp", "/sound", "/ns", "/cs", "/ms", "/identify", "/ghost",
            "/ignore", "/unignore", "/alias", "/unalias", "/dcc",
            "/scripts", "/load", "/unload", "/reload", "/away", "/back", "/clear", "/clearall",
            "/wallops", "/oper", "/kill", "/lag", "/uptime", "/netinfo", "/sysinfo", "/mute", "/unmute",
            "/shrug", "/tableflip", "/unflip", "/lenny", "/disapprove",
            "/amsg", "/ame", "/onotice", "/leave", "/who", "/halfop", "/dehalfop", "/hop", "/raw", "/quit",
        ]
        net = self._active_net
        names = net.names.get(self._active_buf, []) if net else []
        return [self._strip(x) for x in names] + cmds

    # ---- rendering + logging ---------------------------------------------
    def _ts(self) -> str:
        if not self._show_ts:
            return ""
        return f'<span style="color:{self._ts_color}">{escape(datetime.now().strftime(self._ts_format))}</span> '

    def _render(self, text: str) -> str:
        if self._show_fmt:
            return to_html(text, self._fg, self._bg)
        return escape(strip_formatting(text))

    def _nick_color(self, nick: str) -> str:
        override = (config.pref("nick_colors") or {}).get(self._strip(nick).lower())
        if override:  # an explicit per-user colour wins, even in mono/colour-off modes
            return override
        if not self._colored_nicks or self._nick_mode == "mono":
            return self._fg  # irssi-style: nicks in the chat foreground, not rainbow
        if self._nick_mode == "palette":
            return nick_color(nick, self._nick_palette)  # e.g. BitchX's bright set
        return nick_color(nick)

    def _nick_html(self, nick: str, action: bool = False) -> str:
        name = escape(nick)
        color = self._nick_color(nick)
        if action:
            return f'<span style="color:{color}">* {name}</span>'
        bracket = self._bracket_color or color
        return (
            f'<span style="color:{bracket}">&lt;</span>'
            f'<span style="color:{color}">{name}</span>'
            f'<span style="color:{bracket}">&gt;</span>'
        )

    def _ts_plain(self) -> str:
        return (datetime.now().strftime(self._ts_format) + " ") if self._show_ts else ""

    def _emit(self, net: Network, name: str, inner_html: str, prefix_plain: str = "",
              label_len: int = -1) -> None:
        if not self._network_live(net):
            return
        view = self._ensure_buffer(net, name)
        active_here = self._active_net is net and self._active_buf == name
        if self._marker_line and not active_here and name not in net.unread_marked:
            view.appendHtml(f'<span style="color:{self._ts_color}">────────  new  ────────</span>')
            net.unread_marked.add(name)
        view.appendHtml(self._ts() + inner_html)
        view.document().lastBlock().setUserState(label_len)  # tag the line kind for column reflow
        if self._indent_wrap or self._align_nicks:  # the chat-line column always hangs wrapped text
            full = self._ts_plain() + prefix_plain
            if full:
                cur = view.textCursor()
                cur.movePosition(QTextCursor.MoveOperation.End)
                w = round(view.fontMetrics().horizontalAdvance(full))
                bf = cur.blockFormat()
                bf.setLeftMargin(w)
                bf.setTextIndent(-w)
                cur.setBlockFormat(bf)

    @staticmethod
    def _log_path(net: Network, name: str) -> str | None:
        """Path of the per-chat log file (None for the server tab). The filename comes from the
        ``log_mask`` pref: ``%network`` / ``%channel`` tokens plus strftime date codes (e.g. ``%Y``).
        ``/`` in the mask makes sub-folders; the channel/network values themselves are sanitised."""
        if name == SERVER_BUFFER:
            return None

        def _safe(s):
            s = s.lstrip("#&")
            for ch in "/\\%":
                s = s.replace(ch, "_")
            return s or "chat"

        mask = (config.pref("log_mask") or "%network-%channel").strip() or "%network-%channel"
        rel = mask.replace("%network", _safe(net.name)).replace("%channel", _safe(name))
        try:
            rel = datetime.now().strftime(rel)  # date tokens (%Y %m %d …)
        except ValueError:
            pass
        parts = [p for p in rel.replace("\\", "/").split("/") if p not in ("", ".", "..")]
        return os.path.join(config.config_dir(), "logs", *parts) + ".log"

    def _log(self, net: Network, name: str, plain: str) -> None:
        if not self._logging:
            return
        path = self._log_path(net, name)
        if not path:
            return
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            stamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            with open(path, "a", encoding="utf-8") as f:
                f.write(f"{stamp} {plain}\n")
        except OSError:
            pass

    def _fmt_when(self, when: datetime) -> str:
        """Friendly date + time for the resume divider, honouring the 12h/24h timestamp setting."""
        tf = (self._ts_format or "%I:%M %p").replace("[", "").replace("]", "")
        return when.strftime("%b %d, %Y " + tf)

    def _replay_log(self, net: Network, name: str, view: ChatView) -> None:
        """On opening a buffer, replay the tail of its log (dimmed) followed by an
        '--- Ended <date> <time> ---' divider marking where the previous session left off."""
        if not self._replay_log_on:
            return
        path = self._log_path(net, name)
        if not path or not os.path.exists(path):
            return
        try:
            limit = self._replay_lines or 50
            with open(path, encoding="utf-8", errors="replace") as f:
                lines = [line.rstrip("\n") for line in deque(f, maxlen=limit)]
        except OSError:
            return
        if not lines:
            return
        for ln in lines:
            self._replay_emit(view, ln)            # aligned + dimmed, like the live chat
            self._replay_to_comic(net, name, ln)   # resume the comic from the same history
        try:
            when = self._fmt_when(datetime.fromtimestamp(os.path.getmtime(path)))
        except OSError:
            when = ""
        label = f"Chat ended {when}".strip()
        dim = self._ts_color or "#8a8a8a"
        view.insert_rule(label, QColor(dim))  # a full-width rule with the label centred in the gap

    _REPLAY_MSG_RE = re.compile(r"^<([^>!@ ]+)> (.+)$")

    def _replay_to_comic(self, net: Network, name: str, log_line: str) -> None:
        """Feed a replayed *message* line back into the comic so it resumes with the same conversation.
        Only ``<nick> text`` lines — ``* …`` lines are events (joins/parts/modes log the same ``* nick``
        shape as a /me, so they're indistinguishable in the log; keep them out of the comic)."""
        body = log_line
        if len(body) > 20 and body[4] == "-" and body[7] == "-" and body[13] == ":":
            body = body[20:]  # strip the timestamp prefix the logger wrote
        m = self._REPLAY_MSG_RE.match(body)
        if m:
            self._maybe_comic(net, name, m.group(1), m.group(2))

    def _replay_emit(self, view: ChatView, log_line: str) -> None:
        """Render one replayed log line dimmed but ALIGNED to the chat line, like the live chat —
        rather than dumping the raw log text at the left margin. Reformats the log's
        ``YYYY-MM-DD HH:MM:SS`` stamp to your clock format so the nick column lines up with live messages."""
        dim = self._ts_color or "#8a8a8a"
        body, ts_disp = log_line, ""
        if len(log_line) > 20 and log_line[4] == "-" and log_line[7] == "-" and log_line[13] == ":":
            body = log_line[20:]
            if self._show_ts:
                try:
                    ts_disp = datetime.strptime(log_line[:19], "%Y-%m-%d %H:%M:%S").strftime(self._ts_format)
                except ValueError:
                    ts_disp = ""
        m = self._REPLAY_MSG_RE.match(body)
        if m:
            label, rest = f"<{m.group(1)}>", m.group(2)
        else:  # everything else — joins/parts/modes/nick-changes AND /me (same "* …" shape in the log) —
            label, rest = "", body  # is a system line: the whole text sits after the chat line
        text = f'<span style="color:{dim}">{escape(strip_formatting(rest))}</span>'
        if not label:  # system line
            if self._align_nicks:
                pad = self._nick_width + 1
                inner, prefix, state = "&nbsp;" * pad + text, " " * pad, self.SYS_LINE
            else:
                inner, prefix, state = text, "", self.SYS_LINE
        elif self._align_nicks:
            pad = max(0, self._nick_width - len(label))
            lbl = f'<span style="color:{dim}">{escape(label)}</span>'
            inner = "&nbsp;" * pad + lbl + " " + text
            prefix, state = " " * (pad + len(label) + 1), len(label)
        else:
            inner = f'<span style="color:{dim}">{escape(label)}</span> ' + text
            prefix, state = label + " ", len(label)
        ts_html = f'<span style="color:{dim}">{escape(ts_disp)}</span> ' if ts_disp else ""
        view.appendHtml(ts_html + inner)
        view.document().lastBlock().setUserState(state)
        if self._indent_wrap or self._align_nicks:  # hang-indent wrapped lines to the chat-line column
            full = ((ts_disp + " ") if ts_disp else "") + prefix
            if full:
                cur = view.textCursor()
                cur.movePosition(QTextCursor.MoveOperation.End)
                w = round(view.fontMetrics().horizontalAdvance(full))
                bf = cur.blockFormat()
                bf.setLeftMargin(w)
                bf.setTextIndent(-w)
                cur.setBlockFormat(bf)

    SYS_LINE = -3  # block tag: a system/server line — text fills the whole nick column (no nick)

    def _append(self, net: Network, name: str, text: str, event: bool = False) -> None:  # system line
        if not self._network_live(net):
            return
        inner = self._render(text)
        # event lines (join/part/quit/mode/nick) can take their own colour; else the system colour
        color = (self._event_color if event and self._event_color else self._sys_color)
        if color:  # tint system/server lines (BitchX loud cyan, irssi calm blue-grey)
            inner = f'<span style="color:{color}">{inner}</span>'
        if self._align_nicks:  # push server text into the message column, right of the chat line
            pad = self._nick_width + 1
            self._emit(net, name, "&nbsp;" * pad + inner, " " * pad, label_len=self.SYS_LINE)
        else:
            self._emit(net, name, inner)
        self._log(net, name, strip_formatting(text))

    def _append_msg(self, net: Network, name: str, nick: str, text: str) -> None:
        if not self._network_live(net):
            return
        label = f"<{nick}>"
        self._emit(net, name, *self._line_parts(label, self._nick_html(nick), text), label_len=len(label))
        self._log(net, name, f"<{nick}> {strip_formatting(text)}")
        self._grab_urls(nick, name, text)
        self._autoload_images(net, name, text)  # per-service gating happens inside
        self._maybe_comic(net, name, nick, text)

    def _autoload_images(self, net: Network, name: str, text: str) -> None:
        """Fetch any image / X links in the message and drop the preview in below it (anchored, so it
        lands in the right place even as more lines arrive). Each kind is gated by its own Services
        toggle — when off, the bare link stays in chat (still clickable), just no auto-fetch."""
        view = net.buffers.get(name)
        if view is None:
            return
        from maxchat.ui import media
        from maxchat.ui.chat_view import URL_RE, _normalise
        for m in URL_RE.finditer(strip_formatting(text)):
            url = _normalise(m.group().rstrip(".,);]!?"))
            if media.is_image_url(url):
                if not self._service_on("images"):
                    continue
                anchor = view.end_anchor()  # the just-appended message's end
                self._media_loader().fetch(
                    url, lambda data, nid=net.id, nm=name, a=anchor, u=url:
                    self._insert_autoload_image(nid, nm, a, u, data))
            elif media.is_x_url(url):  # X/Twitter → an inline summary card (FixTweet metadata)
                if not self._service_on("xcards"):
                    continue
                anchor = view.end_anchor()
                self._media_loader().fetch(
                    media.fxtwitter_api_url(url),
                    lambda data, nid=net.id, nm=name, a=anchor, link=url:
                    self._insert_autoload_card(nid, nm, a, link, data))
            elif media.is_web_card_url(url):
                if not self._service_on("webcards"):
                    continue
                anchor = view.end_anchor()
                self._media_loader().fetch(
                    url,
                    lambda data, nid=net.id, nm=name, a=anchor, link=url:
                    self._insert_autoload_web_card(nid, nm, a, link, data))

    def _append_action(self, net: Network, name: str, nick: str, text: str) -> None:
        if not self._network_live(net):
            return
        label = f"* {nick}"
        self._emit(net, name, *self._line_parts(label, self._nick_html(nick, action=True), text),
                   label_len=len(label))
        self._log(net, name, f"* {nick} {strip_formatting(text)}")
        self._maybe_comic(net, name, nick, text, action=True)

    def _gutter_px(self, view) -> int:
        """Pixel width of the time + nick gutter, so an inline embed/preview indents to the message
        column and never crosses the chat line into the time/nick area. Follows the nick COLUMN
        (align_nicks) — the thing that draws the line — not the text-wrap setting; 0 when there's no
        aligned column (then there's no fixed line to cross)."""
        if not self._align_nicks:
            return 0
        sample = self._ts_plain() + " " * (self._nick_width + 1)  # timestamp + nick column + a space
        return round(view.fontMetrics().horizontalAdvance(sample))

    def _line_parts(self, label: str, label_html: str, text: str) -> tuple[str, str]:
        """(inner_html, prefix_plain) — right-pads the nick into a fixed column when align_nicks is on."""
        if self._align_nicks:
            pad = max(0, self._nick_width - len(label))
            inner = "&nbsp;" * pad + label_html + " " + self._render(text)
            prefix = " " * (pad + len(label) + 1)
        else:
            inner = label_html + " " + self._render(text)
            prefix = label + " "
        return inner, prefix

    # ---- URL grabber + raw log -------------------------------------------
    _URL_RE = re.compile(r"(?:https?://|www\.)[^\s<>\"']+", re.IGNORECASE)

    def _grab_urls(self, nick: str, where: str, text: str) -> None:
        found = self._URL_RE.findall(strip_formatting(text))
        if not found:
            return
        when = self._fmt_when(datetime.now())
        for url in found:
            url = url.rstrip(".,);]!?")  # trailing sentence punctuation isn't part of the link
            entry = (when, nick, where, url)
            self._urls.append(entry)
            if len(self._urls) > 1000:
                self._urls.pop(0)
            if self._url_window is not None:
                self._url_window.add_row(entry)

    def _open_url_grabber(self) -> None:
        if self._url_window is None:
            from maxchat.ui.url_grabber import UrlGrabberDialog
            self._url_window = UrlGrabberDialog(self, self._urls, self._urls.clear)
            self._url_window.finished.connect(lambda _r: setattr(self, "_url_window", None))
        self._url_window.show()
        self._url_window.raise_()

    def _on_raw(self, net: Network, direction: str, line: str) -> None:
        entry = (net.name, direction, line)
        self._rawlog.append(entry)
        if len(self._rawlog) > 4000:
            del self._rawlog[:1000]
        if self._rawlog_window is not None:
            self._rawlog_window.add_line(*entry)

    def _open_raw_log(self) -> None:
        if self._rawlog_window is None:
            from maxchat.ui.raw_log import RawLogDialog
            self._rawlog_window = RawLogDialog(self, list(self._rawlog), self._rawlog.clear)
            self._rawlog_window.finished.connect(lambda _r: setattr(self, "_rawlog_window", None))
        self._rawlog_window.show()
        self._rawlog_window.raise_()

    def _is_highlight(self, net: Network, text: str) -> bool:
        low = strip_formatting(text).lower()
        me = (net.client.nick or "").lower()
        if me and me in low:
            return True
        return any(w in low for w in self._highlight_words)

    # ---- client wiring (per network) -------------------------------------
    def _wire_client(self, net: Network) -> None:
        c = net.client
        c.connected.connect(
            lambda n=net: self._call_if_network_live(
                n, self._append, SERVER_BUFFER, "* Socket connected; registering…"
            )
        )
        c.registered.connect(lambda n=net: self._call_if_network_live(n, self._on_registered))
        c.systemText.connect(lambda t, n=net: self._call_if_network_live(n, self._append, SERVER_BUFFER, t))
        c.message.connect(
            lambda target, nick, text, n=net:
            self._call_if_network_live(n, self._on_message, target, nick, text)
        )
        c.notice.connect(
            lambda target, nick, text, n=net:
            self._call_if_network_live(n, self._on_notice, target, nick, text)
        )
        c.joined.connect(lambda channel, nick, n=net: self._call_if_network_live(n, self._on_join, channel, nick))
        c.parted.connect(lambda channel, nick, n=net: self._call_if_network_live(n, self._on_part, channel, nick))
        c.kicked.connect(
            lambda channel, nick, reason, n=net:
            self._call_if_network_live(n, self._on_kick, channel, nick, reason)
        )
        c.quitUser.connect(lambda nick, n=net: self._call_if_network_live(n, self._on_quit, nick))
        c.nickChanged.connect(lambda old, new, n=net: self._call_if_network_live(n, self._on_nick, old, new))
        c.namesReply.connect(
            lambda channel, nicks, n=net: self._call_if_network_live(n, self._on_names, channel, nicks)
        )
        c.namesEnd.connect(
            lambda channel, n=net: self._call_if_network_live(n, self._on_names_end, channel)
        )
        c.topicChanged.connect(
            lambda channel, topic, n=net: self._call_if_network_live(n, self._on_topic, channel, topic)
        )
        c.modeChanged.connect(
            lambda target, by, modes, n=net: self._call_if_network_live(n, self._on_mode, target, by, modes)
        )
        c.whois.connect(lambda nick, text, n=net: self._call_if_network_live(n, self._on_whois, nick, text))
        c.awayChanged.connect(
            lambda nick, is_away, n=net: self._call_if_network_live(n, self._on_away, nick, is_away)
        )
        c.invited.connect(lambda nick, chan, n=net: self._call_if_network_live(n, self._on_invite, nick, chan))
        c.dccRequest.connect(lambda nick, body, n=net: self._call_if_network_live(n, self._on_dcc, nick, body))
        c.channelText.connect(
            lambda chan, text, n=net: self._call_if_network_live(n, self._on_channel_text, chan, text)
        )
        c.replyText.connect(lambda text, n=net: self._call_if_network_live(n, self._on_reply_text, text))
        c.ctcpReply.connect(
            lambda nick, kind, info, n=net: self._call_if_network_live(n, self._on_ctcp_reply, nick, kind, info)
        )
        c.ctcpSound.connect(
            lambda nick, target, f, t, n=net:
                self._call_if_network_live(n, self._on_ctcp_sound, nick, target, f, t)
        )
        c.set_ctcp_version(bool(config.pref("hide_version")), str(config.pref("ctcp_version") or ""))
        c.lag.connect(lambda secs, n=net: self._call_if_network_live(n, self._on_lag, secs))
        c.channelModeIs.connect(
            lambda chan, ml, n=net: self._call_if_network_live(n, self._on_channel_modes, chan, ml)
        )
        c.isonReply.connect(lambda online, n=net: self._call_if_network_live(n, self._on_ison, online))
        c.errorOccurred.connect(lambda m, n=net: self._call_if_network_live(n, self._on_net_error, m))
        c.disconnected.connect(lambda reason, n=net: self._call_if_network_live(n, self._on_disconnected, reason))
        c.rawLine.connect(lambda direction, line, n=net: self._call_if_network_live(n, self._on_raw, direction, line))

    def _on_channel_text(self, net: Network, chan: str, text: str) -> None:
        self._append(net, chan if chan in net.buffers else SERVER_BUFFER, f"* {text}")

    def _notice_echo_target(self, net: Network, target: str) -> str:
        return target if target in net.buffers else SERVER_BUFFER

    def _on_notice(self, net: Network, target: str, nick: str, text: str) -> None:
        source = self._strip(nick)
        if target.startswith(("#", "&")) and target in net.buffers:
            where = target
        elif source in net.buffers:
            where = source
        else:
            where = SERVER_BUFFER
        self._append(net, where, f"-{nick}- {text}")

    def _on_reply_text(self, net: Network, text: str) -> None:
        self._append(net, self._active_target(net), text)

    def _on_registered(self, net: Network) -> None:
        net.reconnect_attempt = 0
        net.server_attempt = 0
        net.connect_generation += 1  # successful registration invalidates the in-flight connect watchdog
        net.connected = True
        net.connected_at = time.monotonic()  # for /uptime per-connection
        net.online_friends = None  # re-seed the notify list for this (re)connection
        self._autoconnect_advance((net.name or "").lower())  # this one's up → start the next queued network
        self._style_network_item(net)
        self._append(net, SERVER_BUFFER, f"* Registered as {net.client.nick}")
        self._autoset_nick_width(net.client.nick)
        if self._friends:
            net.client.ison(self._friends)  # initial notify-list poll
        if net is self._active_net:
            self.nick_label.setText(net.client.nick)
            self.statusBar().showMessage(f"Connected to {net.name} as {net.client.nick}")
            self._set_connected_ui()
        self._run_perform(net)  # perform-on-connect commands

    def _run_perform(self, net: Network) -> None:
        """Run the bookmark's perform-on-connect commands against this network ($me → our nick)."""
        if not net.perform:
            return
        for raw in net.perform:  # target THIS net explicitly — never touch the global active pointers
            line = raw.strip().replace("$me", net.client.nick or "")
            if not line:
                continue
            if line.startswith("/"):
                self._command(line[1:], net=net, active=SERVER_BUFFER)
            else:
                net.client.send_raw(line)

    def _on_message(self, net: Network, target: str, nick: str, text: str) -> None:
        is_pm = not target.startswith(("#", "&"))
        buf = nick if is_pm else target  # PM → query keyed by sender
        if self._flood_check(net, nick):
            return  # sender just tripped the flood guard and was auto-ignored — drop this one
        is_action = text.startswith("\x01ACTION ") and text.endswith("\x01")
        body = text[8:-1] if is_action else text
        if is_action:
            self._append_action(net, buf, nick, body)
        else:
            self._append_msg(net, buf, nick, body)
        if is_pm:
            self._echo_pm(net, nick, body, is_action)
        highlight = (is_pm or self._is_highlight(net, body)) and not self._is_muted(net, buf)
        if highlight and self._beep_highlight and not config.pref("dnd"):
            QApplication.beep()
        if highlight and ((is_pm and self._notify_pm) or (not is_pm and self._notify_highlight)):
            who = self._strip(nick)
            self._notify(f"Private message · {who}" if is_pm else f"{who} mentioned you",
                         strip_formatting(body)[:140], net=net, buf=buf)
        self._mark_activity(net, buf, highlight)
        self._scripts.dispatch("on_message", net.name, target, nick, body, net=net)

    def _echo_pm(self, net: Network, sender: str, body: str, is_action: bool) -> None:
        """A private message opens its own query tab (under the server name). Also surface it where it
        will be SEEN: a running log in the server tab + the chat you're currently viewing."""
        if not self._pm_echo:
            return
        line = f"[PM] * {sender} {body}" if is_action else f"[PM] <{sender}> {body}"
        self._append(net, SERVER_BUFFER, line)  # a PM log under the server name
        act = self._active_buf
        if self._active_net is net and act and act not in (sender, SERVER_BUFFER):
            self._append(net, act, line)  # and in the chat you're looking at right now

    def _on_join(self, net: Network, channel: str, nick: str) -> None:
        if nick == net.client.nick:
            key = self._channel_key(channel)
            net.joined_channels.add(key)
            net.pending_names[key] = []
            net.names[channel] = []
            self._ensure_buffer(net, channel)
            net.client.send_raw(f"MODE {channel}")  # fetch the channel's modes for the Channel Modes popup
            cl = channel.lower()
            if cl in net.autojoin:  # autojoining: only the first configured channel takes focus
                net.autojoin.discard(cl)
                if cl == net.autojoin_focus:
                    self._switch_to(net, channel)
                # other autojoin channels open their buffer but don't steal focus
            else:
                self._switch_to(net, channel)  # manual /join → switch to it
        else:
            self._names_add(net, channel, nick)
            self._refresh_members(net, channel)
        if not self._hide_jp:
            self._append(net, channel, f"* {nick} joined {channel}", event=True)
        self._scripts.dispatch("on_join", net.name, channel, nick, net=net)

    def _on_part(self, net: Network, channel: str, nick: str) -> None:
        if nick == net.client.nick:
            key = self._channel_key(channel)
            net.joined_channels.discard(key)
            net.pending_names.pop(key, None)
            self._cancel_paste_timers(net.id, channel)
            if (net.id, key) in self._pending_channel_closes:
                self._pending_channel_closes.discard((net.id, key))
                self._close_buffer(net, channel)
                self._set_connected_ui()
                return
            net.names[channel] = []  # keep the buffer (scrollback) — re-joining resumes it
            self._refresh_members(net, channel)
            if not self._hide_jp:
                self._append(net, channel, f"* You have left {channel}", event=True)
            self._set_connected_ui()
            return
        self._names_remove(net, channel, nick)
        self._refresh_members(net, channel)
        if not self._hide_jp:
            self._append(net, channel, f"* {nick} left {channel}", event=True)

    def _on_kick(self, net: Network, channel: str, nick: str, reason: str) -> None:
        if self._strip(nick).lower() == self._strip(net.client.nick).lower():
            key = self._channel_key(channel)
            net.joined_channels.discard(key)
            net.pending_names.pop(key, None)
            self._cancel_paste_timers(net.id, channel)
            net.names[channel] = []
            self._refresh_members(net, channel)
            self._append(net, channel, f"* You were kicked from {channel}: {reason or nick}", event=True)
            self._set_connected_ui()
            if config.pref("auto_rejoin"):
                delay = max(0, int(config.pref("rejoin_delay") or 2)) * 1000
                self._append(net, channel, f"* Auto-rejoining {channel}…", event=True)
                QTimer.singleShot(delay, lambda n=net, ch=channel:
                                  n.connected and n.client.send_raw(f"JOIN {ch}"))
            return
        self._names_remove(net, channel, nick)
        self._refresh_members(net, channel)
        self._append(net, channel, f"* {nick} was kicked from {channel}: {reason or nick}", event=True)

    def _on_quit(self, net: Network, nick: str) -> None:
        for channel in list(net.names.keys()):
            if any(self._strip(x) == self._strip(nick) for x in net.names[channel]):
                self._names_remove(net, channel, nick)
                self._refresh_members(net, channel)
                if not self._hide_jp:
                    self._append(net, channel, f"* {nick} quit", event=True)

    def _on_nick(self, net: Network, old: str, new: str) -> None:
        is_me = self._strip(new) == self._strip(net.client.nick or "")  # client._nick already updated
        note = (f"* You are now known as {new}" if is_me
                else f"* {old} is now known as {new}")
        for channel, lst in net.names.items():
            present = False
            for idx, x in enumerate(lst):
                if self._strip(x) == self._strip(old):
                    prefix = x[: len(x) - len(self._strip(x))]
                    lst[idx] = prefix + new
                    present = True
            if present:  # announce the change in every channel you share with them
                self._append(net, channel, note, event=True)
            self._refresh_members(net, channel)
        if is_me:  # your own rename also shows on the server tab + updates the bottom label
            self._append(net, SERVER_BUFFER, note, event=True)
            if net is self._active_net:
                self.nick_label.setText(net.client.nick or new)

    def _on_names(self, net: Network, channel: str, nicks: list) -> None:
        key = self._channel_key(channel)
        pending = net.pending_names.setdefault(key, [])
        pending.extend(nicks)
        net.names[channel] = self._dedupe_names(pending)
        self._refresh_members(net, channel)

    def _on_names_end(self, net: Network, channel: str) -> None:
        key = self._channel_key(channel)
        if key in net.pending_names:
            net.names[channel] = self._dedupe_names(net.pending_names.pop(key))
            self._refresh_members(net, channel)

    @staticmethod
    def _dedupe_names(nicks: list) -> list:
        out, seen = [], set()
        for nick in nicks:
            bare = str(nick).lstrip("~&@%+").lower()
            if bare in seen:
                continue
            seen.add(bare)
            out.append(nick)
        return out

    def _on_topic(self, net: Network, channel: str, topic: str) -> None:
        net.topics[channel] = topic
        if self._active_net is net and self._active_buf == channel:
            self._set_topic(topic)

    def _modes_state(self, net: Network, chan: str) -> dict:
        return net.channel_modes.setdefault(chan, {"flags": set(), "key": "", "limit": 0})

    def _on_mode(self, net: Network, target: str, by: str, modes: str) -> None:
        if modes and target.startswith(("#", "&")):  # keep the tracked channel modes current
            parts = modes.split()
            _apply_mode_delta(self._modes_state(net, target), parts[0], parts[1:])
            if self._active_net is net and self._active_buf == target:
                self.chan_modes_btn.setEnabled(True)
        if not self._show_mode or not modes:
            return
        if target.startswith(("#", "&")):
            self._append(net, target, f"* {by} sets mode {modes}", event=True)
        else:
            self._append(net, SERVER_BUFFER, f"* mode {modes} ({target})", event=True)

    def _on_channel_modes(self, net: Network, chan: str, modeline: str) -> None:
        """RPL_CHANNELMODEIS (324) — the COMPLETE current modes; reset the tracked state then apply."""
        state = self._modes_state(net, chan)
        state["flags"].clear()
        state["key"], state["limit"] = "", 0
        parts = modeline.split()
        if parts:
            _apply_mode_delta(state, parts[0], parts[1:])

    def _open_channel_modes(self) -> None:
        net, chan = self._active_net, self._active_buf
        if net is None or not self._is_joined_channel(net, chan):
            shadow_message.information(self, "Channel Modes", "Open a channel first.")
            return
        net.client.send_raw(f"MODE {chan}")  # refresh from the server (updates the cache for next open)
        st = self._modes_state(net, chan)
        dlg = ChannelModesDialog(
            self, channel=chan, net_name=net.name,
            flags=set(st["flags"]), key=st["key"], limit=st["limit"],
            apply_cb=lambda change, n=net, c=chan: n.client.send_raw(f"MODE {c} {change}"),
        )
        dlg.exec()

    # ---- ignore list ------------------------------------------------------
    def _note(self, text: str) -> None:
        """Print a short status line to the active chat (or server tab)."""
        net = self._active_net
        if net is not None:
            self._append(net, self._active_target(net), text)

    def _apply_ignores(self) -> None:
        for net in self._networks.values():
            net.client.set_ignores(self._ignores)

    def _save_ignores(self) -> None:
        config.set_pref("ignores", self._ignores)
        self._apply_ignores()

    def _ignored(self, mask: str) -> bool:
        return any(x.lower() == mask.lower() for x in self._ignores)

    def _add_ignore(self, mask: str) -> None:
        m = normalize_mask(mask)
        if m and not self._ignored(m):
            self._ignores.append(m)
            self._save_ignores()
            self._note(f"* Ignoring {m}")

    def _remove_ignore(self, mask: str) -> None:
        m = normalize_mask(mask)
        if self._ignored(m):
            self._ignores = [x for x in self._ignores if x.lower() != m.lower()]
            self._save_ignores()
            self._note(f"* No longer ignoring {m}")

    def _set_ignores(self, masks: list) -> None:
        self._ignores = list(masks)
        self._save_ignores()

    def _open_ignore_list(self) -> None:
        IgnoreListDialog(self, ignores=list(self._ignores), save_cb=self._set_ignores).exec()

    def _set_aliases(self, aliases: dict) -> None:
        self._aliases = {str(k): str(v) for k, v in aliases.items()}
        config.set_pref("aliases", self._aliases)

    def _open_aliases(self) -> None:
        AliasEditorDialog(self, aliases=dict(self._aliases), save_cb=self._set_aliases).exec()

    # ---- friends / notify list -------------------------------------------
    def _friend(self, nick: str) -> bool:
        return any(f.lower() == self._strip(nick).lower() for f in self._friends)

    def _save_friends(self) -> None:
        config.set_pref("friends", self._friends)
        for net in self._networks.values():
            net.online_friends = None  # re-seed (avoids spurious online/offline alerts)
        self._poll_friends()

    def _add_friend(self, nick: str) -> None:
        nick = self._strip(nick)
        if nick and not self._friend(nick):
            self._friends.append(nick)
            self._save_friends()
            self._note(f"* Added {nick} to your notify list")

    def _remove_friend(self, nick: str) -> None:
        nick = self._strip(nick)
        if self._friend(nick):
            self._friends = [f for f in self._friends if f.lower() != nick.lower()]
            self._save_friends()
            self._note(f"* Removed {nick} from your notify list")

    def _set_friends(self, friends: list) -> None:
        self._friends = [str(f).strip() for f in friends if str(f).strip()]
        self._save_friends()

    def _open_friends(self) -> None:
        FriendsDialog(self, friends=list(self._friends), save_cb=self._set_friends).exec()

    def _poll_friends(self) -> None:
        if self._friends:
            for net in self._networks.values():
                if net.connected:
                    net.client.ison(self._friends)

    def _on_ison(self, net: Network, online: list) -> None:
        now = {n.lower() for n in online}
        cur = {f for f in self._friends if f.lower() in now}
        if net.online_friends is None:  # first poll → seed quietly (no spam on connect)
            net.online_friends = cur
            return
        for f in self._friends:
            on, was = f in cur, f in net.online_friends
            if on and not was:
                self._friend_event(net, f, True)
            elif was and not on:
                self._friend_event(net, f, False)
        net.online_friends = cur

    def _friend_event(self, net: Network, nick: str, online: bool) -> None:
        word = "is online" if online else "went offline"
        self._append(net, SERVER_BUFFER, f"* {nick} {word} ({net.name})")
        self._notify(f"{nick} {word}", net.name)

    # ---- flood / paste / invite protection -------------------------------
    def _flood_check(self, net: Network, nick: str) -> bool:
        """Auto-ignore a sender who exceeds the flood threshold. Returns True if they were just ignored."""
        if not self._flood_protect:
            return False
        bare = self._strip(nick)
        if not bare or self._friend(bare) or bare.lower() == (net.client.nick or "").lower():
            return False  # never flood-ignore yourself or a friend
        now = datetime.now().timestamp()
        times = self._flood_times.setdefault((net.id, bare.lower()), [])
        times.append(now)
        times[:] = [t for t in times if t >= now - self._flood_secs]
        if len(times) > self._flood_msgs:
            times.clear()
            self._add_ignore(bare)  # nick!*@* — their messages are now dropped
            self._note(f"* Auto-ignored {bare} (flood: {self._flood_msgs}+ messages in "
                       f"{int(self._flood_secs)}s) — /unignore {bare} to undo, or turn this off in "
                       "Preferences ▸ Protection")
            return True
        return False

    def _on_paste(self, lines: list) -> None:
        net, buf = self._active_net, self._active_buf
        if net is None or not buf or buf == SERVER_BUFFER:
            return
        if self._is_channel_name(buf) and not self._is_joined_channel(net, buf):
            self._append(net, buf, "! You are not joined to this channel; paste not sent.")
            return
        if not shadow_message.question(
                self, "Send paste",
                f"Send {len(lines)} pasted lines to {buf}?\n"
                "They'll be sent one per line, throttled to avoid flooding the channel."
        ):
            return
        self._cancel_paste_timers(net.id, buf)
        timers = self._paste_timers.setdefault((net.id, buf), [])
        for i, line in enumerate(lines):
            timer = QTimer(self)
            timer.setSingleShot(True)
            timer.timeout.connect(lambda ln=line, nid=net.id, b=buf, tm=timer: self._send_one(nid, b, ln, tm))
            timers.append(timer)
            timer.start(i * 650)

    def _cancel_paste_timers(self, net_id: int, buf: str | None = None) -> None:
        keys = [k for k in self._paste_timers if k[0] == net_id and (buf is None or k[1] == buf)]
        for key in keys:
            for timer in self._paste_timers.pop(key, []):
                timer.stop()
                timer.deleteLater()

    def _send_one(self, net_id: int | Network, buf: str, text: str, timer: QTimer | None = None) -> None:
        if timer is not None:
            key = (net_id.id if isinstance(net_id, Network) else int(net_id), buf)
            timers = self._paste_timers.get(key)
            if timers is not None and timer in timers:
                timers.remove(timer)
                if not timers:
                    self._paste_timers.pop(key, None)
            timer.deleteLater()
        net = net_id if isinstance(net_id, Network) else self._networks.get(int(net_id))
        if not text or not self._network_live(net) or not net.connected or buf not in net.buffers:
            return
        if self._is_channel_name(buf) and not self._is_joined_channel(net, buf):
            return
        if net.client.privmsg(buf, text):
            self._append_msg(net, buf, net.client.nick, text)

    def _on_image_pasted(self, img) -> None:
        """An image was pasted into the message box. Core no longer uploads it — that's handled by an
        optional plugin (e.g. the bundled image-upload example) via the ``on_image_paste`` hook. If no
        plugin consumes it, say so (you can't put an image into a text box on IRC)."""
        scripts = getattr(self, "_scripts", None)
        if scripts is not None and scripts.dispatch("on_image_paste", img):
            return
        self._note("* Pasted an image — enable an image-upload plugin to share it (Settings ▸ Scripts…).")

    def _script_insert_input(self, text: str) -> None:
        """Insert text at the cursor in the message box (used by plugins, e.g. to drop an upload link)."""
        cur = self.input.cursorPosition()
        cur_text = self.input.text()
        lead = "" if (cur == 0 or cur_text[cur - 1:cur] == " ") else " "
        snippet = f"{lead}{text} "
        self.input.setText(cur_text[:cur] + snippet + cur_text[cur:])
        self.input.setCursorPosition(cur + len(snippet))
        self.input.setFocus()

    def _on_invite(self, net: Network, nick: str, channel: str) -> None:
        bare = self._strip(nick)
        if self._invite_protect and bare and not self._friend(bare):  # invite-spam → auto-ignore
            now = datetime.now().timestamp()
            t = self._invite_times.setdefault((net.id, bare.lower()), [])
            t.append(now)
            t[:] = [x for x in t if x >= now - 30]
            if len(t) >= 4:
                t.clear()
                self._add_ignore(bare)
                self._note(f"* Auto-ignored {bare} (invite spam)")
                return
        if self._ignore_invites:
            return  # silently dropped
        self._append(net, self._active_target(net), f"* {bare} invites you to {channel}  (/join {channel})")
        self._notify(f"Invite from {bare}", channel)

    # ---- scripts (the api bridge + manager dialog) -----------------------
    def _script_echo(self, text: str) -> None:
        # a script reacting to a network event echoes into THAT network (its front buffer, or its server
        # tab if it isn't the network on screen) — never leaking into whatever tab is currently in front
        net = self._scripts.ctx_net or self._active_net
        if net is not None:
            self._append(net, self._active_target(net), text)

    def _script_say(self, target: str, text: str) -> None:
        net = self._scripts.ctx_net or self._active_net
        if net is not None and target and text:
            if not net.client.privmsg(target, text):
                self._append_send_failure(net, target)
                return
            if target in net.buffers:
                self._append_msg(net, target, net.client.nick, text)

    def _open_scripts(self) -> None:
        ScriptsDialog(self, manager=self._scripts).exec()

    # ---- DCC file transfer -----------------------------------------------
    def _dcc_ip(self) -> str:
        return self._dcc_ip_for(self._pending_dcc_net or self._active_net)

    def _dcc_ip_for(self, net: Network | None) -> str:
        ip = str(config.pref("dcc_ip") or "").strip()
        if ip:
            return ip
        if net is not None and net.client.local_address().count(".") == 3:  # IPv4 only
            return net.client.local_address()
        return "127.0.0.1"

    def _dcc_dir(self) -> str:
        d = str(config.pref("dcc_dir") or "").strip() or os.path.join(config.config_dir(), "downloads")
        os.makedirs(d, exist_ok=True)
        return d

    def _dcc_ctcp(self, net: Network, nick: str, body: str) -> None:
        if self._network_live(net):
            net.client.send_raw(f"PRIVMSG {nick} :\x01{body}\x01")  # send a DCC CTCP over this network

    def _on_dcc(self, net: Network, nick: str, body: str) -> None:
        if not self._network_live(net):
            return
        self._pending_dcc_net = net  # so a DCC CHAT born inside handle_dcc lands on the right network
        try:
            self._dcc.handle_dcc(
                self._strip(nick), body, lambda n, b, nt=net: self._dcc_ctcp(nt, n, b),
                advertised_ip=self._dcc_ip_for(net),
            )
        finally:
            if self._pending_dcc_net is net:
                self._pending_dcc_net = None

    def _on_dcc_added(self, tr) -> None:
        self._open_dcc()  # surface the transfers window for offers + sends
        if tr.send:
            return
        policy = str(config.pref("dcc_accept") or "ask")
        trusted = [str(t).lower() for t in (config.pref("dcc_trusted") or [])]
        if policy == "all" or (policy == "trusted" and self._strip(tr.nick).lower() in trusted):
            self._dcc.accept(tr)  # auto-accept per policy
            self._notify(f"Auto-accepting file from {tr.nick}", tr.filename)
        else:
            self._notify(f"File offer from {tr.nick}", tr.filename)

    def _send_file(self, net: Network, nick: str) -> None:
        from PySide6.QtWidgets import QFileDialog
        path, _ = QFileDialog.getOpenFileName(self, f"Send a file to {self._strip(nick)}")
        if path:
            self._pending_dcc_net = net
            try:
                self._dcc.offer(
                    self._strip(nick), path, lambda n, b, nt=net: self._dcc_ctcp(nt, n, b),
                    advertised_ip=self._dcc_ip_for(net),
                )
            finally:
                if self._pending_dcc_net is net:
                    self._pending_dcc_net = None
            self._open_dcc()

    # ---- DCC chat ---------------------------------------------------------
    def _start_dcc_chat(self, net: Network, nick: str) -> None:
        if not self._network_live(net):
            return
        self._pending_dcc_net = net
        try:
            self._dcc.offer_chat(
                self._strip(nick), lambda n, b, nt=net: self._dcc_ctcp(nt, n, b),
                advertised_ip=self._dcc_ip_for(net),
            )
        finally:
            if self._pending_dcc_net is net:
                self._pending_dcc_net = None

    def _on_dcc_chat(self, nick: str, chat) -> None:
        net = self._pending_dcc_net or self._active_net
        if not self._network_live(net):
            return
        peer = self._strip(nick)
        name = "=" + peer  # DCC-chat buffers are keyed with a leading "=" (classic convention)
        old = self._dcc_chats.get((net.id, name))
        if old is not None and old is not chat:
            self._append(net, name, f"* Replacing existing DCC chat with {peer}.")
            try:
                old.close()
                old.deleteLater()
            except Exception:
                pass
        self._dcc_chats[(net.id, name)] = chat
        self._ensure_buffer(net, name)
        if chat.status == "failed":
            self._append(net, name, f"* DCC chat with {peer} failed to listen.")
            self._switch_to(net, name)
            return
        self._append(net, name, f"* DCC chat with {peer} — connecting…")
        chat.lineReceived.connect(
            lambda line, nt=net, nm=name, pk=peer, ch=chat: self._on_dcc_chat_line(nt, nm, pk, line, ch))
        chat.stateChanged.connect(
            lambda state, nt=net, nm=name, pk=peer, ch=chat:
            self._on_dcc_chat_state(nt, nm, pk, state, ch))
        self._switch_to(net, name)
        self._notify("DCC chat", f"with {peer}")

    def _on_dcc_chat_line(self, net: Network, name: str, peer: str, line: str, chat) -> None:
        if self._network_live(net) and self._dcc_chats.get((net.id, name)) is chat:
            self._append_msg(net, name, peer, line)

    def _on_dcc_chat_state(self, net: Network, name: str, peer: str, state: str, chat=None) -> None:
        if not self._network_live(net):
            return
        if chat is not None and self._dcc_chats.get((net.id, name)) is not chat:
            return
        if state == "active":
            self._append(net, name, f"* DCC chat with {peer} connected.")
        elif state == "closed":
            self._append(net, name, f"* DCC chat with {peer} closed.")
            if chat is not None:
                self._dcc_chats.pop((net.id, name), None)

    def _open_dcc(self) -> None:
        if self._dcc_window is None:
            self._dcc_window = DCCDialog(self, manager=self._dcc)
        self._dcc_window.show()
        self._dcc_window.raise_()

    def _active_target(self, net: Network) -> str:
        """Where query replies (WHOIS / CTCP) print: the front buffer if it's this net, else its server tab."""
        if self._active_net is net and self._active_buf:
            return self._active_buf
        return SERVER_BUFFER

    def _on_whois(self, net: Network, nick: str, text: str) -> None:
        target = self._active_target(net)
        for line in text.split("\n"):  # WHOIS is multi-line — print each line to the active chat
            self._append(net, target, f"* {line}")

    def _on_ctcp_reply(self, net: Network, nick: str, kind: str, info: str) -> None:
        line = f"[CTCP {kind} reply from {self._strip(nick)}]"
        self._append(net, self._active_target(net), f"{line} {info}".rstrip())

    def _send_ctcp(self, net: Network, nick: str, kind: str) -> None:
        target = self._active_target(net)
        if net.client.ctcp(nick, kind):
            self._append(net, target, f"[CTCP {kind} → {self._strip(nick)}]")
        else:
            self._append_send_failure(net, target)

    def _on_ctcp_sound(self, net: Network, nick: str, target: str, file: str, text: str) -> None:
        """An incoming CTCP SOUND. Always show the (optional) action text; play the named .wav only if
        the feature is enabled AND we actually have that file (resolve() returns None otherwise)."""
        if self._flood_check(net, nick):
            return
        is_pm = not target.startswith(("#", "&"))
        buf = self._strip(nick) if is_pm else (target or SERVER_BUFFER)
        self._show_sound(net, buf, nick, file, text, play=bool(config.pref("ctcp_sound")))
        if is_pm:
            self._echo_pm(net, nick, text or f"plays {file}", True)

    def _show_sound(self, net: Network, buf: str, nick: str, file: str, text: str, play: bool) -> None:
        """Display a SOUND as an action line, and play it if asked + the .wav exists locally."""
        self._append_action(net, buf, nick, text or (f"plays {file}" if file else "plays a sound"))
        if play and file:
            path = sounds.resolve(file)
            if path:
                self._sound.play(path)

    def _append_send_failure(self, net: Network, target: str) -> None:
        where = target if target in net.buffers else self._active_target(net)
        self._append(net, where, "! Not connected; message not sent.")

    def _on_away(self, net: Network, nick: str, is_away: bool) -> None:
        n = self._strip(nick).lower()
        if is_away:
            net.away.add(n)
        else:
            net.away.discard(n)
        if net is self._active_net:
            self._refresh_active_members()

    def _on_net_error(self, net: Network, msg: str) -> None:
        """Show a socket/IRC error, with an actionable hint for TLS handshake failures (a cryptic
        OpenSSL string like 'packet length too long' usually means TLS was attempted against something
        not speaking TLS right then — wrong port, or a firewall/antivirus intercepting the connection)."""
        self._append(net, SERVER_BUFFER, f"! {msg}")
        low = msg.lower()
        if "handshake" in low or "ssl" in low or "tls" in low or "record layer" in low:
            self._append(
                net, SERVER_BUFFER,
                "  ↳ TLS handshake failed. Likely causes: the port isn't actually TLS (try the server's "
                "OTHER SSL port, or a plaintext port with SSL un-ticked — Server List ▸ Edit), or a "
                "firewall / antivirus that scans encrypted connections is breaking it (add an exception "
                "or turn off HTTPS/SSL scanning).",
            )

    def _on_disconnected(self, net: Network, reason: str) -> None:
        net.connected = False
        self._autoconnect_advance((net.name or "").lower())  # connect failed → don't stall the queue
        net.joined_channels.clear()
        net.pending_names.clear()
        self._cancel_paste_timers(net.id)
        self._pending_channel_closes = {
            key for key in self._pending_channel_closes if key[0] != net.id
        }
        self._style_network_item(net)
        if net is self._active_net:
            self.statusBar().showMessage("Disconnected")
            self._set_connected_ui()
        if net.reconnect_pending:
            return  # a reconnect is already queued (a connect failure can fire error + disconnect)
        self._append(net, SERVER_BUFFER, f"* Disconnected: {reason}")
        if self._auto_reconnect and net.reconnect_params and not net.intentional:
            net.reconnect_attempt += 1
            net.reconnect_pending = True
            delay = min(60, 5 * net.reconnect_attempt)
            retry = ""
            nextsrv = ""
            if net.servers:
                next_index = net.server_index
                next_try = max(1, net.server_attempt + 1)
                if len(net.servers) > 1 and net.server_attempt >= SERVER_RETRY_LIMIT:
                    next_index = (net.server_index + 1) % len(net.servers)
                    next_try = 1
                h, port, _ = net.servers[next_index]
                nextsrv = f" → {h}:{port}"
                retry = f" (server try {next_try}/{SERVER_RETRY_LIMIT})"
            self._append(net, SERVER_BUFFER,
                         f"* Auto-reconnect in {delay}s (attempt {net.reconnect_attempt}){retry}{nextsrv}…")
            QTimer.singleShot(delay * 1000, lambda n=net: self._do_reconnect(n))

    def _set_connected_ui(self) -> None:
        net = self._active_net
        connected = bool(net and net.connected)
        reconnecting = bool(net and not connected and net.reconnect_params is not None
                            and (net.reconnect_pending or not net.intentional))
        self.act_connect.setEnabled(True)  # you can always open another network
        self.act_disconnect.setEnabled(connected or reconnecting)  # also stops a reconnect loop
        self.act_join.setEnabled(connected)
        self.act_list.setEnabled(connected)
        in_channel = self._is_joined_channel(net, self._active_buf)
        self.act_leave.setEnabled(in_channel)
        self.chan_modes_btn.setEnabled(in_channel)

    # ---- actions ----------------------------------------------------------
    def _open_server_list(self) -> None:
        dlg = ServerListDialog(self)
        dlg.connectRequested.connect(self._connect_network)
        dlg.exec()

    def _open_channel_list(self) -> None:
        net = self._active_net
        if net is None or not net.connected:
            shadow_message.information(self, "Channel List", "Connect to a network first.")
            return
        from maxchat.ui.channel_list import ChannelListDialog
        ChannelListDialog(self, net, lambda ch: net.client.join(ch)).exec()

    def _connect_network(self, net: dict) -> None:
        self._promote_network(net.get("name") or "", net.get("host") or "")
        self._connect_with(
            name=net.get("name") or "", host=net.get("host", ""),
            port=int(net.get("port", 6697)), tls=bool(net.get("tls", True)),
            nick=net.get("nick") or "comicfan", username=net.get("username", ""),
            realname=net.get("realname", ""), account=net.get("account", ""),
            accept_invalid_cert=bool(net.get("accept_invalid_cert", False)),
            allow_insecure_auth=bool(net.get("allow_insecure_auth", False)),
            password=net.get("password", ""), server_pass=net.get("server_pass", ""),
            channels=net.get("channels", ""),
            servers=self._parse_servers(net.get("servers")), perform=net.get("perform") or [],
            proxy=self._proxy_config(net),
        )

    @staticmethod
    def _merge_default_networks() -> None:
        """One-time-per-version: merge bundled defaults into the saved address book.

        We append missing networks and fill in missing bundled failover servers/homepages for existing
        networks, leaving the user's order, edits, and custom servers untouched.
        """
        if int(config.get_setting("networks_merge_version", 0) or 0) >= NETWORKS_MERGE_VERSION:
            return
        saved = list(config.get_setting("networks", None) or [])
        if saved:  # only MERGE into a real saved list; a fresh config already has the full defaults
            defaults = {(n.get("name") or "").lower(): n for n in DEFAULT_NETWORKS}
            have = {(n.get("name") or "").lower() for n in saved}
            for net in saved:
                default = defaults.get((net.get("name") or "").lower())
                if not default:
                    continue
                servers = list(net.get("servers") or [])
                seen = {str(s).lower() for s in servers}
                for server in default.get("servers") or []:
                    if str(server).lower() not in seen:
                        servers.append(server)
                        seen.add(str(server).lower())
                net["servers"] = servers
                if not net.get("website") and default.get("website"):
                    net["website"] = default.get("website")
            saved += [dict(d) for d in DEFAULT_NETWORKS if (d.get("name") or "").lower() not in have]
            config.set_setting("networks", saved)
        config.set_setting("networks_merge_version", NETWORKS_MERGE_VERSION)

    @staticmethod
    def _promote_network(name: str, host: str) -> None:
        """Move the just-connected network to the TOP of the saved address book, so the networks you
        actually use bubble up near the top of the Server List over time."""
        nets = list(config.pref("networks") or [])
        idx = next((i for i, n in enumerate(nets)
                    if (n.get("name") or "") == name and (n.get("host") or "") == host), -1)
        if idx > 0:
            nets.insert(0, nets.pop(idx))
            config.set_pref("networks", nets)

    @staticmethod
    def _parse_servers(spec) -> list:
        """Parse a bookmark's extra servers → [(host, port, tls), …]. Each entry is `host`,
        `host:port`, or `host:+port` (a leading `+` on the port means TLS). `/` also works as the
        separator. ``spec`` may be a list of such strings or one newline/comma-separated string."""
        if not spec:
            return []
        lines = spec if isinstance(spec, list) else re.split(r"[\s,]+", str(spec))
        out = []
        for raw in lines:
            s = raw.strip()
            if not s:
                continue
            host, port, tls = s, None, False
            for sep in (":", "/"):
                if sep in s:
                    host, _, prt = s.partition(sep)
                    prt = prt.strip()
                    tls = prt.startswith("+")
                    try:
                        port = int(prt.lstrip("+"))
                    except ValueError:
                        port = None
                    break
            out.append((host.strip(), port or (6697 if tls else 6667), tls))
        return out

    @staticmethod
    def _proxy_config(net: dict) -> dict:
        ptype = str(net.get("proxy_type") or "none").strip().lower()
        if ptype in ("", "none"):
            return {"type": "none"}
        try:
            port = int(net.get("proxy_port") or 0)
        except (TypeError, ValueError):
            port = 0
        return {
            "type": ptype,
            "host": str(net.get("proxy_host") or "").strip(),
            "port": port,
            "username": str(net.get("proxy_username") or ""),
            "password": str(net.get("proxy_password") or ""),
        }

    @staticmethod
    def _connection_key(*, name: str, host: str, port: int, tls: bool, nick: str, account: str,
                        server_pass: str, proxy: dict | None) -> str:
        proxy = proxy or {}
        proxy_part = (
            str(proxy.get("type") or "none").lower(),
            str(proxy.get("host") or "").lower(),
            str(proxy.get("port") or 0),
            str(proxy.get("username") or "").lower(),
        )
        pass_part = (
            "pass:" + hashlib.sha256(server_pass.encode("utf-8")).hexdigest()[:12]
            if server_pass else "nopass"
        )
        parts = (
            name.lower(), host.lower(), str(int(port)), "tls" if tls else "plain",
            nick.lower(), account.lower(), pass_part, *proxy_part,
        )
        return "|".join(parts)

    def _open_connect(self) -> None:
        dlg = ConnectDialog(self)
        if not dlg.exec():
            return
        v = dlg.values()
        self._connect_with(
            name="", host=v["host"], port=v["port"], tls=v["tls"], nick=v["nick"],
            password=v["password"] or "", server_pass=v.get("server_pass", ""), channels=v["channels"],
        )

    def _connect_with(self, *, host, port, tls, nick, username="", realname="", account="", password="",
                      server_pass="", accept_invalid_cert=False, allow_insecure_auth=False, channels,
                      name="", servers=None, perform=None, proxy=None) -> None:
        chans = (
            [c if c.startswith(("#", "&")) else "#" + c for c in channels.replace(",", " ").split()]
            if isinstance(channels, str)
            else list(channels)
        )
        disp = name or host  # the network name comes from the bookmark; Quick Connect falls back to host
        proxy = proxy or {"type": "none"}
        key = self._connection_key(
            name=disp, host=host, port=int(port), tls=bool(tls), nick=nick,
            account=account or "", server_pass=server_pass or "", proxy=proxy,
        )
        existing = self._find_network(key)
        if existing is not None and existing.connected:
            self._switch_to(existing, SERVER_BUFFER)  # already connected → just focus it
            return
        net = existing or self._new_network(disp)
        net.connection_key = key
        net.servers = [(host, int(port), bool(tls))] + list(servers or [])  # primary, then failovers
        net.server_index = 0
        if perform is not None:  # preserve across reconnects unless a fresh connect overrides it
            net.perform = perform if isinstance(perform, list) else re.split(r"[\n]+", str(perform))
        net.reconnect_params = {
            "host": host, "port": port, "tls": tls, "nick": nick,
            "username": username or None, "realname": realname or None,
            "sasl_password": password or None, "sasl_account": account or None,
            "password": server_pass or None,  # server PASS (private servers / bouncers) — distinct from SASL
            "accept_invalid_cert": accept_invalid_cert, "allow_insecure_auth": allow_insecure_auth,
            "autojoin": chans,
            "proxy": proxy,
        }
        net.autojoin = {c.lower() for c in chans}
        net.autojoin_focus = chans[0].lower() if chans else None
        net.intentional = False
        net.reconnect_attempt = 0
        net.server_attempt = 1
        net.reconnect_pending = False
        joining = (" — joining " + " ".join(chans)) if chans else ""
        self._append(
            net, SERVER_BUFFER,
            f"* Connecting to {host}:{port}{' (SSL)' if tls else ''} as {nick}{joining}…",
        )
        self._start_connect_attempt(net)
        self._switch_to(net, SERVER_BUFFER)
        self.statusBar().showMessage(f"Connecting to {disp}…")

    def _disconnect_net(self, net: Network) -> None:
        net.intentional = True          # block auto-reconnect
        net.reconnect_pending = False   # cancel any reconnect already scheduled/looping
        net.connect_generation += 1     # invalidate any in-flight connect watchdog
        try:
            if net.client.is_connected():
                net.client.quit()        # polite QUIT when actually connected
            else:
                net.client.disconnect()  # abort an in-flight connect (stops the reconnect loop)
                self._append(net, SERVER_BUFFER, "* Reconnection stopped.")
        except Exception:
            pass
        net.connected = False
        self._style_network_item(net)
        if net is self._active_net:
            self._set_connected_ui()

    def _reconnect_net(self, net: Network) -> None:
        net.intentional = False  # a manual reconnect overrides a prior intentional disconnect
        if net.reconnect_params:
            self._do_reconnect(net, force_next=True)

    def _disconnect(self) -> None:
        if self._active_net is not None:
            self._disconnect_net(self._active_net)

    def _disconnect_all(self) -> None:
        for net in list(self._networks.values()):
            if net.connected or net.reconnect_pending:
                self._disconnect_net(net)

    def _reconnect_all(self) -> None:
        for net in list(self._networks.values()):
            if not net.connected and net.reconnect_params:
                self._reconnect_net(net)

    def _mark_all_read(self) -> None:
        """Clear every buffer's activity/highlight marker (and the title/tray count)."""
        self._unread.clear()
        self._unread_hi.clear()
        for net in self._networks.values():
            net.unread_marked.clear()
            for item in net.tree_items.values():
                self._clear_mark(item, net)
        if self._use_tabs:
            for i in range(self._tabbar.count()):
                self._tabbar.setTabTextColor(i, QColor())
        self._refresh_title()

    def _do_reconnect(self, net: Network, force_next: bool = False) -> None:
        net.reconnect_pending = False
        if net.reconnect_params and not net.intentional:
            if len(net.servers) > 1:  # failover → advance to the next server in the list
                if force_next or net.server_attempt >= SERVER_RETRY_LIMIT:
                    net.server_index = (net.server_index + 1) % len(net.servers)
                    net.server_attempt = 1
                else:
                    net.server_attempt += 1
                host, port, tls = net.servers[net.server_index]
                net.reconnect_params.update(host=host, port=port, tls=tls)
            else:
                net.server_attempt = max(1, net.server_attempt + 1)
            aj = net.reconnect_params.get("autojoin") or []
            net.autojoin = {c.lower() for c in aj}
            net.autojoin_focus = aj[0].lower() if aj else None
            p = net.reconnect_params
            retry = (
                f" (server try {net.server_attempt}/{SERVER_RETRY_LIMIT})"
                if net.servers else ""
            )
            self._append(net, SERVER_BUFFER,
                         f"* Reconnecting to {p['host']}:{p['port']}{' (SSL)' if p['tls'] else ''}{retry}…")
            self._start_connect_attempt(net)

    def _start_connect_attempt(self, net: Network) -> None:
        net.connect_generation += 1
        generation = net.connect_generation
        net.client.connect_to(**net.reconnect_params)
        QTimer.singleShot(
            CONNECT_ATTEMPT_TIMEOUT_MS,
            lambda n=net, g=generation: self._connect_attempt_timeout(n, g),
        )

    def _connect_attempt_timeout(self, net: Network, generation: int) -> None:
        if (
            not self._network_live(net)
            or net.connect_generation != generation
            or net.connected
            or net.reconnect_pending
            or net.intentional
            or not net.reconnect_params
        ):
            return
        host = net.reconnect_params.get("host", "?")
        port = net.reconnect_params.get("port", "?")
        self._append(net, SERVER_BUFFER, f"! Connection timed out while connecting to {host}:{port}")
        try:
            net.client.disconnect()
        except Exception:
            pass
        self._on_disconnected(net, "connection timed out")

    def _join_channel(self) -> None:
        net = self._active_net
        if net is None or not net.connected:
            return
        text, ok = shadow_message.get_text(self, "Join Channel", "Channel name:")
        if ok and text.strip():
            ch = text.strip().split()[0]
            if not ch.startswith(("#", "&")):
                ch = "#" + ch
            net.client.join(ch)

    def _leave_channel(self) -> None:
        net = self._active_net
        if net is not None and self._active_buf:
            self._leave_channel_for(net, self._active_buf)

    def _leave_channel_for(self, net: Network, channel: str) -> None:
        if not self._is_joined_channel(net, channel):
            return
        if net.client.send_raw(f"PART {channel}"):
            net.joined_channels.discard(self._channel_key(channel))
            self._cancel_paste_timers(net.id, channel)
            self._set_connected_ui()
        else:
            self._append_send_failure(net, channel)

    def _toggle_comic(self, on: bool) -> None:
        self.comic_area.setVisible(on)
        if not on and self._emotion_wheel is not None:
            self._emotion_wheel.hide()
        if on:
            sizes = self._comic_split.sizes()
            if not sizes or sizes[0] < 80:  # give the comic area room if it was collapsed
                h = max(self._comic_split.height(), 400)
                self._comic_split.setSizes([h // 2, h - h // 2])
            if not self._comic_ready:
                self._load_comic_art()
            self._relayout_panels()

    def eventFilter(self, obj, event):
        et = event.type()
        if et == QEvent.Type.KeyPress and self._redirect_key(event):
            return True
        if obj is getattr(self, "comic_area", None) and et == QEvent.Type.Resize:
            self._relayout_panels()  # reflow + rescale panels to the new size
        elif obj is getattr(self, "topic", None) and et == QEvent.Type.MouseButtonDblClick:
            self._edit_topic()  # double-click the topic bar to set the channel topic
            return True
        return super().eventFilter(obj, event)

    @staticmethod
    def _is_text_entry(w) -> bool:
        """True if w is an editable text field — so we DON'T hijack typing meant for it."""
        from PySide6.QtWidgets import QAbstractSpinBox, QComboBox, QLineEdit, QPlainTextEdit, QTextEdit
        if w is None:
            return False
        if isinstance(w, QAbstractSpinBox):
            return True
        if isinstance(w, QComboBox):
            return w.isEditable()
        if isinstance(w, QLineEdit):
            return True
        if isinstance(w, (QTextEdit, QPlainTextEdit)):
            return not w.isReadOnly()  # the chat view is read-only → not a text entry
        return False

    def _redirect_key(self, event) -> bool:
        """HexChat-style focus: typing anywhere in the main window (chat, tree, an empty spot) jumps to
        the message box; Esc closes the find bar / returns focus there. Never interferes with a menu,
        a dialog, or an editable field."""
        app = QApplication.instance()
        if app is None or app.activePopupWidget() is not None or app.activeModalWidget() is not None:
            return False  # a menu/combo popup or a modal dialog is open — leave it alone
        if app.activeWindow() is not self:
            return False  # a non-modal pop-up window (image viewer, raw log…) is focused
        if event.key() == Qt.Key.Key_Escape:
            if self._find_bar.isVisible():
                self._hide_find()
                return True
            if app.focusWidget() is not self.input:
                self.input.setFocus()
                return True
            return False
        focus = app.focusWidget()
        if self._is_text_entry(focus):
            return False  # already typing in a real field
        from PySide6.QtWidgets import QAbstractButton, QAbstractItemView, QAbstractSlider, QTabBar
        if isinstance(focus, (QAbstractButton, QAbstractItemView, QAbstractSlider, QTabBar)):
            return False
        mods = event.modifiers()
        if mods & (Qt.KeyboardModifier.ControlModifier | Qt.KeyboardModifier.AltModifier
                   | Qt.KeyboardModifier.MetaModifier):
            return False  # let Ctrl/Alt shortcuts through (Ctrl+F, Ctrl+C, …)
        text = event.text()
        if not text or not text.isprintable():
            return False  # arrows, Tab, function keys, etc. — not typing
        self.input.setFocus()
        self.input.insert(text)
        return True

    def showEvent(self, event) -> None:
        super().showEvent(event)
        if not getattr(self, "_focused_once", False):  # land in the message box once the window is up
            self._focused_once = True
            self.input.setFocus()

    def _edit_topic(self) -> None:
        net = self._active_net
        if net is not None and self._is_joined_channel(net, self._active_buf):
            self._edit_topic_for(net, self._active_buf)  # server rejects if you lack +t rights

    def _build_comic_panels(self, n: int) -> None:
        """Create 1–6 comic panels parented to the comic area. They are positioned by
        `_relayout_panels`, which reflows them into rows to fit the area's current width/height."""
        n = max(1, min(6, int(n)))
        for w in self._comic_panel_widgets:
            w.deleteLater()
        self._comic_panel_widgets = []
        for i in range(1, n + 1):
            panel = QLabel(f"Panel {i}", self.comic_area)  # rendered pixmap (scaled), or placeholder text
            panel.setObjectName("comicPanel")
            panel.setAlignment(Qt.AlignmentFlag.AlignCenter)
            panel.setEnabled(False)
            panel.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)  # right-click → comic menu
            panel.customContextMenuRequested.connect(lambda _pos, p=panel: self._comic_menu(p))
            panel.show()
            self._comic_panel_widgets.append(panel)
        self._relayout_panels()

    def _relayout_panels(self) -> None:
        """Reflow the 1–6 square panels to fill the comic area. We pick the row/column split that
        makes the squares as large as possible: a short, wide area packs them onto one line; a taller
        area wraps them into more rows (e.g. 6 → [1][2][3] / [4][5][6]). Then rescale their pixmaps."""
        panels = getattr(self, "_comic_panel_widgets", None)
        if not panels:
            return
        n = len(panels)
        gap, margin = 4, 4
        aw = max(1, self.comic_area.width() - 2 * margin)
        ah = max(1, self.comic_area.height() - 2 * margin)
        best = None  # (size, rows, cols) maximising the square edge
        for rows in range(1, n + 1):
            cols = (n + rows - 1) // rows  # ceil — columns needed for this row count
            size = min((aw - (cols - 1) * gap) / cols, (ah - (rows - 1) * gap) / rows)
            if size > 0 and (best is None or size > best[0]):
                best = (size, rows, cols)
        if best is None:
            return
        size, rows, cols = max(1, int(best[0])), best[1], best[2]
        block_h = rows * size + (rows - 1) * gap
        y0 = margin + (ah - block_h) // 2
        for idx, panel in enumerate(panels):
            r, col = divmod(idx, cols)
            in_row = min(cols, n - r * cols)  # last row may be partial → centre it
            row_w = in_row * size + (in_row - 1) * gap
            x0 = margin + (aw - row_w) // 2
            panel.setGeometry(x0 + col * (size + gap), y0 + r * (size + gap), size, size)
        self._render_strip()

    # ---- comic rendering --------------------------------------------------
    def _load_comic_prefs(self) -> None:
        """Comic settings — read once at startup and again whenever the Comic Settings dialog saves."""
        if getattr(self, "act_comic_captions", None) is not None:  # keep the Comic-menu toggle in sync
            self.act_comic_captions.blockSignals(True)
            self.act_comic_captions.setChecked(bool(config.pref("comic_captions")))
            self.act_comic_captions.blockSignals(False)
        self._comic_panels = max(1, min(MAX_COMIC_PANELS, int(config.pref("comic_panels") or 4)))
        self._comic_dir = str(config.pref("comic_art_dir") or "")
        self._comic_self_char = str(config.pref("comic_self_char") or "")
        self._comic_bg_name = str(config.pref("comic_bg") or "")
        self._comic_per_panel = max(1, min(6, int(config.pref("comic_per_panel") or 3)))
        self._comic_chars_global = {  # GLOBAL nick → character-stem assignments
            str(k).lower(): v for k, v in (config.pref("comic_chars") or {}).items()
        }
        self._comic_channels = dict(config.pref("comic_channels") or {})  # per-channel overrides
        self._comic_ignore = {str(n).lower() for n in (config.pref("comic_ignore") or [])}  # comic-only ignore
        self._comic_ignore_cmds = bool(config.pref("comic_ignore_cmds"))  # master toggle for command filter
        self._comic_bot_patterns = [str(p).lower() for p in (config.pref("comic_bot_patterns") or []) if p]
        self._set_comic_cmd_regex(config.pref("comic_exclude_regex") or "")  # custom exclude pattern

    def _load_comic_art(self) -> None:
        art_dir = self._comic_dir or comic_assets.bundled_art_dir()  # bundled test art if none configured
        self._comic_backgrounds, self._comic_characters = (
            comic_assets.scan_art_dir(art_dir) if art_dir else ([], [])
        )
        self._nick_char.clear()
        self._comic_bg_cache.clear()
        self._comic_blank_cache.clear()
        self._comic_bg_img = None
        if self._comic_backgrounds:
            chosen = next(
                (p for p in self._comic_backgrounds if p.name.lower() == self._comic_bg_name.lower()),
                self._comic_backgrounds[0],
            )
            self._comic_bg_img = comic_assets.load_background(chosen)
            self._comic_bg_cache[chosen.name] = self._comic_bg_img
        self._update_self_view()  # refresh the emotion-picker faces to the (possibly new) self character

    @staticmethod
    def _chan_key(net: Network, buf: str) -> str:
        return f"{net.name}/{buf}"

    def _chan_cfg(self) -> dict:
        """Per-channel comic overrides {bg, chars} for the ACTIVE channel (empty if none/server)."""
        if self._active_net is None or not self._active_buf or self._active_buf == SERVER_BUFFER:
            return {}
        return self._comic_channels.get(self._chan_key(self._active_net, self._active_buf), {}) or {}

    def _char_path_by_stem(self, stem: str):
        if not stem:
            return None
        return next((p for p in self._comic_characters if p.stem.lower() == stem.lower()), None)

    def _bg_by_name(self, name: str):
        """Background image for a filename, falling back to the global default then the first available."""
        if not self._comic_backgrounds:
            return None
        chosen = next((p for p in self._comic_backgrounds if p.name.lower() == (name or "").lower()), None)
        if chosen is None:
            chosen = next(
                (p for p in self._comic_backgrounds if p.name.lower() == (self._comic_bg_name or "").lower()),
                self._comic_backgrounds[0],
            )
        if chosen.name not in self._comic_bg_cache:
            self._comic_bg_cache[chosen.name] = comic_assets.load_background(chosen)
        return self._comic_bg_cache[chosen.name]

    def _active_bg_img(self):
        """Background for the active channel — its per-channel override, else the global default; or a
        stable random one if 'random background' is on and nothing is set for this channel."""
        bg = self._chan_cfg().get("bg")
        if not bg and config.pref("comic_random_bg") and self._comic_backgrounds:
            import hashlib
            key = (self._chan_key(self._active_net, self._active_buf)
                   if self._active_net and self._active_buf else "")
            idx = int(hashlib.md5(key.encode()).hexdigest(), 16) % len(self._comic_backgrounds)
            bg = self._comic_backgrounds[idx].name
        return self._bg_by_name(bg or self._comic_bg_name)

    @property
    def _comic_ready(self) -> bool:
        return self._comic_bg_img is not None or bool(self._comic_characters)

    def _char_for_nick(self, nick: str):
        if not self._comic_characters:
            return None
        bare = self._strip(nick)
        low = bare.lower()
        # 1) explicit assignment — this channel first, then the global table
        assigned = self._chan_cfg().get("chars", {}).get(low) or self._comic_chars_global.get(low)
        if assigned:
            path = self._char_path_by_stem(assigned)
            if path is not None:
                return load_character(path)
        # 2) your own nick → your chosen character
        own = (self._active_net is not None and low == (self._active_net.client.nick or "").lower())
        if own and self._comic_self_char:
            path = self._char_path_by_stem(self._comic_self_char)
            if path is not None:
                return load_character(path)
        # 3) auto-assign an UNUSED character — never yours or a manual pick — and save it to the channel,
        #    so the same person always looks the same and nobody shares a look until the pool runs out
        nid = self._active_net.id if self._active_net is not None else 0  # cache is PER-network
        if (nid, bare) in self._nick_char:
            return load_character(self._nick_char[(nid, bare)])
        key = (self._chan_key(self._active_net, self._active_buf)
               if (self._active_net is not None and (self._active_buf or "").startswith(("#", "&")))
               else None)
        used = {str(self._comic_self_char).lower()} if self._comic_self_char else set()
        used |= {str(v).lower() for v in self._comic_chars_global.values()}     # your global manual picks
        used |= {p.stem.lower() for (n, _b), p in self._nick_char.items() if n == nid}  # this net's so far
        if key:
            used |= {str(v).lower()                                            # this channel's assignments
                     for v in (self._comic_channels.get(key, {}) or {}).get("chars", {}).values()}
        pool = [p for p in self._comic_characters if p.stem.lower() not in used] or self._comic_characters
        h = 0
        for c in (low or nick):
            h = (h * 31 + ord(c)) & 0xFFFFFFFF
        path = pool[h % len(pool)]
        self._nick_char[(nid, bare)] = path
        if key:
            self._save_auto_char(key, low, path.stem)
        return load_character(path)

    def _save_auto_char(self, key: str, low: str, stem: str) -> None:
        """Persist an auto-assigned character to the channel + mirror it in memory (no reload needed)."""
        chans = dict(config.pref("comic_channels") or {})
        entry = dict(chans.get(key, {}) or {})
        charmap = dict(entry.get("chars") or {})
        charmap[low] = stem
        entry["chars"] = charmap
        chans[key] = entry
        config.set_pref("comic_channels", chans)
        self._comic_channels.setdefault(key, {}).setdefault("chars", {})[low] = stem

    @staticmethod
    def _emotion_for(text: str) -> str:
        low = text.lower()
        if ":d" in low or ":-d" in low or "lol" in low or "rotfl" in low or "haha" in low:
            return "laughing"
        if any(s in text for s in (":)", ":-)", "(:", "=)")):
            return "happy"
        if any(s in text for s in (":(", ":-(", "):", "=(")):
            return "sad"
        if ";)" in text or ";-)" in text:
            return "coy"
        if "?!" in text or "!?" in text:
            return "scared"
        letters = [c for c in text if c.isalpha()]
        if len(letters) >= 3 and all(c.isupper() for c in letters):
            return "shouting"
        return "neutral"

    def _on_emotion_pick(self, emo: str) -> None:
        self._my_emotion = emo  # sticky until you change it; "auto" returns to guessing from text

    def _self_emotion(self, nick: str):
        """Your chosen emotion for YOUR own messages (None → fall back to guessing from text)."""
        net = self._active_net
        if (net is not None and self._my_emotion != "auto"
                and self._strip(nick).lower() == (net.client.nick or "").lower()):
            return self._my_emotion
        return None

    def _update_self_view(self) -> None:
        """Point the emotion wheel's faces + preview at your own character (a live self-view)."""
        if getattr(self, "_emotion_wheel", None) is None:
            return
        net = self._active_net
        ch = (self._char_for_nick(net.client.nick or "you")
              if (net is not None and self._comic_characters) else None)
        self._emotion_wheel.set_character(ch)

    def _toggle_emotion_wheel(self) -> None:
        if self._emotion_wheel is None:
            self._emotion_wheel = EmotionWheel(self)
            self._emotion_wheel.emotionChanged.connect(self._on_emotion_pick)
        wheel = self._emotion_wheel
        if wheel.isVisible():
            wheel.hide()
            return
        self._update_self_view()  # refresh the avatar/faces to your current character
        wheel.show()
        c = self.frameGeometry().center()  # centre the picker over the window (no toolbar button now)
        wheel.move(c.x() - wheel.width() // 2, c.y() - wheel.height() // 2)

    def _comic_cur(self):
        """Per-buffer comic state for the active channel (None for server/no buffer).

        ``strip`` = rendered panel pixmaps; ``panels`` = the source line-tuples for each panel (kept in
        lockstep with ``strip``) so a character/background change can re-draw the visible panels.
        """
        if self._active_net is None or not self._active_buf or self._active_buf == SERVER_BUFFER:
            return None
        return self._comic.setdefault(
            (self._active_net.id, self._active_buf), {"strip": [], "panels": []}
        )

    @staticmethod
    def _comic_speech(text: str, action: bool):
        """The balloon text for a message, or ``None`` if it isn't human speech worth drawing.

        Any message containing a URL is skipped entirely — link / image / media / X posts show as
        embeds in the chat pane, never as comic panels (the comic shows talk, not links).
        Returns ``(body, think)`` — ``think`` marks a (parenthesised) thought balloon."""
        from maxchat.ui.chat_view import URL_RE
        plain = " ".join(strip_formatting(text).split())
        if not plain or URL_RE.search(plain):
            return None
        think = (not action) and plain.startswith("(") and plain.endswith(")") and len(plain) > 2
        return (plain[1:-1].strip() if think else plain), think

    def _push_comic_panel(self, state: dict, nick: str, text: str, action: bool = False,
                          *, render: bool = True) -> None:
        """Append a message to a buffer's comic state. Lines always accumulate (cheap), so a buffer
        composed in the background is ready the moment you open comic mode; pixmaps are (re)built only
        when ``render`` (the buffer is the one on screen) — see `_maybe_comic`."""
        speech = self._comic_speech(text, action)
        if speech is None:
            return  # bare link / file drop — not comic speech
        body, think = speech
        emo = self._self_emotion(nick) or self._emotion_for(body)  # your picked emotion beats the guess
        line = (self._strip(nick), emo, body, think, action)
        panels = state["panels"]
        cur = panels[-1] if panels else None
        # extend the in-progress panel only if it's under the bubble cap AND (for the visible buffer) the
        # next line still fits at a readable size — otherwise spill into a fresh panel
        extend = cur is not None and len(cur) < self._comic_per_panel
        if extend and render:
            extend = self._panel_accepts(cur, line)
        if extend:
            panels[-1] = cur + [line]
        else:
            panels.append([line])
        del panels[: max(0, len(panels) - MAX_COMIC_PANELS)]  # keep up to 6 panels
        state["dirty"] = True  # pixmaps now stale vs lines
        if render and self._comic_ready:
            self._rebuild_strip(state)

    def _rebuild_strip(self, state: dict) -> None:
        """(Re)draw a buffer's cached panels from their source lines, clearing the dirty flag."""
        state["strip"] = [pm for pm in (self._render_comic_lines(p) for p in state["panels"]) if pm]
        state["dirty"] = False

    def _rerender_channel(self) -> None:
        """Re-draw the active channel's cached panels from their source lines — used after a comic
        settings change (new character/background) so the visible strip updates, not just new panels."""
        state = self._comic_cur()
        if state is None:
            return
        self._rebuild_strip(state)
        self._render_strip()

    @staticmethod
    def _pose_for(ch, nick: str, text: str) -> int:
        """Pick a body gesture/stance for this figure — deterministic by (nick, text) so it's varied
        but stable when the panel re-renders. Was always pose 0 (one static stance)."""
        n = len(ch.bodies)
        if n <= 1:
            return 0
        h = 0
        for c in (nick.lower() + "|" + (text or "")):
            h = (h * 31 + ord(c)) & 0xFFFFFFFF
        return h % n

    def _panel_actors(self, lines):
        """(actors, rlines) for a panel's line-tuples — distinct speakers in speaking order."""
        order, emo_by, text_by = [], {}, {}
        for nick, emo, t, _th, _a in lines:
            if nick not in emo_by:
                order.append(nick)
            emo_by[nick] = emo   # a character's figure uses their latest emotion in the panel
            text_by[nick] = t    # …and a gesture/pose derived from their latest line
        actors, idx_of = [], {}
        for nick in order:
            ch = self._char_for_nick(nick)
            if ch is None:
                continue
            idx_of[nick] = len(actors)
            actors.append((ch, emo_by[nick], nick, self._pose_for(ch, nick, text_by[nick])))
        rlines = [(idx_of[n], t, th, a) for (n, _e, t, th, a) in lines if n in idx_of]
        return actors, rlines

    def _render_comic_lines(self, lines):
        actors, rlines = self._panel_actors(lines)
        if not actors:
            return None
        cap = bool(config.pref("comic_captions"))
        colors = None
        if cap:
            if (config.pref("comic_caption_mode") or "nick") == "fixed":
                fixed = str(config.pref("comic_caption_color") or "#363636")
                colors = {a[2].lower(): fixed for a in actors}
            else:  # per-speaker colour — honours per-user overrides, else a stable hashed colour
                colors = {a[2].lower(): self._caption_color(a[2]) for a in actors}
        return render_panel(BG_SIZE, self._active_bg_img(), actors, rlines, captions=cap,
                            caption_scale=float(config.pref("comic_caption_scale") or 1.0),
                            caption_colors=colors)

    def _caption_color(self, nick: str) -> str:
        ov = (config.pref("nick_colors") or {}).get(self._strip(nick).lower())
        return ov or nick_color(self._strip(nick))  # per-user override, else a stable hashed colour

    def _panel_accepts(self, panel_lines, new_line) -> bool:
        """Would (panel + the new line) still render at or above the readable floor? Used to break to a
        fresh panel before the lettering gets too small (the count cap is checked separately)."""
        actors, rlines = self._panel_actors(panel_lines + [new_line])
        if not actors:
            return True
        floor = max(6, min(13, int(config.pref("comic_min_font") or 9)))
        return panel_min_font(BG_SIZE, actors, rlines) >= floor

    def _blank_panel(self):
        """Background-only panel pixmap (placeholder for empty slots) — cached per background."""
        img = self._active_bg_img()
        if img is None:
            return None
        key = self._chan_cfg().get("bg") or self._comic_bg_name or "_default"
        blank = self._comic_blank_cache.get(key)
        if blank is None:
            blank = render_panel(BG_SIZE, img, [], [])
            self._comic_blank_cache[key] = blank
        return blank

    def _render_strip(self) -> None:
        if not getattr(self, "_comic_panel_widgets", None):
            return
        state = self._comic_cur()
        if state is not None and state.get("dirty") and self._comic_ready:
            self._rebuild_strip(state)  # this buffer composed in the background — draw it now
        n = len(self._comic_panel_widgets)
        strip = (state["strip"] if state else [])[-n:]
        blank = self._blank_panel()
        for i, w in enumerate(self._comic_panel_widgets):
            pm = strip[i] if i < len(strip) else blank  # fill empty slots with the background
            if pm is not None and not pm.isNull():
                w.setText("")
                cw, ch = w.width(), w.height()
                w.setPixmap(pm.scaled(cw - 4, ch - 4, Qt.AspectRatioMode.KeepAspectRatio,
                                      Qt.TransformationMode.SmoothTransformation)
                            if (cw > 8 and ch > 8) else pm)
                w.setEnabled(True)
            else:
                w.setPixmap(QPixmap())
                w.setText(f"Panel {i + 1}")
                w.setEnabled(False)

    def _maybe_comic(self, net: Network, name: str, nick: str, text: str, action: bool = False) -> None:
        """Feed a human message to the comic. Panels accumulate for EVERY channel in the background —
        cheap line-tuples, no pixmaps — so opening comic mode shows what's been going on. Pixmaps are
        drawn only for the buffer currently on screen (``visible``); others render lazily on display."""
        if net is None or name == SERVER_BUFFER:
            return
        low = self._strip(nick).lower()
        if low in self._comic_ignore:  # bots / URL-info nicks (everywhere): chat only, no comic
            return
        if low in self._chan_comic_ignore(net, name):  # nicks hidden from comic in THIS channel only
            return
        if self._comic_text_filtered(text):  # bot commands (!cmd, .mlb) / custom regex: chat only, no comic
            return
        state = self._comic.setdefault((net.id, name), {"strip": [], "panels": []})
        visible = (self.act_comic.isChecked() and net is self._active_net and name == self._active_buf)
        self._push_comic_panel(state, nick, text, action, render=visible)
        if visible:
            self._render_strip()

    def _about(self) -> None:
        app = QApplication.instance()
        ui_font = app.font() if app is not None else QFont()
        chat_font = self._chat_appearance()[2]
        jb = "yes" if fonts.mono_loaded() else "NO — using a system font"
        dlg = AboutDialog(
            self,
            ui_font=f"{ui_font.family()} {ui_font.pointSize()}",
            chat_font=f"{chat_font.family()} {chat_font.pointSize()}",
            bundled_mono=jb,
        )
        dlg.exec()

    # ---- find in buffer (Ctrl+F) -----------------------------------------
    def _build_find_bar(self) -> QWidget:
        bar = QWidget()
        h = QHBoxLayout(bar)
        h.setContentsMargins(6, 2, 6, 2)
        self._find_edit = QLineEdit()
        self._find_edit.setPlaceholderText("Find in this conversation…")
        self._find_edit.returnPressed.connect(lambda: self._find_next(False))
        self._find_edit.textChanged.connect(lambda _t: self._find_next(False, from_start=True))
        prev = QToolButton(); prev.setText("▲"); prev.setToolTip("Previous match")
        prev.clicked.connect(lambda: self._find_next(True))
        nxt = QToolButton(); nxt.setText("▼"); nxt.setToolTip("Next match")
        nxt.clicked.connect(lambda: self._find_next(False))
        self._find_status = QLabel("")
        close = QToolButton(); close.setText("✕"); close.setToolTip("Close (Esc)")
        close.clicked.connect(self._hide_find)
        esc = QShortcut(QKeySequence(Qt.Key.Key_Escape), self._find_edit)
        esc.setContext(Qt.ShortcutContext.WidgetShortcut)
        esc.activated.connect(self._hide_find)
        h.addWidget(QLabel("Find:"))
        h.addWidget(self._find_edit, 1)
        h.addWidget(prev)
        h.addWidget(nxt)
        h.addWidget(self._find_status)
        h.addWidget(close)
        bar.setVisible(False)
        return bar

    def _show_find(self) -> None:
        self._find_bar.setVisible(True)
        self._find_edit.setFocus()
        self._find_edit.selectAll()

    def _hide_find(self) -> None:
        self._find_bar.setVisible(False)
        self.input.setFocus()

    def _find_next(self, backward: bool = False, from_start: bool = False) -> None:
        from PySide6.QtGui import QTextDocument
        view = self.stack.currentWidget()
        text = self._find_edit.text()
        if not isinstance(view, ChatView) or not text:
            self._find_status.setText("")
            return
        if from_start:  # incremental: each keystroke searches from the top
            c = view.textCursor()
            c.movePosition(QTextCursor.MoveOperation.Start)
            view.setTextCursor(c)

        def _do():
            return (view.find(text, QTextDocument.FindFlag.FindBackward) if backward
                    else view.find(text))

        found = _do()
        if not found:  # wrap around
            c = view.textCursor()
            c.movePosition(QTextCursor.MoveOperation.End if backward else QTextCursor.MoveOperation.Start)
            view.setTextCursor(c)
            found = _do()
        self._find_status.setText("" if found else "no matches")

    # ---- sending ----------------------------------------------------------
    def _send(self) -> None:
        text = self.input.text().strip()
        if not text:
            return
        self.input.remember(text)
        if text.startswith("/"):
            self.input.clear()
            self._command(text[1:])
            return
        net = self._active_net
        if net is None or self._active_buf is None:
            return
        if self._active_buf.startswith("="):  # a DCC chat buffer → send over the direct socket
            chat = self._dcc_chats.get((net.id, self._active_buf))
            if chat is not None and chat.status == "active":
                chat.send(text)
                self._append_msg(net, self._active_buf, net.client.nick, text)
                self.input.clear()
            else:
                self._append(net, self._active_buf, "* DCC chat is not connected.")
        elif self._active_buf != SERVER_BUFFER:
            if self._is_channel_name(self._active_buf) and not self._is_joined_channel(net, self._active_buf):
                self._append(net, self._active_buf, "! You are not joined to this channel; message not sent.")
                self.input.clear()
                return
            if net.client.privmsg(self._active_buf, text):
                self._append_msg(net, self._active_buf, net.client.nick, text)
                self.input.clear()
            else:
                self._append_send_failure(net, self._active_buf)
        else:
            self._append(net, SERVER_BUFFER, "! Join a channel first: /join #channel (or Server ▸ Join Channel…)")

    def _expand_alias(self, template: str, rest: str, net: Network, active: str) -> str:
        """Substitute an alias template: $1 $2 (args), $1- (rest from N), $* (all), $me, $chan."""
        args = rest.split()
        nick = net.client.nick or ""
        chan = active if active.startswith(("#", "&")) else ""

        def sub(m):
            tok = m.group(0)
            if tok == "$me":
                return nick
            if tok == "$chan":
                return chan
            if tok in ("$*", "$1-"):
                return " ".join(args)
            try:
                if tok.endswith("-"):
                    return " ".join(args[int(tok[1:-1]) - 1:])
                return args[int(tok[1:]) - 1]
            except (ValueError, IndexError):
                return ""

        return re.sub(r"\$(?:me|chan|\*|\d+-?)", sub, template).strip()

    def _command(self, body: str, _depth: int = 0, *, net: "Network | None" = None,
                 active: str | None = None) -> None:
        """Dispatch a /command. Channel commands default to the CURRENT channel. ``net``/``active`` let a
        caller target a SPECIFIC network/buffer (perform-on-connect uses this so it never has to hijack
        the global active network — which used to misroute other networks' events)."""
        if net is None:
            net = self._active_net
        if net is None:
            return
        cmd, _, rest = body.partition(" ")
        cmd = cmd.lower()
        rest = rest.strip()
        if active is None:
            active = self._active_buf or SERVER_BUFFER
        active_is_channel = MainWindow._is_channel_name(active)

        def joined_channel(name: str | None) -> bool:
            if not MainWindow._is_channel_name(name):
                return False
            joined = getattr(net, "joined_channels", None)
            if joined is None:
                return True  # test fakes and older callers without membership state
            return MainWindow._channel_key(name) in joined

        in_channel = joined_channel(active)
        c = net.client

        if _depth < 5 and cmd in self._aliases:  # user alias → expand and run the result
            expanded = self._expand_alias(self._aliases[cmd], rest, net, active)
            if expanded:
                self._command(expanded, _depth + 1, net=net, active=active)
            return
        if self._scripts.dispatch("on_command", cmd, rest, net=net):
            return  # a script's on_command consumed this command

        if cmd in ("join", "j") and rest:
            parts = rest.split()
            ch = parts[0] if parts[0].startswith(("#", "&")) else "#" + parts[0]
            if len(parts) > 1:  # /join #chan key
                c.send_raw(f"JOIN {ch} {parts[1]}")
            else:
                c.join(ch)
        elif cmd in ("part", "leave"):  # /part [#chan] [reason] — current channel by default
            target, reason = active, rest
            if rest.startswith(("#", "&")):
                target, _, reason = rest.partition(" ")
            if not MainWindow._is_channel_name(target):
                self._append(net, active, "! /part only works in a channel")
                return
            if not joined_channel(target):
                self._append(net, active, f"! You are not joined to {target}")
                return
            c.send_raw(f"PART {target} :{reason.strip()}" if reason.strip() else f"PART {target}")
            if hasattr(net, "joined_channels"):
                net.joined_channels.discard(MainWindow._channel_key(target))
            if hasattr(self, "_cancel_paste_timers"):
                self._cancel_paste_timers(net.id, target)
            if hasattr(self, "_set_connected_ui"):
                self._set_connected_ui()
        elif cmd in ("cycle", "hop") and in_channel:  # part + rejoin the current channel
            c.send_raw(f"PART {active}")
            c.join(active)
        elif cmd == "topic":  # /topic [#chan] [text] — current channel; sets it (no text = show it)
            target, text = active, rest
            if rest.startswith(("#", "&")):
                target, _, text = rest.partition(" ")
            if not MainWindow._is_channel_name(target):
                self._append(net, active, "! /topic only works in a channel")
                return
            if not joined_channel(target):
                self._append(net, active, f"! You are not joined to {target}")
                return
            c.send_raw(f"TOPIC {target} :{text.strip()}" if text.strip() else f"TOPIC {target}")
        elif cmd == "kick" and in_channel and rest:  # /kick <nick> [reason]
            nick, _, reason = rest.partition(" ")
            c.send_raw(f"KICK {active} {nick} :{reason.strip() or nick}")
        elif cmd in ("op", "deop", "voice", "devoice", "halfop", "dehalfop") and in_channel and rest:
            flag = {"op": "+o", "deop": "-o", "voice": "+v", "devoice": "-v",
                    "halfop": "+h", "dehalfop": "-h"}[cmd]
            for nick in rest.split():
                c.send_raw(f"MODE {active} {flag} {nick}")
        elif cmd in ("ban", "kickban", "kb") and in_channel and rest:
            nick, _, reason = rest.partition(" ")
            mask = nick if any(ch in nick for ch in "!@*") else f"{nick}!*@*"
            c.send_raw(f"MODE {active} +b {mask}")
            if cmd in ("kickban", "kb"):
                c.send_raw(f"KICK {active} {nick} :{reason.strip() or nick}")
        elif cmd == "mode" and rest:  # /mode [#chan] <modes> — current channel if no target given
            arg = rest if (rest.startswith(("#", "&")) or not active_is_channel) else f"{active} {rest}"
            c.send_raw(f"MODE {arg}")
        elif cmd == "whois" and rest:
            self._do_whois(net, rest.split()[0])
        elif cmd == "whowas" and rest:
            c.send_raw(f"WHOWAS {rest.split()[0]}")
        elif cmd == "who" and rest:
            c.send_raw(f"WHO {rest.split()[0]}")
        elif cmd == "query" and rest:  # open a PM tab (and optionally send)
            nick, _, msg = rest.partition(" ")
            self._open_query(net, nick)
            if msg.strip():
                if c.privmsg(nick, msg):
                    self._append_msg(net, nick, c.nick, msg)
                else:
                    self._append_send_failure(net, nick)
        elif cmd in ("msg", "privmsg") and " " in rest:
            target, _, msg = rest.partition(" ")
            if target not in net.buffers:
                self._ensure_buffer(net, target)
            if c.privmsg(target, msg):
                self._append_msg(net, target, c.nick, msg)
            else:
                self._append_send_failure(net, target)
        elif cmd == "notice" and " " in rest:
            target, _, msg = rest.partition(" ")
            echo_target = self._notice_echo_target(net, target)
            if c.send_raw(f"NOTICE {target} :{msg}"):
                self._append(net, echo_target, f"-> -{target}- {msg}")
            else:
                self._append_send_failure(net, echo_target)
        elif cmd == "amsg" and rest:  # message every channel you're in (on this network)
            joined = {cname.lower() for cname in getattr(net, "joined_channels", set(net.buffers))}
            for chan in [b for b in net.buffers if MainWindow._is_channel_name(b) and b.lower() in joined]:
                if c.privmsg(chan, rest):
                    self._append_msg(net, chan, c.nick, rest)
                else:
                    self._append_send_failure(net, chan)
        elif cmd == "ame" and rest:  # /me to every channel you're in
            joined = {cname.lower() for cname in getattr(net, "joined_channels", set(net.buffers))}
            for chan in [b for b in net.buffers if MainWindow._is_channel_name(b) and b.lower() in joined]:
                if c.action(chan, rest):
                    self._append_action(net, chan, c.nick, rest)
                else:
                    self._append_send_failure(net, chan)
        elif cmd == "onotice" and in_channel and rest:  # NOTICE to the channel's ops (@#chan)
            if c.send_raw(f"NOTICE @{active} :{rest}"):
                self._append(net, active, f"-> -@{active}- {rest}")
            else:
                self._append_send_failure(net, active)
        elif cmd in ("me", "action"):
            if active != SERVER_BUFFER and rest and (not MainWindow._is_channel_name(active) or in_channel):
                if c.action(active, rest):
                    self._append_action(net, active, c.nick, rest)
                else:
                    self._append_send_failure(net, active)
            else:
                self._append(net, active, "! /me needs a channel or query and some text")
        elif cmd == "sound":  # CTCP SOUND: "/sound <file.wav> [text]" — play locally + send to the room
            if active != SERVER_BUFFER and (not MainWindow._is_channel_name(active) or in_channel):
                fname, _, snd_text = rest.partition(" ")
                fname = fname.strip()
                if not fname:
                    self._append(net, active, "Usage: /sound <file.wav> [text]")
                elif c.ctcp(active, "SOUND", f"{fname} {snd_text}".strip()):
                    self._show_sound(net, active, c.nick, fname, snd_text.strip(), play=True)
                else:
                    self._append_send_failure(net, active)
            else:
                self._append(net, active, "! /sound needs a channel or query")
        elif cmd in ("ns", "nickserv", "cs", "chanserv", "ms", "memoserv", "identify", "id", "ghost"):
            svc = {"cs": "ChanServ", "chanserv": "ChanServ",
                   "ms": "MemoServ", "memoserv": "MemoServ"}.get(cmd, "NickServ")
            payload = f"IDENTIFY {rest}" if cmd in ("identify", "id") else \
                      f"GHOST {rest}" if cmd == "ghost" else rest
            if not payload.strip() or (cmd in ("identify", "id", "ghost") and not rest.strip()):
                self._append(net, active, f"Usage: /{cmd} …")
            elif c.privmsg(svc, payload):
                shown = payload
                if cmd in ("identify", "id"):
                    shown = "IDENTIFY ******"  # never echo the password into the buffer / logs
                elif cmd == "ghost":
                    t = rest.split()
                    shown = f"GHOST {t[0]} ******" if t else "GHOST ******"
                self._append(net, active, f"-> {svc}: {shown}")
            else:
                self._append_send_failure(net, active)
        elif cmd == "ctcp" and " " in rest:
            target, _, ctype = rest.partition(" ")
            self._send_ctcp(net, target, ctype.strip())
        elif cmd == "dcc":
            sub, _, dr = rest.partition(" ")
            sub, dr = sub.lower(), dr.strip()
            if sub == "chat" and dr:
                self._start_dcc_chat(net, dr.split()[0])
            elif sub == "send" and dr:
                who, _, path = dr.partition(" ")
                if path.strip():
                    self._pending_dcc_net = net
                    try:
                        self._dcc.offer(
                            self._strip(who), path.strip(),
                            lambda n, b, nt=net: self._dcc_ctcp(nt, n, b),
                            advertised_ip=self._dcc_ip_for(net),
                        )
                    finally:
                        if self._pending_dcc_net is net:
                            self._pending_dcc_net = None
                    self._open_dcc()
                else:
                    self._send_file(net, who)
            elif sub in ("close", "end") and active.startswith("="):
                self._close_buffer(net, active)
            else:
                self._open_dcc()  # /dcc (or /dcc list) → the transfers window
        elif cmd == "ignore":
            self._add_ignore(rest.strip()) if rest.strip() else self._open_ignore_list()
        elif cmd == "unignore" and rest:
            self._remove_ignore(rest.strip())
        elif cmd == "alias":
            name, _, tmpl = rest.partition(" ")
            if name and tmpl.strip():
                self._set_aliases({**self._aliases, name.lstrip("/").lower(): tmpl.strip()})
                self._append(net, active, f"* Alias /{name.lstrip('/').lower()} set")
            else:
                self._open_aliases()
        elif cmd == "unalias" and rest:
            name = rest.split()[0].lstrip("/").lower()
            if name in self._aliases:
                self._set_aliases({k: v for k, v in self._aliases.items() if k != name})
                self._append(net, active, f"* Alias /{name} removed")
        elif cmd == "scripts":
            self._append(net, active, "* Scripts loaded: "
                         + (", ".join(sorted(self._scripts.scripts)) or "(none)") + "  —  Settings ▸ Scripts…")
        elif cmd == "load" and rest:
            nm = rest.strip()
            path = nm if os.path.isabs(nm) else os.path.join(
                self._scripts.dir(), nm if nm.endswith(".py") else nm + ".py")
            self._append(net, active, f"* Loaded {nm}" if self._scripts.load(path) else f"! Could not load {nm}")
        elif cmd == "unload" and rest:
            nm = rest.split()[0].removesuffix(".py")
            self._append(net, active, f"* Unloaded {nm}" if self._scripts.unload(nm) else f"! {nm} not loaded")
        elif cmd == "reload":
            if rest:
                nm = rest.split()[0].removesuffix(".py")
                self._append(net, active, f"* Reloaded {nm}" if self._scripts.reload(nm) else f"! {nm} not found")
            else:
                self._scripts.load_all()  # reload the whole folder (picks up new + previously-unloaded)
                self._append(net, active, "* Reloaded all scripts")
        elif cmd == "names" and (rest or in_channel):
            target = rest or active
            names = net.names.get(target)
            if names:  # show who we already know is here, then refresh from the server
                self._append(net, active,
                             f"* Users on {target} ({len(names)}): " + " ".join(self._sorted_names(names)))
            if MainWindow._is_channel_name(target) and hasattr(net, "pending_names"):
                net.pending_names[MainWindow._channel_key(target)] = []
            c.send_raw(f"NAMES {target}")
        elif cmd == "invite" and rest:
            parts = rest.split()
            c.send_raw(f"INVITE {parts[0]} {parts[1] if len(parts) > 1 else active}")
        elif cmd == "away":
            c.send_raw(("AWAY :" + rest) if rest else "AWAY")
        elif cmd == "back":
            c.send_raw("AWAY")
        elif cmd == "nick" and rest:
            c.send_raw(f"NICK {rest.split()[0]}")
        elif cmd == "clear":  # clear the current chat
            view = net.buffers.get(active)
            if view is not None:
                view.clear()
        elif cmd == "clearall":  # clear every buffer's scrollback
            for v in self._all_views():
                v.clear()
            self._note("* All chats cleared.")
        elif cmd == "wallops":
            c.send_raw(f"WALLOPS :{rest}") if rest else self._append(net, active, "Usage: /wallops <message>")
        elif cmd == "oper":
            c.send_raw(f"OPER {rest}") if rest else self._append(net, active, "Usage: /oper <user> <password>")
        elif cmd == "kill":
            tok = rest.split(None, 1)
            if tok:
                c.send_raw(f"KILL {tok[0]} :{tok[1] if len(tok) > 1 else 'killed'}")
            else:
                self._append(net, active, "Usage: /kill <nick> [reason]")
        elif cmd == "lag":
            self._append(net, active, "* Measuring lag…" if (net.connected and c.measure_lag())
                         else "! Not connected.")
        elif cmd == "uptime":
            up = self._fmt_duration(time.monotonic() - self._started)
            conn = (f" · connected to {net.name} for "
                    f"{self._fmt_duration(time.monotonic() - net.connected_at)}"
                    if getattr(net, "connected_at", None) and net.connected else "")
            self._append(net, active, f"* MaxChat uptime {up}{conn}")
        elif cmd == "netinfo":
            self._append(net, active, "* " + self._netinfo_str(net))
        elif cmd in ("sysinfo", "sys"):
            info = self._sysinfo_str()
            if rest.strip().lower() in ("send", "share") and active != SERVER_BUFFER \
                    and (not MainWindow._is_channel_name(active) or in_channel) and c.privmsg(active, info):
                self._append_msg(net, active, c.nick, info)  # explicitly shared to the channel
            else:
                self._append(net, active, "* " + info)  # local only by default
        elif cmd in ("mute", "unmute"):
            if active != SERVER_BUFFER:
                self._toggle_mute(net, active)
            else:
                self._append(net, active, "! Mute applies to a channel or query.")
        elif cmd in ("shrug", "tableflip", "flip", "unflip", "lenny", "disapprove"):
            deco = {"shrug": "¯\\_(ツ)_/¯", "tableflip": "(╯°□°)╯︵ ┻━┻", "flip": "(╯°□°)╯︵ ┻━┻",
                    "unflip": "┬─┬ ノ( ゜-゜ノ)", "lenny": "( ͡° ͜ʖ ͡°)", "disapprove": "ಠ_ಠ"}[cmd]
            text = (rest + " " + deco) if rest else deco
            if active != SERVER_BUFFER and (not MainWindow._is_channel_name(active) or in_channel):
                if c.privmsg(active, text):
                    self._append_msg(net, active, c.nick, text)
                else:
                    self._append_send_failure(net, active)
            else:
                self._append(net, active, "! Use that in a channel or query")
                net.unread_marked.discard(active)
        elif cmd == "quit":
            self._disconnect_net(net)
        elif cmd in ("raw", "quote") and rest:
            c.send_raw(rest)
        else:
            c.send_raw(body)

    def _restore_geometry(self) -> None:
        """Explicit size/position restore (more reliable than a restoreGeometry blob, esp. on Wayland);
        values are plain ints in settings, so they travel with config export/import too."""
        from PySide6.QtGui import QGuiApplication
        scr = QGuiApplication.primaryScreen()
        avail = scr.availableGeometry() if scr is not None else None
        size = config.get_setting("win_size")
        if isinstance(size, (list, tuple)) and len(size) == 2:
            try:
                w, h = int(size[0]), int(size[1])
                if avail is not None:
                    w, h = max(400, min(w, avail.width())), max(300, min(h, avail.height()))
                self.resize(w, h)
            except (TypeError, ValueError):
                pass
        pos = config.get_setting("win_pos")
        if isinstance(pos, (list, tuple)) and len(pos) == 2:
            try:
                x, y = int(pos[0]), int(pos[1])
                if avail is not None:  # keep it on-screen
                    x = max(avail.left(), min(x, avail.right() - 120))
                    y = max(avail.top(), min(y, avail.bottom() - 120))
                self.move(x, y)
            except (TypeError, ValueError):
                pass
        if config.get_setting("win_max"):
            self.setWindowState(self.windowState() | Qt.WindowState.WindowMaximized)
        st = config.get_setting("win_splitter")
        if st:
            try:
                self._split.restoreState(QByteArray.fromBase64(st.encode()))
            except Exception:
                pass
        self._apply_saved_side_panel_visibility()

    def _save_geometry(self) -> None:
        try:
            config.set_setting("win_max", self.isMaximized())
            g = self.normalGeometry()  # the un-maximised size, even if currently maximised
            config.set_setting("win_size", [g.width(), g.height()])
            config.set_setting("win_pos", [g.x(), g.y()])
            sizes = self._split.sizes()
            config.set_pref("server_list_visible", len(sizes) > 0 and sizes[0] > 0)
            config.set_pref("member_list_visible", len(sizes) > 2 and sizes[2] > 0)
            config.set_setting("win_splitter", bytes(self._split.saveState().toBase64()).decode())
        except Exception:
            pass

    # ---- tray + notifications --------------------------------------------
    def _app_icon(self) -> QIcon:
        """The window/tray icon: a theme-tinted speech bubble, or the chosen emoji (Appearance ▸ Icon)."""
        if self._cached_app_icon is None:
            accent = QColor(theme.ui_color(self._theme, "on"))
            self._cached_app_icon = app_icon.make_icon(config.pref("tray_icon") or "bubble", accent)
        return self._cached_app_icon

    def _apply_app_icon(self) -> None:
        """Re-generate + apply the window/tray icon (after a theme change or an icon-choice change)."""
        self._cached_app_icon = None
        icon = self._app_icon()
        self.setWindowIcon(icon)
        if getattr(self, "_tray", None) is not None:
            self._tray.setIcon(icon)

    def _build_tray(self) -> None:
        """System-tray icon (Show/Hide + Quit). Absent if the desktop has no tray (e.g. plain Wayland)."""
        self._tray = None
        self._tray_menu = None
        self.setWindowIcon(self._app_icon())
        if not QSystemTrayIcon.isSystemTrayAvailable():
            return
        self._tray = QSystemTrayIcon(self._app_icon(), self)
        self._tray.setToolTip(__app_name__)
        self._tray_menu = QMenu(self)
        self._tray_menu.addAction("Show / Hide", self._toggle_window)
        self._tray_menu.addSeparator()
        self._tray_menu.addAction(self.act_dnd)  # reuse the Tools-menu action → both stay in sync
        self._tray_menu.addSeparator()
        self._tray_menu.addAction("Quit", self.close)
        self._tray.setContextMenu(self._tray_menu)
        self._tray.activated.connect(
            lambda r: self._toggle_window() if r == QSystemTrayIcon.ActivationReason.Trigger else None
        )
        self._tray.show()

    def _toggle_window(self) -> None:
        if self.isVisible() and not self.isMinimized():
            self.hide()
        else:
            self.showNormal()
            self.raise_()
            self.activateWindow()

    def _notify(self, title: str, text: str, net: "Network | None" = None, buf: str | None = None) -> None:
        """Alert when the window isn't focused: taskbar flash + a toast (custom in-app or OS-native) +
        an optional sound. ``net``/``buf`` let a toast click jump straight to that conversation."""
        if self.isActiveWindow() or config.pref("dnd"):
            return
        app = QApplication.instance()
        if config.pref("notify_flash") and app is not None:
            app.alert(self, 0)  # flash the taskbar / window until focused
        if config.pref("notify_sound"):
            path = sounds.notify_path()
            if path:
                self._sound.play(path)
        style = str(config.pref("notify_popup") or "custom")
        if style == "off":
            return
        if (style == "system" and getattr(self, "_tray", None) is not None
                and QSystemTrayIcon.supportsMessages()):
            self._tray.showMessage(title, text, self._app_icon(), 5000)
            return
        # custom toast — also the fallback when "system" is chosen but there's no usable tray
        follow = (theme.ui_color(self._theme, "panel"), theme.ui_color(self._theme, "text"),
                  theme.ui_color(self._theme, "on"))
        colors = notifier.palette(str(config.pref("notify_theme") or "follow"), follow)
        corner = str(config.pref("notify_corner") or "br")
        duration = int(config.pref("notify_duration") or 6) * 1000
        if net is not None and buf:
            on_click = lambda n=net, b=buf: self._focus_buffer(n, b)  # noqa: E731
        else:
            on_click = self._raise_self
        self._notifier.show(title, text, colors, corner, duration, self._app_icon(), on_click)

    def _set_dnd(self, on: bool) -> None:
        """Do Not Disturb toggle — shared by the Tools menu, the tray menu, and Preferences."""
        config.set_pref("dnd", bool(on))
        self._refresh_title()  # reflect DnD in the tray tooltip
        self.statusBar().showMessage("Do Not Disturb " + ("on" if on else "off"), 2500)

    def _refresh_title(self) -> None:
        """Window title + tray tooltip: unread/highlight count · active network/buffer · DnD state."""
        n, h = len(self._unread), len(self._unread_hi)
        prefix = f"({h}⚑) " if h else (f"({n}) " if n else "")
        base = f"{__app_name__} {__version__}"
        net, buf = self._active_net, self._active_buf
        if net is not None:
            base += f" — {net.name}"
            if buf and buf != SERVER_BUFFER:
                base += f" / {buf}"
        self.setWindowTitle(prefix + base)
        if getattr(self, "_tray", None) is not None:
            tip = f"{prefix}{__app_name__}"
            if config.pref("dnd"):
                tip += " — Do Not Disturb"
            self._tray.setToolTip(tip)

    def _apply_ctcp_settings(self) -> None:
        """Push the CTCP VERSION reply prefs (hide / custom string) to every connected client."""
        hide, custom = bool(config.pref("hide_version")), str(config.pref("ctcp_version") or "")
        for net in self._networks.values():
            net.client.set_ctcp_version(hide, custom)

    def _check_updates(self, manual: bool = False) -> None:
        """Ask GitHub Releases whether a newer MaxChat exists. Quiet unless newer (or ``manual``)."""
        from PySide6.QtCore import QUrl
        from PySide6.QtNetwork import QNetworkAccessManager, QNetworkRequest
        if getattr(self, "_upd_mgr", None) is None:
            self._upd_mgr = QNetworkAccessManager(self)
        req = QNetworkRequest(QUrl("https://api.github.com/repos/IronWolve/MaxChat/releases/latest"))
        req.setRawHeader(b"User-Agent", b"MaxChat-update-check")
        req.setRawHeader(b"Accept", b"application/vnd.github+json")
        reply = self._upd_mgr.get(req)
        reply.finished.connect(lambda r=reply, m=manual: self._on_update_reply(r, m))

    def _on_update_reply(self, reply, manual: bool) -> None:
        import json
        from PySide6.QtNetwork import QNetworkReply
        ok = reply.error() == QNetworkReply.NetworkError.NoError
        raw = bytes(reply.readAll().data())
        reply.deleteLater()
        latest, url = "", "https://github.com/IronWolve/MaxChat/releases"
        if ok:
            try:
                data = json.loads(raw.decode("utf-8", "replace"))
                latest = str(data.get("tag_name") or "").lstrip("vV")
                url = str(data.get("html_url") or url)
            except Exception:
                ok = False
        if latest and self._version_newer(latest, __version__):
            if shadow_message.open_or_close(
                self, "Update available",
                f"MaxChat v{latest} is available — you have v{__version__}.",
            ):
                from PySide6.QtCore import QUrl
                from PySide6.QtGui import QDesktopServices
                QDesktopServices.openUrl(QUrl(url))
        elif manual:
            msg = (f"You're on the latest version (v{__version__})." if ok
                   else "Couldn't check for updates (no releases yet, or no connection).")
            shadow_message.information(self, "Check for Updates", msg)

    @staticmethod
    def _version_newer(remote: str, local: str) -> bool:
        rp = [int(x) for x in re.findall(r"\d+", remote)] or [0]
        lp = [int(x) for x in re.findall(r"\d+", local)] or [0]
        return rp > lp

    def _zoom_chat(self, delta: int) -> None:
        """Ctrl+= / Ctrl+- nudges the chat font; Ctrl+0 (delta 0) resets to the app default."""
        if delta == 0:
            config.set_pref("chat_font_size", 0)  # 0 = follow the app font size
        else:
            cur = int(config.pref("chat_font_size") or 0) or self._chat_appearance()[2].pointSize()
            config.set_pref("chat_font_size", max(6, min(40, cur + delta)))
        self._font_size = int(config.pref("chat_font_size") or 0)
        self._apply_chat_appearance()

    def _raise_self(self) -> None:
        self.showNormal()
        self.raise_()
        self.activateWindow()

    def _focus_buffer(self, net: "Network", buf: str) -> None:
        self._raise_self()
        if net is not None and buf and buf in net.buffers:
            self._switch_to(net, buf)

    def _test_notification(self, popup: str, corner: str, duration_s: int, ntheme: str, sound: bool) -> None:
        """Fire a sample notification with the given (unsaved) settings — Preferences ▸ Test button.
        Skips the is-active check so it shows even while the Preferences dialog is focused."""
        if sound:
            path = sounds.notify_path()
            if path:
                self._sound.play(path)
        title, text = "Test · MaxChat", "This is what a notification looks like — click to open the chat."
        if (popup == "system" and getattr(self, "_tray", None) is not None
                and QSystemTrayIcon.supportsMessages()):
            self._tray.showMessage(title, text, self._app_icon(), 4000)
            return
        follow = (theme.ui_color(self._theme, "panel"), theme.ui_color(self._theme, "text"),
                  theme.ui_color(self._theme, "on"))
        colors = notifier.palette(str(ntheme or "follow"), follow)
        self._notifier.show(title, text, colors, str(corner or "br"),
                            int(duration_s or 6) * 1000, self._app_icon(), self._raise_self)

    def changeEvent(self, event) -> None:
        if (event.type() == QEvent.Type.WindowStateChange and self._minimize_to_tray
                and getattr(self, "_tray", None) is not None and self.isMinimized()):
            QTimer.singleShot(0, self.hide)  # minimise → hide to the tray
        super().changeEvent(event)

    def closeEvent(self, event) -> None:
        if config.pref("confirm_quit") and any(n.connected for n in self._networks.values()):
            if not shadow_message.question(self, "Quit MaxChat", "Disconnect from all networks and quit?"):
                event.ignore()
                return
        self._save_geometry()
        for net in self._networks.values():
            net.intentional = True
            try:
                net.client.quit()
            except Exception:
                pass
        super().closeEvent(event)
