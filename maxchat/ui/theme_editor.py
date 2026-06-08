"""Theme customizer — edit the key colors of an app or chat theme and save it as your own.

Opened from Preferences ▸ Themes (the *Customize theme…* button under App and under Chat). Starts
from the currently-selected theme, lets you repaint its main colors with the OS color picker, and
saves the result as a reusable user theme (``themes/u-*.json`` for app, ``chat_themes.json`` for chat).
"""

from __future__ import annotations

from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QCheckBox,
    QColorDialog,
    QDialogButtonBox,
    QFormLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QVBoxLayout,
)

from maxchat.ui import theme
from maxchat.ui.shadow_dialog import ShadowDialog

APP_FIELDS = [
    ("bg", "Window background"), ("panel", "Panels (tree / list)"),
    ("panel2", "Bars & headers"), ("text", "Text"), ("accent", "Accent / highlight"),
]
CHAT_FIELDS = [
    ("bg", "Chat background"), ("fg", "Chat text"), ("ts", "Timestamp"),
    ("bracket", "Nick brackets"), ("system", "System / join-part lines"),
]


class _Swatch(QPushButton):
    """A button that shows its color and opens the OS color picker when clicked."""

    def __init__(self, rgb) -> None:
        super().__init__()
        self._rgb = tuple(int(x) for x in rgb)
        self.setMinimumWidth(130)
        self.clicked.connect(self._pick)
        self._refresh()

    def _refresh(self) -> None:
        r, g, b = self._rgb
        ink = "black" if (0.299 * r + 0.587 * g + 0.114 * b) > 140 else "white"
        self.setText("#%02x%02x%02x" % (r, g, b))
        self.setStyleSheet(f"background:rgb({r},{g},{b}); color:{ink}; border:1px solid grey; padding:5px;")

    def _pick(self) -> None:
        c = QColorDialog.getColor(QColor(*self._rgb), self, "Pick a color")
        if c.isValid():
            self._rgb = (c.red(), c.green(), c.blue())
            self._refresh()

    def rgb(self) -> tuple:
        return self._rgb


class ThemeEditor(ShadowDialog):
    def __init__(self, parent=None, scope: str = "app", base_id: str = "dark") -> None:
        super().__init__(parent)
        self.scope = scope
        self.result_id: str | None = None
        self.setWindowTitle(f"Customize {'app' if scope == 'app' else 'chat'} theme")
        self.set_autosize(42, 0)  # font-relative width; height follows the content
        root = QVBoxLayout(self.body)
        form = QFormLayout()
        root.addLayout(form)

        labels = theme.THEME_LABELS if scope == "app" else theme.CHAT_THEME_LABELS
        self.name = QLineEdit(f"{labels.get(base_id, base_id)} (custom)")
        form.addRow("Name", self.name)

        self.swatches: dict[str, _Swatch] = {}
        if scope == "app":
            self._base = theme.theme_export_dict(base_id)  # full theme as lists
            for key, label in APP_FIELDS:
                sw = _Swatch(self._base[key])
                self.swatches[key] = sw
                form.addRow(label, sw)
        else:
            base = dict(theme.CHAT_THEMES.get(base_id) or {})
            base.setdefault("bg", (0, 0, 0))
            base.setdefault("fg", (208, 208, 208))
            self._base = base
            for key, label in CHAT_FIELDS:
                fallback = base["fg"] if key in ("ts", "bracket", "system") else base["bg"]
                sw = _Swatch(base.get(key) or fallback)
                self.swatches[key] = sw
                form.addRow(label, sw)
            self.fixed = QCheckBox("Fixed-width (terminal) font")
            self.fixed.setChecked(bool(base.get("fixed", True)))
            form.addRow("", self.fixed)
            self.mono = QCheckBox("Monochrome nicks (irssi-style)")
            self.mono.setChecked(base.get("nicks") == "mono")
            form.addRow("", self.mono)

        hint = QLabel("Saved as your own theme in the config folder — pick it from the dropdown after.")
        hint.setWordWrap(True)
        root.addWidget(hint)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self._save)
        buttons.rejected.connect(self.reject)
        root.addWidget(buttons)

    def _save(self) -> None:
        name = self.name.text().strip() or "Custom"
        try:
            if self.scope == "app":
                d = dict(self._base)
                for key, sw in self.swatches.items():
                    d[key] = list(sw.rgb())
                self.result_id = theme.save_user_app_theme(name, d)
            else:
                d: dict = {"label": name, "fixed": self.fixed.isChecked()}
                for key, sw in self.swatches.items():
                    d[key] = tuple(sw.rgb())
                if self.mono.isChecked():
                    d["nicks"] = "mono"
                elif isinstance(self._base.get("nicks"), (list, tuple)):
                    d["nicks"] = self._base["nicks"]  # keep a bright palette (e.g. BitchX)
                self.result_id = theme.save_user_chat_theme(name, d)
        except Exception:
            self.result_id = None
        self.accept()
