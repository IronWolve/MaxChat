"""Character & emotion model — built from a decoded Comic Chat ``.avb`` (see ``assets.py``).

A character has body-pose cells + expression face cells keyed by emotion id. ``image(emotion, facing)``
composes a body with the right face, hanging the head **on the body's neck** (the body cell starts at
the neck, so the head sits above with the chin overlapping it), then mirrors for facing. Cached.
"""

from __future__ import annotations

from PySide6.QtGui import QImage, QPainter

from maxchat.comic import assets

# Comic Chat's emotion wheel: neutral at the centre, eight emotions around the rim.
EMOTIONS = ["neutral", "happy", "laughing", "coy", "bored", "scared", "sad", "angry", "shouting"]


def _alpha_bbox(img: QImage):
    """Bounding box (x0, y0, x1, y1) of non-transparent pixels, or None."""
    img = img.convertToFormat(QImage.Format.Format_ARGB32)
    w, h, bpl = img.width(), img.height(), img.bytesPerLine()
    buf = bytes(img.constBits())[: bpl * h]
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        a = buf[y * bpl + 3: y * bpl + w * 4: 4]
        lo, hi = -1, -1
        for x, v in enumerate(a):
            if v > 16:
                if lo < 0:
                    lo = x
                hi = x
        if lo >= 0:
            x0, x1 = min(x0, lo), max(x1, hi)
            y0, y1 = min(y0, y), max(y1, y)
    return (x0, y0, x1, y1) if x1 >= 0 else None


def _neck(img: QImage):
    """Locate the neck as ``(y, cx)``: the topmost row of the body's CENTRAL column.

    The torso centre is the densest column of body pixels; the neck is the highest row that actually
    spans that centre. This ignores off-centre protrusions that reach higher than the shoulders — a
    **raised arm / pointing hand** — which would otherwise pull the head up (and sideways) off the body.
    """
    img = img.convertToFormat(QImage.Format.Format_ARGB32)
    w, h, bpl = img.width(), img.height(), img.bytesPerLine()
    buf = bytes(img.constBits())[: bpl * h]
    col_count = [0] * w
    spans: dict[int, tuple] = {}
    for y in range(h):
        a = buf[y * bpl + 3: y * bpl + w * 4: 4]
        lo, hi = -1, -1
        for x, v in enumerate(a):
            if v > 16:
                col_count[x] += 1
                if lo < 0:
                    lo = x
                hi = x
        if lo >= 0:
            spans[y] = (lo, hi)
    if not spans:
        return None
    cx = max(range(w), key=lambda x: col_count[x])  # densest column ≈ torso centre line
    for y in sorted(spans):  # topmost row that spans the torso centre = the neck / shoulders
        lo, hi = spans[y]
        if lo <= cx <= hi:
            return (y, cx)
    y = min(spans)  # fallback (shouldn't happen): the very top row
    return (y, (spans[y][0] + spans[y][1]) // 2)


class Character:
    def __init__(self, name: str, icon, bodies, faces) -> None:
        self.name = name
        self.icon = icon
        self.bodies = bodies            # list[QImage] — tall pose cells
        self.faces = faces              # dict[int, QImage] — emotion id → face cell
        self._ids = sorted(faces)
        self._base: dict[tuple, QImage] = {}      # (emotion, pose) → right-facing composite
        self._composed: dict[tuple, QImage] = {}  # (emotion, facing, pose) → composite
        self._trimmed: dict[tuple, QImage] = {}   # (emotion, facing, pose) → composite cropped to content
        self._neck_cache: dict[int, tuple] = {}   # id(body) → neck anchor (per pose)

    @property
    def ok(self) -> bool:
        return bool(self.bodies or self.faces)

    def face_cell(self, emotion: str):
        """The face-cell image for an emotion (used by the self-view emotion picker)."""
        return self._face_for(emotion)

    def _face_for(self, emotion: str):
        if not self._ids:
            return None
        idx = EMOTIONS.index(emotion) if emotion in EMOTIONS else 0
        eid = self._ids[round(idx / max(1, len(EMOTIONS) - 1) * (len(self._ids) - 1))]
        return self.faces[eid]

    def _neck_of(self, body):
        """Cached neck anchor per body cell (each pose has its own neck)."""
        k = id(body)
        if k not in self._neck_cache:
            self._neck_cache[k] = _neck(body) or False  # (neck_y, neck_cx); robust to raised arms
        return self._neck_cache[k]

    def _compose(self, body, face):
        if body is None:
            return face
        if face is None:
            return body
        geom = self._neck_of(body)
        if not geom:
            return body
        neck_y, neck_cx = geom
        fb = _alpha_bbox(face)
        if fb is None:
            return body
        fx0, fy0, fx1, fy1 = fb
        overlap = int((fy1 - fy0) * 0.22)         # chin sinks ONTO the shoulders (≈22% of the head) so the
        #                                           head seats on the neck instead of floating back off it
        fdx = neck_cx - (fx0 + fx1) // 2          # face centre → neck centre
        fdy = neck_y + overlap - fy1              # face chin → just below the neck; head rises above
        left = min(0, fdx)
        right = max(body.width(), fdx + face.width())
        top = max(0, -fdy)
        out = QImage(right - left, body.height() + top, QImage.Format.Format_ARGB32)
        out.fill(0)
        p = QPainter(out)
        p.drawImage(-left, top, body)
        p.drawImage(fdx - left, fdy + top, face)
        p.end()
        return out

    def image_trimmed(self, emotion: str = "neutral", facing: str = "right", pose: int = 0):
        """Like image(), but cropped to its non-transparent content — so a figure's actual FEET (not the
        cell's transparent bottom padding) sit on the floor line. Cached."""
        emo = emotion if emotion in EMOTIONS else "neutral"
        pose = (pose % len(self.bodies)) if self.bodies else 0
        key = (emo, facing, pose)
        if key not in self._trimmed:
            img = self.image(emo, facing, pose)
            if img is not None:
                bb = _alpha_bbox(img)
                if bb is not None:
                    x0, y0, x1, y1 = bb
                    img = img.copy(x0, y0, x1 - x0 + 1, y1 - y0 + 1)
            self._trimmed[key] = img
        return self._trimmed[key]

    def image(self, emotion: str = "neutral", facing: str = "right", pose: int = 0) -> QImage | None:
        emo = emotion if emotion in EMOTIONS else "neutral"
        pose = (pose % len(self.bodies)) if self.bodies else 0  # body gesture/stance cell
        key = (emo, facing, pose)
        if key in self._composed:
            return self._composed[key]
        bkey = (emo, pose)
        if bkey not in self._base:
            body = self.bodies[pose] if self.bodies else None
            self._base[bkey] = self._compose(body, self._face_for(emo))
        img = self._base[bkey]
        if img is not None and facing == "left":
            img = img.mirrored(True, False)
        self._composed[key] = img
        return img


_cache: dict[str, Character] = {}


def load_character(path) -> Character | None:
    key = str(path)
    if key in _cache:
        return _cache[key]
    name, icon, bodies, faces = assets.load_character_cells(path)
    ch = Character(name, icon, bodies, faces)
    if not ch.ok:
        return None
    _cache[key] = ch
    return ch
