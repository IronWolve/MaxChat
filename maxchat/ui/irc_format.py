"""Convert IRC-formatted text (mIRC control codes) to HTML for the text views.

Handles bold/italic/underline/strikethrough/reverse, the 0–98 colour palette, and
6-hex-digit colours. Output is HTML safe to pass to ``QPlainTextEdit.appendHtml``. Plain
text (no codes) comes back HTML-escaped with no markup, so it inherits the view's
fixed-font default colours. ``default_fg``/``default_bg`` (the active theme's text/panel)
are used only for reverse-video when no explicit colours are set.
"""

from __future__ import annotations

import html

# mIRC palette: 0–15 (classic) + 16–98 (modern extended). Values are RRGGBB.
MIRC_COLORS = {
    0: "FFFFFF", 1: "000000", 2: "00007F", 3: "009300", 4: "FF0000", 5: "7F0000",
    6: "9C009C", 7: "FC7F00", 8: "FFFF00", 9: "00FC00", 10: "009393", 11: "00FFFF",
    12: "0000FC", 13: "FF00FF", 14: "7F7F7F", 15: "D2D2D2",
    16: "470000", 17: "472100", 18: "474700", 19: "324700", 20: "004700", 21: "00472C",
    22: "004747", 23: "002747", 24: "000047", 25: "2E0047", 26: "470047", 27: "47002A",
    28: "740000", 29: "743A00", 30: "747400", 31: "517400", 32: "007400", 33: "007449",
    34: "007474", 35: "004074", 36: "000074", 37: "4B0074", 38: "740074", 39: "740045",
    40: "B50000", 41: "B56300", 42: "B5B500", 43: "7DB500", 44: "00B500", 45: "00B571",
    46: "00B5B5", 47: "0063B5", 48: "0000B5", 49: "7500B5", 50: "B500B5", 51: "B5006B",
    52: "FF0000", 53: "FF8C00", 54: "FFFF00", 55: "B2FF00", 56: "00FF00", 57: "00FFA0",
    58: "00FFFF", 59: "008CFF", 60: "0000FF", 61: "A500FF", 62: "FF00FF", 63: "FF0098",
    64: "FF5959", 65: "FFB459", 66: "FFFF71", 67: "CFFF60", 68: "6FFF6F", 69: "65FFC9",
    70: "6DFFFF", 71: "59B4FF", 72: "5959FF", 73: "C459FF", 74: "FF66FF", 75: "FF59BC",
    76: "FF9C9C", 77: "FFD39C", 78: "FFFF9C", 79: "E2FF9C", 80: "9CFF9C", 81: "9CFFDB",
    82: "9CFFFF", 83: "9CD3FF", 84: "9C9CFF", 85: "DC9CFF", 86: "FF9CFF", 87: "FF94D3",
    88: "000000", 89: "131313", 90: "282828", 91: "363636", 92: "4D4D4D", 93: "656565",
    94: "818181", 95: "9F9F9F", 96: "BCBCBC", 97: "E2E2E2", 98: "FFFFFF",
}

_HEX = set("0123456789abcdefABCDEF")


def _color(n: int) -> str | None:
    h = MIRC_COLORS.get(n)
    return "#" + h if h else None


# Per-nick colours so each speaker is easy to tell apart (deterministic, prefix-insensitive).
NICK_COLORS = [
    "#e06c75", "#e59572", "#e5c07b", "#98c379", "#56b6c2", "#61afef", "#c678dd",
    "#d19a66", "#5fb3a1", "#7aa2f7", "#bb9af7", "#f7768e", "#e0af68", "#9ece6a",
    "#7dcfff", "#ff9e64",
]


def nick_color(nick: str, palette: list | None = None) -> str:
    pal = palette or NICK_COLORS  # a chat theme may supply its own (e.g. BitchX's bright set)
    key = nick.lstrip("~&@%+").lower() or nick
    h = 0
    for ch in key:
        h = (h * 31 + ord(ch)) & 0xFFFFFFFF
    return pal[h % len(pal)]


def strip_formatting(text: str) -> str:
    """Remove all mIRC control/colour codes, leaving plain text (for the "no formatting" option)."""
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        o = ord(text[i])
        if o in (0x02, 0x1D, 0x1F, 0x1E, 0x16, 0x11, 0x0F):
            i += 1
        elif o == 0x03:
            i += 1
            d = 0
            while i < n and text[i].isdigit() and d < 2:
                i += 1; d += 1
            if i + 1 < n and text[i] == "," and text[i + 1].isdigit():
                i += 1; d = 0
                while i < n and text[i].isdigit() and d < 2:
                    i += 1; d += 1
        elif o == 0x04:
            i += 1
            if i + 6 <= n and all(c in _HEX for c in text[i : i + 6]):
                i += 6
                if i + 7 <= n and text[i] == "," and all(c in _HEX for c in text[i + 1 : i + 7]):
                    i += 7
        else:
            out.append(text[i]); i += 1
    return "".join(out)


def to_html(text: str, default_fg: str = "#cfcfcf", default_bg: str = "#0a0a0a") -> str:
    out: list[str] = []
    buf: list[str] = []
    st = {"b": False, "i": False, "u": False, "s": False, "rev": False, "fg": None, "bg": None}

    def flush() -> None:
        if not buf:
            return
        seg = html.escape("".join(buf))
        buf.clear()
        fg, bg = st["fg"], st["bg"]
        if st["rev"]:
            fg, bg = (bg or default_bg), (fg or default_fg)
        styles = []
        if fg:
            styles.append(f"color:{fg}")
        if bg:
            styles.append(f"background-color:{bg}")
        if st["b"]:
            styles.append("font-weight:bold")
        if st["i"]:
            styles.append("font-style:italic")
        deco = []
        if st["u"]:
            deco.append("underline")
        if st["s"]:
            deco.append("line-through")
        if deco:
            styles.append("text-decoration:" + " ".join(deco))
        out.append(f'<span style="{";".join(styles)}">{seg}</span>' if styles else seg)

    i, n = 0, len(text)
    while i < n:
        o = ord(text[i])
        if o == 0x02:
            flush(); st["b"] = not st["b"]; i += 1
        elif o == 0x1D:
            flush(); st["i"] = not st["i"]; i += 1
        elif o == 0x1F:
            flush(); st["u"] = not st["u"]; i += 1
        elif o == 0x1E:
            flush(); st["s"] = not st["s"]; i += 1
        elif o == 0x16:
            flush(); st["rev"] = not st["rev"]; i += 1
        elif o == 0x11:
            i += 1  # monospace marker — view is already fixed-font
        elif o == 0x0F:
            flush(); st.update(b=False, i=False, u=False, s=False, rev=False, fg=None, bg=None); i += 1
        elif o == 0x03:  # colour: NN[,NN]
            flush(); i += 1
            digits = ""
            while i < n and text[i].isdigit() and len(digits) < 2:
                digits += text[i]; i += 1
            if not digits:
                st["fg"] = st["bg"] = None
            else:
                st["fg"] = _color(int(digits))
                if i + 1 < n and text[i] == "," and text[i + 1].isdigit():
                    i += 1
                    d2 = ""
                    while i < n and text[i].isdigit() and len(d2) < 2:
                        d2 += text[i]; i += 1
                    st["bg"] = _color(int(d2))
        elif o == 0x04:  # hex colour: RRGGBB[,RRGGBB]
            flush(); i += 1
            if i + 6 <= n and all(c in _HEX for c in text[i : i + 6]):
                st["fg"] = "#" + text[i : i + 6]; i += 6
                if i + 7 <= n and text[i] == "," and all(c in _HEX for c in text[i + 1 : i + 7]):
                    st["bg"] = "#" + text[i + 1 : i + 7]; i += 7
            else:
                st["fg"] = st["bg"] = None
        else:
            buf.append(text[i]); i += 1
    flush()
    return "".join(out)
