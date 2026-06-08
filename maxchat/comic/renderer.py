"""Render a comic panel: background + characters (facing each other) + speech balloons over each speaker.

Characters are laid out left-to-right (speaking order), spread toward the walls as the crowd grows, each
facing the centre and trimmed so its feet rest on the floor line. Balloons hang ABOVE the person who said
them (MS Comic Chat style): each goes in its speaker's column, a speaker's lines stack down their own
column, and the lowest balloon grows a long thin tail to that speaker's head. A ``/me`` action is a
tailless narration box. Lettering shrinks until the tallest column fits. Called with empty
``actors``/``lines`` it renders just the background. Returns a square ``QPixmap``.
"""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QPointF, QRect, Qt
from PySide6.QtGui import (
    QColor,
    QFont,
    QFontDatabase,
    QImage,
    QPainter,
    QPainterPath,
    QPen,
    QPixmap,
)

WRAP = int(Qt.TextFlag.TextWordWrap)
WRAP_CENTRE = int(Qt.TextFlag.TextWordWrap | Qt.AlignmentFlag.AlignCenter)

_FONTS_DIR = Path(__file__).resolve().parents[2] / "assets" / "fonts"
_COMIC_FAMILY: str | None = None  # resolved + registered on first use ("" if the bundled font is missing)


def _comic_family() -> str:
    """Register the bundled OFL comic font (Comic Relief) once; return its family name ("" if absent)."""
    global _COMIC_FAMILY
    if _COMIC_FAMILY is None:
        _COMIC_FAMILY = ""
        for name in ("ComicRelief-Regular.ttf", "ComicRelief-Bold.ttf"):
            fid = QFontDatabase.addApplicationFont(str(_FONTS_DIR / name))
            fams = QFontDatabase.applicationFontFamilies(fid) if fid >= 0 else []
            if fams and not _COMIC_FAMILY:
                _COMIC_FAMILY = fams[0]
    return _COMIC_FAMILY


def _comic_font(point_size: int, bold: bool = False) -> QFont:
    """Comic-style balloon lettering — the bundled comic font, or the Qt default if it isn't there."""
    fam = _comic_family()
    f = QFont(fam) if fam else QFont()
    f.setPointSize(point_size)
    f.setBold(bold)
    return f


def _floor(size: int) -> int:
    return size - max(3, int(size * 0.02)) - int(size * 0.085)  # feet line, above the caption strip


def _place_chars(size: int, actors, target_h: int):
    """Lay characters along the floor at the GIVEN height: widths from each figure's aspect, shrunk to fit
    the panel width, spread across a crowd-widening band. Returns (xs, widths, target_h)."""
    n = len(actors)
    widths = []
    for ch, emo, _nick, pose in actors:
        im = ch.image_trimmed(emo, "right", pose)
        widths.append(int(im.width() * target_h / im.height())
                      if (im is not None and im.height()) else int(target_h * 0.6))
    gap = max(2, int(size * 0.012))
    total = sum(widths) + gap * (n - 1)
    if total > size * 0.96 and total > 0:  # shrink to fit the panel width; never overlap
        f = size * 0.96 / total
        target_h = max(20, int(target_h * f))
        widths = [max(1, int(w * f)) for w in widths]
        total = sum(widths) + gap * (n - 1)
    if n == 1:
        xs = [(size - widths[0]) // 2]
    else:
        factor = 0.58 if n == 2 else 0.76 if n == 3 else 0.9   # fraction of the panel the group spans
        span = max(total, size * factor)
        left = max(int(size * 0.02), (size - span) / 2)
        step = max(0.0, (span - total) / (n - 1))             # extra space spread BETWEEN characters
        xs, xacc = [], float(left)
        for w in widths:
            xs.append(int(round(xacc)))
            xacc += w + gap + step
    return xs, widths, target_h


def _character_layout(size: int, actors, n_lines: int):
    """Initial placement by crowd + line count — used only to get cx for the balloon pass and the spill
    probe. ``render_panel`` RE-places the characters BELOW the fitted balloons so a bubble never covers a
    face."""
    n = len(actors)
    nb = max(1, n_lines)
    crowd = 1.0 if n <= 2 else 0.86 if n == 3 else 0.74 if n <= 4 else 0.64
    target_h = int(size * (0.62 if nb <= 1 else 0.48 if nb <= 3 else 0.4) * crowd)
    xs, widths, target_h = _place_chars(size, actors, target_h)
    feet = _floor(size)
    cx = [xs[i] + widths[i] // 2 for i in range(n)]
    return xs, widths, target_h, feet, cx, feet - target_h


def render_panel(size: int, background, actors, lines, captions: bool = True,
                 caption_scale: float = 1.0, caption_colors=None) -> QPixmap:
    """actors = [(character, emotion, nick, pose), ...]; lines = [(actor_idx, text, think, action), ...].
    ``captions`` draws the nick label under each character; ``caption_colors`` maps lower-nick → box hex
    (per-speaker colour) and ``caption_scale`` sizes the label."""
    pm = QPixmap(size, size)
    pm.fill(QColor("white"))
    p = QPainter(pm)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
    if background is not None and not background.isNull():
        p.drawImage(QRect(0, 0, size, size), background, background.rect())

    feet = _floor(size)
    if actors:
        n = len(actors)
        gap = max(6, int(size * 0.02))
        tail_room = max(gap, int(size * 0.06))  # gap below the stack for a tail / thought-puff trail
        min_h, max_h = int(size * 0.30), int(size * 0.62)  # the character's height floor / ceiling
        *_, cx, _ct = _character_layout(size, actors, len(lines))   # initial cx (x barely moves w/ h)
        # 1) fit the balloons into the TOP — above where a minimum-size character's head would reach
        font, rows = _fit_balloons(p, size, lines, cx, actors, feet - min_h - gap)
        stack_bottom = max((r[3] + r[5] for r in rows), default=max(6, size // 26))  # lowest balloon edge
        # 2) the character sits BELOW the balloons (never over a face); clamp its height to [min, max]
        char_top = max(feet - max_h, min(stack_bottom + tail_room, feet - min_h))
        target_h = feet - char_top
        xs, widths, target_h = _place_chars(size, actors, target_h)
        cx = [xs[i] + widths[i] // 2 for i in range(n)]
        for i, (ch, emo, nick, pose) in enumerate(actors):
            facing = "right" if cx[i] < size / 2 else "left"  # face toward the centre / each other
            im = ch.image_trimmed(emo, facing, pose)
            if im is not None and im.height():
                p.drawImage(QRect(xs[i], feet - target_h, widths[i], target_h), im)
                if captions:
                    _caption(p, size, nick, xs[i], widths[i], feet,
                             box=(caption_colors or {}).get(nick.lower()), scale=caption_scale)
        _draw_balloons(p, rows, font, size, char_top, cx)
    elif lines:  # balloons with no characters (rare) — just fit them in the upper panel
        font, rows = _fit_balloons(p, size, lines, [], actors, int(size * 0.72))
        _draw_balloons(p, rows, font, size, size, [])

    p.setPen(QPen(QColor("black"), 2))
    p.setBrush(Qt.BrushStyle.NoBrush)
    p.drawRect(1, 1, size - 2, size - 2)  # panel frame
    p.end()
    return pm


def _balloon_maxw(size: int, n: int) -> int:                # wide bubbles; narrower as the crowd grows
    return int(size * (0.66 if n <= 1 else 0.58 if n == 2 else 0.5 if n == 3 else 0.42))


def _fit_top(size: int, has_actors: bool) -> int:
    """The lowest y the balloon stack may reach: above a minimum-size character (so bubbles never need to
    cover a face). The character then sizes itself to whatever room is left below the actual stack."""
    if not has_actors:
        return size - 6
    return _floor(size) - int(size * 0.30) - max(6, int(size * 0.02))


def _fit_balloons(p: QPainter, size: int, lines, cx, actors, bottom: int):
    """Shrink the comic font until the whole balloon stack fits above ``bottom``. Returns (font, rows)."""
    if not lines:
        return _comic_font(max(8, size // 26)), []
    maxw = _balloon_maxw(size, max(1, len(cx)))
    font, rows = None, []
    for fs in range(max(8, size // 26), 6, -1):
        font = _comic_font(fs)
        rows, overflow = _layout(p, font, size, lines, cx, actors, maxw)
        if overflow <= bottom:
            break
    return font, rows


def panel_min_font(size: int, actors, lines) -> int:
    """The font point-size this panel would actually use (largest that fits, floored). The panel-spill
    logic compares this to a readable floor: if it's smaller, the panel is too crammed → start a new one."""
    cx = []
    if actors:
        *_, cx, _ = _character_layout(size, actors, len(lines))
    bottom = _fit_top(size, bool(actors))
    maxw = _balloon_maxw(size, max(1, len(cx)))
    img = QImage(1, 1, QImage.Format.Format_ARGB32)
    p = QPainter(img)
    chosen = max(7, size // 26)
    for fs in range(max(8, size // 26), 6, -1):
        chosen = fs
        _rows, overflow = _layout(p, _comic_font(fs), size, lines, cx, actors, maxw)
        if overflow <= bottom:
            break
    p.end()
    return chosen


def _caption(p: QPainter, size: int, nick: str, x: int, w: int, feet: int,
             box: str | None = None, scale: float = 1.0) -> None:
    f = _comic_font(max(6, int((size // 30) * max(0.5, scale))), bold=True)  # bold + a solid box reads
    p.setFont(f)
    fm = p.fontMetrics()
    maxw = max(int(w * 1.25), int(size * 0.34))  # keep the caption roughly under its own character…
    text = fm.elidedText(nick, Qt.TextElideMode.ElideRight, maxw)  # …elide a long nick instead of sprawling
    tw = fm.horizontalAdvance(text) + 12
    th = fm.height() + 4
    bx = max(2, min(int(x + w / 2 - tw / 2), size - tw - 2))  # centred, but kept inside the panel
    by = min(feet + 2, size - th - 2)                         # just BELOW the feet (no body overlap)
    bg = QColor(box) if box else QColor(54, 54, 54)           # per-speaker colour, or the classic grey
    bg.setAlpha(236)
    lum = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue()  # auto-contrast the text
    txt = QColor(20, 20, 20) if lum > 140 else QColor("white")
    p.setPen(QPen(QColor(255, 255, 255, 150) if lum <= 140 else QColor(0, 0, 0, 110), 1))  # readable edge
    p.setBrush(bg)
    p.drawRoundedRect(bx, by, tw, th, 4, 4)
    p.setPen(txt)
    p.drawText(QRect(bx, by, tw, th), int(Qt.AlignmentFlag.AlignCenter), text)


def _layout(p: QPainter, font: QFont, size: int, lines, cx, actors, maxw: int):
    """Lay balloons out OVER their speaker: each goes in its speaker's column (centred on that character),
    a speaker's lines stack down their own column. Returns (geoms, overflow_bottom). Each geom is
    (action, body, bx, by, bw, bh, col, think, is_col_tail)."""
    n = max(1, len(cx))
    gap = max(6, size // 32)          # a touch more stacking room so each balloon has space for a tail
    top = max(6, size // 26)
    col_y = [top] * n                 # next free y in each speaker's column
    placed: list[tuple] = []          # (x, y, w, h) of every box, for collision tests
    rows = []

    def collides(bx, by, bw, bh):
        return any(bx < ox + ow and bx + bw > ox and by < oy + oh + gap and by + bh + gap > oy
                   for ox, oy, ow, oh in placed)

    def place(center, bw, bh, start_y):
        """Find the HIGHEST free slot at/below start_y. Keep the box up: slide it sideways (into the gap
        nearest the speaker) before dropping to a new row — so a wide neighbour can't shove it down below
        empty space. Returns (bx, by)."""
        target = int(min(max(4, center - bw / 2), size - 4 - bw))  # ideal x: centred over the speaker
        by = start_y
        while True:
            cands = {target, 4, size - 4 - bw}                     # centred, or flush to either wall
            for ox, oy, ow, oh in placed:
                if by < oy + oh + gap and by + bh + gap > oy:      # a box sharing this row's height
                    cands.add(int(min(max(4, ox + ow + gap), size - 4 - bw)))   # …slot just right of it
                    cands.add(int(min(max(4, ox - bw - gap), size - 4 - bw)))   # …slot just left of it
            valid = [c for c in cands if not collides(c, by, bw, bh)]
            if valid:
                return min(valid, key=lambda c: abs(c - target)), by  # the free x closest to the speaker
            by += max(6, bh // 3)                                  # row is full → step down and rescan
            if by + bh > size - 4:                                 # safety: don't run off the panel
                return target, by

    for idx, text, think, action in lines:
        col = idx if 0 <= idx < n else 0
        center = cx[col] if col < len(cx) else size / 2
        if action:
            nick = actors[idx][2] if 0 <= idx < len(actors) else ""
            body = f"{nick} {text}".strip()
            ital = QFont(font)
            ital.setItalic(True)
            p.setFont(ital)
            r = p.boundingRect(QRect(0, 0, maxw - 16, size), WRAP, body)
            bw, bh = min(maxw, r.width() + 16), r.height() + 8
        else:
            body = text
            p.setFont(font)
            r = p.boundingRect(QRect(0, 0, maxw - 18, size), WRAP, body)
            bw, bh = min(maxw, r.width() + 20), r.height() + 10
        bx, by = place(center, bw, bh, col_y[col])
        placed.append((bx, by, bw, bh))
        col_y[col] = by + bh + gap
        rows.append([action, body, bx, by, bw, bh, col, think, False])
    # EVERY speech/thought balloon gets a tail toward its speaker; cap each so it stops above the next
    # balloon in its column (can't spear the one below). The lowest in a column has nothing below, so
    # it runs all the way down to the speaker's head.
    for r in rows:
        r[8] = not r[0]  # is_tail: all balloons except /me action boxes
        below = [o[3] for o in rows if o is not r and o[6] == r[6] and o[3] > r[3] + 1]
        r.append(min(below) if below else None)  # tail_stop: top of the next balloon below, else None
    overflow = max((y + h for _x, y, _w, h in placed), default=top)
    return rows, overflow


def _draw_tail(p: QPainter, size, bx, by, bw, bh, tail_x, think, long, head_y, to_head=True) -> None:
    """A tail under the balloon, leaning toward the speaker, stopping just before the head so it never
    spears the figure. ``to_head`` balloons (the lowest in a column — the one that actually points at the
    speaker) get a LONG tail that runs all the way down to them; the short hops BETWEEN stacked balloons
    stay short. Drawn BEFORE the boxes, so it tucks behind a neighbour."""
    cy = by + bh - 1
    room = head_y - (by + bh)                          # vertical gap before the speaker's head
    # root near whichever bottom corner the speaker is on; centred if the speaker is under the box
    if tail_x < bx + bw * 0.33:
        root = bx + max(6, int(bw * 0.10))           # bottom-left corner
    elif tail_x > bx + bw * 0.67:
        root = bx + bw - max(6, int(bw * 0.10))       # bottom-right corner
    else:
        root = max(bx + 10, min(int(tail_x), bx + bw - 10))

    if think:  # a trail of shrinking puffs running from the balloon DOWN toward the speaker (or head)
        span = max(5, room - 3)                          # span the gap — always a visible trail
        end_x = root + (tail_x - root) * 0.6             # drift toward the speaker as it descends
        big = min(max(3.0, size * 0.019), span * 0.5)    # scale puffs to the panel AND the available gap
        steps = max(2, min(7, int(span / max(4.0, big))))
        p.setPen(QPen(QColor("black"), max(1, size // 260)))
        p.setBrush(QColor(255, 255, 255))
        for s in range(1, steps + 1):
            t = s / steps
            r = big * (1.0 - 0.45 * t)                   # shrink toward the head
            p.drawEllipse(QPointF(root + (end_x - root) * t, (by + bh) + span * t), r, r * 0.85)
        return

    if room < 8:
        return                                          # balloon already on the speaker → don't spear it

    def _tri(b0, b1, apex):  # filled white triangle + black outline (the base edge stays open)
        path = QPainterPath()
        path.moveTo(b0[0], b0[1])
        path.lineTo(apex[0], apex[1])
        path.lineTo(b1[0], b1[1])
        path.closeSubpath()
        p.setPen(Qt.PenStyle.NoPen)
        p.fillPath(path, QColor(255, 255, 255))
        p.setPen(QPen(QColor("black"), 2))
        p.drawLine(int(b0[0]), int(b0[1]), int(apex[0]), int(apex[1]))
        p.drawLine(int(apex[0]), int(apex[1]), int(b1[0]), int(b1[1]))

    if not to_head:                                     # short hop down to the balloon stacked below
        cap = int(size * 0.09) if long else int(size * 0.045)
        tip_y = by + bh + min(room - 3, max(8, cap))
        tip_x = root + int((tail_x - root) * 0.55)
        half = max(4.0, bw * 0.05)
        _tri((max(bx + 2, root - half), cy), (min(bx + bw - 2, root + half), cy), (tip_x, tip_y))
        return

    # to_head: point AT the speaker. Exit the SIDE of the balloon when the speaker is well outside it
    # horizontally, so the tail angles toward them instead of dropping through a figure under the box.
    tip_y = by + bh + max(8, room - max(4, int(size * 0.02)))  # just above the speaker's head
    margin = bw * 0.12
    if tail_x < bx - margin or tail_x > bx + bw + margin:       # speaker off to one side → side tail
        ex = (bx + 1) if tail_x < bx else (bx + bw - 1)         # the near vertical edge
        rooty = by + bh * 0.5
        halfv = max(5.0, bh * 0.24)
        _tri((ex, rooty - halfv), (ex, rooty + halfv), (float(tail_x), tip_y))
    else:                                                       # speaker under the box → bottom tail
        tip_x = root + int((tail_x - root) * 0.82)
        half = max(4.0, bw * 0.05)
        _tri((max(bx + 2, root - half), cy), (min(bx + bw - 2, root + half), cy), (tip_x, tip_y))


def _draw_balloons(p: QPainter, rows, font, size: int, char_top: int, cx) -> None:
    """Draw the already-laid-out balloons: tails first (so they tuck behind the boxes), then the boxes +
    text on top. ``char_top`` is where the characters' heads start, so tails stop above them."""
    if not rows:
        return
    multi = max(1, len(cx)) >= 2
    # PASS 1 — tails, behind everything
    for action, body, bx, by, bw, bh, col, think, is_tail, tail_stop in rows:
        if action or not is_tail:
            continue
        floor_y = char_top if tail_stop is None else tail_stop  # stop at the head, or the next balloon
        _draw_tail(p, size, bx, by, bw, bh, cx[col] if col < len(cx) else size / 2, think, multi,
                   floor_y, tail_stop is None)  # to_head balloons get a long tail down to the speaker
    # PASS 2 — boxes + text, on top
    for action, body, bx, by, bw, bh, col, think, is_tail, tail_stop in rows:
        if action:  # /me → a tailless narration box
            ital = QFont(font)
            ital.setItalic(True)
            p.setPen(QPen(QColor("black"), 1))
            p.setBrush(QColor(255, 251, 224))
            p.drawRect(bx, by, bw, bh)
            p.setFont(ital)
            p.setPen(QColor(40, 40, 40))
            p.drawText(QRect(bx + 8, by + 4, bw - 16, bh - 8), WRAP_CENTRE, body)
            continue
        p.setPen(QPen(QColor("black"), 2))
        p.setBrush(QColor(255, 255, 255))
        p.drawRoundedRect(bx, by, bw, bh, 18 if think else 6, 18 if think else 6)
        p.setPen(QColor("black"))
        p.setFont(font)
        p.drawText(QRect(bx + 9, by + 5, bw - 18, bh - 10), WRAP_CENTRE, body)
