"""The message input — a QLineEdit with input history (↑/↓), Tab-completion of nicks/commands,
and mIRC formatting keys (Ctrl+B/I/U/K/O/R inserts the control codes so you can send formatting).
"""

from __future__ import annotations

import re

from PySide6.QtCore import QEvent, Qt, Signal
from PySide6.QtGui import QImage
from PySide6.QtWidgets import QLineEdit

_IMG_EXT = (".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".apng")
_WORD_RE = re.compile(r"[^\W\d_][^\W\d_']{1,}", re.UNICODE)
_SKIP_TOKEN_PREFIXES = ("/", "#", "@", "http://", "https://", "www.")

# Ctrl+key → mIRC control character to insert. (Ctrl+K is NOT here — it opens the colour picker, a
# rebindable shortcut handled by the main window.)
_FMT = {
    Qt.Key.Key_B: "\x02",  # bold
    Qt.Key.Key_I: "\x1d",  # italic
    Qt.Key.Key_U: "\x1f",  # underline
    Qt.Key.Key_O: "\x0f",  # reset
    Qt.Key.Key_R: "\x16",  # reverse
}


class InputBar(QLineEdit):
    multiLinePaste = Signal(list)   # emitted (instead of inserting) when a large paste is guarded
    imagePasted = Signal(QImage)    # emitted when an image is pasted — the window uploads + links it

    def __init__(self, parent=None, completions=None) -> None:
        super().__init__(parent)
        self._history: list[str] = []
        self._hidx = 0
        self._pending = ""
        self._completions = completions or (lambda: [])
        self._comp: dict | None = None
        self.paste_guard = True
        self.paste_lines = 4
        self.emit_image_paste = True   # paste an image → emit imagePasted (a plugin can upload + link it)
        self._spell_enabled = False
        self._spell_language = "en"
        self._spell = None
        self._misspelled: set[str] = set()
        self.textChanged.connect(lambda _text: self._refresh_spellcheck())

    def set_spellcheck(self, enabled: bool, language: str = "en") -> None:
        """Enable pure-Python dictionary checks for the typed message."""
        self._spell_enabled = bool(enabled)
        self._spell_language = language or "en"
        self._spell = None
        self._refresh_spellcheck()

    def misspelled_words(self, text: str | None = None) -> list[str]:
        """Misspelled words in display order, used by the live UI and tests."""
        if not self._spell_enabled:
            return []
        spell = self._spellchecker()
        if spell is None:
            return []
        words = [word for word in self._words(text if text is not None else self.text()) if word.isalpha()]
        if not words:
            return []
        unknown = set(spell.unknown(words))
        seen = set()
        ordered = []
        for word in words:
            key = word.lower()
            if key in unknown and key not in seen:
                ordered.append(word)
                seen.add(key)
        return ordered

    def _spellchecker(self):
        if self._spell is False:
            return None
        if self._spell is not None:
            return self._spell
        try:
            from spellchecker import SpellChecker

            self._spell = SpellChecker(language=self._spell_language)
        except Exception:
            self._spell = False
        return None if self._spell is False else self._spell

    def _words(self, text: str) -> list[str]:
        words = []
        for match in _WORD_RE.finditer(text):
            word = match.group(0)
            start = match.start()
            token_start = text.rfind(" ", 0, start) + 1
            token_end = text.find(" ", match.end())
            if token_end < 0:
                token_end = len(text)
            token = text[token_start:token_end].lower()
            if token.startswith(_SKIP_TOKEN_PREFIXES):
                continue
            words.append(word.lower())
        return words

    def _refresh_spellcheck(self) -> None:
        words = self.misspelled_words()
        self._misspelled = {word.lower() for word in words}
        has_error = bool(words)
        if self.property("spellError") != has_error:
            self.setProperty("spellError", has_error)
            self.style().unpolish(self)
            self.style().polish(self)
        self.setToolTip("Possible spelling: " + ", ".join(words[:5]) if has_error else "")

    def contextMenuEvent(self, event) -> None:
        menu = self.createStandardContextMenu()
        word, start, end = self._word_at(self.cursorPosition())
        suggestions = self._suggestions(word)
        if suggestions:
            menu.insertSeparator(menu.actions()[0] if menu.actions() else None)
            for suggestion in reversed(suggestions):
                action = menu.insertAction(menu.actions()[0] if menu.actions() else None, suggestion)
                action.triggered.connect(
                    lambda _checked=False, s=suggestion, a=start, b=end: self._replace_word(a, b, s)
                )
        menu.exec(event.globalPos())

    def _word_at(self, pos: int) -> tuple[str, int, int]:
        text = self.text()
        for match in _WORD_RE.finditer(text):
            if match.start() <= pos <= match.end():
                return match.group(0), match.start(), match.end()
        return "", -1, -1

    def _suggestions(self, word: str) -> list[str]:
        if not word or word.lower() not in self._misspelled:
            return []
        spell = self._spellchecker()
        if spell is None:
            return []
        suggestions = []
        for suggestion in spell.candidates(word.lower()) or ():
            fixed = suggestion.capitalize() if word[:1].isupper() else suggestion
            if fixed != word and fixed not in suggestions:
                suggestions.append(fixed)
            if len(suggestions) >= 5:
                break
        return suggestions

    def _replace_word(self, start: int, end: int, replacement: str) -> None:
        if start < 0 or end < start:
            return
        text = self.text()
        self.setText(text[:start] + replacement + text[end:])
        self.setCursorPosition(start + len(replacement))

    def insertFromMimeData(self, source) -> None:
        """Paste handling: an image → emit ``imagePasted`` (an upload plugin can handle it); a big
        multi-line text paste → hand it to the window's paste guard; otherwise the normal inline paste."""
        if source is not None and self.emit_image_paste:
            img = self._image_from(source)
            if img is not None:
                self.imagePasted.emit(img)
                return
        text = source.text() if source is not None else ""
        lines = [ln for ln in text.splitlines() if ln.strip()]
        if self.paste_guard and len(lines) >= max(2, self.paste_lines):
            self.multiLinePaste.emit(lines)
            return
        super().insertFromMimeData(source)

    @staticmethod
    def _image_from(source) -> QImage | None:
        """A QImage from the clipboard: raw image data (a screenshot) or a pasted local image file."""
        if source.hasImage():
            img = source.imageData()
            if isinstance(img, QImage) and not img.isNull():
                return img
        if source.hasUrls():
            for u in source.urls():
                if u.isLocalFile() and u.toLocalFile().lower().endswith(_IMG_EXT):
                    img = QImage(u.toLocalFile())
                    if not img.isNull():
                        return img
        return None

    def remember(self, text: str) -> None:
        if text and (not self._history or self._history[-1] != text):
            self._history.append(text)
        self._hidx = len(self._history)
        self._pending = ""

    def event(self, e):
        # Grab Tab/Backtab before Qt's focus navigation eats them.
        if e.type() == QEvent.Type.KeyPress and e.key() in (Qt.Key.Key_Tab, Qt.Key.Key_Backtab):
            self._complete(back=e.key() == Qt.Key.Key_Backtab)
            return True
        return super().event(e)

    def keyPressEvent(self, e) -> None:
        key = e.key()
        if key == Qt.Key.Key_Up:
            self._comp = None
            self._history_move(-1)
            return
        if key == Qt.Key.Key_Down:
            self._comp = None
            self._history_move(1)
            return
        if e.modifiers() & Qt.KeyboardModifier.ControlModifier and key in _FMT:
            self._comp = None
            self.insert(_FMT[key])
            return
        self._comp = None
        super().keyPressEvent(e)

    def _history_move(self, delta: int) -> None:
        if not self._history:
            return
        if self._hidx == len(self._history):
            self._pending = self.text()
        self._hidx = max(0, min(len(self._history), self._hidx + delta))
        self.setText(self._pending if self._hidx == len(self._history) else self._history[self._hidx])
        self.end(False)

    def _complete(self, back: bool = False) -> None:
        text = self.text()
        pos = self.cursorPosition()
        c = self._comp
        if c and c["end"] == pos:  # continue cycling through matches
            c["idx"] = (c["idx"] + (-1 if back else 1)) % len(c["matches"])
            seg_start, seg_end = c["start"], c["end"]
        else:
            start = pos
            while start > 0 and text[start - 1] != " ":
                start -= 1
            base = text[start:pos]
            if not base:
                self._comp = None
                return
            matches = [x for x in self._completions() if x.lower().startswith(base.lower())]
            if not matches:
                self._comp = None
                return
            c = {"start": start, "matches": matches, "idx": 0}
            seg_start, seg_end = start, pos
        match = c["matches"][c["idx"]]
        suffix = ": " if c["start"] == 0 and not match.startswith("/") else " "  # nick at line start → "nick: "
        inserted = match + suffix
        self.setText(text[:seg_start] + inserted + text[seg_end:])
        c["end"] = seg_start + len(inserted)
        self.setCursorPosition(c["end"])
        self._comp = c
