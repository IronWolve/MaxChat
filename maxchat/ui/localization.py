"""Localization helpers for bundled Qt translations."""

from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtCore import QLocale, QTranslator
from PySide6.QtWidgets import QApplication

from maxchat import config


LANGUAGES: tuple[tuple[str, str], ...] = (
    ("system", "System default"),
    ("en", "English"),
    ("ar", "Arabic"),
    ("de", "German"),
    ("es", "Spanish"),
    ("fr", "French"),
    ("hi", "Hindi"),
    ("it", "Italian"),
    ("ja", "Japanese"),
    ("ko", "Korean"),
    ("nl", "Dutch"),
    ("pl", "Polish"),
    ("pt_BR", "Portuguese (Brazil)"),
    ("ru", "Russian"),
    ("tr", "Turkish"),
    ("uk", "Ukrainian"),
    ("zh_CN", "Chinese (Simplified)"),
    ("zh_TW", "Chinese (Traditional)"),
)

SUPPORTED_SPELL_LANGUAGES = {
    "en", "es", "de", "fr", "pt", "it", "nl", "ru", "lv", "eu", "fa",
}

SPELL_LANGUAGES: tuple[tuple[str, str], ...] = (
    ("en", "English"),
    ("ar", "Arabic [dictionary not bundled]"),
    ("eu", "Basque"),
    ("cs_CZ", "Czech [dictionary not bundled]"),
    ("da_DK", "Danish [dictionary not bundled]"),
    ("de", "German"),
    ("el_GR", "Greek [dictionary not bundled]"),
    ("es", "Spanish"),
    ("fa", "Persian"),
    ("fi_FI", "Finnish [dictionary not bundled]"),
    ("fr", "French"),
    ("he_IL", "Hebrew [dictionary not bundled]"),
    ("hi_IN", "Hindi [dictionary not bundled]"),
    ("hu_HU", "Hungarian [dictionary not bundled]"),
    ("id_ID", "Indonesian [dictionary not bundled]"),
    ("it", "Italian"),
    ("ja_JP", "Japanese [dictionary not bundled]"),
    ("ko_KR", "Korean [dictionary not bundled]"),
    ("lv", "Latvian"),
    ("nl", "Dutch"),
    ("no_NO", "Norwegian [dictionary not bundled]"),
    ("pl_PL", "Polish [dictionary not bundled]"),
    ("pt", "Portuguese"),
    ("ro_RO", "Romanian [dictionary not bundled]"),
    ("ru", "Russian"),
    ("sv_SE", "Swedish [dictionary not bundled]"),
    ("th_TH", "Thai [dictionary not bundled]"),
    ("tr_TR", "Turkish [dictionary not bundled]"),
    ("uk_UA", "Ukrainian [dictionary not bundled]"),
    ("vi_VN", "Vietnamese [dictionary not bundled]"),
    ("zh_CN", "Chinese (Simplified) [dictionary not bundled]"),
    ("zh_TW", "Chinese (Traditional) [dictionary not bundled]"),
)


def spell_dictionary_available(code: str) -> bool:
    return code in SUPPORTED_SPELL_LANGUAGES


def translations_dir() -> Path:
    base = getattr(sys, "_MEIPASS", None)
    root = Path(base) if base else Path(__file__).resolve().parents[2]
    return root / "assets" / "translations"


def language_code() -> str:
    code = str(config.pref("interface_language") or "system")
    if code == "system":
        system_name = QLocale.system().name()
        supported = {code for code, _label in LANGUAGES}
        if system_name in supported:
            return system_name
        code = system_name.split("_", 1)[0].lower()
    return code or "en"


def install_translator(app: QApplication) -> None:
    code = language_code()
    if code == "en":
        return
    qm = translations_dir() / f"maxchat_{code}.qm"
    if not qm.is_file():
        return
    translator = QTranslator(app)
    if translator.load(str(qm)):
        app.installTranslator(translator)
        app._maxchat_translator = translator
