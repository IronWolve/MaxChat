"""Decode Microsoft Comic Chat ``.bgb`` (background) / ``.avb`` (character) art.

We never ship this art. The user points the app at their own Comic Chat install (the folder that
holds ``cchat.exe`` + the ``.avb``/``.bgb`` files; they download/install it themselves).

Format (reverse-engineered): a TLV chunk stream wraps **zlib-deflated Windows DIBs**. After a
``BITMAPINFOHEADER`` (starts ``28 00 00 00``) come two u32 (orig_len, cmpr_len) then a zlib stream
(``78 DA``). A preceding ``0x0101`` chunk holds an RGB palette. Backgrounds are one 315×315 paletted
image (4- or 8-bpp). Characters are many **2-bpp cells** — tall body poses + wide face/expression
cells — where colour index 0 is transparent (1=ink, 2=fill, 3=grey).
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

from PySide6.QtGui import QImage

BG_NATIVE = 315  # backgrounds are 315×315

# 2-bpp character palette → (B, G, R, A) for Format_ARGB32: 0 transparent, 1 ink, 2 fill, 3 grey.
# Ink is a soft grey, NOT pure black: body cells only ever use grey (index 3 = 150) for their outlines,
# so a dark face ink (index 1) left a harsh line around every head the grey bodies never had — keep the
# face ink (140) close to the body grey (150) so heads and bodies have a matching, soft outline.
_CELL_LUT = [(0, 0, 0, 0), (140, 140, 140, 255), (255, 255, 255, 255), (150, 150, 150, 255)]
_BYTE_EXP: list[bytes] = []  # byte → 16 bytes (4 ARGB pixels), MSB-first 2-bit packing
for _b in range(256):
    _chunk = bytearray()
    for _k in range(4):
        _chunk += bytes(_CELL_LUT[(_b >> (6 - 2 * _k)) & 3])
    _BYTE_EXP.append(bytes(_chunk))


def _header(data: bytes, j: int):
    return struct.unpack("<IiiHHIIiiII", data[j:j + 40])  # BITMAPINFOHEADER fields


def _inflate(data: bytes, j: int):
    """(w, h, bitcount, raw_pixels) for the zlib DIB whose header is at offset j."""
    _sz, w, h, _planes, bc, _comp, _si, *_rest = _header(data, j)
    _orig, cmpr = struct.unpack("<II", data[j + 40:j + 48])
    pixels = zlib.decompress(data[j + 48:j + 48 + cmpr])
    return w, abs(h), bc, pixels


def _palette_before(data: bytes, j: int) -> bytes:
    """RGB palette from the ``0x0101`` chunk preceding a DIB header → BGRA bytes."""
    pj = data.rfind(b"\x01\x01", max(0, j - 8000), j)
    if pj < 0:
        return b""
    _tag, _cklen, num = struct.unpack("<HHH", data[pj:pj + 6])
    if not (0 < num <= 256):
        return b""
    rgb = data[pj + 6:pj + 6 + num * 3]
    return b"".join(bytes((rgb[k * 3 + 2], rgb[k * 3 + 1], rgb[k * 3], 0)) for k in range(num))


def _paletted_image(data: bytes, j: int) -> QImage:
    """Decode a 4/8-bpp paletted DIB (backgrounds, icons) → QImage."""
    w, h, bc, pixels = _inflate(data, j)
    pal = _palette_before(data, j)
    nc = len(pal) // 4
    ih = struct.pack("<IiiHHIIiiII", 40, w, h, 1, bc, 0, len(pixels), 0, 0, nc, 0)
    dib = ih + pal + pixels
    bmp = b"BM" + struct.pack("<IHHI", 14 + len(dib), 0, 0, 14 + 40 + len(pal)) + dib
    return QImage.fromData(bmp, "BMP").convertToFormat(QImage.Format.Format_ARGB32)


def _cell_image(data: bytes, j: int) -> QImage:
    """Decode a 2-bpp character cell → transparent ARGB QImage (index 0 = transparent)."""
    w, h, _bc, pixels = _inflate(data, j)
    stride = ((w * 2 + 31) // 32) * 4
    rows = bytearray()
    for y in range(h):
        sr = h - 1 - y  # DIB rows are bottom-up
        row = pixels[sr * stride:(sr + 1) * stride]
        rows += b"".join(_BYTE_EXP[byte] for byte in row)[: w * 4]
    return QImage(bytes(rows), w, h, QImage.Format.Format_ARGB32).copy()


def _zlib_dib_offsets(data: bytes) -> list[int]:
    """Header offsets of every embedded image (a zlib stream sits 48 bytes after its header)."""
    out, i = [], 0
    while True:
        z = data.find(b"\x78\xda", i)
        if z < 0:
            break
        out.append(z - 48)
        i = z + 1
    return out


def load_background(path) -> QImage | None:
    """Decode a ``.bgb`` background → a 315×315 QImage (or None on failure)."""
    try:
        data = Path(path).read_bytes()
        j = data.find(b"\x28\x00\x00\x00")
        return _paletted_image(data, j) if j >= 0 else None
    except Exception:
        return None


_CELL_TABLES = {0x0004: 43, 0x0005: 35, 0x0009: 35, 0x000a: 33, 0x000b: 25, 0x000c: 25}


def _skip_palette(data: bytes, off: int) -> int:
    """If a 0x0101 palette chunk sits at off, skip past it to the BITMAPINFOHEADER."""
    if data[off:off + 2] == b"\x01\x01":
        num = struct.unpack("<H", data[off + 4:off + 6])[0]
        nxt = off + 6 + num * 3
        if data[nxt:nxt + 4] == b"\x28\x00\x00\x00":
            return nxt
    return off


def _parse_avb(data: bytes):
    """Walk the .avb chunk stream → {name, faces: {emotion_id: header_off}, bodies: [header_off]}.

    Cell tables (0x0004/5/9/a/b/c) hold fixed-size records: three u32 image pointers then a u16 id
    (emotion for faces, gesture for bodies). A pointer + the 0x0107 bias = the cell's header offset.
    Cells are sorted into faces (wide) / bodies (tall) by the decoded image's aspect ratio.
    """
    if data[:2] != b"\x81\x81":
        return None
    u16 = lambda o: struct.unpack("<H", data[o:o + 2])[0]  # noqa: E731
    u32 = lambda o: struct.unpack("<I", data[o:o + 4])[0]  # noqa: E731
    i, bias, name = 6, 0, ""
    faces: dict[int, int] = {}
    bodies: list[int] = []
    try:
        while i < len(data) - 2:
            tag = u16(i)
            if tag in (0x0006, 0x0007):
                break
            if tag >= 0x0100:
                if tag == 0x0107:
                    bias = u32(i + 4)
                i += 4 + u16(i + 2)
            elif tag == 0x0001:
                end = data.index(b"\x00", i + 2)
                name = data[i + 2:end].decode("latin-1", "replace")
                i = end + 1
            elif tag in (0x0002, 0x0008):
                i += 4
            elif tag == 0x0003:
                i += 6
            elif tag in _CELL_TABLES:
                rs, cnt, base = _CELL_TABLES[tag], u16(i + 2), i + 4
                for r in range(cnt):
                    rec = base + r * rs
                    off = _skip_palette(data, u32(rec) + bias)
                    eid = struct.unpack("<h", data[rec + 12:rec + 14])[0]
                    if not (0 <= off < len(data) - 40) or data[off:off + 4] != b"\x28\x00\x00\x00":
                        continue
                    _sz, w, h, *_ = _header(data, off)
                    if w <= 0 or h == 0:
                        continue
                    if abs(h) > w * 1.4:
                        bodies.append(off)
                    else:
                        faces.setdefault(eid, off)
                i = base + cnt * rs
            else:
                break
    except Exception:
        return None
    if not bodies and not faces:
        return None
    return {"name": name, "faces": faces, "bodies": bodies}


def _clean_bodies(bodies, faces):
    """Some ``.avb`` characters (tiki, xeno, tongtyed…) file a few HEAD/mask cells into the bodies list,
    so the default pose ends up a bare head with no body. Drop a "body" that is BOTH much shorter than the
    other bodies AND head-sized (≈ a face cell) — while keeping a legit short pose (a crouch is short but
    not head-sized). Never returns an empty list."""
    if not bodies or not faces or len(bodies) < 2:
        return bodies
    bmed = sorted(b.height() for b in bodies)[len(bodies) // 2]
    fmed = sorted(f.height() for f in faces.values())[len(faces) // 2]
    real = [b for b in bodies if not (b.height() < bmed * 0.85 and b.height() <= fmed * 1.4)]
    return real or bodies


def load_character_cells(path):
    """Decode a ``.avb`` → (name, icon, bodies [QImage], faces {emotion_id: QImage}).

    Uses the chunk-table parse (real emotion ids); falls back to a zlib scan + aspect classification.
    Head/mask cells mis-filed into the bodies list are dropped (see ``_clean_bodies``).
    """
    p = Path(path)
    try:
        data = p.read_bytes()
    except OSError:
        return p.stem, None, [], {}

    parsed = _parse_avb(data)
    if parsed is not None:
        bodies = [img for off in parsed["bodies"] if (img := _try(_cell_image, data, off)) is not None]
        faces = {eid: img for eid, off in parsed["faces"].items()
                 if (img := _try(_cell_image, data, off)) is not None}
        if bodies or faces:
            return (parsed["name"] or p.stem), None, _clean_bodies(bodies, faces), faces

    # fallback: scan every zlib image, classify by aspect, key faces by order
    bodies, faces_list = [], []
    for j in _zlib_dib_offsets(data):
        try:
            sz, w, h, _planes, bc, *_ = _header(data, j)
        except Exception:
            continue
        if sz != 40 or bc != 2 or w < 40 or abs(h) < 40:
            continue
        img = _try(_cell_image, data, j)
        if img is not None:
            (bodies if abs(h) > w * 1.4 else faces_list).append(img)
    faces = {i + 1: f for i, f in enumerate(faces_list)}
    return p.stem, None, _clean_bodies(bodies, faces), faces


def _try(fn, *a):
    try:
        return fn(*a)
    except Exception:
        return None


def bundled_art_dir():
    """Dev-only: a repo-bundled ``assets/cc-art`` folder of test art (gitignored; removed at release).
    Returns its path if present, else None — so comic mode "just works" while testing."""
    d = Path(__file__).resolve().parents[2] / "assets" / "cc-art"
    return str(d) if d.is_dir() else None


def scan_art_dir(folder):
    """(backgrounds, characters) = sorted lists of file Paths in a Comic Chat install dir."""
    d = Path(folder)
    if not d.is_dir():
        return [], []
    seen_bg, seen_ch = {}, {}
    for f in d.iterdir():
        ext = f.suffix.lower()
        if ext == ".bgb":
            seen_bg[f.name.lower()] = f
        elif ext == ".avb":
            seen_ch[f.name.lower()] = f
    return ([seen_bg[k] for k in sorted(seen_bg)], [seen_ch[k] for k in sorted(seen_ch)])
