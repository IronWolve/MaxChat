#!/usr/bin/env python3
"""Convert any image (.jpg/.png/...) to MCB1 (MaxChat Bitmap, 1-bit).

Outputs a .mcb file and/or a Lua PICS-table entry for assets/scripts/bbs.lua.

.mcb file format (text):
    MCB1 <id> <w> <h> <hash> <enc>
    <data, wrapped at 76 chars>

Pixel encodings (see DEVDOCS/MC_DATA.md, "MCB1 Bitmap Cell Art"):
    raw1  packed 1-bit pixels, hex armor
    rle1  alternating runs from bit 0, one byte per run (0 = flip), hex armor
    raw1z / rle1z  the same, Z85 armor (~25% overhead vs hex's 100%)

The encoder auto-picks rle vs raw (whichever is smaller) and uses Z85 unless
--hex is given. <hash> is the BBS frame_hash (djb-33 mod 2^32) of the data
string exactly as transmitted.

Examples:
    mcb1_convert.py photo.jpg                          # preview + photo.mcb
    mcb1_convert.py photo.jpg --threshold 110          # flat-area look, rle-friendly
    mcb1_convert.py logo.png --lua --id logo --title "My Logo"
"""

import argparse
import sys

try:
    from PIL import Image, ImageOps, ImageEnhance, ImageFilter
except ImportError:
    sys.exit("error: needs Pillow (pip install Pillow)")

Z85 = ("0123456789abcdefghijklmnopqrstuvwxyz"
       "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#")


def z85_encode(data: bytes) -> str:
    data += b"\x00" * ((-len(data)) % 4)
    out = []
    for i in range(0, len(data), 4):
        v = int.from_bytes(data[i:i + 4], "big")
        block = []
        for _ in range(5):
            block.append(Z85[v % 85])
            v //= 85
        out.extend(reversed(block))
    return "".join(out)


def frame_hash(text: str) -> str:
    h = 0
    for ch in text:
        h = (h * 33 + ord(ch)) % 4294967296
    return "%08X" % h


def prepare(path, width, height, contrast, blur, threshold):
    im = Image.open(path).convert("L")
    im = ImageOps.autocontrast(im, cutoff=2)
    im = ImageEnhance.Contrast(im).enhance(contrast)
    im = ImageOps.fit(im, (width, height))
    if blur > 0:
        im = im.filter(ImageFilter.GaussianBlur(blur))
    if threshold is not None:
        return im.point(lambda p: 255 if p > threshold else 0).convert(
            "1", dither=Image.NONE)
    return im.convert("1", dither=Image.FLOYDSTEINBERG)


def image_bits(img):
    px = img.load()
    w, h = img.size
    return [1 if px[c, r] else 0 for r in range(h) for c in range(w)]


def rle_bytes(bits):
    out = bytearray()
    cur, run = 0, 0
    for bit in bits:
        if bit == cur:
            run += 1
            if run == 255:
                out += b"\xff\x00"
                run = 0
        else:
            out.append(run)
            cur, run = bit, 1
    out.append(run)
    return bytes(out)


def raw_bytes(bits):
    out = bytearray()
    for i in range(0, len(bits), 8):
        v = 0
        for j in range(8):
            v = (v << 1) | bits[i + j]
        out.append(v)
    return bytes(out)


def encode(bits, hex_armor):
    rle, raw = rle_bytes(bits), raw_bytes(bits)
    base, payload = ("rle1", rle) if len(rle) < len(raw) else ("raw1", raw)
    if hex_armor:
        return base, payload.hex().upper()
    return base + "z", z85_encode(payload)


def preview(img):
    px = img.load()
    w, h = img.size
    lines = []
    for r in range(0, h, 2):
        line = []
        for c in range(w):
            top = px[c, r] != 0
            bot = px[c, r + 1] != 0 if r + 1 < h else False
            line.append("█" if top and bot else
                        "▀" if top else
                        "▄" if bot else " ")
        lines.append("".join(line))
    return "\n".join(lines)


def lua_entry(pic_id, title, w, h, enc, data, chunk=280):
    chunks = [data[i:i + chunk] for i in range(0, len(data), chunk)]
    body = ",\n      ".join('"%s"' % c for c in chunks)
    return ('  {{ id = "{id}", title = "{title}", w = {w}, h = {h}, '
            'enc = "{enc}",\n    data = {{\n      {body}\n    }} }},').format(
                id=pic_id, title=title, w=w, h=h, enc=enc, body=body)


def main():
    ap = argparse.ArgumentParser(
        description="Convert an image to MCB1 (1-bit BBS art).")
    ap.add_argument("input", help="source image (.jpg/.png/anything PIL reads)")
    ap.add_argument("-o", "--output", help="output .mcb path (default: <input>.mcb)")
    ap.add_argument("--id", help="asset id (default: input basename)")
    ap.add_argument("--title", help="display title (default: id)")
    ap.add_argument("--size", default="80x50",
                    help="pixel grid WxH; H should be 2x terminal rows (default 80x50)")
    ap.add_argument("--threshold", type=int, default=None, metavar="0-255",
                    help="hard threshold instead of dithering (flat areas, rle-friendly)")
    ap.add_argument("--contrast", type=float, default=1.6)
    ap.add_argument("--blur", type=float, default=0.8)
    ap.add_argument("--hex", action="store_true",
                    help="hex armor (raw1/rle1) instead of Z85 (raw1z/rle1z)")
    ap.add_argument("--lua", action="store_true",
                    help="print a bbs.lua PICS entry instead of writing a .mcb file")
    ap.add_argument("--no-preview", action="store_true")
    args = ap.parse_args()

    try:
        width, height = (int(v) for v in args.size.lower().split("x"))
    except ValueError:
        sys.exit("error: --size must look like 80x50")
    if width < 2 or height < 2 or width > 255 or height > 510:
        sys.exit("error: size out of range (max 255 wide; height 2x rows)")

    img = prepare(args.input, width, height, args.contrast, args.blur,
                  args.threshold)
    bits = image_bits(img)
    enc, data = encode(bits, args.hex)
    digest = frame_hash(data)

    import os
    pic_id = args.id or os.path.splitext(os.path.basename(args.input))[0]
    pic_id = "".join(ch for ch in pic_id if ch.isalnum() or ch in "-_") or "pic"
    title = args.title or pic_id

    if not args.no_preview:
        print(preview(img))
    print(f"# {pic_id}: {width}x{height} {enc}  {len(data)} chars on the wire  "
          f"hash {digest}", file=sys.stderr)

    if args.lua:
        print(lua_entry(pic_id, title, width, height, enc, data))
        return

    out = args.output or (os.path.splitext(args.input)[0] + ".mcb")
    with open(out, "w", encoding="ascii") as f:
        f.write(f"MCB1 {pic_id} {width} {height} {digest} {enc}\n")
        for i in range(0, len(data), 76):
            f.write(data[i:i + 76] + "\n")
    print(f"# wrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
