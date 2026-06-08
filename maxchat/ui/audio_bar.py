"""A slim inline media transport for audio/video links — play/pause/stop + seek + download, docked
under the chat. Video plays in a small popup window driven by the same transport.

Uses QtMultimedia (QMediaPlayer). If that isn't available, ``available()`` is False and the window
falls back to opening the link in the browser.
"""

from __future__ import annotations

import os
from urllib.parse import urlparse

from PySide6.QtCore import Qt, QUrl
from PySide6.QtWidgets import (
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QSlider,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from maxchat.ui.shadow_dialog import ShadowDialog

try:
    from PySide6.QtMultimedia import QAudioOutput, QMediaPlayer
    from PySide6.QtMultimediaWidgets import QVideoWidget
    _HAVE = True
except Exception:  # pragma: no cover - platform without QtMultimedia
    _HAVE = False


def _mmss(ms: int) -> str:
    s = max(0, ms) // 1000
    return f"{s // 60}:{s % 60:02d}"


class _VideoWindow(ShadowDialog):
    """A small shadowed popup that shows the video output of the shared player."""

    def __init__(self, parent, video_widget) -> None:
        super().__init__(parent)
        self.setWindowTitle("Video")
        self.setWindowFlag(Qt.WindowType.Window, True)
        self.resize(640, 400)
        v = QVBoxLayout(self.body)
        v.setContentsMargins(0, 0, 0, 0)
        v.addWidget(video_widget)


class AudioBar(QWidget):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setVisible(False)
        h = QHBoxLayout(self)
        h.setContentsMargins(6, 2, 6, 2)
        h.setSpacing(6)
        self._play = QToolButton()
        self._play.setText("▶")
        self._play.setToolTip("Play / pause")
        self._play.clicked.connect(self._toggle)
        self._stop = QToolButton()
        self._stop.setText("⏹")
        self._stop.setToolTip("Stop")
        self._stop.clicked.connect(self.stop)
        self._name = QLabel("")
        self._name.setMinimumWidth(120)
        self._slider = QSlider(Qt.Orientation.Horizontal)
        self._slider.sliderMoved.connect(self._seek)
        self._time = QLabel("0:00")
        self._download = QToolButton()
        self._download.setText("⭳")
        self._download.setToolTip("Download…")
        self._download.clicked.connect(self._save)
        close = QToolButton()
        close.setText("✕")
        close.setToolTip("Close player")
        close.clicked.connect(self._close)
        for w in (self._play, self._stop, self._name, self._slider, self._time,
                  self._download, close):
            h.addWidget(w)
        h.setStretchFactor(self._slider, 1)

        self._url = ""
        self._player = None
        self._video = None
        self._video_win = None
        if _HAVE:
            self._player = QMediaPlayer(self)
            self._out = QAudioOutput(self)
            self._player.setAudioOutput(self._out)
            self._player.positionChanged.connect(self._on_pos)
            self._player.durationChanged.connect(lambda d: self._slider.setRange(0, d))
            self._player.playbackStateChanged.connect(self._on_state)
            self._player.mediaStatusChanged.connect(self._on_status)

    @staticmethod
    def available() -> bool:
        return _HAVE

    def play(self, url: str, name: str = "", video: bool = False) -> bool:
        if self._player is None:
            return False
        self._url = url
        self._name.setText(name or os.path.basename(urlparse(url).path) or url)
        self._player.setSource(QUrl(url))
        if video:
            self._ensure_video_window()
            self._video_win.show()
            self._video_win.raise_()
        elif self._video_win is not None:
            self._video_win.hide()  # reuse the bar for an audio link → hide any old video popup
        self._player.play()
        self.show()
        return True

    def _ensure_video_window(self) -> None:
        if self._video is None:
            self._video = QVideoWidget()
            self._player.setVideoOutput(self._video)
            self._video_win = _VideoWindow(self.window(), self._video)

    def stop(self) -> None:
        if self._player is not None:
            self._player.stop()  # resets to the start; ends playback cleanly (no loop)

    def _close(self) -> None:
        self.stop()
        if self._video_win is not None:
            self._video_win.hide()
        self.hide()

    def _toggle(self) -> None:
        if self._player is None:
            return
        st = self._player.playbackState()
        if st == QMediaPlayer.PlaybackState.PlayingState:
            self._player.pause()
        else:
            self._player.play()

    def _seek(self, pos: int) -> None:
        if self._player is not None:
            self._player.setPosition(pos)

    def _save(self) -> None:
        """Download the current track to a file the user chooses (separate fetch, doesn't disturb play)."""
        if not self._url:
            return
        suggested = os.path.basename(urlparse(self._url).path) or "download"
        path, _ = QFileDialog.getSaveFileName(self, "Download media", suggested)
        if not path:
            return
        from PySide6.QtNetwork import QNetworkAccessManager, QNetworkRequest
        if not hasattr(self, "_nam"):
            self._nam = QNetworkAccessManager(self)
            self._nam.setRedirectPolicy(QNetworkRequest.RedirectPolicy.NoLessSafeRedirectPolicy)
        req = QNetworkRequest(QUrl(self._url))
        req.setHeader(QNetworkRequest.KnownHeaders.UserAgentHeader, "Mozilla/5.0 ComicIRC")
        reply = self._nam.get(req)
        self._name.setText(f"↓ {os.path.basename(path)}…")
        try:
            out = open(path, "wb")
        except OSError:
            self._name.setText("✗ download failed")
            reply.abort()
            reply.deleteLater()
            return

        def ready():
            try:
                out.write(bytes(reply.readAll()))
            except OSError:
                reply.abort()

        def done():
            ready()
            out.close()
            ok = reply.error() == reply.NetworkError.NoError
            if ok:
                self._name.setText(f"✓ {os.path.basename(path)}")
            if not ok:
                self._name.setText("✗ download failed")
            reply.deleteLater()

        reply.readyRead.connect(ready)
        reply.finished.connect(done)

    def _on_pos(self, pos: int) -> None:
        if not self._slider.isSliderDown():
            self._slider.setValue(pos)
        self._time.setText(f"{_mmss(pos)} / {_mmss(self._player.duration())}")

    def _on_state(self, state) -> None:
        playing = state == QMediaPlayer.PlaybackState.PlayingState
        self._play.setText("⏸" if playing else "▶")

    def _on_status(self, status) -> None:
        # "end after playing" — when the media finishes, stop cleanly (rewind, reset button), no loop.
        if status == QMediaPlayer.MediaStatus.EndOfMedia:
            self._player.stop()
