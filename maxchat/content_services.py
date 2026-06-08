"""Content services — optional things the client fetches/renders from links in chat.

Each is independently switchable in Preferences ▸ Services (all default ON). Turning one off keeps
the bare link in chat (still clickable) but skips the auto-fetch — handy for privacy or noise. The
list is the single source of truth, so adding a new service here surfaces a toggle automatically.
"""

from __future__ import annotations

from PySide6.QtCore import QT_TRANSLATE_NOOP


_noop = QT_TRANSLATE_NOOP

CONTENT_SERVICES: list[dict] = [
    {
        "key": "images",
        "label": _noop("ContentServices", "Image previews"),
        "desc": _noop("ContentServices", "Show linked or pasted image URLs inline (.png .jpg .gif .webp …)."),
    },
    {
        "key": "xcards",
        "label": _noop("ContentServices", "X / Twitter cards"),
        "desc": _noop("ContentServices", "Show a summary card (handle · text · link) for x.com / twitter.com status links."),
    },
    {
        "key": "webcards",
        "label": _noop("ContentServices", "Website cards"),
        "desc": _noop("ContentServices", "Show OpenGraph summary cards and thumbnails for public web links, including Mastodon."),
    },
]

DEFAULT_CONTENT_SERVICES: dict[str, bool] = {s["key"]: True for s in CONTENT_SERVICES}
