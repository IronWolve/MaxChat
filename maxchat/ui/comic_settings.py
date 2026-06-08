"""Comic settings popup — a left-nav, tabbed dialog (same look as Preferences).

Pages:
  • Global     — defaults used everywhere: background, your character, panel/bubble counts.
  • Characters — the nick → character table applied across every channel.
  • Channels   — pick ANY open channel and set its own background + nick → character table + a
                 per-channel "hide from comic" list; blank fields fall back to Global.
  • Bots       — keep bot noise out of the comic: command/correction prefixes (!cmd, .mlb, s/…),
                 bot nicknames to hide everywhere, and an advanced custom regex.

Everything is saved straight to ``config`` and the window reloads + re-renders on accept.
"""

from __future__ import annotations

import re

from PySide6.QtCore import QSize
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialogButtonBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QPushButton,
    QSpinBox,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from maxchat.config import get_setting, pref, set_pref
from maxchat.ui import theme
from maxchat.ui import shadow_message
from maxchat.ui.shadow_dialog import ShadowDialog
from maxchat.ui.tooltips import info_tip


def _parse_nicks(text: str) -> list[str]:
    """Split a space/comma-separated nick list into a clean, de-duplicated, lowercased list."""
    out: list[str] = []
    for n in re.split(r"[\s,]+", str(text or "").strip().lower()):
        if n and n not in out:
            out.append(n)
    return out


class _ListEditor(QWidget):
    """A simple editable string list: a list box + Add / Edit / Remove buttons."""

    def __init__(self, items: list[str], add_label: str, prompt: str) -> None:
        super().__init__()
        self._prompt = prompt
        v = QVBoxLayout(self)
        v.setContentsMargins(0, 0, 0, 0)
        self.list = QListWidget()
        self.list.addItems([str(i) for i in items])
        self.list.itemDoubleClicked.connect(lambda _i: self._edit())
        v.addWidget(self.list)
        row = QHBoxLayout()
        add = QPushButton(add_label)
        add.clicked.connect(self._add)
        edit = QPushButton("Edit")
        edit.clicked.connect(self._edit)
        rm = QPushButton("Remove")
        rm.clicked.connect(self._remove)
        row.addWidget(add)
        row.addWidget(edit)
        row.addWidget(rm)
        row.addStretch(1)
        v.addLayout(row)

    def _add(self) -> None:
        text, ok = shadow_message.get_text(self, "Add", self._prompt)
        if ok and text.strip():
            self.list.addItem(text.strip())

    def _edit(self) -> None:
        it = self.list.currentItem()
        if it is None:
            return
        text, ok = shadow_message.get_text(self, "Edit", self._prompt, text=it.text())
        if ok and text.strip():
            it.setText(text.strip())

    def _remove(self) -> None:
        r = self.list.currentRow()
        if r >= 0:
            self.list.takeItem(r)

    def values(self) -> list[str]:
        seen: list[str] = []
        for r in range(self.list.count()):
            t = self.list.item(r).text().strip()
            if t and t not in seen:
                seen.append(t)
        return seen


class _CharTable(QWidget):
    """A nick → character assignment editor: a 2-column table + add/remove row controls."""

    def __init__(self, char_stems: list[str], assignments: dict, member_suggestions: list[str]) -> None:
        super().__init__()
        self._stems = char_stems
        v = QVBoxLayout(self)
        v.setContentsMargins(0, 0, 0, 0)
        self.table = QTableWidget(0, 2)
        self.table.setHorizontalHeaderLabels(["Nick", "Character"])
        self.table.verticalHeader().setVisible(False)
        hh = self.table.horizontalHeader()
        hh.setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        hh.setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        v.addWidget(self.table)
        row = QHBoxLayout()
        self.add_nick = QComboBox()
        self.add_nick.setEditable(True)
        self.add_nick.addItems([""] + member_suggestions)  # current members for convenience
        add = QPushButton("Add / set")
        add.clicked.connect(self._add)
        rm = QPushButton("Remove")
        rm.clicked.connect(self._remove)
        row.addWidget(self.add_nick, 1)
        row.addWidget(add)
        row.addWidget(rm)
        v.addLayout(row)
        for nick, stem in (assignments or {}).items():
            self._set_row(str(nick), str(stem))

    def _char_combo(self, stem: str = "") -> QComboBox:
        c = QComboBox()
        c.addItem("(random)", "")
        for s in self._stems:
            c.addItem(s, s)
        i = c.findData(stem)
        if i >= 0:
            c.setCurrentIndex(i)
        return c

    def _find(self, nick: str) -> int:
        for r in range(self.table.rowCount()):
            it = self.table.item(r, 0)
            if it and it.text().lower() == nick.lower():
                return r
        return -1

    def _set_row(self, nick: str, stem: str) -> None:
        r = self._find(nick)
        if r < 0:
            r = self.table.rowCount()
            self.table.insertRow(r)
            self.table.setItem(r, 0, QTableWidgetItem(nick))
        self.table.setCellWidget(r, 1, self._char_combo(stem))

    def _add(self) -> None:
        nick = self.add_nick.currentText().strip()
        if nick:
            self._set_row(nick, "")
            self.add_nick.setCurrentText("")

    def _remove(self) -> None:
        r = self.table.currentRow()
        if r >= 0:
            self.table.removeRow(r)

    def values(self) -> dict:
        out: dict = {}
        for r in range(self.table.rowCount()):
            it = self.table.item(r, 0)
            cb = self.table.cellWidget(r, 1)
            if it and it.text().strip() and cb and cb.currentData():
                out[it.text().strip().lower()] = cb.currentData()
        return out


class ComicSettingsDialog(ShadowDialog):
    def __init__(self, parent=None, *, characters: list[str], backgrounds: list[tuple[str, str]],
                 channels: list[tuple[str, str, list[str]]] | None = None, art_dir: str = "") -> None:
        super().__init__(parent)
        self.setWindowTitle("Comic Settings")
        self.set_autosize(74, 30)  # font-relative sizing (tabbed content + nick tables)
        self._art_dir = art_dir
        self._char_stems = list(characters)
        self._backgrounds = list(backgrounds)  # [(stem, filename)]
        self._channels = channels or []        # [(chan_key, "label", members)] for ALL open channels
        # working copy of every saved per-channel override (so channels you don't open keep theirs)
        self._chan_work = {
            k: {"bg": str(v.get("bg") or ""), "chars": dict(v.get("chars") or {}),
                "ignore": list(v.get("ignore") or [])}
            for k, v in (dict(pref("comic_channels") or {})).items()
        }
        self._cur_idx = -1
        self.c_bg = None
        self.c_chars = None
        self.c_ignore = None
        self._members = sorted({m for _k, _l, ms in self._channels for m in ms})  # all known nicks

        root = QVBoxLayout(self.body)
        body = QHBoxLayout()
        body.setSpacing(0)
        root.addLayout(body, 1)
        self.nav = QListWidget()
        self.nav.setObjectName("comicNav")
        self.nav.setStyleSheet(theme.nav_list_style(get_setting("theme", "dark"), "comicNav"))
        self.stack = QStackedWidget()
        body.addWidget(self.nav)
        body.addWidget(self.stack, 1)
        self.nav.currentRowChanged.connect(self.stack.setCurrentIndex)

        self._add_page("Global", self._build_global())
        self._add_page("Characters", self._build_characters())
        self._add_page("Channels", self._build_channels())
        self._add_page("Bots", self._build_bots())
        self.nav.setFixedWidth(  # autosize to the longest tab label
            theme.nav_fit_width(self.nav, [self.nav.item(i).text() for i in range(self.nav.count())]))
        self.nav.setCurrentRow(0)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self._save)
        buttons.rejected.connect(self.reject)
        root.addWidget(buttons)

    def _add_page(self, title: str, widget: QWidget) -> None:
        self.stack.addWidget(widget)
        item = QListWidgetItem(title)
        item.setSizeHint(QSize(0, 38))
        self.nav.addItem(item)

    # ---- Global ----------------------------------------------------------
    def _build_global(self) -> QWidget:
        g = QWidget()
        gf = QFormLayout(g)
        self.g_art = QLineEdit(self._art_dir)
        self.g_art.setPlaceholderText("folder with your .avb/.bgb comic art (no art is bundled)")
        browse = QPushButton("Browse…")
        browse.clicked.connect(self._browse_art)
        art_row = QWidget()
        ar = QHBoxLayout(art_row)
        ar.setContentsMargins(0, 0, 0, 0)
        ar.addWidget(self.g_art, 1)
        ar.addWidget(browse)
        gf.addRow("Comic art folder", art_row)
        self.g_bg = self._bg_combo(str(pref("comic_bg") or ""), use_global=False)
        gf.addRow("Default background", self.g_bg)
        self.g_self = self._char_combo_simple(str(pref("comic_self_char") or ""), "(random per nick)")
        gf.addRow("Your character", self.g_self)
        self.g_panels = QSpinBox()
        self.g_panels.setRange(1, 6)
        self.g_panels.setValue(int(pref("comic_panels") or 4))
        gf.addRow("Comic panels (1–6)", self.g_panels)
        self.g_bubbles = QSpinBox()
        self.g_bubbles.setRange(1, 6)
        self.g_bubbles.setValue(int(pref("comic_per_panel") or 4))
        self.g_bubbles.setToolTip(info_tip(
            "Hard cap on bubbles per panel — a panel also breaks earlier if the next line wouldn't fit "
            "at a readable size."
        ))
        gf.addRow("Max bubbles per panel", self.g_bubbles)
        self.g_minfont = QSpinBox()
        self.g_minfont.setRange(6, 13)
        self.g_minfont.setValue(int(pref("comic_min_font") or 9))
        self.g_minfont.setToolTip(info_tip(
            "Start a NEW panel rather than shrinking the lettering below this. Higher = roomier panels "
            "(and more of them); lower = packed in, smaller text. Leave it alone for a sensible default."
        ))
        gf.addRow("Min text size (spill)", self.g_minfont)
        # --- name labels under each character ---
        self.g_captions = QCheckBox()
        self.g_captions.setChecked(bool(pref("comic_captions")))
        gf.addRow("Show character names", self.g_captions)
        self.g_cap_mode = QComboBox()
        for _lbl, _val in (("Match speaker color", "nick"), ("Fixed color", "fixed")):
            self.g_cap_mode.addItem(_lbl, _val)
        _i = self.g_cap_mode.findData(str(pref("comic_caption_mode") or "nick"))
        self.g_cap_mode.setCurrentIndex(_i if _i >= 0 else 0)
        gf.addRow("Name color", self.g_cap_mode)
        self.g_cap_color = str(pref("comic_caption_color") or "#363636")
        self.g_cap_color_btn = QPushButton("Fixed name color…")
        self.g_cap_color_btn.setStyleSheet(f"color:{self.g_cap_color}")
        self.g_cap_color_btn.clicked.connect(self._pick_caption_color)
        gf.addRow("", self.g_cap_color_btn)
        self.g_cap_size = QComboBox()
        for _lbl, _val in (("Small", 0.8), ("Normal", 1.0), ("Large", 1.3)):
            self.g_cap_size.addItem(_lbl, _val)
        _j = self.g_cap_size.findData(float(pref("comic_caption_scale") or 1.0))
        self.g_cap_size.setCurrentIndex(_j if _j >= 0 else 1)
        gf.addRow("Name size", self.g_cap_size)
        self.g_random_bg = QCheckBox()
        self.g_random_bg.setChecked(bool(pref("comic_random_bg")))
        self.g_random_bg.setToolTip("Channels with no background set get a random (but stable) one.")
        gf.addRow("Random background (unset rooms)", self.g_random_bg)
        note = QLabel("The comic runs in the background for every channel. Use the Characters tab to give "
                      "people a fixed look, the Channels tab for per-room settings, and Bots to keep "
                      "command spam out.")
        note.setWordWrap(True)
        gf.addRow(note)
        return g

    def _pick_caption_color(self) -> None:
        from PySide6.QtGui import QColor
        from PySide6.QtWidgets import QColorDialog
        c = QColorDialog.getColor(QColor(self.g_cap_color), self, "Fixed name color")
        if c.isValid():
            self.g_cap_color = c.name()
            self.g_cap_color_btn.setStyleSheet(f"color:{self.g_cap_color}")

    # ---- Characters ------------------------------------------------------
    def _build_characters(self) -> QWidget:
        w = QWidget()
        v = QVBoxLayout(w)
        v.addWidget(QLabel("Assign a character to a nick everywhere (per-channel overrides live on the "
                           "Channels tab):"))
        gchars = {str(k).lower(): v2 for k, v2 in (pref("comic_chars") or {}).items()}
        self.g_chars = _CharTable(self._char_stems, gchars, self._members)
        v.addWidget(self.g_chars, 1)
        return w

    # ---- Channels --------------------------------------------------------
    def _build_channels(self) -> QWidget:
        c = QWidget()
        cv = QVBoxLayout(c)
        if self._channels:
            sel = QHBoxLayout()
            sel.addWidget(QLabel("Channel:"))
            self.chan_select = QComboBox()
            for _key, label, _m in self._channels:
                self.chan_select.addItem(label, _key)
            sel.addWidget(self.chan_select, 1)
            cv.addLayout(sel)
            cv.addWidget(QLabel("Overrides for this channel — blank fields fall back to Global."))
            self._chan_host = QWidget()  # the bg + char-table are rebuilt here when you switch channel
            self._chan_hlay = QVBoxLayout(self._chan_host)
            self._chan_hlay.setContentsMargins(0, 0, 0, 0)
            cv.addWidget(self._chan_host, 1)
            self.chan_select.currentIndexChanged.connect(self._on_chan_changed)
            self._load_channel(0)
        else:
            cv.addWidget(QLabel("Join a channel to set its comic background / characters here."))
            cv.addStretch(1)
        return c

    # ---- Bots ------------------------------------------------------------
    def _build_bots(self) -> QWidget:
        b = QWidget()
        v = QVBoxLayout(b)
        self.g_ignore_cmds = _checkbox("Filter bot commands and corrections", bool(pref("comic_ignore_cmds")))
        v.addWidget(self.g_ignore_cmds)

        pbox = QGroupBox("Command prefixes — a message starting with one is skipped")
        pv = QVBoxLayout(pbox)
        self.bot_patterns = _ListEditor(
            [str(p) for p in (pref("comic_bot_patterns") or [])],
            "Add prefix", "Prefix or token (e.g. !  .  ~  @  ?  s/):")
        pv.addWidget(self.bot_patterns)
        hint = QLabel("A single character (! . ~ @ ?) only counts when a letter follows it, so \"...\" and "
                      "\":)\" still draw. \"s/\" catches s/typo/fix/ corrections.")
        hint.setWordWrap(True)
        pv.addWidget(hint)
        v.addWidget(pbox, 1)

        nbox = QGroupBox("Bot nicknames — hidden from the comic everywhere")
        nv = QVBoxLayout(nbox)
        self.bot_nicks = _ListEditor(
            [str(n) for n in (pref("comic_ignore") or [])], "Add bot", "Bot nickname to hide from the comic:")
        nv.addWidget(self.bot_nicks)
        v.addWidget(nbox, 1)

        adv = QHBoxLayout()
        adv.addWidget(QLabel("Advanced regex"))
        self.g_exclude = QLineEdit(str(pref("comic_exclude_regex") or ""))
        self.g_exclude.setPlaceholderText("optional — messages matching this regex are kept out of the comic")
        adv.addWidget(self.g_exclude, 1)
        v.addLayout(adv)
        return b

    # ---- per-channel editor ----------------------------------------------
    def _load_channel(self, idx: int) -> None:
        """Show the bg + character editor for channels[idx], populated from the working overrides."""
        if not (0 <= idx < len(self._channels)):
            return
        self._cur_idx = idx
        key, _label, members = self._channels[idx]
        cc = self._chan_work.get(key, {})
        while self._chan_hlay.count():  # clear the previous channel's widgets
            item = self._chan_hlay.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
        holder = QWidget()
        form = QFormLayout(holder)
        self.c_bg = self._bg_combo(str(cc.get("bg") or ""), use_global=True)
        form.addRow("Background", self.c_bg)
        form.addRow(QLabel("Assign characters to nicks (this channel):"))
        self.c_chars = _CharTable(self._char_stems, cc.get("chars") or {}, members)
        form.addRow(self.c_chars)
        self.c_ignore = QLineEdit(" ".join(cc.get("ignore") or []))
        self.c_ignore.setPlaceholderText("nicks hidden from the comic in this channel — still shown in chat")
        form.addRow("Hide from comic", self.c_ignore)
        self._chan_hlay.addWidget(holder)

    def _save_current_channel(self) -> None:
        """Capture the visible channel editor's state into the working dict (before switching/saving)."""
        if self._cur_idx < 0 or self.c_bg is None:
            return
        key = self._channels[self._cur_idx][0]
        self._chan_work[key] = {
            "bg": self.c_bg.currentData() or "",
            "chars": self.c_chars.values(),
            "ignore": _parse_nicks(self.c_ignore.text()) if self.c_ignore is not None else [],
        }

    def _on_chan_changed(self, idx: int) -> None:
        self._save_current_channel()  # keep edits when switching channels
        self._load_channel(idx)

    # ---- shared combos ---------------------------------------------------
    def _bg_combo(self, current_name: str, use_global: bool) -> QComboBox:
        c = QComboBox()
        c.addItem("(use global default)" if use_global else "(first available)", "")
        for stem, fname in self._backgrounds:
            c.addItem(stem, fname)
        i = c.findData(current_name)
        if i >= 0:
            c.setCurrentIndex(i)
        return c

    def _browse_art(self) -> None:
        from PySide6.QtWidgets import QFileDialog
        d = QFileDialog.getExistingDirectory(self, "Comic art folder", self.g_art.text())
        if d:
            self.g_art.setText(d)
            self._rescan(d)

    def _rescan(self, folder: str) -> None:
        """Re-read the art folder and repopulate the Global background + your-character pickers live.
        (The Characters/Channels tables keep their current options — reopen to refresh those.)"""
        from maxchat.comic import assets
        bgs, chars = assets.scan_art_dir(folder) if folder else ([], [])
        self._backgrounds = [(p.stem, p.name) for p in bgs]
        self._char_stems = [p.stem for p in chars]
        bg_cur, self_cur = self.g_bg.currentData(), self.g_self.currentData()
        self.g_bg.clear()
        self.g_bg.addItem("(first available)", "")
        for stem, fname in self._backgrounds:
            self.g_bg.addItem(stem, fname)
        self.g_self.clear()
        self.g_self.addItem("(random per nick)", "")
        for s in self._char_stems:
            self.g_self.addItem(s, s)
        for combo, cur in ((self.g_bg, bg_cur), (self.g_self, self_cur)):
            i = combo.findData(cur)
            if i >= 0:
                combo.setCurrentIndex(i)

    def _char_combo_simple(self, current_stem: str, label: str) -> QComboBox:
        c = QComboBox()
        c.addItem(label, "")
        for s in self._char_stems:
            c.addItem(s, s)
        i = c.findData(current_stem)
        if i >= 0:
            c.setCurrentIndex(i)
        return c

    # ---- save ------------------------------------------------------------
    def _save(self) -> None:
        set_pref("comic_art_dir", self.g_art.text().strip())
        set_pref("comic_bg", self.g_bg.currentData() or "")
        set_pref("comic_self_char", self.g_self.currentData() or "")
        set_pref("comic_panels", self.g_panels.value())
        set_pref("comic_per_panel", self.g_bubbles.value())
        set_pref("comic_min_font", self.g_minfont.value())
        set_pref("comic_captions", self.g_captions.isChecked())
        set_pref("comic_caption_mode", self.g_cap_mode.currentData())
        set_pref("comic_caption_color", self.g_cap_color)
        set_pref("comic_caption_scale", float(self.g_cap_size.currentData()))
        set_pref("comic_random_bg", self.g_random_bg.isChecked())
        set_pref("comic_ignore_cmds", self.g_ignore_cmds.isChecked())
        set_pref("comic_bot_patterns", self.bot_patterns.values())
        set_pref("comic_exclude_regex", self.g_exclude.text().strip())
        set_pref("comic_ignore", _parse_nicks(" ".join(self.bot_nicks.values())))  # comic-only, everywhere
        set_pref("comic_chars", self.g_chars.values())
        if self._channels:
            self._save_current_channel()
            # keep only non-empty overrides (channels you never touched stay as they were)
            chans = {k: {kk: vv for kk, vv in v.items() if vv}  # drop empty bg/chars/ignore fields
                     for k, v in self._chan_work.items() if v.get("bg") or v.get("chars") or v.get("ignore")}
            set_pref("comic_channels", chans)
        self.accept()


def _checkbox(label: str, checked: bool):
    from PySide6.QtWidgets import QCheckBox
    cb = QCheckBox(label)
    cb.setChecked(checked)
    return cb
