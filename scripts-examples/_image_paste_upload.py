"""Image paste → upload → link  (EXPERIMENTAL / opt-in plugin — status: TBD).

When you paste an image into the message box, this uploads it to a free image host and drops the
resulting link into your input, so it sends like any URL (and shows an inline preview).

This lives as a PLUGIN (not a core feature) because image hosting is a moving target:
  • uguu.se  — works from this client (temporary, ~3h). Default.
  • catbox / litterbox — great hosts, but their Cloudflare front-end currently rejects uploads from
    Qt's HTTP stack (the non-browser TLS fingerprint gets a 400). Left here, disabled, for reference.

Because the filename starts with "_", it is NOT auto-loaded. Enable it from Settings ▸ Scripts… →
"Load file…" (or /load _image_paste_upload). Privacy: uploading sends your image to a third-party
host — only paste images you're OK sharing publicly.

Hook used: on_image_paste(api, image) — returns True to consume the paste.
"""

from PySide6.QtCore import QBuffer, QByteArray, QIODevice, QUrl
from PySide6.QtNetwork import (
    QHttpMultiPart,
    QHttpPart,
    QNetworkAccessManager,
    QNetworkRequest,
)

# Active host. Each entry: url, file_field (multipart field name), fields (extra form fields),
# result ("text" = the reply body is the URL).
SERVICE = {
    "name": "uguu.se",
    "url": "https://uguu.se/upload?output=text",
    "file_field": "files[]",
    "fields": {},
    "result": "text",
}

# Reference hosts that need a real browser (Cloudflare-blocked from Qt) — swap into SERVICE to try:
#   Catbox (permanent):   url=https://catbox.moe/user/api.php          file_field=fileToUpload  fields={reqtype:fileupload}
#   Litterbox (24h):      url=https://litterbox.catbox.moe/resources/internals/api.php  fileToUpload  {reqtype:fileupload,time:24h}

_MAX = 3840  # scale very large pastes down before upload
_nam = None  # kept alive at module scope so in-flight uploads aren't garbage-collected


def _png_bytes(image):
    img = image
    if img.width() > _MAX or img.height() > _MAX:
        from PySide6.QtCore import Qt
        img = img.scaled(_MAX, _MAX, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
    ba = QByteArray()
    buf = QBuffer(ba)
    buf.open(QIODevice.OpenModeFlag.WriteOnly)
    img.save(buf, "PNG")
    buf.close()
    return bytes(ba)


def on_image_paste(api, image):
    global _nam
    if _nam is None:
        _nam = QNetworkAccessManager()
        _nam.setRedirectPolicy(QNetworkRequest.RedirectPolicy.NoLessSafeRedirectPolicy)
    data = _png_bytes(image)
    api.echo(f"[upload] sending pasted image ({len(data) // 1024} KB) to {SERVICE['name']}…")

    multipart = QHttpMultiPart(QHttpMultiPart.ContentType.FormDataType)
    multipart.setBoundary(b"----ComicIRCImagePaste7MA4YWxkTrZu0gW")  # simple boundary (CDN-friendly)
    for key, val in SERVICE.get("fields", {}).items():
        p = QHttpPart()
        p.setHeader(QNetworkRequest.KnownHeaders.ContentDispositionHeader, f'form-data; name="{key}"')
        p.setBody(str(val).encode())
        multipart.append(p)
    fp = QHttpPart()
    fp.setHeader(QNetworkRequest.KnownHeaders.ContentDispositionHeader,
                 f'form-data; name="{SERVICE["file_field"]}"; filename="image.png"')
    fp.setHeader(QNetworkRequest.KnownHeaders.ContentTypeHeader, "image/png")
    fp.setBody(data)
    multipart.append(fp)

    req = QNetworkRequest(QUrl(SERVICE["url"]))
    req.setHeader(QNetworkRequest.KnownHeaders.UserAgentHeader, "Mozilla/5.0 ComicIRC")
    reply = _nam.post(req, multipart)
    multipart.setParent(reply)

    def done():
        body = bytes(reply.readAll()).decode("utf-8", "replace").strip()
        ok = reply.error() == reply.NetworkError.NoError
        reply.deleteLater()
        url = body.splitlines()[0].strip() if (ok and body) else ""
        if url.startswith(("http://", "https://")):
            api.insert_input(url)
            api.echo(f"[upload] {url}")
        else:
            api.echo(f"[upload] failed: {body[:120] or 'no response'}")

    reply.finished.connect(done)
    return True  # consume the paste — we're handling it
