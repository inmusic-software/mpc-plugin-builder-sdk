#!/usr/bin/env python3
"""make_thumbnail.py - draw a default browser thumbnail for an MPC plugin.

Usage:
    make_thumbnail.py --name NAME [--vendor VENDOR] [--accent AARRGGBB] --out soundsmode.png

The MPC shows a 270 x 110 picture for each instrument in its Sounds browser. It reads it from
"Plugin Skins/browser_images/soundsmode.png" inside the plugin folder; without it the instrument
only gets a plain tile with its name. tools/build.py writes the picture this tool draws unless the
project supplies its own (see docs/plugin-folder-format.md, "Browser thumbnail").

Standard library only. Python 3.9+.
"""

import argparse
import colorsys
import os
import struct
import sys
import zlib

WIDTH, HEIGHT = 270, 110

# 5 x 7 pixel font, one 5-bit row per line (bit 4 is the leftmost pixel).
FONT = {
    "A": (0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11), "B": (0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e),
    "C": (0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e), "D": (0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e),
    "E": (0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f), "F": (0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10),
    "G": (0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f), "H": (0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11),
    "I": (0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e), "J": (0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c),
    "K": (0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11), "L": (0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f),
    "M": (0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11), "N": (0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11),
    "O": (0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e), "P": (0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10),
    "Q": (0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d), "R": (0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11),
    "S": (0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e), "T": (0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04),
    "U": (0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e), "V": (0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04),
    "W": (0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a), "X": (0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11),
    "Y": (0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04), "Z": (0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f),
    "0": (0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e), "1": (0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e),
    "2": (0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f), "3": (0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e),
    "4": (0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02), "5": (0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e),
    "6": (0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e), "7": (0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08),
    "8": (0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e), "9": (0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c),
    "-": (0, 0, 0, 0x1f, 0, 0, 0), ".": (0, 0, 0, 0, 0, 0x0c, 0x0c), " ": (0, 0, 0, 0, 0, 0, 0),
}


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = bytearray(w * h * 4)

    def blend(self, x, y, rgb, alpha=1.0):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 4
            for k in range(3):
                self.px[i + k] = int(self.px[i + k] * (1 - alpha) + rgb[k] * alpha)
            self.px[i + 3] = 255

    def text(self, s, x, y, scale, rgb, alpha=1.0):
        for ch in s.upper():
            rows = FONT.get(ch, FONT[" "])
            for ry, bits in enumerate(rows):
                for rx in range(5):
                    if bits & (0x10 >> rx):
                        for dy in range(scale):
                            for dx in range(scale):
                                self.blend(x + rx * scale + dx, y + ry * scale + dy, rgb, alpha)
            x += 6 * scale


def text_width(s, scale):
    return max(0, len(s) * 6 * scale - scale)


def accent_rgb(name, accent):
    if accent:
        a = accent.strip().lstrip("#")
        if len(a) == 8:
            a = a[2:]  # AARRGGBB -> RRGGBB
        if len(a) == 6:
            try:
                return tuple(int(a[i:i + 2], 16) for i in (0, 2, 4))
            except ValueError:
                pass
    h = (sum((i + 1) * ord(c) for i, c in enumerate(name)) % 360) / 360.0
    r, g, b = colorsys.hsv_to_rgb(h, 0.65, 0.85)
    return int(r * 255), int(g * 255), int(b * 255)


def draw(name, vendor, accent):
    c = Canvas(WIDTH, HEIGHT)
    base = accent_rgb(name, accent)
    dark = tuple(int(v * 0.18) for v in base)
    light = tuple(min(255, int(v * 0.95)) for v in base)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            t = min(1.0, max(0.0, (x * 0.55 + (HEIGHT - y) * 1.1) / (WIDTH * 0.55 + HEIGHT * 1.1)))
            t = t * t * (3 - 2 * t)
            col = tuple(int(dark[k] + (light[k] - dark[k]) * t) for k in range(3))
            if ((x + y) // 6) % 2 == 0:  # faint diagonal stripes
                col = tuple(min(255, v + 6) for v in col)
            i = (y * WIDTH + x) * 4
            c.px[i:i + 4] = bytes((col[0], col[1], col[2], 255))

    title = name.strip() or "PLUGIN"
    scale = 1
    for s in range(8, 0, -1):
        if text_width(title, s) <= WIDTH - 30 and 7 * s <= 56:
            scale = s
            break
    tx = (WIDTH - text_width(title, scale)) // 2
    ty = 18 + (56 - 7 * scale) // 2
    c.text(title, tx + scale, ty + scale, scale, (0, 0, 0), 0.45)
    c.text(title, tx, ty, scale, (255, 255, 255))

    sub = vendor.strip()
    if sub:
        ss = 2 if text_width(sub, 2) <= WIDTH - 30 else 1
        c.text(sub, (WIDTH - text_width(sub, ss)) // 2, 86, ss, (255, 255, 255), 0.75)

    for x in range(WIDTH):  # thin frame
        c.blend(x, 0, (255, 255, 255), 0.25); c.blend(x, HEIGHT - 1, (0, 0, 0), 0.35)
    for y in range(HEIGHT):
        c.blend(0, y, (255, 255, 255), 0.25); c.blend(WIDTH - 1, y, (0, 0, 0), 0.35)
    return c


def write_png(path, canvas):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    stride = canvas.w * 4
    raw = b"".join(b"\0" + bytes(canvas.px[y * stride:(y + 1) * stride]) for y in range(canvas.h))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", canvas.w, canvas.h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Draw a default 270x110 MPC browser thumbnail.")
    ap.add_argument("--name", required=True, help="plugin name (large text)")
    ap.add_argument("--vendor", default="", help="manufacturer (small text)")
    ap.add_argument("--accent", default="", help="colour as AARRGGBB or RRGGBB (default: derived from the name)")
    ap.add_argument("--out", required=True, help="output PNG path")
    args = ap.parse_args(argv)
    write_png(args.out, draw(args.name, args.vendor, args.accent))
    print("wrote %s (%dx%d)" % (args.out, WIDTH, HEIGHT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
