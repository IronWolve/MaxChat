"""About dialog for MaxChat."""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QLabel, QPushButton, QVBoxLayout

from maxchat import __app_name__, __version__
from maxchat.ui.shadow_dialog import ShadowDialog


class AboutDialog(ShadowDialog):
    def __init__(
        self,
        parent=None,
        *,
        ui_font: str,
        chat_font: str,
        bundled_mono: str,
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle("About")
        self.set_autosize(54, 14)

        url = "https://github.com/IronWolve/MaxChat"
        body = QLabel(
            f"<b>{__app_name__} {__version__}</b><br>"
            "A comic-strip-style IRC client.<br><br>"
            f'<a href="{url}">{url}</a><br><br>'
            "Find IRC networks and servers at "
            '<a href="https://www.irchelp.org">irchelp.org</a> and '
            '<a href="https://netsplit.de/">netsplit.de</a>.<br><br>'
            f"UI font: {ui_font}<br>"
            f"Chat font: {chat_font}<br>"
            f"Bundled JetBrains Mono loaded: {bundled_mono}"
        )
        body.setOpenExternalLinks(True)
        body.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextBrowserInteraction | Qt.TextInteractionFlag.TextSelectableByMouse
        )
        body.setWordWrap(True)

        close = QPushButton("OK")
        close.clicked.connect(self.accept)

        layout = QVBoxLayout(self.body)
        layout.addWidget(body)
        layout.addWidget(close, alignment=Qt.AlignmentFlag.AlignRight)
