#!/usr/bin/env python3
"""CgBI PNG decoder with full row-filter support (BGRA premultiplied)."""
import struct, zlib
import numpy as np
from PIL import Image


def load_cgbi(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not png'
    i = 8
    width = height = 0
    idat = bytearray()
    while i + 8 <= len(data):
        length = int.from_bytes(data[i:i+4], 'big')
        kind = data[i+4:i+8]
        body = data[i+8:i+8+length]
        if kind == b'IHDR':
            width, height, depth, color, comp, filt, inter = struct.unpack('>IIBBBBB', body)
            assert (depth, color, comp, filt, inter) == (8, 6, 0, 0, 0), (depth, color, comp, filt, inter)
        elif kind == b'IDAT':
            idat += body
        i += 12 + length
        if kind == b'IEND':
            break
    raw = zlib.decompressobj(-15).decompress(bytes(idat))
    stride = width * 4
    rows = np.frombuffer(raw, dtype=np.uint8).reshape(height, stride + 1)
    ft = rows[:, 0].copy()
    px = rows[:, 1:].reshape(height, width, 4).astype(np.uint8).copy()
    # undo PNG filters on the BGRA bytes (bpp = 4)
    bpp = 4
    prev = np.zeros((width, 4), dtype=np.uint8)
    for y in range(height):
        f = ft[y]
        cur = px[y].copy()
        if f == 0:
            pass
        elif f == 1:  # Sub
            flat = cur.reshape(-1).astype(np.int32)
            for x in range(bpp, width*4):
                flat[x] = (flat[x] + flat[x-bpp]) & 0xFF
            cur = flat.reshape(width, 4).astype(np.uint8)
        elif f == 2:  # Up
            cur = (cur.astype(np.int32) + prev.astype(np.int32)).astype(np.uint8)
        elif f == 3:  # Average
            flat = cur.reshape(-1).astype(np.int32)
            pv = prev.reshape(-1).astype(np.int32)
            for x in range(len(flat)):
                a = flat[x-bpp] if x >= bpp else 0
                flat[x] = (flat[x] + ((a + pv[x]) >> 1)) & 0xFF
            cur = flat.reshape(width, 4).astype(np.uint8)
        elif f == 4:  # Paeth
            flat = cur.reshape(-1).astype(np.int32)
            pv = prev.reshape(-1).astype(np.int32)
            for x in range(len(flat)):
                a = flat[x-bpp] if x >= bpp else 0
                b = pv[x]
                c = pv[x-bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                if pa <= pb and pa <= pc:
                    pr = a
                elif pb <= pc:
                    pr = b
                else:
                    pr = c
                flat[x] = (flat[x] + pr) & 0xFF
            cur = flat.reshape(width, 4).astype(np.uint8)
        else:
            raise ValueError('bad filter %d' % f)
        px[y] = cur
        prev = cur
    bgra = px.reshape(height, width, 4).astype(np.uint32)
    b, g, r, a = bgra[..., 0], bgra[..., 1], bgra[..., 2], bgra[..., 3]
    scale = np.where(a > 0, np.maximum(1, (255 + a // 2) // np.maximum(a, 1)), 0)
    r = np.where(a > 0, np.minimum(255, r * scale), 0)
    g = np.where(a > 0, np.minimum(255, g * scale), 0)
    b = np.where(a > 0, np.minimum(255, b * scale), 0)
    rgba = np.stack([r, g, b, a], axis=-1).astype(np.uint8)
    return Image.fromarray(rgba, 'RGBA')


if __name__ == '__main__':
    import sys
    im = load_cgbi(sys.argv[1])
    im.save(sys.argv[2])
