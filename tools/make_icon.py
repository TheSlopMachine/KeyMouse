#!/usr/bin/env python3
"""Regenerate res/app.ico, res/idle.ico and res/active.ico (stdlib only).

Each icon is a rounded tile with a white mouse-pointer arrow, rendered with 4x4
supersampling at every size so small sizes stay crisp:
    app.ico     blue   - exe / window icon
    idle.ico    grey   - tray icon, mouse mode off
    active.ico  green  - tray icon, mouse mode on
"""
import os
import struct

SIZES = (16, 20, 24, 32, 40, 48, 64)
SS = 4  # supersampling per axis

# Classic pointer outline on a 16x16 grid.
ARROW = [(3, 1), (3, 13), (6, 10), (8.5, 15), (10.5, 14), (8, 9.2), (12, 9.2)]
ARROW = [(0.5 + (x - 7.5) / 19.0, 0.5 + (y - 8.0) / 19.0) for x, y in ARROW]


def in_polygon(u, v, poly):
    inside = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > v) != (yj > v) and u < (xj - xi) * (v - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


def in_tile(u, v, margin=0.03, radius=0.22):
    lo, hi = margin, 1.0 - margin
    if not (lo <= u <= hi and lo <= v <= hi):
        return False
    cx = min(max(u, lo + radius), hi - radius)
    cy = min(max(v, lo + radius), hi - radius)
    return (u - cx) ** 2 + (v - cy) ** 2 <= radius ** 2


def render(size, tile):
    """Return rows (top to bottom) of (r, g, b, a) tuples."""
    rows = []
    n = SS * SS
    for y in range(size):
        row = []
        for x in range(size):
            r = g = b = cnt = 0
            for sy in range(SS):
                for sx in range(SS):
                    u = (x + (sx + 0.5) / SS) / size
                    v = (y + (sy + 0.5) / SS) / size
                    if in_tile(u, v):
                        c = (255, 255, 255) if in_polygon(u, v, ARROW) else tile
                        r += c[0]; g += c[1]; b += c[2]; cnt += 1
            if cnt:
                row.append((r // cnt, g // cnt, b // cnt, 255 * cnt // n))
            else:
                row.append((0, 0, 0, 0))
        rows.append(row)
    return rows


def dib(size, rows):
    """BMP-style icon image: header + bottom-up BGRA + 1bpp AND mask."""
    head = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = b"".join(
        b"".join(struct.pack("<BBBB", b, g, r, a) for r, g, b, a in row)
        for row in reversed(rows)
    )
    mask = bytes(((size + 31) // 32) * 4 * size)
    return head + pixels + mask


def write_ico(path, tile):
    images = [dib(s, render(s, tile)) for s in SIZES]
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for s, data in zip(SIZES, images):
        out += struct.pack("<BBBBHHII", s, s, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    with open(path, "wb") as f:
        f.write(out + b"".join(images))
    print("wrote", path)


if __name__ == "__main__":
    res = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res")
    write_ico(os.path.join(res, "app.ico"),    (45, 108, 223))
    write_ico(os.path.join(res, "idle.ico"),   (96, 102, 112))
    write_ico(os.path.join(res, "active.ico"), (31, 164, 99))
