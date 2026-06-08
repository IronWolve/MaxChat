"""Bundled UI fonts + the chat-font fallback chain.

We ship **JetBrains Mono** (SIL OFL) as the default monospace, and **Symbols Nerd Font Mono** (MIT)
as a glyph/icon fallback, so a clean mono renders the same on Windows and Linux and nerd-style symbols
resolve without the user installing anything. Colour emoji fall back to the per-OS emoji font.

Path resolution is PyInstaller-aware (``sys._MEIPASS`` when frozen) so the TTFs load from the bundle,
and we use the family name Qt *actually* registers (not a hardcoded string) so a tiny naming
difference between platforms can't make the default mono silently fall back to a system font.
"""

from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtGui import QFontDatabase

_BUNDLED = (
    "JetBrainsMono-Regular.ttf",
    "JetBrainsMono-Bold.ttf",
    "SymbolsNerdFontMono-Regular.ttf",
)

MONO = "JetBrains Mono"               # expected family name of the bundled default monospace (OFL)
SYMBOLS = "Symbols Nerd Font Mono"    # expected family name of the bundled nerd-glyph fallback (MIT)
EMOJI = ("Noto Color Emoji", "Segoe UI Emoji", "Apple Color Emoji", "Noto Emoji")

_registered = False
_mono_family = ""      # the family name Qt actually registered for JetBrains Mono ("" = failed to load)
_symbols_family = ""   # …and for Symbols Nerd Font Mono


def _fonts_dir() -> Path:
    """assets/fonts — from the PyInstaller bundle when frozen, else the dev checkout."""
    base = getattr(sys, "_MEIPASS", None)
    root = Path(base) if base else Path(__file__).resolve().parents[2]
    return root / "assets" / "fonts"


def register_bundled_fonts() -> None:
    """Register the bundled fonts with Qt once (safe to call repeatedly). Records the family name Qt
    assigns to each, so the rest of the app references the *real* names."""
    global _registered, _mono_family, _symbols_family
    if _registered:
        return
    _registered = True
    d = _fonts_dir()
    for name in _BUNDLED:
        fid = QFontDatabase.addApplicationFont(str(d / name))
        if fid < 0:
            continue
        fams = QFontDatabase.applicationFontFamilies(fid)
        if not fams:
            continue
        low = fams[0].lower()
        if "jetbrains" in low and not _mono_family:
            _mono_family = fams[0]
        elif "symbols nerd" in low and not _symbols_family:
            _symbols_family = fams[0]


def mono_loaded() -> bool:
    """True if the bundled JetBrains Mono registered (diagnostic / selftest)."""
    register_bundled_fonts()
    return bool(_mono_family) or MONO in QFontDatabase.families()


def default_mono() -> str:
    """The bundled default mono family if it registered, else the system fixed-width font."""
    register_bundled_fonts()
    if _mono_family:
        return _mono_family
    if MONO in QFontDatabase.families():  # registered under the expected name
        return MONO
    return QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont).family()


def _symbols() -> str:
    register_bundled_fonts()
    return _symbols_family or SYMBOLS


def fallback_chain(primary: str) -> list[str]:
    """primary family → nerd symbols → colour emoji, so glyphs/icons/emoji all resolve."""
    chain = [primary] if primary else []
    for fam in (_symbols(), *EMOJI):
        if fam not in chain:
            chain.append(fam)
    return chain
