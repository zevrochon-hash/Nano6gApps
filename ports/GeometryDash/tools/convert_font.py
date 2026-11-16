#!/usr/bin/env python3
"""Convert the original's bigFont (BMFont + CgBI atlas) into nano font pages.

The game draws text with gfx_quad, so the font is packed into the same 256x128
pages the sprite atlas uses. font.bin holds one record per ASCII character:
which page it is on, its rect, its offset from the text origin and its advance.
Pages are written as pageNN.bin and appended to files.lst.

Requires Pillow + numpy. Usage: convert_font.py APP_DIR DATA_DIR
"""
import re
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cgbi import load_cgbi

PAGE_W, PAGE_H = 256, 128
FIRST, LAST = 32, 126


def parse_fnt(path):
    """char id -> (x, y, w, h, xoff, yoff, xadv)"""
    out = {}
    for line in Path(path).read_text().splitlines():
        if not line.startswith('char '):
            continue
        n = dict((k, int(v)) for k, v in re.findall(r'(\w+)=(-?\d+)', line))
        if 'id' in n:
            out[n['id']] = (n['x'], n['y'], n['width'], n['height'],
                            n['xoffset'], n['yoffset'], n['xadvance'])
    return out


def pack(glyphs):
    """Shelf-pack glyph images into 256x128 pages with a 1 px border."""
    pages, shelves = [], []
    for g in sorted(glyphs, key=lambda g: -g['im'].shape[0]):
        im = g['im']
        h, w = im.shape[:2]
        place = None
        for pi, shelf in enumerate(shelves):
            for row in shelf:
                if row[2] >= h + 2 and row[1] + w + 2 <= PAGE_W:
                    place = pi, row[1], row[0]
                    row[1] += w + 2
                    break
            if place:
                break
            bottom = sum(r[2] for r in shelf)
            if bottom + h + 2 <= PAGE_H:
                shelf.append([bottom, w + 2, h + 2])
                place = pi, 0, bottom
                break
        if place is None:
            place = len(pages), 0, 0
            pages.append(np.zeros((PAGE_H, PAGE_W, 4), dtype=np.uint8))
            shelves.append([[0, w + 2, h + 2]])
        pi, x, y = place
        pages[pi][y + 1:y + 1 + h, x + 1:x + 1 + w] = im
        g['packed'] = pi, x + 1, y + 1
    return pages


def convert(app, output):
    chars = parse_fnt(app / 'bigFont.fnt')
    sheet = load_cgbi(app / 'bigFont.png')
    ra = np.asarray(sheet, dtype=np.uint8)
    glyphs = []
    for code in range(FIRST, LAST + 1):
        if code not in chars:
            continue
        x, y, w, h, xo, yo, adv = chars[code]
        if w <= 0 or h <= 0:
            glyphs.append(dict(code=code, im=np.zeros((1, 1, 4), dtype=np.uint8),
                               xo=xo, yo=yo, adv=adv, w=0, h=0))
            continue
        im = ra[y:y + h, x:x + w]
        if im.shape[2] == 3:
            im = np.concatenate([im, np.full(im.shape[:2] + (1,), 255, np.uint8)], axis=2)
        glyphs.append(dict(code=code, im=im, xo=xo, yo=yo, adv=adv, w=w, h=h))

    pages = pack([g for g in glyphs if g['w']])
    records = bytearray()
    for g in glyphs:
        pi, x, y = g.get('packed', (0, 0, 0))
        records += struct.pack('<8H', pi, x, y, g['w'], g['h'], g['xo'] & 0xFFFF,
                               g['yo'] & 0xFFFF, g['adv'] & 0xFFFF)
    out = Path(output)
    sprites = (out / 'sprites.bin').read_bytes()
    start = struct.unpack_from('<H', sprites, 4)[0]
    for i, page in enumerate(pages):
        raw = bytearray()
        for r, g, b, a in page.reshape(-1, 4):
            r, g, b, a = int(r), int(g), int(b), int(a)
            raw += bytes((r * a // 255, g * a // 255, b * a // 255, a))
        (out / ('page%02d.bin' % (start + i))).write_bytes(raw)
    (out / 'font.bin').write_bytes(struct.pack('<4s3H6x', b'GDF1', len(glyphs), FIRST, len(pages))
                                   + bytes(records))

    lst = out / 'files.lst'
    names = [p.name for p in sorted(out.glob('*')) if p.is_file() and p.name != 'files.lst']
    lst.write_text(''.join('%s %d\n' % (n, (out / n).stat().st_size) for n in names))
    print('font: %d glyphs, %d pages (page%02d..page%02d), %d bytes'
          % (len(glyphs), len(pages), start, start + len(pages) - 1, (out / 'font.bin').stat().st_size))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: convert_font.py APP_DIR DATA_DIR')
    convert(Path(sys.argv[1]), Path(sys.argv[2]))
