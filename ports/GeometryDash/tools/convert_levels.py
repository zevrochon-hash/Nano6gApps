#!/usr/bin/env python3
"""Extract the original Geometry Dash 1.0 levels from its armv7 executable.

The 2013 build keeps every built-in level as one plain-text level string in
__TEXT: a colour header (kS1..kA1) followed by objects, each a key/value list
separated by semicolons. Keys 1/2/3 are object id / X / Y; key 6 is rotation.
Coordinates are pixels, Y is the sprite centre, the ground top is y=0.

The converter writes levels.bin (a compact runtime format) plus a files.lst
line, so the port can load the real levels instead of hand-written ones.
"""
import argparse
import re
import struct
from pathlib import Path

# built-in levels, in level order (matched to the kS1 headers in the executable)
LEVELS = [
    ('Stereo Madness', 'stereo.wav'),
    ('Back on Track', 'back.wav'),
    ('Polargeist', 'polar.wav'),
    ('Dry Out', 'dry.wav'),
    ('Base After Base', 'base.wav'),
    ("Can't Let Go", 'cant.wav'),
    ('Jumper', 'jumper.wav'),
]

ALLOWED = set(b'0123456789.,;eE+-kSA')


def level_string(data, start):
    """The level string from its kS1 header up to the next NUL / other text."""
    end = start
    while end < len(data) and data[end] in ALLOWED:
        end += 1
    return data[start:end]


def parse(text):
    objs = []
    for chunk in text.split(b';'):
        fields = chunk.split(b',')
        kv = {}
        for i in range(0, len(fields) - 1, 2):
            key = fields[i]
            if not key.isdigit():
                continue
            try:
                kv[int(key)] = float(fields[i + 1])
            except ValueError:
                continue
        if 1 not in kv or 2 not in kv or 3 not in kv:
            continue
        rot = kv.get(6, 0.0)
        # key 5 is the editor's flip flag; 1 means the object is upside down.
        # The rotation key alone therefore does not describe ceiling spikes.
        if int(kv.get(5, 0.0)) == 1:
            rot += 180.0
        objs.append((int(kv[1]), kv[2], kv[3], rot))
    return objs


def parse_colours(text):
    """kS1..kS3 are the background RGB, kS4..kS6 the ground RGB."""
    kv = {}
    for chunk in text.split(b';'):
        fields = chunk.split(b',')
        for i in range(0, len(fields) - 1, 2):
            if fields[i].startswith(b'kS') and fields[i][2:].isdigit():
                try:
                    kv[int(fields[i][2:])] = int(float(fields[i + 1]))
                except ValueError:
                    pass
    bg = [kv.get(1, 40), kv.get(2, 62), kv.get(3, 255)]
    gr = [kv.get(4, 0), kv.get(5, 19), kv.get(6, 200)]
    return bg, gr


def convert(exe, output):
    data = Path(exe).read_bytes()
    starts = [m.start() for m in re.finditer(b'kS1,', data)]
    if len(starts) < len(LEVELS):
        raise ValueError('found %d level headers, expected %d' % (len(starts), len(LEVELS)))
    blob = bytearray(b'GDLS')
    blob += struct.pack('<HH', 2, len(LEVELS))
    for (name, song), off in zip(LEVELS, starts):
        text = level_string(data, off)
        objs = parse(text)
        if not objs:
            raise ValueError('empty level at 0x%x' % off)
        bg, gr = parse_colours(text)
        xs = [o[1] for o in objs]
        ys = [o[2] for o in objs]
        if any(abs(v - round(v)) > 0.01 for v in xs + ys):
            raise ValueError('fractional coordinates in ' + name)
        blob += struct.pack('<B', len(name))
        blob += name.encode()
        blob += struct.pack('<B', len(song))
        blob += song.encode()
        blob += bytes(bg) + bytes(gr)
        blob += struct.pack('<H', len(objs))
        for oid, x, y, rot in objs:
            blob += struct.pack('<BhhH', oid, int(round(x)), int(round(y)), int(round(rot)) & 0xFFFF)
        print('  %-16s %4d objects  x %d..%d  y %d..%d  bg %s ground %s'
              % (name, len(objs), min(xs), max(xs), min(ys), max(ys), bg, gr))
    output.mkdir(parents=True, exist_ok=True)
    (output / 'levels.bin').write_bytes(blob)
    lst = output / 'files.lst'
    lines = [l for l in lst.read_text().splitlines() if not l.startswith('levels.bin')] if lst.exists() else []
    lines.append('levels.bin %d' % len(blob))
    lst.write_text('\n'.join(sorted(lines)) + '\n')
    print('wrote levels.bin (%d bytes, %d levels)' % (len(blob), len(LEVELS)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    convert(args.exe, args.output)
