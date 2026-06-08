"""Media for chat links — an async fetcher (with cache) and a built-in image viewer.

Privacy note: fetching a remote image or page preview makes your client hit that URL, which reveals
your IP to the host. The Preferences > Services tab controls which link previews are fetched.
"""

from __future__ import annotations

import ipaddress
import json
import os
import re
import socket
from collections import OrderedDict
from html import unescape
from html.parser import HTMLParser
from urllib.parse import parse_qs, urljoin, urlparse

from PySide6.QtCore import QObject, Qt, QUrl
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkRequest
from PySide6.QtWidgets import QHBoxLayout, QLabel, QPushButton, QScrollArea, QVBoxLayout

from maxchat.ui.shadow_dialog import ShadowDialog

_IMAGE_EXT = (".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".apng")
_AUDIO_EXT = (".mp3", ".ogg", ".oga", ".wav", ".flac", ".m4a", ".opus")
_VIDEO_EXT = (".mp4", ".webm", ".mkv", ".mov", ".m4v", ".avi", ".ogv")
MAX_FETCH_BYTES = 8 * 1024 * 1024
MAX_CACHE_ITEMS = 48
MAX_CACHE_BYTES = 64 * 1024 * 1024


def _ext(url: str) -> str:
    return os.path.splitext(urlparse(url).path)[1].lower()


def is_image_url(url: str) -> bool:
    return _ext(url) in _IMAGE_EXT


def is_audio_url(url: str) -> bool:
    return _ext(url) in _AUDIO_EXT


def is_video_url(url: str) -> bool:
    return _ext(url) in _VIDEO_EXT


def is_media_url(url: str) -> bool:
    """Any directly-playable audio or video link (handled by the inline transport bar)."""
    return _ext(url) in (_AUDIO_EXT + _VIDEO_EXT)


# X / Twitter status links → a summary fetched from the FixTweet metadata service (no paid X API).
_X_RE = re.compile(r"^https?://(?:www\.)?(?:twitter\.com|x\.com)/([^/?#]+)/status/(\d+)", re.IGNORECASE)
_MASTODON_RE = re.compile(r"^https?://[^/?#]+/(?:@[^/?#]+/\d+|users/[^/?#]+/statuses/\d+)(?:[/?#].*)?$",
                          re.IGNORECASE)


def is_x_url(url: str) -> bool:
    return bool(_X_RE.match(url))


def fxtwitter_api_url(url: str) -> str:
    m = _X_RE.match(url)
    return f"https://api.fxtwitter.com/{m.group(1)}/status/{m.group(2)}" if m else ""


def is_mastodon_url(url: str) -> bool:
    return bool(_MASTODON_RE.match(url)) and not is_x_url(url)


class _OpenGraphParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.data: dict[str, str] = {}

    def handle_starttag(self, tag: str, attrs) -> None:
        if tag.lower() != "meta":
            return
        values = {str(k).lower(): str(v) for k, v in attrs if k and v is not None}
        key = values.get("property") or values.get("name")
        content = values.get("content")
        if key and content and key not in self.data:
            self.data[key] = unescape(content).strip()


def opengraph_card(data: bytes, url: str) -> dict[str, str]:
    """Small card fields from a public page's OpenGraph metadata."""
    try:
        html = bytes(data).decode("utf-8", "replace")
    except Exception:
        return {}
    parser = _OpenGraphParser()
    try:
        parser.feed(html[:300_000])
    except Exception:
        return {}
    title = parser.data.get("og:title") or parser.data.get("twitter:title") or ""
    desc = parser.data.get("og:description") or parser.data.get("description") or ""
    image = parser.data.get("og:image") or parser.data.get("twitter:image") or ""
    if image:
        image = urljoin(url, image)
    title, desc = title.strip(), desc.strip()
    if not title and not desc:
        return {}
    return {"title": title, "description": desc, "image": image}


def mastodon_card(data: bytes, url: str) -> dict[str, str]:
    """Small card fields from a public Mastodon status page's OpenGraph metadata."""
    return opengraph_card(data, url)


def is_web_card_url(url: str) -> bool:
    """True for public page-like links that should get a generic OpenGraph card."""
    parsed = urlparse(url)
    if parsed.scheme not in ("http", "https") or not parsed.hostname:
        return False
    if is_x_url(url) or is_image_url(url) or is_media_url(url):
        return False
    return True


def _is_x_image_url(url: str) -> bool:
    parsed = urlparse(url)
    if parsed.scheme not in ("http", "https"):
        return False
    if is_image_url(url):
        return True
    fmt = (parse_qs(parsed.query).get("format") or [""])[0].lower()
    return parsed.hostname == "pbs.twimg.com" and f".{fmt}" in _IMAGE_EXT


def x_photo_urls(data: bytes) -> list[str]:
    """Photo URLs from a FixTweet API response, in display order."""
    try:
        tw = (json.loads(bytes(data).decode("utf-8", "replace")) or {}).get("tweet") or {}
    except Exception:
        return []
    media = tw.get("media") or {}
    photos = []
    def add_photos(value) -> None:
        if isinstance(value, (list, tuple)):
            photos.extend(value)
        elif value:
            photos.append(value)

    if isinstance(media, dict):
        add_photos(media.get("photos"))
        add_photos(media.get("images"))
    add_photos(tw.get("photos"))

    out: list[str] = []
    seen: set[str] = set()
    for photo in photos:
        candidates = []
        if isinstance(photo, str):
            candidates.append(photo)
        elif isinstance(photo, dict):
            candidates.extend(str(photo.get(k) or "") for k in ("url", "media_url_https", "media_url"))
        for url in candidates:
            if url and url not in seen and _is_x_image_url(url):
                seen.add(url)
                out.append(url)
                break
    return out


def _ip_blocked(ip: str) -> bool:
    """True for any address we must never fetch from (loopback / private / link-local incl. the
    169.254.169.254 cloud-metadata endpoint / reserved / multicast). Unparseable → blocked."""
    try:
        a = ipaddress.ip_address(ip)
    except ValueError:
        return True
    return (a.is_private or a.is_loopback or a.is_link_local or a.is_reserved
            or a.is_multicast or a.is_unspecified)


def is_safe_fetch_url(url: str) -> bool:
    """SSRF guard: only fetch PUBLIC http(s) hosts. A hostile chat link like http://127.0.0.1:8080/… or
    http://169.254.169.254/… (cloud metadata) must not make the client reach into the local network. We
    resolve the host and reject if it points at any private/loopback/link-local/reserved address.
    Each redirect hop is re-checked too (see ``MediaLoader.fetch``'s ``on_redirect``)."""
    p = urlparse(url)
    if p.scheme not in ("http", "https") or not p.hostname:
        return False
    try:
        infos = socket.getaddrinfo(p.hostname, p.port or (443 if p.scheme == "https" else 80),
                                   proto=socket.IPPROTO_TCP)
    except OSError:
        return False  # can't resolve → don't fetch
    return bool(infos) and all(not _ip_blocked(info[4][0]) for info in infos)


class MediaLoader(QObject):
    """Fetch URLs asynchronously (follows redirects), cache the bytes, call back on the GUI thread."""

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._nam = QNetworkAccessManager(self)
        self._nam.setRedirectPolicy(QNetworkRequest.RedirectPolicy.NoLessSafeRedirectPolicy)
        self._cache: OrderedDict[str, bytes] = OrderedDict()
        self._cache_bytes = 0

    def fetch(self, url: str, callback) -> None:
        if url in self._cache:
            data = self._cache.pop(url)
            self._cache[url] = data
            callback(data)
            return
        if not is_safe_fetch_url(url):  # SSRF guard — never reach into the local/private network
            callback(b"")
            return
        req = QNetworkRequest(QUrl(url))
        req.setHeader(QNetworkRequest.KnownHeaders.UserAgentHeader, "Mozilla/5.0 ComicIRC")
        if hasattr(req, "setTransferTimeout"):
            req.setTransferTimeout(15000)
        reply = self._nam.get(req)
        chunks: list[bytes] = []
        total = 0
        aborted = False

        def on_redirect(target):  # re-run the SSRF guard on every hop — a public host can 30x to internal
            nonlocal aborted
            if not is_safe_fetch_url(target.toString()):
                aborted = True
                reply.abort()

        reply.redirected.connect(on_redirect)

        def ready():
            nonlocal total, aborted
            if aborted:
                reply.readAll()
                return
            part = bytes(reply.readAll())
            total += len(part)
            if total > MAX_FETCH_BYTES:
                aborted = True
                chunks.clear()
                reply.abort()
                return
            if part:
                chunks.append(part)

        def done():
            ready()
            data = b""
            if not aborted and reply.error() == reply.NetworkError.NoError:
                data = b"".join(chunks)
            if data:
                self._remember(url, data)
            reply.deleteLater()
            callback(data)

        reply.readyRead.connect(ready)
        reply.finished.connect(done)

    def _remember(self, url: str, data: bytes) -> None:
        if len(data) > MAX_FETCH_BYTES:
            return
        old = self._cache.pop(url, None)
        if old is not None:
            self._cache_bytes -= len(old)
        self._cache[url] = data
        self._cache_bytes += len(data)
        while len(self._cache) > MAX_CACHE_ITEMS or self._cache_bytes > MAX_CACHE_BYTES:
            _url, evicted = self._cache.popitem(last=False)
            self._cache_bytes -= len(evicted)


class ImageViewer(ShadowDialog):
    """A resizable window that loads an image URL and shows it fit-to-window (click-for-big)."""

    def __init__(self, parent, loader: MediaLoader, url: str) -> None:
        super().__init__(parent)
        self.setWindowTitle(os.path.basename(urlparse(url).path) or "Image")
        self.setMinimumSize(360, 280)
        self.resize(720, 560)
        self._url = url
        self._pix: QPixmap | None = None

        v = QVBoxLayout(self.body)
        self._label = QLabel("Loading…")
        self._label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(self._label)
        v.addWidget(scroll, 1)

        row = QHBoxLayout()
        row.addStretch(1)
        openb = QPushButton("Open in browser")
        openb.clicked.connect(self._open_external)
        close = QPushButton("Close")
        close.clicked.connect(self.accept)
        row.addWidget(openb)
        row.addWidget(close)
        v.addLayout(row)

        loader.fetch(url, self._loaded)

    def _open_external(self) -> None:
        from PySide6.QtGui import QDesktopServices
        QDesktopServices.openUrl(QUrl(self._url))

    def _loaded(self, data: bytes) -> None:
        img = QImage.fromData(data)
        if img.isNull():
            self._label.setText("Could not load image.")
            return
        self._pix = QPixmap.fromImage(img)
        # size the WINDOW to the image (capped to the screen) so it opens full-size, no scrolling
        from PySide6.QtGui import QGuiApplication
        scr = QGuiApplication.primaryScreen()
        avail = scr.availableGeometry() if scr is not None else None
        maxw = int(avail.width() * 0.92) if avail else 1400
        maxh = int(avail.height() * 0.88) if avail else 900
        w = min(self._pix.width() + 24, maxw)     # + a little chrome / margin
        h = min(self._pix.height() + 84, maxh)    # + the button row
        self.resize(max(360, w), max(280, h))
        self._rescale()

    def _rescale(self) -> None:
        if self._pix is None:
            return
        area = self._label.parentWidget().size()  # the scroll area's viewport
        self._label.setPixmap(self._pix.scaled(area * 0.99, Qt.AspectRatioMode.KeepAspectRatio,
                                               Qt.TransformationMode.SmoothTransformation))

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        self._rescale()
