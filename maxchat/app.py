"""Application bootstrap: build the QApplication, apply the theme, show the main window."""

from __future__ import annotations

import os
import sys

from PySide6.QtCore import QEvent, QObject, Qt
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QApplication,
    QGraphicsDropShadowEffect,
    QMenu,
    QProxyStyle,
    QStyle,
    QWidget,
)

from maxchat import __app_name__, __version__
from maxchat.ui.main_window import MainWindow


class _SnappyStyle(QProxyStyle):
    """Fusion, but menus/tooltips appear instantly — no submenu hover delay. Together with the
    disabled menu animations below this fixes the sluggish, laggy menus Qt shows on WSL/Wayland.
    """

    def __init__(self, *args, fast_menus: bool = False) -> None:
        super().__init__(*args)
        self._fast_menus = fast_menus

    def styleHint(self, hint, option=None, widget=None, returnData=None):
        if self._fast_menus and hint == QStyle.StyleHint.SH_Menu_SubMenuPopupDelay:
            return 0  # open submenus the instant the mouse is over them (no hover delay)
        if self._fast_menus and hint in (
            QStyle.StyleHint.SH_Menu_MouseTracking,
            QStyle.StyleHint.SH_MenuBar_MouseTracking,
            QStyle.StyleHint.SH_ComboBox_ListMouseTracking,
            QStyle.StyleHint.SH_ComboBox_ListMouseTracking_Active,
        ):
            return 1  # highlight menu/dropdown items immediately as the pointer moves
        if hint == QStyle.StyleHint.SH_ToolTip_WakeUpDelay:
            return 200  # snappier tooltips
        return super().styleHint(hint, option, widget, returnData)


class _PopupShadow(QObject):
    """Give simple frameless popups — combo-box dropdowns and tooltips — a soft drop shadow.
    WSLg/Wayland/WSL sometimes don't draw shadows for popup windows, so we render one client-side:
    the popup is made translucent (the QSS gives it a small transparent margin) and a blur effect
    paints the shadow into that margin. Menus are left native because Qt's platform mouse-grab logic
    is fragile there under WSLg.
    """

    def eventFilter(self, obj, event) -> bool:
        if event.type() in (QEvent.Type.Polish, QEvent.Type.Show) and isinstance(obj, QWidget):
            if (obj.graphicsEffect() is None and obj.isWindow()
                    and not isinstance(obj, QMenu)
                    and obj.objectName() != "colorPicker"
                    and obj.windowType() in (Qt.WindowType.Popup, Qt.WindowType.ToolTip)):
                obj.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground, True)
                shadow = QGraphicsDropShadowEffect(obj)
                shadow.setBlurRadius(16)
                shadow.setColor(QColor(0, 0, 0, 150))
                shadow.setOffset(0, 3)
                obj.setGraphicsEffect(shadow)
        return False


def _migrate_font_defaults() -> None:
    """Bring older configs onto the explicit bundled JetBrains Mono bold default profile.

    Blank font prefs used to mean "resolve the default later", which made Linux builds hard to
    reason about and could leave the chat area looking like the platform default. Only blank/unset
    font family/size values are changed here; a deliberate nonblank font choice stays put.
    """
    from maxchat import config
    from maxchat.ui import fonts

    data = config.load_settings()
    if int(data.get("font_defaults_version") or 0) >= 3:
        return
    family = fonts.default_mono()
    for key in (
        "app_font_family", "chat_font_family", "list_font_family",
        "nick_font_family", "status_font_family", "topic_font_family",
    ):
        if not str(data.get(key) or "").strip():
            data[key] = family
    for key in (
        "app_font_size", "chat_font_size", "list_font_size",
        "nick_font_size", "status_font_size", "topic_font_size",
    ):
        if not int(data.get(key) or 0):
            data[key] = 14
    for key in (
        "app_font_bold", "chat_font_bold", "list_font_bold",
        "nick_font_bold", "status_font_bold", "topic_font_bold",
    ):
        data[key] = True
    data["font_default_migrated"] = True
    data["font_defaults_version"] = 3
    try:
        config.save_settings(data)
    except OSError:
        pass


def _apply_app_font(app) -> None:
    """Set the UI/chrome font: the saved one, else the bundled JetBrains Mono @ 14 (the default look)."""
    from PySide6.QtGui import QFont

    from maxchat import config
    from maxchat.ui import fonts

    fam = str(config.pref("app_font_family") or "") or fonts.default_mono()
    size = int(config.pref("app_font_size") or 0) or 14
    bold = bool(config.pref("app_font_bold"))
    f = QFont(fam, size)
    f.setBold(bold)
    app.setFont(f)


_USAGE = f"""{__app_name__} {__version__} — a comic-strip-style IRC client

Usage: maxchat [options]
  (no options)   start the client
  --selftest     check bundled runtime assets headless and exit 0 — no display needed
  --version, -V  print the version and exit
  --help, -h     show this message
"""


def _out(msg: str, err: bool = False) -> None:
    """Print safely — a windowed (console=False) build has no stdout/stderr, so guard against None."""
    stream = sys.stderr if err else sys.stdout
    try:
        if stream is not None:
            stream.write(msg + "\n")
            stream.flush()
    except Exception:
        pass


def main(argv: list[str] | None = None) -> int:
    # Quiet Qt's "Using Qt multimedia with FFmpeg …" info line (and other multimedia chatter).
    os.environ.setdefault("QT_LOGGING_RULES", "qt.multimedia.ffmpeg.info=false;qt.multimedia.info=false")
    raw = list(sys.argv if argv is None else argv)
    flags = set(raw[1:])
    if flags & {"--version", "-V"}:
        _out(f"{__app_name__} {__version__}")
        return 0
    if flags & {"--help", "-h"}:
        _out(_USAGE)
        return 0
    selftest = "--selftest" in flags
    if selftest:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")  # no display required
        raw = [a for a in raw if a != "--selftest"]

    app = QApplication(raw)
    app.setApplicationName(__app_name__)
    app.setApplicationVersion(__version__)
    app.setOrganizationName("maxchat")  # app identity only; config path is config.config_dir()
    from maxchat.ui import fonts, localization, theme
    fast_menus = theme.needs_stale_menu_workaround()
    app.setStyle(_SnappyStyle("Fusion", fast_menus=fast_menus))  # Fusion base; WSLg gets instant submenus
    localization.install_translator(app)
    fonts.register_bundled_fonts()  # JetBrains Mono + Symbols Nerd Font, so they're selectable everywhere
    if theme.needs_client_popup_shadow():
        app._popup_shadow = _PopupShadow(app)  # client-side shadow for frameless popups
        app.installEventFilter(app._popup_shadow)
    # The WSLg stale-menu workaround is installed on the menu bar only. Keep it out of the
    # app-wide event filter path so normal Windows menus and non-menu-bar popups behave natively.

    # No fade/slide on menus & tooltips — the animation reads as lag on WSL/Wayland.
    for effect in (
        Qt.UIEffect.UI_AnimateMenu,
        Qt.UIEffect.UI_FadeMenu,
        Qt.UIEffect.UI_AnimateCombo,
        Qt.UIEffect.UI_AnimateTooltip,
        Qt.UIEffect.UI_FadeTooltip,
    ):
        app.setEffectEnabled(effect, False)

    from maxchat import config
    from maxchat.ui import theme

    theme.load_user_themes()  # register any user themes/*.json before applying one
    theme.load_user_chat_themes()  # + user chat themes (chat_themes.json)
    _migrate_font_defaults()  # drop legacy auto-saved system fonts so the JetBrains Mono default applies
    mode = config.get_setting("theme", "dark")
    app.setPalette(theme.palette(mode))  # so the OS palette can't bleed into ours
    app.setStyleSheet(theme.stylesheet(mode))
    # Apply the UI font AFTER the stylesheet: an app-wide QSS otherwise shadows QApplication.setFont(),
    # so styled widgets fall back to the style default (Segoe UI on Windows) even though app.font()
    # still reports JetBrains. Setting it last makes the chrome actually use it.
    _apply_app_font(app)

    if selftest:  # exercise bundled runtime assets headless, then exit
        from maxchat.comic import renderer
        from maxchat.ui import fonts

        fam = renderer._comic_family()  # registers assets/fonts; "" if it wasn't bundled in a build
        if not fam:
            _out(f"selftest FAILED: bundled comic font not found (looked in {renderer._FONTS_DIR})", err=True)
            return 1
        mono = "JetBrains Mono" if fonts.mono_loaded() else f"NOT LOADED (looked in {fonts._fonts_dir()})"
        try:
            from spellchecker import SpellChecker
            spelling = "OK" if "wrld" in SpellChecker(language="en").unknown(["wrld"]) else "FAILED"
        except Exception as exc:
            spelling = f"FAILED ({exc})"
        if not spelling.startswith("OK"):
            _out(f"selftest FAILED: spellcheck dictionary not available: {spelling}", err=True)
            return 1
        bold = "bold" if app.font().bold() else "regular"
        _out(f"{__app_name__} {__version__} selftest OK — comic font: {fam} · "
             f"UI font: {app.font().family()} {bold} · bundled mono: {mono} · spellcheck: {spelling}")
        return 0

    window = MainWindow()
    window.show()
    return app.exec()
