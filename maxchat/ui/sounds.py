"""Short ``.wav`` sound effects for the CTCP SOUND feature (mIRC / Comic Chat heritage; default off).

No bundled sounds — keeps third-party audio out of the build. Drop your own ``.wav`` files in
``<config>/sounds/``. A received SOUND only plays if you actually HAVE a file of that name (so a
remote peer can't make you fetch or play anything you don't already own) AND the feature is enabled.
"""

from __future__ import annotations

import os
from pathlib import Path

from maxchat import config

_BUNDLED_DIR = Path(__file__).resolve().parents[2] / "assets" / "sounds"  # survives PyInstaller freeze

try:
    from PySide6.QtCore import QUrl
    from PySide6.QtMultimedia import QSoundEffect
    _AVAILABLE = True
except Exception:  # QtMultimedia not present in this build
    _AVAILABLE = False


def sounds_dir() -> str:
    """``<config>/sounds`` — where the user keeps their own ``.wav`` files."""
    return os.path.join(config.config_dir(), "sounds")


def resolve(name: str) -> str | None:
    """Map a CTCP SOUND filename to a local ``.wav`` path, or ``None``. Basename only (no path
    traversal), ``.wav`` enforced, and the file must already exist in the sounds dir."""
    if not name:
        return None
    base = os.path.basename(name.replace("\\", "/")).strip()
    if not base:
        return None
    if not base.lower().endswith(".wav"):
        base += ".wav"
    path = os.path.join(sounds_dir(), base)
    return path if os.path.isfile(path) else None


def notify_path() -> str | None:
    """The notification chime: the user's own ``<config>/sounds/notify.wav`` if they dropped one in,
    otherwise the bundled default (``assets/sounds/notify.wav``)."""
    user = resolve("notify.wav")
    if user:
        return user
    bundled = _BUNDLED_DIR / "notify.wav"
    return str(bundled) if bundled.is_file() else None


class SoundPlayer:
    """Plays a ``.wav`` via QSoundEffect (low-latency). A safe no-op if QtMultimedia is unavailable."""

    def __init__(self) -> None:
        self._fx = None

    def available(self) -> bool:
        return _AVAILABLE

    def play(self, path: str) -> bool:
        if not _AVAILABLE or not path or not os.path.isfile(path):
            return False
        if self._fx is None:
            self._fx = QSoundEffect()
        self._fx.setSource(QUrl.fromLocalFile(path))
        self._fx.setVolume(0.9)
        self._fx.play()
        return True
