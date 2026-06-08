"""Preferences dialog — a left-hand navigation list with stacked pages.

Add an option by adding a default in ``config.DEFAULTS``, a widget on the right page here, a save
line in ``_save_and_accept``, and a read in ``MainWindow._load_prefs``.
"""

from __future__ import annotations

import copy
import os
import re

from PySide6.QtCore import QCoreApplication, QSize, Qt, QUrl
from PySide6.QtGui import QColor, QDesktopServices, QFont, QFontDatabase, QFontInfo
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDialogButtonBox,
    QFileDialog,
    QFontComboBox,
    QFormLayout,
    QFrame,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from maxchat import config
from maxchat.config import get_setting, pref, set_pref, set_setting
from maxchat.default_networks import DEFAULT_NETWORKS
from maxchat.network_import import merge_imported_networks
from maxchat.ui import app_icon, fonts, localization, notifier, theme
from maxchat.ui import shadow_message
from maxchat.ui.shadow_dialog import ShadowDialog
from maxchat.ui.tooltips import info_tip


def _dir_size(path: str) -> int:
    total = 0
    try:
        for root, _dirs, files in os.walk(path):
            for f in files:
                try:
                    total += os.path.getsize(os.path.join(root, f))
                except OSError:
                    pass
    except OSError:
        pass
    return total


def _human(n) -> str:
    if n is None:
        return "—"
    n = float(n)
    for unit in ("B", "KB", "MB"):
        if n < 1024:
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} GB"


def _rss_bytes():
    """Resident memory of this process, cross-platform best-effort (None if it can't be read)."""
    try:
        import resource
        import sys
        rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        return rss if sys.platform == "darwin" else rss * 1024  # macOS=bytes, Linux=KiB
    except Exception:
        pass
    try:  # Windows
        import ctypes
        import ctypes.wintypes as wt

        class _PMC(ctypes.Structure):
            _fields_ = [("cb", wt.DWORD), ("PageFaultCount", wt.DWORD),
                        ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                        ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]
        c = _PMC()
        c.cb = ctypes.sizeof(_PMC)
        h = ctypes.windll.kernel32.GetCurrentProcess()
        if ctypes.windll.psapi.GetProcessMemoryInfo(h, ctypes.byref(c), c.cb):
            return c.WorkingSetSize
    except Exception:
        pass
    return None

# Timestamp presets — the stored value is always an strftime string; the combo stays editable so a
# power user can type a custom one. 12-hour is the default (user preference).
TS_PRESETS: list[tuple[str, str]] = [  # the example IS the label — keeps the dropdown compact
    ("3:45 PM", "%I:%M %p"),
    ("3:45:09 PM", "%I:%M:%S %p"),
    ("[3:45 PM]", "[%I:%M %p]"),
    ("15:45", "%H:%M"),
    ("15:45:09", "%H:%M:%S"),
    ("[15:45]", "[%H:%M]"),
    ("[2026-06-06 3:45 PM]", "[%Y-%m-%d %I:%M %p]"),
    ("[2026-06-06 15:45]", "[%Y-%m-%d %H:%M]"),
]


def _prepare_imported_settings(data: dict) -> dict:
    prepared = copy.deepcopy(data)
    if isinstance(prepared.get("networks"), list):
        prepared["networks"] = merge_imported_networks(prepared["networks"], DEFAULT_NETWORKS)
        prepared["networks_merge_version"] = 0
    return prepared


def _spin(value: int, lo: int = 0, hi: int = 48, default_label: str = "default") -> QSpinBox:
    s = QSpinBox()
    s.setRange(lo, hi)
    if lo == 0:
        s.setSpecialValueText(default_label)  # show 0 as "default"
    s.setValue(int(value or lo))
    return s


def _pt(font: QFont) -> int:
    """Resolved point size (QFont reports -1 when only a pixel size is set)."""
    s = font.pointSize()
    return s if s > 0 else max(8, QFontInfo(font).pointSize())


def _size_spin(current: int, lo: int = 6, hi: int = 48) -> QSpinBox:
    s = QSpinBox()
    s.setRange(lo, hi)
    s.setValue(int(current))
    return s


def _group(title: str):
    """A titled section box with its own form layout — returns ``(box, form)``."""
    box = QGroupBox(title)
    form = QFormLayout(box)
    return box, form


class _ColorPick(QWidget):
    """Pick a color (swatch button) with a Default that clears it back to the theme's color."""

    def __init__(self, value: str = "") -> None:
        super().__init__()
        self._value = value or ""
        h = QHBoxLayout(self)
        h.setContentsMargins(0, 0, 0, 0)
        h.setSpacing(4)
        self._btn = QPushButton()
        self._btn.setFixedWidth(120)
        self._btn.clicked.connect(self._pick)
        reset = QPushButton(self.tr("Default"))
        reset.clicked.connect(self._reset)
        h.addWidget(self._btn)
        h.addWidget(reset)
        h.addStretch(1)
        self._refresh()

    def value(self) -> str:
        return self._value

    def _pick(self) -> None:
        from PySide6.QtGui import QColor
        from PySide6.QtWidgets import QColorDialog
        init = QColor(self._value) if self._value else QColor("#dddddd")
        c = QColorDialog.getColor(init, self, self.tr("Choose color"))
        if c.isValid():
            self._value = c.name()
            self._refresh()

    def _reset(self) -> None:
        self._value = ""
        self._refresh()

    def _refresh(self) -> None:
        if self._value:
            self._btn.setText(self._value)
            self._btn.setStyleSheet(f"background:{self._value};color:{_contrast(self._value)};")
        else:
            self._btn.setText(self.tr("Pick"))  # short — the companion "Default" button clears back to the theme
            self._btn.setStyleSheet("")


def _hbox(*widgets) -> QWidget:
    box = QWidget()
    lay = QHBoxLayout(box)
    lay.setContentsMargins(0, 0, 0, 0)
    for w in widgets:
        lay.addWidget(w)
    lay.addStretch(1)
    return box


def _contrast(hex_color: str) -> str:
    """Black or white, whichever reads better on the given #rrggbb."""
    h = hex_color.lstrip("#")
    if len(h) < 6:
        return "#ffffff"
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return "#101010" if (0.299 * r + 0.587 * g + 0.114 * b) > 150 else "#ffffff"


class PreferencesDialog(ShadowDialog):
    def __init__(self, parent=None, initial_tab: str | None = None,
                 app_font: QFont | None = None, chat_font: QFont | None = None,
                 browse_chars=None, open_comic_settings=None, test_notify=None) -> None:
        super().__init__(parent)
        self._browse_chars = browse_chars  # callback that opens the character gallery
        self._open_comic_settings = open_comic_settings  # opens Comic Settings (Global/Characters/Bots…)
        self._test_notify = test_notify  # callback(popup, corner, duration_s, theme, sound) → preview a toast
        self.setWindowTitle(self.tr("Preferences"))
        self.setMinimumWidth(1000)  # autosized to the tallest page at the end of __init__ (no clipping)
        # the live fonts in use, so the pickers pre-fill the *actual* current selection (not a blank)
        inst = QApplication.instance()
        self._app_font0 = app_font or (inst.font() if inst is not None else QFont())
        self._chat_font0 = chat_font or QFont(self._app_font0)

        outer = QVBoxLayout(self.body)
        body = QHBoxLayout()
        body.setSpacing(0)
        outer.addLayout(body, 1)

        self.nav = QListWidget()
        self.nav.setObjectName("prefsNav")
        self.nav.setIconSize(QSize(18, 18))
        self.nav.setStyleSheet(self._nav_style())
        self.stack = QStackedWidget()
        self._pages: dict[str, int] = {}
        body.addWidget(self.nav)
        body.addWidget(self.stack, 1)
        self.nav.currentRowChanged.connect(self.stack.setCurrentIndex)

        # ---- Appearance (grouped into sections; most-used first) ---------
        self.show_ts = QCheckBox()
        self.show_ts.setChecked(bool(pref("show_timestamps")))
        self.ts_format = QComboBox()
        self.ts_format.setEditable(True)
        for lbl, fmt in TS_PRESETS:
            self.ts_format.addItem(lbl, fmt)
        self._set_ts(str(pref("timestamp_format") or "%I:%M %p"))
        self.colored_nicks = QCheckBox()
        self.colored_nicks.setChecked(bool(pref("colored_nicks")))
        self.show_fmt = QCheckBox()
        self.show_fmt.setChecked(bool(pref("show_formatting")))
        self.indent_wrap = QCheckBox()
        self.indent_wrap.setChecked(bool(pref("indent_wrap")))
        self.marker_line = QCheckBox()
        self.marker_line.setChecked(bool(pref("marker_line")))
        self.align_nicks = QCheckBox()
        self.align_nicks.setChecked(bool(pref("align_nicks")))
        self.nick_width = _spin(pref("nick_width") or 10, 4, 24, "")
        self.word_wrap = QCheckBox()
        self.word_wrap.setChecked(bool(pref("word_wrap")))
        self.sort_status = QCheckBox()
        self.sort_status.setChecked(bool(pref("sort_status")))
        self.separator_line = QCheckBox()
        self.separator_line.setChecked(bool(pref("separator_line")))
        self.strip_copy = QCheckBox()
        self.strip_copy.setChecked(bool(pref("strip_color_copy")))
        self.show_input_hint = QCheckBox()
        self.show_input_hint.setChecked(bool(pref("show_input_hint")))
        self.show_button_bar = QCheckBox()
        self.show_button_bar.setChecked(bool(pref("show_button_bar")))
        self.tray_icon = QComboBox()
        _accent = QColor(theme.ui_color(get_setting("theme", "dark"), "on"))  # tint the bubble preview
        for _val, _label in app_icon.CHOICES:
            self.tray_icon.addItem(app_icon.make_icon(_val, _accent), _label, _val)
        self._select(self.tray_icon, pref("tray_icon") or "bubble")

        ts_box, ts_form = _group(self.tr("Timestamps"))
        ts_form.addRow(self.tr("Show timestamps"), self.show_ts)
        ts_form.addRow(self.tr("Clock / format"), self.ts_format)
        nick_box, nick_form = _group(self.tr("Nicknames"))
        nick_form.addRow(self.tr("Color nicknames"), self.colored_nicks)
        nick_form.addRow(self.tr("Align nick column"), self.align_nicks)
        nick_form.addRow(self.tr("Nick column width"), self.nick_width)
        nick_form.addRow(self.tr("Nick separator line"), self.separator_line)
        text_box, text_form = _group(self.tr("Text"))
        text_form.addRow(self.tr("Show colors && formatting"), self.show_fmt)
        text_form.addRow(self.tr("Word wrap"), self.word_wrap)
        text_form.addRow(self.tr("Indent wrapped lines"), self.indent_wrap)
        text_form.addRow(self.tr("Strip colors on copy"), self.strip_copy)
        win_box, win_form = _group(self.tr("Window"))
        win_form.addRow(self.tr("Window / tray icon"), self.tray_icon)
        win_form.addRow(self.tr("Show button bar"), self.show_button_bar)
        win_form.addRow(self.tr("Message-box hint text"), self.show_input_hint)
        win_form.addRow(self.tr("Unread marker line"), self.marker_line)
        win_form.addRow(self.tr("Sort users by status"), self.sort_status)
        # (per-section fonts AND colors live together on the dedicated Fonts tab)

        ap = QWidget()
        ap_outer = QVBoxLayout(ap)
        ap_cols = QHBoxLayout()
        ap_outer.addLayout(ap_cols)
        ap_outer.addStretch(1)
        left_col, right_col = QVBoxLayout(), QVBoxLayout()
        ap_cols.addLayout(left_col)
        ap_cols.addLayout(right_col)
        for b in (ts_box, nick_box):
            left_col.addWidget(b)
        left_col.addStretch(1)
        for b in (text_box, win_box):
            right_col.addWidget(b)
        right_col.addStretch(1)
        self._add_page("appearance", self.tr("Appearance"), ap)

        # ---- Messages ----------------------------------------------------
        ms = QWidget()
        mf = QFormLayout(ms)
        self.hide_jp = QCheckBox()
        self.hide_jp.setChecked(bool(pref("hide_joinpart")))
        self.show_mode = QCheckBox()
        self.show_mode.setChecked(bool(pref("show_mode")))
        self.pm_echo = QCheckBox()
        self.pm_echo.setChecked(bool(pref("pm_echo")))
        self.logging = QCheckBox()
        self.logging.setChecked(bool(pref("logging")))
        self.log_mask = QLineEdit(str(pref("log_mask") or ""))
        self.log_mask.setPlaceholderText(self.tr("%network-%channel  (＋ strftime: %Y %m %d; / = subfolder)"))
        self.replay = QCheckBox()
        self.replay.setChecked(bool(pref("replay_log")))
        self.auto_reconnect = QCheckBox()
        self.auto_reconnect.setChecked(bool(pref("auto_reconnect")))
        self.auto_rejoin = QCheckBox()
        self.auto_rejoin.setChecked(bool(pref("auto_rejoin")))
        self.hide_version = QCheckBox()
        self.hide_version.setChecked(bool(pref("hide_version")))
        self.ctcp_version = QLineEdit(str(pref("ctcp_version") or ""))
        self.ctcp_version.setPlaceholderText(self.tr("custom CTCP VERSION reply (blank = MaxChat <version>)"))
        self.confirm_quit = QCheckBox()
        self.confirm_quit.setChecked(bool(pref("confirm_quit")))
        self.scrollback = QSpinBox()
        self.scrollback.setRange(100, 100000)
        self.scrollback.setSingleStep(100)
        self.scrollback.setValue(int(pref("scrollback") or 2000))
        self.replay_lines = QSpinBox()
        self.replay_lines.setRange(0, 1000)
        self.replay_lines.setSpecialValueText(self.tr("all"))
        self.replay_lines.setValue(int(pref("replay_lines") or 0))
        for label, w in (
            (self.tr("Hide join / part / quit"), self.hide_jp), (self.tr("Show mode changes"), self.show_mode),
            (self.tr("Private msgs in server tab && chat"), self.pm_echo),
            (self.tr("Log conversations to disk"), self.logging),
            (self.tr("Log filename mask"), self.log_mask),
            (self.tr("Replay last log on open"), self.replay),
            (self.tr("Lines to replay"), self.replay_lines),
            (self.tr("Auto-reconnect on disconnect"), self.auto_reconnect),
            (self.tr("Auto-rejoin after kick"), self.auto_rejoin),
            (self.tr("Hide CTCP VERSION replies"), self.hide_version),
            (self.tr("Custom CTCP VERSION"), self.ctcp_version),
            (self.tr("Confirm before quitting"), self.confirm_quit),
            (self.tr("Scrollback (lines)"), self.scrollback),
        ):
            mf.addRow(label, w)
        self._add_page("messages", self.tr("Messages"), ms)

        # ---- Notifications -----------------------------------------------
        nt = QWidget()
        nf = QFormLayout(nt)
        self.notify_popup = QComboBox()
        for label, data in ((self.tr("Off"), "off"), (self.tr("Custom toast (in-app)"), "custom"),
                            (self.tr("System / OS native"), "system")):
            self.notify_popup.addItem(label, data)
        self._select(self.notify_popup, str(pref("notify_popup") or "custom"))
        self.dnd = QCheckBox()
        self.dnd.setChecked(bool(pref("dnd")))
        self.dnd.setToolTip(self.tr("Suppress ALL notifications. Also toggleable from the Tools menu and the tray."))
        self.notify_pm = QCheckBox()
        self.notify_pm.setChecked(bool(pref("notify_pm")))
        self.notify_highlight = QCheckBox()
        self.notify_highlight.setChecked(bool(pref("notify_highlight")))
        self.notify_flash = QCheckBox()
        self.notify_flash.setChecked(bool(pref("notify_flash")))
        self.notify_corner = QComboBox()
        for data, label in notifier.CORNERS.items():
            self.notify_corner.addItem(label, data)
        self._select(self.notify_corner, str(pref("notify_corner") or "br"))
        self.notify_duration = _spin(pref("notify_duration") or 6, 2, 30, "")
        self.notify_theme = QComboBox()
        for data, label in notifier.THEME_LABELS.items():
            self.notify_theme.addItem(label, data)
        self._select(self.notify_theme, str(pref("notify_theme") or "follow"))
        self.notify_sound = QCheckBox()
        self.notify_sound.setChecked(bool(pref("notify_sound")))
        self.notify_sound.setToolTip(info_tip(self.tr(
            "Play a chime when a notification fires — a built-in default, or your own if you drop a "
            "notify.wav in <config>/sounds/."
        )))
        self.minimize_to_tray = QCheckBox()
        self.minimize_to_tray.setChecked(bool(pref("minimize_to_tray")))
        self.highlight_words = QLineEdit(str(pref("highlight_words") or ""))
        self.highlight_words.setPlaceholderText(self.tr("extra words that highlight you (space/comma-separated)"))
        self.beep_highlight = QCheckBox()
        self.beep_highlight.setChecked(bool(pref("beep_highlight")))
        self.ctcp_sound = QCheckBox()
        self.ctcp_sound.setChecked(bool(pref("ctcp_sound")))
        self.ctcp_sound.setToolTip(info_tip(self.tr(
            "Play .wav sounds others send via CTCP SOUND (mIRC/Comic Chat). Drop your own .wav files in "
            "the 'sounds' folder under your config directory; a sound only plays if you have that file."
        )))
        nf.addRow(self.tr("Do Not Disturb"), self.dnd)
        nf.addRow(self.tr("Popup style"), self.notify_popup)
        nf.addRow(self.tr("Notify on PMs"), self.notify_pm)
        nf.addRow(self.tr("Notify on highlights"), self.notify_highlight)
        nf.addRow(self.tr("Highlight words"), self.highlight_words)
        nf.addRow(self.tr("Taskbar flash"), self.notify_flash)
        nf.addRow(self.tr("Toast corner"), self.notify_corner)
        nf.addRow(self.tr("Toast duration (s)"), self.notify_duration)
        nf.addRow(self.tr("Toast theme"), self.notify_theme)
        nf.addRow(self.tr("Beep on highlight / PM"), self.beep_highlight)
        nf.addRow(self.tr("Sound on notification"), self.notify_sound)
        nf.addRow(self.tr("Play CTCP sounds"), self.ctcp_sound)
        nf.addRow(self.tr("Minimize to tray"), self.minimize_to_tray)
        if self._test_notify is not None:
            test_btn = QPushButton(self.tr("Test notification"))
            test_btn.setToolTip(self.tr("Preview a toast with the settings selected above (no need to save first)."))
            test_btn.clicked.connect(self._do_test_notify)
            nf.addRow("", test_btn)
        hint = QLabel(self.tr("Alerts fire only when the window isn't focused (no sounds). Tray features need a "
                              "system tray — most Linux desktops and Windows; not plain Wayland."))
        hint.setWordWrap(True)
        nf.addRow(hint)
        self._add_page("notifications", self.tr("Notifications"), nt, scrollable=True)

        # ---- Protection --------------------------------------------------
        pr = QWidget()
        pf = QFormLayout(pr)
        self.flood_protect = QCheckBox()
        self.flood_protect.setChecked(bool(pref("flood_protect")))
        self.flood_msgs = QSpinBox()
        self.flood_msgs.setRange(2, 50)
        self.flood_msgs.setValue(int(pref("flood_msgs") or 6))
        self.flood_secs = QSpinBox()
        self.flood_secs.setRange(1, 60)
        self.flood_secs.setValue(int(pref("flood_secs") or 4))
        self.flood_secs.setSuffix(" s")
        self.paste_guard = QCheckBox()
        self.paste_guard.setChecked(bool(pref("paste_guard")))
        self.paste_lines = QSpinBox()
        self.paste_lines.setRange(2, 100)
        self.paste_lines.setValue(int(pref("paste_lines") or 4))
        self.invite_protect = QCheckBox()
        self.invite_protect.setChecked(bool(pref("invite_protect")))
        self.ignore_invites = QCheckBox()
        self.ignore_invites.setChecked(bool(pref("ignore_invites")))
        for label, w in (
            (self.tr("Flood protection (auto-ignore)"), self.flood_protect),
            (self.tr("Flood: max messages"), self.flood_msgs),
            (self.tr("Flood: within"), self.flood_secs),
            (self.tr("Large-paste guard"), self.paste_guard),
            (self.tr("Paste guard: lines"), self.paste_lines),
            (self.tr("Auto-ignore invite spam"), self.invite_protect),
            (self.tr("Ignore all channel invites"), self.ignore_invites),
        ):
            pf.addRow(label, w)
        phint = QLabel(self.tr("Flood / invite-spam protection adds the offender to your ignore list "
                               "automatically (friends are never auto-ignored). The paste guard confirms and "
                               "throttles large pastes you send."))
        phint.setWordWrap(True)
        pf.addRow(phint)
        self._add_page("protection", self.tr("Protection"), pr)

        # ---- Files (DCC) -------------------------------------------------
        fd = QWidget()
        ff = QFormLayout(fd)
        self.dcc_accept = QComboBox()
        for label, data in ((self.tr("Ask each time"), "ask"),
                            (self.tr("Auto-accept trusted nicks"), "trusted"),
                            (self.tr("Auto-accept everything (careful)"), "all")):
            self.dcc_accept.addItem(label, data)
        self._select(self.dcc_accept, str(pref("dcc_accept") or "ask"))
        self.dcc_trusted = QLineEdit(", ".join(pref("dcc_trusted") or []))
        self.dcc_trusted.setPlaceholderText(self.tr("alice, bob — auto-accepted when “trusted”"))
        self.dcc_passive = QCheckBox()
        self.dcc_passive.setChecked(bool(pref("dcc_passive")))
        self.dcc_ip = QLineEdit(str(pref("dcc_ip") or ""))
        self.dcc_ip.setPlaceholderText(self.tr("auto — your connection's IP"))
        self.dcc_pfirst = QSpinBox()
        self.dcc_pfirst.setRange(0, 65535)
        self.dcc_pfirst.setSpecialValueText(self.tr("any"))
        self.dcc_pfirst.setValue(int(pref("dcc_port_first") or 0))
        self.dcc_plast = QSpinBox()
        self.dcc_plast.setRange(0, 65535)
        self.dcc_plast.setSpecialValueText(self.tr("any"))
        self.dcc_plast.setValue(int(pref("dcc_port_last") or 0))
        self.dcc_dir = QLineEdit(str(pref("dcc_dir") or ""))
        self.dcc_dir.setPlaceholderText("<config>/downloads")
        dbrowse = QPushButton(self.tr("Browse…"))
        dbrowse.clicked.connect(self._browse_dcc_dir)
        for label, w in (
            (self.tr("Incoming files"), self.dcc_accept),
            (self.tr("Trusted nicks (auto-accept)"), self.dcc_trusted),
            (self.tr("Passive / reverse DCC (NAT-friendly)"), self.dcc_passive),
            (self.tr("Your DCC IP (for active DCC)"), self.dcc_ip),
            (self.tr("Listen port from"), self.dcc_pfirst),
            (self.tr("Listen port to"), self.dcc_plast),
        ):
            ff.addRow(label, w)
        ff.addRow(self.tr("Download folder"), _hbox(self.dcc_dir, dbrowse))
        fhint = QLabel(self.tr("Passive DCC asks the OTHER side to open the port — best when you're behind NAT "
                               "and they aren't. For active DCC (you open the port) behind NAT, set your public "
                               "IP + a forwarded port range here."))
        fhint.setWordWrap(True)
        ff.addRow(fhint)
        self._add_page("files", self.tr("Files (DCC)"), fd)

        # ---- Themes (App + Chat: theme + customize; the FONTS live on the Fonts tab) --
        th = QWidget()
        tlay = QVBoxLayout(th)

        app_box, ag = _group(self.tr("App  ·  window / menus / chrome"))
        self.app_theme = QComboBox()
        self._fill_theme_combo(self.app_theme, theme.THEME_LABELS, get_setting("theme", "dark"))
        ag.addRow(self.tr("Theme"), self.app_theme)
        self.app_default = QPushButton(self.tr("Default"))
        self.app_default.setToolTip(self.tr("Selects the normal MaxChat default theme. Click OK to apply."))
        self.app_default.clicked.connect(self._select_default_theme)
        self.app_theme_off = QPushButton(self.tr("Turn themes off"))
        self.app_theme_off.setToolTip(self.tr("Uses Qt/system colors and disables MaxChat app styling. Click OK to apply."))
        self.app_theme_off.clicked.connect(self._select_system_theme)
        ag.addRow("", _hbox(self.app_default, self.app_theme_off))
        self.app_customize = QPushButton(self.tr("Customize…"))
        self.app_customize.clicked.connect(self._customize_app)
        ag.addRow("", self.app_customize)
        tlay.addWidget(app_box)

        chat_box, cg = _group(self.tr("Chat area  ·  the message view + input"))
        self.chat_theme = QComboBox()
        self._fill_theme_combo(self.chat_theme, theme.CHAT_THEME_LABELS, pref("chat_theme"))
        cg.addRow(self.tr("Theme"), self.chat_theme)
        self.chat_customize = QPushButton(self.tr("Customize…"))
        self.chat_customize.clicked.connect(self._customize_chat)
        cg.addRow("", self.chat_customize)
        tlay.addWidget(chat_box)

        hint = QLabel(self.tr("Customize builds your own theme (saved in the config folder). App and chat are "
                              "independent — e.g. a light app with a black BitchX/irssi chat."))
        hint.setWordWrap(True)
        tlay.addWidget(hint)
        tlay.addStretch(1)
        self._add_page("themes", self.tr("Themes"), th)

        # ---- Fonts (every text area: font · size · bold · color) --------
        def _bold(key: str) -> QCheckBox:
            cb = QCheckBox(self.tr("Bold"))
            cb.setChecked(bool(pref(key)))
            return cb

        # App / UI chrome
        self.app_font = QFontComboBox()
        self.app_font.setCurrentFont(self._app_font0)  # pre-fill the actual current UI font
        self._app_fam0 = self.app_font.currentFont().family()  # normalised initial (unchanged check)
        self.app_font_size = _size_spin(_pt(self._app_font0))
        self.app_bold = QCheckBox(self.tr("Bold"))
        self.app_bold.setChecked(self._app_font0.bold())
        # Chat
        self.chat_font = QFontComboBox()
        self.chat_font.setCurrentFont(self._chat_font0)  # pre-fill the actual current chat font
        self._chat_fam0 = self.chat_font.currentFont().family()  # normalised initial
        self.chat_font_size = _size_spin(_pt(self._chat_font0))
        self.chat_bold = QCheckBox(self.tr("Bold"))
        self.chat_bold.setChecked(self._chat_font0.bold())
        self.chat_color = _ColorPick(str(pref("chat_text_color") or ""))
        self.event_color = _ColorPick(str(pref("event_color") or ""))
        # Channel tree + user list
        self.list_font = QFontComboBox()
        self.list_font.setCurrentFont(QFont(str(pref("list_font_family") or "") or self._app_font0.family()))
        self._list_fam0 = self.list_font.currentFont().family()
        self.list_font_size = _size_spin(int(pref("list_font_size") or 0) or _pt(self._app_font0))
        self.list_bold = _bold("list_font_bold")
        self.tree_color = _ColorPick(str(pref("tree_color") or ""))
        self.userlist_color = _ColorPick(str(pref("userlist_color") or ""))
        # Label fonts (nick / status / topic) — each with its own bold + color
        self.nick_font, self.nick_font_size, self._nick_fam0 = self._label_font("nick_font_family", "nick_font_size")
        self.nick_bold = _bold("nick_font_bold")
        self.nick_color = _ColorPick(str(pref("nick_label_color") or ""))
        self.status_font, self.status_font_size, self._status_fam0 = self._label_font("status_font_family", "status_font_size")
        self.status_bold = _bold("status_font_bold")
        self.status_color = _ColorPick(str(pref("status_text_color") or ""))
        self.topic_font, self.topic_font_size, self._topic_fam0 = self._label_font("topic_font_family", "topic_font_size")
        self.topic_bold = _bold("topic_font_bold")
        self.topic_color = _ColorPick(str(pref("topic_color") or ""))

        chatf_box, cf3 = _group(self.tr("Chat  ·  the message view + input"))
        cf3.addRow(self.tr("Font"), self.chat_font)
        cf3.addRow(self.tr("Size"), _hbox(self.chat_font_size, self.chat_bold))
        cf3.addRow(self.tr("Text color"), self.chat_color)
        cf3.addRow(self.tr("Event lines"), self.event_color)

        ui_box, uf = _group(self.tr("App  ·  window / menus / chrome"))
        uf.addRow(self.tr("Font"), self.app_font)
        uf.addRow(self.tr("Size"), _hbox(self.app_font_size, self.app_bold))

        list_box, lf = _group(self.tr("Channel + user list"))
        lf.addRow(self.tr("Font"), self.list_font)
        lf.addRow(self.tr("Size"), _hbox(self.list_font_size, self.list_bold))
        lf.addRow(self.tr("Tree color"), self.tree_color)
        lf.addRow(self.tr("Users color"), self.userlist_color)

        def _label_group(title, fontw, sizew, boldw, colorw):
            box, form = _group(title)
            form.addRow(self.tr("Font"), fontw)
            form.addRow(self.tr("Size"), _hbox(sizew, boldw))
            form.addRow(self.tr("Color"), colorw)
            return box

        nick_g = _label_group(self.tr("Your nick  ·  beside the input"),
                              self.nick_font, self.nick_font_size, self.nick_bold, self.nick_color)
        status_g = _label_group(self.tr("Status bar"),
                                self.status_font, self.status_font_size, self.status_bold, self.status_color)
        topic_g = _label_group(self.tr("Channel topic"),
                               self.topic_font, self.topic_font_size, self.topic_bold, self.topic_color)

        fo = QWidget()
        folay = QVBoxLayout(fo)
        preset_row = QHBoxLayout()
        jetbrains_btn = QPushButton(self.tr("Set all to JetBrains Mono"))
        jetbrains_btn.clicked.connect(self._set_all_fonts_jetbrains)
        system_btn = QPushButton(self.tr("Set all to System Default"))
        system_btn.setToolTip(self.tr("Uses the system UI font for chrome and the system fixed-width font for chat alignment."))
        system_btn.clicked.connect(self._set_all_fonts_system)
        preset_row.addWidget(jetbrains_btn)
        preset_row.addWidget(system_btn)
        preset_row.addStretch(1)
        folay.addLayout(preset_row)
        cols = QHBoxLayout()
        folay.addLayout(cols)
        fl_left, fl_right = QVBoxLayout(), QVBoxLayout()
        cols.addLayout(fl_left)
        cols.addLayout(fl_right)
        for b in (chatf_box, ui_box, list_box):
            fl_left.addWidget(b)
        fl_left.addStretch(1)
        for b in (nick_g, status_g, topic_g):
            fl_right.addWidget(b)
        fl_right.addStretch(1)
        fnote = QLabel(self.tr("Each area has its own font, size, bold and color. \"Pick\" sets a custom color; "
                               "\"Default\" returns to the theme. Size \"0\" = the app default. App chrome color "
                               "follows the theme (Themes ▸ Customize)."))
        fnote.setWordWrap(True)
        folay.addWidget(fnote)
        folay.addStretch(1)
        self._add_page("fonts", self.tr("Fonts"), fo)

        # ---- Localization ------------------------------------------------
        loc = QWidget()
        locv = QVBoxLayout(loc)

        ui_box, ui_form = _group(self.tr("Interface"))
        self.interface_language = QComboBox()
        for code, label in localization.LANGUAGES:
            self.interface_language.addItem(label, code)
        self._select(self.interface_language, str(pref("interface_language") or "system"))
        ui_form.addRow(self.tr("Language"), self.interface_language)
        translations_btn = QPushButton(self.tr("Open translations folder…"))
        translations_btn.setToolTip(str(localization.translations_dir()))
        translations_btn.clicked.connect(
            lambda: self._open_dir(str(localization.translations_dir()))
        )
        ui_form.addRow("", translations_btn)
        ui_note = QLabel(
            self.tr("Language changes apply after restarting MaxChat. English is always bundled; translated "
                    ".qm files can be added under assets/translations.")
        )
        ui_note.setWordWrap(True)
        ui_form.addRow(ui_note)
        locv.addWidget(ui_box)

        spell_box, spell_form = _group(self.tr("Spelling"))
        self.spellcheck_enabled = QCheckBox()
        self.spellcheck_enabled.setChecked(bool(pref("spellcheck_enabled")))
        self.spell_language = QComboBox()
        for code, label in localization.SPELL_LANGUAGES:
            self.spell_language.addItem(label, code)
            if not localization.spell_dictionary_available(code):
                item = self.spell_language.model().item(self.spell_language.count() - 1)
                if item is not None:
                    item.setEnabled(False)
        self._select(self.spell_language, str(pref("spell_language") or "en"))
        if not localization.spell_dictionary_available(str(self.spell_language.currentData() or "")):
            self._select(self.spell_language, "en")
        spell_form.addRow(self.tr("Enable spellcheck"), self.spellcheck_enabled)
        spell_form.addRow(self.tr("Dictionary"), self.spell_language)
        spell_note = QLabel(
            self.tr("Spellcheck uses bundled pure-Python dictionaries where available. Additional languages can "
                    "be added later with Hunspell dictionaries.")
        )
        spell_note.setWordWrap(True)
        spell_form.addRow(spell_note)
        locv.addWidget(spell_box)
        locv.addStretch(1)
        self._add_page("localization", self.tr("Localization"), loc)

        # ---- Comic — everything comic now lives in the Comic Settings window ----
        cc = QWidget()
        cf = QFormLayout(cc)
        ccnote = QLabel(
            self.tr("Comic mode draws the chat as a comic strip (Toolbar ▸ Comic, or the Comic menu). All of its "
                    "settings are in one place — the Comic Settings window:\n\n"
                    "  • the art folder (your own .avb/.bgb character/background files — none are bundled)\n"
                    "  • your character, the default background, panel & bubble counts\n"
                    "  • per-channel characters/backgrounds, and the Bots tab (keep #mlb / bot spam out)")
        )
        ccnote.setWordWrap(True)
        cf.addRow(ccnote)
        if self._open_comic_settings is not None:
            cs = QPushButton(self.tr("Open Comic Settings…"))
            cs.setToolTip(self.tr("Art folder · your character · backgrounds · per-channel · Bots"))
            cs.clicked.connect(lambda: self._open_comic_settings())  # opens on top of Preferences
            cf.addRow("", cs)
        if self._browse_chars is not None:
            bc = QPushButton(self.tr("Browse characters…"))
            bc.setToolTip(self.tr("See what each comic avatar looks like"))
            bc.clicked.connect(lambda: self._browse_chars())
            cf.addRow("", bc)
        self._add_page("comic", self.tr("Comic"), cc)

        # ---- Services (per-service link-preview toggles) -----------------
        self._add_page("services", self.tr("Services"), self._build_services_page())

        # ---- Data (folders · stats · import / export / reset) ------------
        cfg_dir = config.config_dir()
        cache = config.cache_dir()
        logs = os.path.join(cfg_dir, "logs")
        downloads = str(pref("dcc_dir") or "") or os.path.join(cfg_dir, "downloads")
        scripts = os.path.join(cfg_dir, "scripts")

        da = QWidget()
        dv = QVBoxLayout(da)
        fbox, fform = _group(self.tr("Folders"))
        for label, path in ((self.tr("Config"), cfg_dir), (self.tr("Cache"), cache), (self.tr("Logs"), logs),
                            (self.tr("Downloads"), downloads), (self.tr("Scripts"), scripts)):
            fform.addRow(label, self._dir_row(path))
        dv.addWidget(fbox)

        sbox, sform = _group(self.tr("Storage && stats"))
        sform.addRow(self.tr("Config size"), QLabel(_human(_dir_size(cfg_dir))))
        sform.addRow(self.tr("Cache size"), QLabel(_human(_dir_size(cache))))
        sform.addRow(self.tr("Logs size"), QLabel(_human(_dir_size(logs))))
        sform.addRow(self.tr("Memory in use"), QLabel(_human(_rss_bytes())))
        self.saved_networks_count = QLabel(str(len(pref("networks") or [])))
        sform.addRow(self.tr("Saved networks"), self.saved_networks_count)
        sform.addRow(self.tr("Ignored masks"), QLabel(str(len(pref("ignores") or []))))
        dv.addWidget(sbox)

        cbox, cform = _group(self.tr("Configuration"))
        exp = QPushButton(self.tr("Export…"))
        exp.clicked.connect(self._export_config)
        imp = QPushButton(self.tr("Import…"))
        imp.clicked.connect(self._import_config)
        rst_servers = QPushButton(self.tr("Reset server list…"))
        rst_servers.clicked.connect(self._reset_server_list)
        rst = QPushButton(self.tr("Reset to defaults…"))
        rst.clicked.connect(self._reset_config)
        cform.addRow(_hbox(exp, imp, rst_servers, rst))
        dv.addWidget(cbox)
        dv.addStretch(1)
        self._add_page("data", self.tr("Data"), da)

        self.nav.setFixedWidth(  # autosize so no tab label (e.g. "Notifications") is clipped
            theme.nav_fit_width(self.nav, [self.nav.item(i).text() for i in range(self.nav.count())]))
        self.nav.setCurrentRow(self._pages.get(initial_tab or "appearance", 0))

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self._save_and_accept)
        buttons.rejected.connect(self.reject)
        outer.addWidget(buttons)
        self._autosize()

    def _autosize(self) -> None:
        """Grow the dialog to fit the tallest page so no field is clipped at the 14pt font, capped to
        the screen (then a scrollbar would appear, but in practice every page fits)."""
        from PySide6.QtGui import QGuiApplication
        self.stack.setMinimumHeight(max((self.stack.widget(i).sizeHint().height()
                                         for i in range(self.stack.count())), default=0))
        self.adjustSize()
        hint = self.sizeHint()
        w, h = max(1040, hint.width()), hint.height() + 8
        scr = QGuiApplication.primaryScreen()
        if scr is not None:
            avail = scr.availableGeometry()
            w = min(w, int(avail.width() * 0.96))
            h = min(h, int(avail.height() * 0.95))
        self.resize(w, max(640, h))

    # ---- nav / pages ------------------------------------------------------
    def _add_page(self, key: str, title: str, widget: QWidget, scrollable: bool = False) -> None:
        # Options pages don't scroll — the dialog is sized to fit them (only theme *lists* scroll, in
        # their own dropdowns). `scrollable` stays as an escape hatch for any future oversized page.
        page = widget
        if scrollable:
            page = QScrollArea()
            page.setWidgetResizable(True)
            page.setFrameShape(QFrame.Shape.NoFrame)
            page.setWidget(widget)
        self._pages[key] = self.stack.count()
        self.stack.addWidget(page)
        item = QListWidgetItem(title)
        item.setSizeHint(QSize(0, 40))
        self.nav.addItem(item)

    def _nav_style(self) -> str:
        return theme.nav_list_style(get_setting("theme", "dark"), "prefsNav")

    # ---- timestamp combo helpers -----------------------------------------
    def _label_font(self, famkey: str, sizekey: str):
        """A (QFontComboBox, size-spin, initial-family) trio for a small label, pre-filled from prefs."""
        combo = QFontComboBox()
        combo.setCurrentFont(QFont(str(pref(famkey) or "") or self._app_font0.family()))
        size = _size_spin(int(pref(sizekey) or 0) or _pt(self._app_font0))
        return combo, size, combo.currentFont().family()

    def _font_controls(self) -> tuple[tuple[QFontComboBox, QSpinBox, QCheckBox], ...]:
        return (
            (self.app_font, self.app_font_size, self.app_bold),
            (self.chat_font, self.chat_font_size, self.chat_bold),
            (self.list_font, self.list_font_size, self.list_bold),
            (self.nick_font, self.nick_font_size, self.nick_bold),
            (self.status_font, self.status_font_size, self.status_bold),
            (self.topic_font, self.topic_font_size, self.topic_bold),
        )

    def _set_all_fonts(self, font: QFont, *, bold: bool = False) -> None:
        family = font.family()
        size = _pt(font)
        for combo, size_spin, bold_check in self._font_controls():
            combo.setCurrentFont(QFont(family))
            size_spin.setValue(size)
            bold_check.setChecked(bold)

    def _set_all_fonts_jetbrains(self) -> None:
        self._set_all_fonts(QFont(fonts.default_mono(), 14), bold=True)

    def _set_all_fonts_system(self) -> None:
        ui_font = QFontDatabase.systemFont(QFontDatabase.SystemFont.GeneralFont)
        fixed_font = QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont)
        ui_size = _pt(ui_font)
        fixed_size = _pt(fixed_font)
        for combo, size_spin, bold_check in (
            (self.app_font, self.app_font_size, self.app_bold),
            (self.nick_font, self.nick_font_size, self.nick_bold),
            (self.status_font, self.status_font_size, self.status_bold),
            (self.topic_font, self.topic_font_size, self.topic_bold),
        ):
            combo.setCurrentFont(QFont(ui_font.family()))
            size_spin.setValue(ui_size)
            bold_check.setChecked(False)
        for combo, size_spin, bold_check in (
            (self.chat_font, self.chat_font_size, self.chat_bold),
            (self.list_font, self.list_font_size, self.list_bold),
        ):
            combo.setCurrentFont(QFont(fixed_font.family()))
            size_spin.setValue(fixed_size)
            bold_check.setChecked(False)

    def _set_ts(self, fmt: str) -> None:
        i = self.ts_format.findData(fmt)
        if i >= 0:
            self.ts_format.setCurrentIndex(i)
        else:
            self.ts_format.setEditText(fmt)  # a custom strftime the user typed before

    def _ts_value(self) -> str:
        c = self.ts_format
        idx = c.currentIndex()
        if idx >= 0 and c.itemText(idx) == c.currentText() and c.itemData(idx):
            return c.itemData(idx)  # a preset is selected → store its strftime, not the label
        return c.currentText().strip() or "%I:%M %p"

    @staticmethod
    def _select(combo: QComboBox, data) -> None:
        i = combo.findData(data)
        if i >= 0:
            combo.setCurrentIndex(i)

    @staticmethod
    def _family_to_save(combo: QFontComboBox, initial_family: str, original: str) -> str:
        chosen = combo.currentFont().family()
        # unchanged from what was shown at open → stay unset so it keeps tracking the theme/system font
        if not original and chosen == initial_family:
            return ""
        return chosen

    @staticmethod
    def _size_to_save(spin: QSpinBox, resolved: QFont, original) -> int:
        v = spin.value()
        if not original and v == _pt(resolved):
            return 0  # unchanged from the shown default → stay unset
        return v

    def _fill_theme_combo(self, combo: QComboBox, labels: dict, current) -> None:
        combo.clear()
        for tid, lbl in labels.items():
            combo.addItem(lbl, tid)
        self._select(combo, current)

    def _customize_app(self) -> None:
        from maxchat.ui.theme_editor import ThemeEditor
        dlg = ThemeEditor(self, scope="app", base_id=self.app_theme.currentData())
        if dlg.exec() and dlg.result_id:
            self._fill_theme_combo(self.app_theme, theme.THEME_LABELS, dlg.result_id)

    def _customize_chat(self) -> None:
        from maxchat.ui.theme_editor import ThemeEditor
        dlg = ThemeEditor(self, scope="chat", base_id=self.chat_theme.currentData())
        if dlg.exec() and dlg.result_id:
            self._fill_theme_combo(self.chat_theme, theme.CHAT_THEME_LABELS, dlg.result_id)

    def _select_default_theme(self) -> None:
        self._select(self.app_theme, theme.DEFAULT_THEME_ID)

    def _select_system_theme(self) -> None:
        self._select(self.app_theme, theme.SYSTEM_THEME_ID)

    # ---- Services tab (per-service link-preview toggles) ------------------
    def _build_services_page(self) -> QWidget:
        from maxchat.content_services import CONTENT_SERVICES, DEFAULT_CONTENT_SERVICES

        page = QWidget()
        v = QVBoxLayout(page)
        box, form = _group(self.tr("Link previews"))
        saved = pref("content_services")
        saved = saved if isinstance(saved, dict) else {}
        self._service_toggles: dict[str, QCheckBox] = {}
        for svc in CONTENT_SERVICES:
            cb = QCheckBox()
            on = bool(saved.get(svc["key"], DEFAULT_CONTENT_SERVICES.get(svc["key"], True)))
            cb.setChecked(on)
            desc_text = QCoreApplication.translate("ContentServices", svc.get("desc", ""))
            cb.setToolTip(desc_text)
            self._service_toggles[svc["key"]] = cb
            form.addRow(QCoreApplication.translate("ContentServices", svc["label"]), cb)
            desc = QLabel(desc_text)
            desc.setWordWrap(True)
            desc.setStyleSheet("color: palette(mid);")
            form.addRow("", desc)
        v.addWidget(box)
        note = QLabel(self.tr("Turning a service off keeps the bare link in chat (still clickable) — it just "
                              "won't auto-fetch.  Pasting an image to upload it to a host is a separate, optional "
                              "plugin (Settings ▸ Scripts…)."))
        note.setWordWrap(True)
        v.addWidget(note)
        v.addStretch(1)
        return page

    # ---- Data tab ---------------------------------------------------------
    def _dir_row(self, path: str) -> QWidget:
        """A folder path + an [Open] button (the path is also the button's tooltip)."""
        w = QWidget()
        h = QHBoxLayout(w)
        h.setContentsMargins(0, 0, 0, 0)
        h.setSpacing(6)
        lbl = QLabel(path)
        lbl.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        btn = QPushButton(self.tr("Open"))
        btn.setFixedWidth(64)
        btn.setToolTip(path)
        btn.clicked.connect(lambda _=False, p=path: self._open_dir(p))
        h.addWidget(lbl, 1)
        h.addWidget(btn)
        return w

    @staticmethod
    def _open_dir(path: str) -> None:
        try:
            os.makedirs(path, exist_ok=True)
        except OSError:
            pass
        QDesktopServices.openUrl(QUrl.fromLocalFile(path))

    def _export_config(self) -> None:
        import json
        path, _ = QFileDialog.getSaveFileName(self, self.tr("Export config"), "maxchat-settings.json",
                                              "JSON (*.json)")
        if not path:
            return
        if not path.lower().endswith(".json"):
            path += ".json"
        try:
            with open(path, "w", encoding="utf-8") as f:
                json.dump(config.load_settings(), f, indent=2)
        except OSError as e:
            shadow_message.warning(
                self, self.tr("Export config"), self.tr("Could not write the file:\n{error}").format(error=e)
            )
            return
        shadow_message.information(
            self, self.tr("Export config"), self.tr("Settings exported to\n{path}").format(path=path)
        )

    def _import_config(self) -> None:
        import json
        path, _ = QFileDialog.getOpenFileName(self, self.tr("Import config"), "", "JSON (*.json)")
        if not path:
            return
        try:
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
            if not isinstance(data, dict):
                raise ValueError(self.tr("that file isn't a settings object"))
        except (OSError, ValueError) as e:
            shadow_message.warning(
                self, self.tr("Import config"), self.tr("Could not read settings:\n{error}").format(error=e)
            )
            return
        if not shadow_message.question(
            self,
            self.tr("Import config"),
            self.tr("Import this settings file?\n"
                    "General settings will be replaced. The server list keeps MaxChat's current bundled "
                    "servers and restores your saved nick/password/channel fields.\n"
                    "The app will close so it can reload on next launch."),
        ):
            return
        data = _prepare_imported_settings(data)
        config.save_settings(data)
        shadow_message.information(self, self.tr("Import config"), self.tr("Imported. Restart the app to apply."))
        self.reject()  # close WITHOUT saving the dialog's widgets over the imported file

    def _reset_config(self) -> None:
        if not shadow_message.question(
            self,
            self.tr("Reset to defaults"),
            self.tr("Reset ALL settings to defaults?\nNetworks, themes, colors and options "
                    "are cleared. Logs and downloads are kept.\n"
                    "The app will close so it can reload on next launch."),
        ):
            return
        config.save_settings({})
        shadow_message.information(self, self.tr("Reset"), self.tr("Settings reset. Restart the app to apply."))
        self.reject()

    def _reset_server_list(self) -> None:
        if not shadow_message.question(
            self,
            self.tr("Reset server list"),
            self.tr("Replace your saved server list with the bundled defaults?\n"
                    "Custom networks and server edits will be removed."),
        ):
            return
        config.set_setting("networks", copy.deepcopy(DEFAULT_NETWORKS))
        config.set_setting("networks_merge_version", 0)
        self.saved_networks_count.setText(str(len(DEFAULT_NETWORKS)))
        shadow_message.information(
            self, self.tr("Reset server list"), self.tr("Server list reset to bundled defaults.")
        )

    def _browse_dcc_dir(self) -> None:
        d = QFileDialog.getExistingDirectory(self, self.tr("Download folder"), self.dcc_dir.text())
        if d:
            self.dcc_dir.setText(d)


    def _do_test_notify(self) -> None:
        """Preview a toast using the values selected on the page right now (unsaved)."""
        if self._test_notify is not None:
            self._test_notify(self.notify_popup.currentData(), self.notify_corner.currentData(),
                              self.notify_duration.value(), self.notify_theme.currentData(),
                              self.notify_sound.isChecked())

    def _save_and_accept(self) -> None:
        set_pref("show_timestamps", self.show_ts.isChecked())
        set_pref("timestamp_format", self._ts_value())
        set_pref("colored_nicks", self.colored_nicks.isChecked())
        set_pref("show_formatting", self.show_fmt.isChecked())
        set_pref("indent_wrap", self.indent_wrap.isChecked())
        set_pref("marker_line", self.marker_line.isChecked())
        set_pref("align_nicks", self.align_nicks.isChecked())
        set_pref("nick_width", self.nick_width.value())
        set_pref("word_wrap", self.word_wrap.isChecked())
        set_pref("sort_status", self.sort_status.isChecked())
        set_pref("separator_line", self.separator_line.isChecked())
        set_pref("strip_color_copy", self.strip_copy.isChecked())
        set_pref("show_input_hint", self.show_input_hint.isChecked())
        set_pref("show_button_bar", self.show_button_bar.isChecked())
        set_pref("tray_icon", self.tray_icon.currentData())
        set_pref("nick_label_color", self.nick_color.value())
        set_pref("status_text_color", self.status_color.value())
        set_pref("topic_color", self.topic_color.value())
        set_pref("tree_color", self.tree_color.value())
        set_pref("userlist_color", self.userlist_color.value())
        set_pref("event_color", self.event_color.value())
        set_pref("list_font_family",
                 self._family_to_save(self.list_font, self._list_fam0, pref("list_font_family")))
        for fam_w, fam0, famkey, size_w, sizekey, bold_w, boldkey in (
            (self.nick_font, self._nick_fam0, "nick_font_family", self.nick_font_size, "nick_font_size",
             self.nick_bold, "nick_font_bold"),
            (self.status_font, self._status_fam0, "status_font_family", self.status_font_size,
             "status_font_size", self.status_bold, "status_font_bold"),
            (self.topic_font, self._topic_fam0, "topic_font_family", self.topic_font_size, "topic_font_size",
             self.topic_bold, "topic_font_bold"),
        ):
            set_pref(famkey, self._family_to_save(fam_w, fam0, pref(famkey)))
            set_pref(sizekey, self._size_to_save(size_w, self._app_font0, pref(sizekey)))
            set_pref(boldkey, bold_w.isChecked())
        set_pref("chat_font_family",
                 self._family_to_save(self.chat_font, self._chat_fam0, pref("chat_font_family")))
        set_pref("chat_font_size",
                 self._size_to_save(self.chat_font_size, self._chat_font0, pref("chat_font_size")))
        set_pref("chat_font_bold", self.chat_bold.isChecked())
        set_pref("chat_text_color", self.chat_color.value())
        set_pref("app_font_family",
                 self._family_to_save(self.app_font, self._app_fam0, pref("app_font_family")))
        set_pref("app_font_size",
                 self._size_to_save(self.app_font_size, self._app_font0, pref("app_font_size")))
        set_pref("app_font_bold", self.app_bold.isChecked())
        set_pref("list_font_size",
                 self._size_to_save(self.list_font_size, self._app_font0, pref("list_font_size")))
        set_pref("list_font_bold", self.list_bold.isChecked())
        set_pref("interface_language", self.interface_language.currentData())
        set_pref("spellcheck_enabled", self.spellcheck_enabled.isChecked())
        set_pref("spell_language", self.spell_language.currentData())
        set_pref("hide_joinpart", self.hide_jp.isChecked())
        set_pref("show_mode", self.show_mode.isChecked())
        set_pref("pm_echo", self.pm_echo.isChecked())
        set_pref("notify_popup", self.notify_popup.currentData())
        set_pref("notify_pm", self.notify_pm.isChecked())
        set_pref("notify_highlight", self.notify_highlight.isChecked())
        set_pref("notify_flash", self.notify_flash.isChecked())
        set_pref("notify_corner", self.notify_corner.currentData())
        set_pref("notify_duration", self.notify_duration.value())
        set_pref("notify_theme", self.notify_theme.currentData())
        set_pref("notify_sound", self.notify_sound.isChecked())
        set_pref("dnd", self.dnd.isChecked())
        set_pref("minimize_to_tray", self.minimize_to_tray.isChecked())
        set_pref("highlight_words", self.highlight_words.text().strip())
        set_pref("beep_highlight", self.beep_highlight.isChecked())
        set_pref("ctcp_sound", self.ctcp_sound.isChecked())
        set_pref("flood_protect", self.flood_protect.isChecked())
        set_pref("flood_msgs", self.flood_msgs.value())
        set_pref("flood_secs", self.flood_secs.value())
        set_pref("paste_guard", self.paste_guard.isChecked())
        set_pref("paste_lines", self.paste_lines.value())
        set_pref("invite_protect", self.invite_protect.isChecked())
        set_pref("ignore_invites", self.ignore_invites.isChecked())
        set_pref("dcc_accept", self.dcc_accept.currentData())
        set_pref("dcc_trusted",
                 [n for n in re.split(r"[,\s]+", self.dcc_trusted.text().strip()) if n])
        set_pref("dcc_passive", self.dcc_passive.isChecked())
        set_pref("dcc_ip", self.dcc_ip.text().strip())
        set_pref("dcc_port_first", self.dcc_pfirst.value())
        set_pref("dcc_port_last", self.dcc_plast.value())
        set_pref("dcc_dir", self.dcc_dir.text().strip())
        set_pref("logging", self.logging.isChecked())
        set_pref("log_mask", self.log_mask.text().strip() or "%network-%channel")
        set_pref("replay_log", self.replay.isChecked())
        set_pref("replay_lines", self.replay_lines.value())
        set_pref("auto_reconnect", self.auto_reconnect.isChecked())
        set_pref("auto_rejoin", self.auto_rejoin.isChecked())
        set_pref("hide_version", self.hide_version.isChecked())
        set_pref("ctcp_version", self.ctcp_version.text().strip())
        set_pref("confirm_quit", self.confirm_quit.isChecked())
        set_pref("scrollback", self.scrollback.value())
        set_setting("theme", self.app_theme.currentData())
        set_setting("chat_theme", self.chat_theme.currentData())
        # (comic art folder / character / background / panels / bubbles now live in Comic Settings)
        set_pref("content_services", {k: cb.isChecked() for k, cb in self._service_toggles.items()})
        self.accept()
