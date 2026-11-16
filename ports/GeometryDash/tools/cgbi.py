#!/usr/bin/env python3
"""Decode Apple's CgBI PNG variant (raw-deflate IDAT, premultiplied BGRA).

The 2013 iOS builds ship their atlases as CgBI: a PNG with a CgBI chunk before
IHDR, a raw deflate stream with no zlib header, and premultiplied BGRA pixels.
Pillow and ffmpeg both reject the stream, so decode it here.
"""
import struct
import zlib
import numpy as np
from PIL import Image


def load_cgbi(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a png: ' + str(path))
    i = 8
    width = height = 0
    idat = bytearray()
    while i + 8 <= len(data):
        length = int.from_bytes(data[i:i + 4], 'big')
        kind = data[i + 4:i + 8]
        body = data[i + 8:i + 8 + length]
        if kind == b'IHDR':
            width, height, depth, color, comp, filt, inter = struct.unpack('>IIBBBBB', body)
            if (depth, color, comp, filt, inter) != (8, 6, 0, 0, 0):
                raise ValueError('unsupported png variant: %r' % ((depth, color, comp, filt, inter),))
        elif kind == b'IDAT':
            idat += body
        i += 12 + length
        if kind == b'IEND':
            break
    raw = zlib.decompressobj(-15).decompress(bytes(idat))
    stride = width * 4
    if len(raw) != height * (stride + 1):
        raise ValueError('unexpected decoded size: %d' % len(raw))
    rows = np.frombuffer(raw, dtype=np.uint8).reshape(height, stride + 1)
    if rows[:, 0].any():
        raise ValueError('png row filters are not supported (CgBI should be all zero)')
    bgra = rows[:, 1:].reshape(height, width, 4).astype(np.uint32)
    b, g, r, a = bgra[..., 0], bgra[..., 1], bgra[..., 2], bgra[..., 3]
    scale = np.where(a > 0, np.maximum(1, (255 + a // 2) // np.maximum(a, 1)), 0)
    r = np.where(a > 0, np.minimum(255, r * scale), 0)
    g = np.where(a > 0, np.minimum(255, g * scale), 0)
    b = np.where(a > 0, np.minimum(255, b * scale), 0)
    rgba = np.stack([r, g, b, a], axis=-1).astype(np.uint8)
    return Image.fromarray(rgba, 'RGBA')
