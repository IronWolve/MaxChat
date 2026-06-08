"""Small shadowed replacements for simple message and input prompts."""

from __future__ import annotations

from PySide6.QtWidgets import (
    QComboBox,
    QDialogButtonBox,
    QLabel,
    QLineEdit,
    QVBoxLayout,
)

from maxchat.ui.shadow_dialog import ShadowDialog


class _PromptDialog(ShadowDialog):
    def __init__(self, parent, title: str, text: str) -> None:
        super().__init__(parent)
        self.setWindowTitle(title)
        self.set_autosize(46, 8)
        self.label = QLabel(text)
        self.label.setWordWrap(True)
        self.body_layout = QVBoxLayout(self.body)
        self.body_layout.addWidget(self.label)


def information(parent, title: str, text: str) -> None:
    dlg = _PromptDialog(parent, title, text)
    buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok)
    buttons.accepted.connect(dlg.accept)
    dlg.body_layout.addWidget(buttons)
    dlg.exec()


def warning(parent, title: str, text: str) -> None:
    information(parent, title, text)


def question(parent, title: str, text: str, *, default_yes: bool = False) -> bool:
    dlg = _PromptDialog(parent, title, text)
    buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Yes | QDialogButtonBox.StandardButton.No)
    default = QDialogButtonBox.StandardButton.Yes if default_yes else QDialogButtonBox.StandardButton.No
    default_button = buttons.button(default)
    if default_button is not None:
        default_button.setDefault(True)
    buttons.accepted.connect(dlg.accept)
    buttons.rejected.connect(dlg.reject)
    dlg.body_layout.addWidget(buttons)
    return bool(dlg.exec())


def open_or_close(parent, title: str, text: str) -> bool:
    dlg = _PromptDialog(parent, title, text)
    row = QDialogButtonBox()
    open_btn = row.addButton("Open", QDialogButtonBox.ButtonRole.AcceptRole)
    row.addButton("Close", QDialogButtonBox.ButtonRole.RejectRole)
    open_btn.setDefault(True)
    row.accepted.connect(dlg.accept)
    row.rejected.connect(dlg.reject)
    dlg.body_layout.addWidget(row)
    return bool(dlg.exec())


def get_text(parent, title: str, label: str, *, text: str = "") -> tuple[str, bool]:
    dlg = _PromptDialog(parent, title, label)
    edit = QLineEdit(text)
    dlg.body_layout.addWidget(edit)
    buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
    buttons.accepted.connect(dlg.accept)
    buttons.rejected.connect(dlg.reject)
    dlg.body_layout.addWidget(buttons)
    ok = bool(dlg.exec())
    return edit.text(), ok


def get_item(parent, title: str, label: str, items: list[str], current: int = 0) -> tuple[str, bool]:
    dlg = _PromptDialog(parent, title, label)
    combo = QComboBox()
    combo.addItems(items)
    if items:
        combo.setCurrentIndex(max(0, min(current, len(items) - 1)))
    dlg.body_layout.addWidget(combo)
    buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
    buttons.accepted.connect(dlg.accept)
    buttons.rejected.connect(dlg.reject)
    dlg.body_layout.addWidget(buttons)
    ok = bool(dlg.exec())
    return combo.currentText(), ok
