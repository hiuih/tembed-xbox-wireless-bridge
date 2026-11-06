#!/usr/bin/env python3
"""Convert the simulator's raw RGB565 frames (320x170) to PNG, scaled up so details are visible."""
import sys, zlib, struct, glob, os
SCALE = 3
def conv(path):
    d = open(path, 'rb').read()
    w, h = 320, 170
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            v = d[(y * w + x) * 2] | (d[(y * w + x) * 2 + 1] << 8)
            r = ((v >> 11) & 31) * 255 // 31; g = ((v >> 5) & 63) * 255 // 63; b = (v & 31) * 255 // 31
            row += bytes((r, g, b)) * SCALE
        for _ in range(SCALE): rows.append(b'\x00' + bytes(row))
    raw = b''.join(rows)
    def chunk(t, c): return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * SCALE, h * SCALE, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    out = path[:-4] + '.png'
    open(out, 'wb').write(png); return out
for p in sorted(glob.glob(os.path.join(sys.argv[1] if len(sys.argv) > 1 else 'out', '*.raw'))): print(conv(p))
