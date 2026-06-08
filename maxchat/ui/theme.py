"""Theming — dark/light + common palettes (Dracula, Nord, Solarized, Gruvbox) and terminal
"classic" looks, applied via Qt QSS + QPalette. Mirrors the StreamTuner-ng approach.

A theme is a structural palette. ``accent`` is the "on/active" highlight (green-family per theme,
so "lit = on" stays true — our button rule); ``on``/``on_text`` are selection/hover colours;
``dark`` drives :func:`is_dark`. A theme MAY also set ``chat_bg``/``chat_fg`` to give the chat text
area its own colours (e.g. a black "terminal" chat); otherwise the chat uses ``panel``/``text``.
Users can drop their own ``themes/*.json`` in the config dir (see THEME-HOWTO.md) — a bad theme
file is skipped, never crashes startup.
"""

from __future__ import annotations

import platform
from pathlib import Path

_WP_DIR = Path(__file__).resolve().parents[2] / "assets" / "wallpapers"


def wallpaper_path(name: str) -> str:
    """Resolve a wallpaper to an absolute path (forward slashes, for QSS url()), '' if missing.
    Accepts a bundled filename (in assets/wallpapers/) **or** an absolute path to the user's own image."""
    if not name:
        return ""
    p = Path(name)
    if p.is_absolute():
        return p.as_posix() if p.is_file() else ""
    q = _WP_DIR / name
    return q.as_posix() if q.is_file() else ""


def _effective_wallpaper(mode: str) -> str:
    """The wallpaper to actually use: a user override (custom path, or 'none' = off) wins over the
    theme's own. Empty override → the theme default."""
    try:
        from maxchat import config
        override = str(config.pref("wallpaper") or "")
    except Exception:
        override = ""
    if override == "none":
        return ""
    if override:
        return override
    return _theme(mode).get("wallpaper", "")


def _rgb(c) -> str:
    return f"rgb({c[0]},{c[1]},{c[2]})"


def _rgba(c, a) -> str:
    return f"rgba({c[0]},{c[1]},{c[2]},{a})"


def _hex(c) -> str:
    return "#%02x%02x%02x" % (c[0], c[1], c[2])


GREEN = (80, 230, 150)  # default "on/active" accent
DEFAULT_THEME_ID = "dark"
SYSTEM_THEME_ID = "system"


def _mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def _lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def _term(bg, fg, on, accent, dark=True, chat=None):
    """Build a full app-theme dict from a terminal scheme's bg/fg + a selection (`on`) + `accent`.
    Panels/scrollbar shades are derived by blending bg toward fg (dark) or white (light)."""
    if dark:
        panel, panel2 = _mix(bg, fg, 0.07), _mix(bg, fg, 0.13)
        groove, scroll, scroll_hi = _mix(bg, (0, 0, 0), 0.4), _mix(bg, fg, 0.22), _mix(bg, fg, 0.36)
    else:
        panel, panel2 = _mix(bg, (255, 255, 255), 0.6), _mix(bg, fg, 0.10)
        groove, scroll, scroll_hi = _mix(bg, fg, 0.22), _mix(bg, fg, 0.20), _mix(bg, fg, 0.32)
    d = dict(dark=dark, bg=bg, panel=panel, panel2=panel2, text=fg,
             on=on, on_text=("white" if _lum(on) < 140 else "black"), accent=accent,
             groove=groove, scroll=scroll, scroll_hi=scroll_hi)
    if chat:
        d["chat_bg"], d["chat_fg"] = chat
    return d


THEMES: dict[str, dict] = {
    "dark": dict(dark=True, bg=(28, 30, 33), panel=(37, 40, 44), panel2=(44, 48, 53),
                 text=(224, 226, 229), on=(40, 95, 60), on_text="white", accent=GREEN,
                 groove=(18, 20, 23), scroll=(70, 75, 82), scroll_hi=(98, 104, 112)),
    "light": dict(dark=False, bg=(236, 238, 241), panel=(251, 251, 252), panel2=(225, 228, 233),
                  text=(28, 30, 33), on=(70, 200, 130), on_text="black", accent=GREEN,
                  groove=(148, 154, 162), scroll=(178, 183, 190), scroll_hi=(150, 155, 163)),
    "dracula": dict(dark=True, bg=(40, 42, 54), panel=(52, 55, 70), panel2=(68, 71, 90),
                    text=(248, 248, 242), on=(98, 114, 164), on_text="white", accent=(80, 250, 123),
                    groove=(33, 34, 44), scroll=(68, 71, 90), scroll_hi=(98, 114, 164)),
    "nord": dict(dark=True, bg=(46, 52, 64), panel=(59, 66, 82), panel2=(67, 76, 94),
                 text=(216, 222, 233), on=(94, 129, 172), on_text="white", accent=(163, 190, 140),
                 groove=(39, 44, 53), scroll=(76, 86, 106), scroll_hi=(94, 129, 172)),
    "solarized-dark": dict(dark=True, bg=(0, 43, 54), panel=(7, 54, 66), panel2=(0, 33, 43),
                           text=(147, 161, 161), on=(38, 139, 210), on_text="white", accent=(133, 153, 0),
                           groove=(0, 25, 33), scroll=(88, 110, 117), scroll_hi=(101, 123, 131)),
    "solarized-light": dict(dark=False, bg=(238, 232, 213), panel=(253, 246, 227), panel2=(221, 214, 193),
                            text=(88, 110, 117), on=(38, 139, 210), on_text="white", accent=(133, 153, 0),
                            groove=(213, 206, 185), scroll=(204, 196, 172), scroll_hi=(184, 176, 149)),
    "gruvbox-dark": dict(dark=True, bg=(40, 40, 40), panel=(60, 56, 54), panel2=(80, 73, 69),
                         text=(235, 219, 178), on=(69, 133, 136), on_text="white", accent=(184, 187, 38),
                         groove=(29, 32, 33), scroll=(80, 73, 69), scroll_hi=(102, 92, 84)),
    # Terminal "classic" looks — dark chrome + a black chat with old-school text colours.
    "classic": dict(dark=True, bg=(24, 24, 24), panel=(32, 32, 32), panel2=(40, 40, 40),
                    text=(208, 208, 208), on=(48, 86, 64), on_text="white", accent=GREEN,
                    groove=(14, 14, 14), scroll=(70, 70, 70), scroll_hi=(100, 100, 100),
                    chat_bg=(10, 10, 10), chat_fg=(204, 204, 204)),
    "classic-green": dict(dark=True, bg=(24, 24, 24), panel=(32, 32, 32), panel2=(40, 40, 40),
                          text=(208, 208, 208), on=(40, 90, 50), on_text="white", accent=(0, 210, 0),
                          groove=(14, 14, 14), scroll=(70, 70, 70), scroll_hi=(100, 100, 100),
                          chat_bg=(8, 12, 8), chat_fg=(0, 210, 0)),
    "amber": dict(dark=True, bg=(24, 22, 18), panel=(32, 29, 24), panel2=(40, 36, 30),
                  text=(220, 200, 160), on=(90, 70, 30), on_text="black", accent=(255, 176, 0),
                  groove=(14, 12, 8), scroll=(70, 62, 48), scroll_hi=(100, 88, 64),
                  chat_bg=(12, 10, 6), chat_fg=(255, 176, 0)),
    # Terminal colour schemes (Alacritty) recreated as full app themes — bg/fg exact, chrome derived.
    "cyberpunk": _term((0, 11, 30), (10, 189, 198), on=(28, 97, 194), accent=(211, 0, 196)),
    "monokai-pro": _term((45, 42, 46), (255, 241, 243), on=(168, 169, 235), accent=(173, 218, 120)),
    "tokyo-night": _term((26, 27, 38), (169, 177, 214), on=(122, 162, 247), accent=(158, 206, 106)),
    "ubuntu": _term((48, 10, 36), (238, 238, 236), on=(52, 101, 164), accent=(78, 154, 6)),
    "gruvbox-material": _term((40, 40, 40), (223, 191, 142), on=(125, 174, 163), accent=(169, 182, 101)),
    # Synthwave — neon palette + a sunset-gradient window background.
    "synthwave": {**_term((13, 11, 30), (248, 248, 255), on=(255, 42, 109), accent=(5, 217, 232),
                          chat=((16, 12, 30), (245, 245, 255))),
                  "bg_gradient": [(0.0, (13, 11, 30)), (0.36, (95, 10, 135)), (0.52, (255, 42, 109)),
                                  (0.57, (10, 6, 20)), (1.0, (5, 3, 10))],
                  "wallpaper": "synthwave.png"},  # sunset + neon grid behind the (translucent) chrome
    # Vaporwave — the user's own generated wallpaper (assets/wallpapers/vaporwave-2.jpg).
    "vaporwave": {**_term((8, 6, 16), (240, 240, 255), on=(255, 60, 150), accent=(0, 229, 232),
                          chat=((12, 9, 22), (240, 240, 255))),
                  "wallpaper": "vaporwave-2.jpg"},
}

# display label for the View → Theme menu (insertion order = menu order)
THEME_LABELS: dict[str, str] = {
    DEFAULT_THEME_ID: "Default",
    SYSTEM_THEME_ID: "Themes Off (system default)",
    "light": "Light", "dracula": "Dracula", "nord": "Nord",
    "solarized-dark": "Solarized Dark", "solarized-light": "Solarized Light",
    "gruvbox-dark": "Gruvbox Dark",
    "cyberpunk": "Cyberpunk", "monokai-pro": "Monokai Pro", "tokyo-night": "Tokyo Night",
    "ubuntu": "Ubuntu", "gruvbox-material": "Gruvbox Material", "synthwave": "Synthwave",
    "vaporwave": "Vaporwave",
    "classic": "Classic (terminal)", "classic-green": "Classic Green", "amber": "Amber",
}

# Chat-area themes — independent of the app/chrome theme. "follow" = use the app theme's chat colours.
CHAT_THEMES: dict[str, dict] = {
    "follow": {"label": "Follow app theme"},
    "terminal": {"label": "Terminal — grey on black", "bg": (10, 10, 10), "fg": (208, 208, 208), "fixed": True},
    "green": {"label": "Terminal — green on black", "bg": (8, 12, 8), "fg": (0, 210, 0), "fixed": True},
    "amber": {"label": "Terminal — amber on black", "bg": (12, 10, 6), "fg": (255, 176, 0), "fixed": True},
    # irssi default look: clean black, light-grey text, grey timestamps + brackets, MONOCHROME nicks
    # (irssi doesn't rainbow-colour nicks by default), calm blue-grey system lines.
    "irssi": {"label": "irssi — clean (mono nicks)", "bg": (0, 0, 0), "fg": (192, 192, 192),
              "fixed": True, "ts": (95, 95, 110), "bracket": (120, 120, 120), "nicks": "mono",
              "system": (108, 132, 168)},
    # BitchX default look: loud. Black, bright text, cyan timestamps, green brackets, bright cyan
    # system lines, and a vivid bright-ANSI nick palette (BitchX colours everything).
    "bitchx": {"label": "BitchX — bright & loud", "bg": (0, 0, 0), "fg": (205, 205, 205),
               "fixed": True, "ts": (0, 200, 200), "bracket": (0, 205, 0), "system": (0, 200, 200),
               "nicks": [(0, 255, 255), (0, 255, 0), (255, 255, 0), (255, 0, 255), (90, 130, 255),
                         (255, 90, 90), (255, 255, 255), (255, 156, 0), (90, 255, 200), (180, 120, 255)]},
    "white": {"label": "White — proportional", "bg": (255, 255, 255), "fg": (24, 24, 24), "fixed": False},
    "paper": {"label": "Paper — sepia", "bg": (250, 247, 238), "fg": (43, 40, 33), "fixed": False},
}
CHAT_THEME_LABELS = {k: v["label"] for k, v in CHAT_THEMES.items()}

BUILTIN_THEMES = tuple(THEMES)  # users can't overwrite these
_THEME_RGB_KEYS = ("bg", "panel", "panel2", "text", "on", "accent", "groove", "scroll", "scroll_hi")


def _theme(mode: str) -> dict:
    return THEMES.get(mode, THEMES[DEFAULT_THEME_ID])


def is_system(mode: str) -> bool:
    return str(mode or "") == SYSTEM_THEME_ID


def _system_color(role_name: str) -> str:
    from PySide6.QtGui import QPalette
    from PySide6.QtWidgets import QApplication

    role_map = {
        "bg": QPalette.ColorRole.Window,
        "panel": QPalette.ColorRole.Base,
        "panel2": QPalette.ColorRole.AlternateBase,
        "text": QPalette.ColorRole.WindowText,
        "on": QPalette.ColorRole.Highlight,
        "on_text": QPalette.ColorRole.HighlightedText,
        "groove": QPalette.ColorRole.Mid,
        "scroll": QPalette.ColorRole.Mid,
        "scroll_hi": QPalette.ColorRole.Dark,
    }
    app = QApplication.instance()
    pal = app.palette() if app is not None else QPalette()
    return pal.color(role_map.get(role_name, QPalette.ColorRole.Window)).name()


def is_dark(mode: str) -> bool:
    if is_system(mode):
        h = _system_color("bg").lstrip("#")
        r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
        return _lum((r, g, b)) < 150
    return _theme(mode).get("dark", True)


def has_wallpaper(mode: str) -> bool:
    if is_system(mode):
        return False
    return bool(wallpaper_path(_effective_wallpaper(mode)))


def chat_colors(mode: str) -> tuple[str, str]:
    """(chat fg hex, chat bg hex). A theme may set chat_fg/chat_bg distinct from text/panel."""
    if is_system(mode):
        return _system_color("text"), _system_color("panel")
    p = _theme(mode)
    return _hex(p.get("chat_fg", p["text"])), _hex(p.get("chat_bg", p["panel"]))


def ui_color(mode: str, key: str) -> str:
    """Hex of a structural palette colour (bg/panel/panel2/text/…); falls back to panel2."""
    if is_system(mode):
        return _system_color(key)
    p = _theme(mode)
    v = p.get(key)
    return _hex(v) if isinstance(v, (list, tuple)) else _hex(p["panel2"])


def contrast(hex_color: str) -> str:
    """Black or white, whichever reads better on the given #rrggbb."""
    h = str(hex_color or "").lstrip("#")
    if len(h) < 6:
        return "#ffffff"
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return "#101010" if (0.299 * r + 0.587 * g + 0.114 * b) > 150 else "#ffffff"


def nav_fit_width(widget, labels) -> int:
    """A width for a left-nav QListWidget that fits its longest label at the current font (so a tab
    label like 'Notifications' isn't clipped by the divider). Includes the QSS item padding/margins."""
    fm = widget.fontMetrics()
    longest = max((fm.horizontalAdvance(str(s)) for s in labels), default=90)
    return max(150, longest + 64)  # item padding (14·2) + margin (4·2) + list padding (8·2) + slack


def nav_list_style(mode: str, object_name: str) -> str:
    """QSS for a left-hand nav QListWidget (the pretty rounded tab strip used by Preferences and the
    Comic Settings dialog) — themed, with the active item painted in the theme's accent."""
    if is_system(mode):
        return ""
    bg = ui_color(mode, "bg")
    panel = ui_color(mode, "panel2")
    fg = ui_color(mode, "text")
    sel = ui_color(mode, "on")  # the theme's active/"on" accent (not a hardcoded colour)
    sel_fg = contrast(sel)
    return f"""
    QListWidget#{object_name} {{
        background: {bg};
        border: none;
        border-right: 1px solid {panel};
        outline: 0;
        padding: 10px 8px;
    }}
    QListWidget#{object_name}::item {{
        color: {fg};
        padding: 9px 14px;
        margin: 3px 4px;
        border-radius: 7px;
        font-size: 14px;
    }}
    QListWidget#{object_name}::item:hover {{ background: {panel}; }}
    QListWidget#{object_name}::item:selected {{
        background: {sel};
        color: {sel_fg};
        font-weight: 600;
    }}
    """


# ---- user themes: shareable JSON in <config>/themes/ -------------------------
def validate_theme(d: dict) -> dict:
    if not isinstance(d, dict):
        raise ValueError("theme must be a JSON object")
    out: dict = {}
    for k in _THEME_RGB_KEYS:
        v = d.get(k)
        if (not isinstance(v, (list, tuple)) or len(v) != 3
                or not all(isinstance(x, int) and 0 <= x <= 255 for x in v)):
            raise ValueError(f"'{k}' must be [r, g, b] with three 0-255 integers")
        out[k] = tuple(v)
    out["on_text"] = str(d.get("on_text", "white"))
    out["dark"] = bool(d.get("dark", True))
    for opt in ("chat_bg", "chat_fg"):  # optional: distinct chat colours
        v = d.get(opt)
        if (isinstance(v, (list, tuple)) and len(v) == 3
                and all(isinstance(x, int) and 0 <= x <= 255 for x in v)):
            out[opt] = tuple(v)
    return out


def register_theme(tid: str, raw: dict) -> str:
    THEMES[tid] = validate_theme(raw)
    THEME_LABELS[tid] = str(raw.get("name") or tid).strip() or tid
    return tid


def theme_export_dict(mode: str) -> dict:
    p = _theme(mode)
    d: dict = {"name": THEME_LABELS.get(mode, mode), "dark": bool(p.get("dark", True)),
               "on_text": p["on_text"]}
    for k in _THEME_RGB_KEYS:
        d[k] = list(p[k])
    for opt in ("chat_bg", "chat_fg"):
        if opt in p:
            d[opt] = list(p[opt])
    return d


def user_theme_dir():
    from pathlib import Path

    from maxchat.config import config_dir

    d = Path(config_dir()) / "themes"
    d.mkdir(parents=True, exist_ok=True)
    return d


def _ensure_theme_template(folder) -> None:
    import json
    try:
        sample = theme_export_dict("dracula")
        sample["name"] = "My Theme (copy me, drop the underscore)"
        (folder / "_example.json").write_text(json.dumps(sample, indent=2), encoding="utf-8")
    except Exception:  # a read-only themes dir must never crash startup
        pass


def load_user_themes() -> list[tuple[str, str]]:
    """Register every themes/*.json (skips _-prefixed templates). A bad file is skipped, not fatal."""
    import json
    errors: list[tuple[str, str]] = []
    folder = user_theme_dir()
    _ensure_theme_template(folder)
    for path in sorted(folder.glob("*.json")):
        if path.name.startswith("_"):
            continue
        try:
            register_theme(path.stem, json.loads(path.read_text(encoding="utf-8")))
        except Exception as e:
            errors.append((path.name, f"{type(e).__name__}: {e}"))
    return errors


def save_user_app_theme(name: str, raw: dict) -> str:
    """Save an edited app theme as themes/<id>.json and register it. Returns the new id."""
    import json
    import re
    slug = re.sub(r"[^a-z0-9]+", "-", str(name).strip().lower()).strip("-") or "custom"
    tid = f"u-{slug}"
    raw = dict(raw)
    raw["name"] = name
    register_theme(tid, raw)  # validates; raises on bad colours
    try:
        (user_theme_dir() / f"{tid}.json").write_text(
            json.dumps(theme_export_dict(tid), indent=2), encoding="utf-8"
        )
    except Exception:
        pass
    return tid


# ---- user chat themes (separate registry from app themes) -------------------
def _coerce_chat_theme(d: dict) -> dict:
    def rgb(v):
        return (isinstance(v, (list, tuple)) and len(v) == 3
                and all(isinstance(x, int) and 0 <= x <= 255 for x in v))

    out: dict = {"label": str(d.get("label") or "Custom chat"), "fixed": bool(d.get("fixed", True))}
    for k in ("bg", "fg", "ts", "system", "bracket"):
        if rgb(d.get(k)):
            out[k] = tuple(d[k])
    nk = d.get("nicks")
    if nk == "mono":
        out["nicks"] = "mono"
    elif isinstance(nk, (list, tuple)) and nk:
        pal = [tuple(c) for c in nk if rgb(c)]
        if pal:
            out["nicks"] = pal
    if "bg" not in out or "fg" not in out:
        raise ValueError("a chat theme needs bg and fg as [r, g, b]")
    return out


def register_chat_theme(tid: str, d: dict) -> str:
    CHAT_THEMES[tid] = _coerce_chat_theme(d)
    CHAT_THEME_LABELS[tid] = CHAT_THEMES[tid]["label"]
    return tid


def _chat_themes_file():
    from pathlib import Path

    from maxchat.config import config_dir

    return Path(config_dir()) / "chat_themes.json"


def load_user_chat_themes() -> None:
    """Register saved user chat themes from chat_themes.json (a bad entry is skipped)."""
    import json
    try:
        data = json.loads(_chat_themes_file().read_text(encoding="utf-8"))
    except Exception:
        return
    if isinstance(data, dict):
        for tid, d in data.items():
            if isinstance(d, dict):
                try:
                    register_chat_theme(str(tid), d)
                except Exception:
                    pass


def save_user_chat_theme(name: str, d: dict) -> str:
    """Register an edited chat theme and persist it to chat_themes.json. Returns the new id."""
    import json
    import re
    slug = re.sub(r"[^a-z0-9]+", "-", str(name).strip().lower()).strip("-") or "custom"
    tid = f"u-{slug}"
    d = dict(d)
    d["label"] = name
    register_chat_theme(tid, d)  # validates
    saved = CHAT_THEMES[tid]
    store: dict = {"label": name, "fixed": bool(saved.get("fixed", True))}
    for k in ("bg", "fg", "ts", "system", "bracket"):
        if k in saved:
            store[k] = list(saved[k])
    nk = saved.get("nicks")
    if nk == "mono":
        store["nicks"] = "mono"
    elif isinstance(nk, (list, tuple)):
        store["nicks"] = [list(c) for c in nk]
    path = _chat_themes_file()
    try:
        existing = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    except Exception:
        existing = {}
    if not isinstance(existing, dict):
        existing = {}
    existing[tid] = store
    try:
        path.write_text(json.dumps(existing, indent=2), encoding="utf-8")
    except Exception:
        pass
    return tid


# ---- application -------------------------------------------------------------
def is_wayland() -> bool:
    """True on Wayland (WSLg), where the compositor draws NO popup/menu shadow so we add our own.
    Native platforms (Windows/X11/macOS) draw their own — our extra margin+shadow just makes a thick
    black frame there, so it's disabled."""
    try:
        from PySide6.QtGui import QGuiApplication
        return (QGuiApplication.platformName() or "").lower().startswith("wayland")
    except Exception:
        return False


def is_wsl() -> bool:
    """True when running inside Windows Subsystem for Linux."""
    try:
        import os
        if os.environ.get("WSL_DISTRO_NAME") or os.environ.get("WSL_INTEROP"):
            return True
        with open("/proc/version", "r", encoding="utf-8", errors="ignore") as fh:
            return "microsoft" in fh.read().lower()
    except Exception:
        return False


def needs_client_popup_shadow() -> bool:
    """True when Qt popup windows need MaxChat to draw the drop shadow itself."""
    return is_wayland() or is_wsl()


def needs_stale_menu_workaround() -> bool:
    """True when Qt/WSLg can leave old menu popup windows alive after switching menus."""
    if platform.system().lower() == "windows":
        return False
    return is_wayland() or is_wsl()


def _popup_margin() -> int:
    return 8 if needs_client_popup_shadow() else 0


def stylesheet(mode: str = "dark") -> str:
    if is_system(mode):
        return ""
    p = _theme(mode)
    wp = wallpaper_path(_effective_wallpaper(mode))  # theme default, a custom image, or off ('none')
    pa = 0.58 if wp else None  # panel translucency so the wallpaper shows through the chrome
    bg = _rgb(p["bg"])
    panel = _rgba(p["panel"], pa) if wp else _rgb(p["panel"])
    panel2 = _rgba(p["panel2"], pa) if wp else _rgb(p["panel2"])
    text, on, on_text = _rgb(p["text"]), _rgb(p["on"]), p["on_text"]
    scroll, scroll_hi = _rgb(p["scroll"]), _rgb(p["scroll_hi"])
    chat_rgb = p.get("chat_bg", p["panel"])
    chat_bg = _rgba(chat_rgb, 0.80) if wp else _rgb(chat_rgb)  # a dark scrim over the wallpaper
    chat_fg = _rgb(p.get("chat_fg", p["text"]))
    tc = p["text"] if isinstance(p["text"], (list, tuple)) else (200, 200, 200)
    border = f"rgba({tc[0]},{tc[1]},{tc[2]},0.45)"  # menu/frame edge — themed, visible on light & dark
    pm = _popup_margin()  # transparent gap for our client-side popup shadow (0 off Wayland)
    grad = p.get("bg_gradient")  # optional sunset-style window background (e.g. Synthwave)
    win_bg = (
        "qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        + ", ".join(f"stop:{pos:.2f} {_rgb(rgb)}" for pos, rgb in grad) + ")"
    ) if grad else bg
    if wp:  # a stretched wallpaper image fills the window behind the translucent chrome
        window_decl = f"border-image: url({wp}) 0 0 0 0 stretch stretch;"
    else:
        window_decl = f"background:{win_bg};"
    comic_bg = "transparent" if wp else win_bg
    return f"""
    QMainWindow {{ {window_decl} }}
    QDialog#ShadowDialog {{ background:transparent; }}
    QFrame#dialogCard {{ background:{bg}; border:1px solid {border}; border-radius:8px; }}
    QWidget#dialogTitleBar {{ background:{panel2}; border-top-left-radius:8px; border-top-right-radius:8px; }}
    QLabel#dialogTitle {{ font-weight:600; padding:2px; }}
    QToolButton#dialogClose {{ border:none; border-radius:5px; padding:2px 9px; font-size:13px; }}
    QToolButton#dialogClose:hover {{ background:{on}; color:{on_text}; }}
    QWidget {{ color:{text}; }}
    QMenuBar {{ background:{panel2}; color:{text}; }}
    QMenuBar::item {{ background:transparent; padding:4px 9px; }}
    QMenuBar::item:selected {{ background:{on}; color:{on_text}; border-radius:4px; }}
    QMenu {{ background:{panel}; color:{text}; border:1px solid {border}; padding:6px; margin:0; }}
    QMenu::item {{ padding:6px 30px 6px 16px; border-radius:4px; }}
    QMenu::item:selected {{ background:{on}; color:{on_text}; }}
    QMenu::separator {{ height:1px; background:{border}; margin:6px 10px; }}
    QMenu::icon {{ left:8px; }}
    QStatusBar {{ background:{panel}; color:{text}; }}
    QPlainTextEdit, QTextEdit {{ background:{chat_bg}; color:{chat_fg}; border:none; }}
    QTreeWidget, QListWidget {{ background:{panel}; color:{text}; border:1px solid {panel2};
        selection-background-color:{on}; selection-color:{on_text}; outline:0; }}
    QListWidget::item:selected, QTreeWidget::item:selected {{ background:{on}; color:{on_text}; }}
    #topicBar {{ background:{panel2}; color:{text}; padding:3px; }}
    QLabel#emotionAvatar {{ background:{panel}; border:1px solid {border}; border-radius:6px; }}
    QFrame#colorPickerCard {{ background:{panel}; color:{text}; border:1px solid {border}; border-radius:8px; }}
    QFrame#comicArea {{ background:{comic_bg}; border:1px solid {panel2}; }}
    QFrame#comicPanel {{ background:{panel}; border:2px solid {scroll_hi}; border-radius:3px; }}
    QLineEdit {{ background:{panel}; color:{text}; border:1px solid {bg}; border-radius:4px; padding:4px 6px; }}
    QLineEdit[spellError="true"] {{ border:1px solid #e5484d; }}
    QPushButton {{ background:{panel2}; color:{text}; border:1px solid rgba(128,128,128,0.45);
        border-radius:5px; padding:5px 10px; }}
    QPushButton:hover {{ background:{on}; color:{on_text}; }}
    QPushButton:checked {{ background:{on}; color:{on_text}; }}
    QScrollBar:vertical {{ background:{bg}; width:12px; margin:0; border:none; }}
    QScrollBar::handle:vertical {{ background:{scroll}; min-height:28px; border-radius:5px; margin:2px; }}
    QScrollBar::handle:vertical:hover {{ background:{scroll_hi}; }}
    QScrollBar::add-line, QScrollBar::sub-line {{ width:0; height:0; background:none; border:none; }}
    QToolBar {{ background:{panel2}; border:none; spacing:4px; padding:3px; }}
    QToolButton {{ background:{panel}; color:{text}; border:1px solid rgba(128,128,128,0.4);
        border-radius:4px; padding:4px 12px; }}
    QToolButton:hover {{ background:{on}; color:{on_text}; }}
    QToolButton:checked {{ background:{on}; color:{on_text}; border:1px solid {on_text}; }}
    /* the main button bar: its toggle buttons (Comic Mode, Member List) stay plain when ON — a lit
       button there reads as 'pressed/broken' rather than 'active', so keep checked == normal */
    QToolBar#mainToolbar {{ spacing:2px; padding:2px; }}
    QToolBar#mainToolbar QToolButton {{ padding:3px 8px; }}  /* tighter, so the bar fits one row */
    QToolBar#mainToolbar QToolButton:checked {{ background:{panel}; color:{text};
        border:1px solid rgba(128,128,128,0.4); }}
    QToolBar#mainToolbar QToolButton:checked:hover {{ background:{on}; color:{on_text}; }}
    QTabWidget::pane {{ border:1px solid {panel2}; }}
    QTabBar {{ background:transparent; }}
    QTabBar::tab {{ background:{panel}; color:{text}; padding:6px 14px; margin:3px 2px;
        border:1px solid rgba(128,128,128,0.45); border-radius:5px; }}  /* outlined, button-like */
    QTabBar::tab:hover {{ background:{panel2}; }}
    QTabBar::tab:selected {{ background:{on}; color:{on_text}; border:1px solid {on_text}; }}
    QComboBox {{ background:{panel}; color:{text}; border:1px solid {bg}; border-radius:4px; padding:3px 6px;
        combobox-popup:0; }}  /* drop the list DOWN under the box (not Qt's centre-on-current popup) */
    QComboBox QAbstractItemView {{ background:{panel}; color:{text}; border:1px solid {border};
        padding:3px; margin:{pm}px; outline:0; selection-background-color:{on}; selection-color:{on_text}; }}
    QComboBox QAbstractItemView::item {{ padding:4px 8px; }}
    QToolTip {{ background:{panel}; color:{text}; border:1px solid {border}; padding:5px 8px; margin:{pm}px; }}
    """


def palette(mode: str = "dark"):
    """A QPalette matching the theme, so the OS palette (e.g. Windows dark mode) can't bleed in."""
    from PySide6.QtGui import QColor, QPalette
    from PySide6.QtWidgets import QApplication

    if is_system(mode):
        app = QApplication.instance()
        return app.style().standardPalette() if app is not None else QPalette()

    p = _theme(mode)

    def c(key: str) -> QColor:
        return QColor(*p[key])

    pal = QPalette()
    pal.setColor(QPalette.ColorRole.Window, c("bg"))
    pal.setColor(QPalette.ColorRole.WindowText, c("text"))
    pal.setColor(QPalette.ColorRole.Base, c("panel"))
    pal.setColor(QPalette.ColorRole.AlternateBase, c("panel2"))
    pal.setColor(QPalette.ColorRole.Text, c("text"))
    pal.setColor(QPalette.ColorRole.Button, c("panel2"))
    pal.setColor(QPalette.ColorRole.ButtonText, c("text"))
    pal.setColor(QPalette.ColorRole.ToolTipBase, c("panel"))
    pal.setColor(QPalette.ColorRole.ToolTipText, c("text"))
    pal.setColor(QPalette.ColorRole.Highlight, c("on"))
    pal.setColor(QPalette.ColorRole.HighlightedText, QColor(p["on_text"]))
    dim = c("text")
    dim.setAlpha(110)
    for role in (QPalette.ColorRole.WindowText, QPalette.ColorRole.Text, QPalette.ColorRole.ButtonText):
        pal.setColor(QPalette.ColorGroup.Disabled, role, dim)
    return pal
