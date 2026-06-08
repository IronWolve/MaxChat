"""Small helpers for Qt tooltips."""

from __future__ import annotations

from html import escape


def info_tip(text: str, width: int = 360) -> str:
    """Return a fixed-width rich-text tooltip so long help text wraps."""
    body = escape(text).replace("\n", "<br>")
    return f"<div style='white-space: normal; width: {width}px;'>{body}</div>"
