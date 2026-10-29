#!/usr/bin/env python3
"""Draws claude95.ico: a little beige monitor with an orange chat bubble.

Windows 95 can't read PNG or 32-bit icons, so this writes classic 16-colour
(4 bits per pixel) 32x32 and 16x16 images with transparency masks.
"""
import struct
import sys

# The standard Windows 16-colour palette.
PAL = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128), (128, 0, 128),
       (0, 128, 128), (192, 192, 192), (128, 128, 128), (255, 0, 0), (0, 255, 0),
       (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]
K, DGRAY, LGRAY, WHITE, TEAL, NAVY, YELLOW, RED, DRED = 0, 8, 7, 15, 6, 4, 11, 9, 1
T = None  # transparent

def icon32():
    g = [[T] * 32 for _ in range(32)]
    def rect(x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                g[y][x] = c
    rect(2, 3, 27, 23, LGRAY)            # monitor case
    rect(2, 3, 27, 3, WHITE); rect(2, 3, 2, 23, WHITE)
    rect(27, 3, 27, 23, DGRAY); rect(2, 23, 27, 23, DGRAY)
    rect(5, 6, 24, 19, K)                # screen bezel
    rect(6, 7, 23, 18, NAVY)             # screen
    for x, y in [(8, 9), (9, 10), (10, 11), (9, 12), (8, 13)]:   # ">" prompt
        g[y][x] = WHITE
    rect(12, 13, 15, 13, WHITE)          # "_" cursor
    rect(10, 24, 19, 25, DGRAY)          # neck
    rect(6, 26, 23, 28, LGRAY); rect(6, 28, 23, 28, DGRAY)   # base
    # orange-red chat bubble, top right
    rect(18, 0, 31, 9, RED)
    rect(18, 0, 31, 0, K); rect(18, 9, 31, 9, K); rect(18, 0, 18, 9, K); rect(31, 0, 31, 9, K)
    for i, (x, y) in enumerate([(21, 10), (20, 11), (19, 12)]):
        g[y][x] = K
        if i < 2: g[y][x + 1] = RED
    g[10][22] = K; g[11][22] = K
    for x in (21, 24, 27):               # "..."
        rect(x, 4, x + 1, 5, WHITE)
    return g

def shrink(g):
    """16x16 by taking each 2x2 block's most common colour (transparent wins ties)."""
    out = []
    for y in range(0, 32, 2):
        row = []
        for x in range(0, 32, 2):
            block = [g[y][x], g[y][x + 1], g[y + 1][x], g[y + 1][x + 1]]
            solid = [c for c in block if c is not None]
            row.append(max(set(solid), key=solid.count) if len(solid) >= 2 else None)
        out.append(row)
    return out

def dib(g):
    n = len(g)
    hdr = struct.pack("<IiiHHIIiiII", 40, n, n * 2, 1, 4, 0, 0, 0, 0, 16, 0)
    pal = b"".join(struct.pack("BBBB", b, gr, r, 0) for r, gr, b in PAL)
    xor_row = (n * 4 + 31) // 32 * 4
    and_row = (n + 31) // 32 * 4
    xor = and_ = b""
    for row in reversed(g):
        bits = bytearray(xor_row)
        mask = bytearray(and_row)
        for x, c in enumerate(row):
            v = 0 if c is None else c
            bits[x // 2] |= v << (4 if x % 2 == 0 else 0)
            if c is None:
                mask[x // 8] |= 0x80 >> (x % 8)
        xor += bytes(bits)
        and_ += bytes(mask)
    return hdr + pal + xor + and_

def main(path):
    big = icon32()
    images = [(32, dib(big)), (16, dib(shrink(big)))]
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for size, data in images:
        out += struct.pack("<BBBBHHII", size, size, 16, 0, 1, 4, len(data), offset)
        offset += len(data)
    out += b"".join(d for _, d in images)
    with open(path, "wb") as f:
        f.write(out)

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "claude95.ico")
