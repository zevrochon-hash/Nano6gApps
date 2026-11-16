#!/usr/bin/env python3
"""Convert the iBeer 4.1 app into nano texture pages, atlas, font and sound.

Source art (all CgBI PNGs / JPEGs) comes from the IPA's iBeer.app bundle.
Everything the port draws is packed into the 256x128 pages the nano's GL
driver is proven with; wide strips are split, the foam/soda animations are
downsampled to 128x32 frames, and the drink labels are halved.

Requires Pillow + numpy.  Usage: convert_assets.py APP_DIR DATA_DIR
"""
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cgbi import load_cgbi

PAGE_W, PAGE_H = 256, 128

# drink label -> liquid colour (0-255), sampled from the original's own icons
DRINKS = [
    ('beerlager',  '_beerlager.png',  (196, 152,  62)),
    ('litebeer',   '_litebeer.png',   (198, 168, 104)),
    ('gudness',    '_gudness.png',    (124,  92,  58)),
    ('amber',      '_amber.png',      (196, 128,  58)),
    ('mouthwash',  '_mouthwash.png',  (120, 176,  92)),
    ('milk',       '_milk.png',       (232, 226, 208)),
    ('milkchoc',   '_milkchoc.png',   (128,  82,  52)),
    ('milkstraw',  '_milkstraw.png',  (226, 150, 140)),
    ('milkalien',  '_milkalien.png',  (120, 200, 110)),
    ('sodacola',   '_sodacola.png',   ( 84,  44,  32)),
    ('sodalime',   '_sodalime.png',   (150, 190, 130)),
    ('sodawater',  '_sodawater.png',  (170, 200, 220)),
    ('sodaenergy', '_sodaenergy.png', (200, 130,  50)),
    ('winered',    '_winered.png',    (128,  48,  62)),
    ('winerose',   '_winerose.png',   (196, 120, 130)),
    ('winewhite',  '_winewhite.png',  (212, 200, 160)),
    ('winebrute',  '_winebrute.png',  (222, 198, 130)),
    ('coffee',     '_coffee.png',     (110,  74,  48)),
]

# original sound -> nano wav name
SOUNDS = {
    'burp.caf': 'burp.wav',
    'ch_bubbles.caf': 'bubbles.wav',
    'ch_fill.caf': 'pour.wav',
    'shakeFizz.caf': 'fizz.wav',
    'breakglass.caf': 'break.wav',
    'Servo.caf': 'click.wav',
    'Data01.caf': 'blip.wav',
}
RATE = 22050


def load(app, name):
    p = app / name
    if p.suffix.lower() == '.png':
        return load_cgbi(p)
    return Image.open(p).convert('RGBA')


def foam_frames(app):
    """12 frames of the 66-frame foam animation at 128x32 (half the native
    strip, so two tiles fill the 240px panel without smearing)."""
    out = []
    for n in range(15, 81, 6):
        f = app / ('Foam_%04d.png' % n)
        if not f.exists():
            continue
        im = load_cgbi(f).resize((128, 32), Image.LANCZOS)
        out.append(('foam_%02d' % len(out), im))
    return out


def soda_frames(app):
    out = []
    for n in range(15, 51, 5):
        f = app / ('Soda_%04d.png' % n)
        if not f.exists():
            continue
        im = load_cgbi(f).resize((128, 32), Image.LANCZOS)
        out.append(('soda_%02d' % len(out), im))
    return out


def pack(tiles):
    """Shelf-pack tiles into 256x128 pages (1 px extruded border)."""
    pages, shelves = [], []
    for tile in sorted(tiles, key=lambda t: -t['im'].height):
        im = tile['im']
        w, h = im.size
        if w + 2 > PAGE_W or h + 2 > PAGE_H:
            raise SystemExit('tile %dx%d cannot fit a page' % (w, h))
        placement = None
        for pi, shelf in enumerate(shelves):
            for row in shelf:
                if row[2] >= h + 2 and row[1] + w + 2 <= PAGE_W:
                    placement = pi, row[1], row[0]
                    row[1] += w + 2
                    break
            if placement:
                break
            bottom = sum(row[2] for row in shelf)
            if bottom + h + 2 <= PAGE_H:
                shelf.append([bottom, w + 2, h + 2])
                placement = pi, 0, bottom
                break
        if placement is None:
            placement = len(pages), 0, 0
            pages.append(Image.new('RGBA', (PAGE_W, PAGE_H)))
            shelves.append([[0, w + 2, h + 2]])
        pi, x, y = placement
        padded = Image.new('RGBA', (w + 2, h + 2))
        padded.paste(im, (1, 1))
        padded.paste(im.crop((0, 0, 1, h)).resize((1, h + 2)), (0, 0))
        padded.paste(im.crop((w - 1, 0, w, h)).resize((1, h + 2)), (w + 1, 0))
        padded.paste(im.crop((0, 0, w, 1)), (1, 0))
        padded.paste(im.crop((0, h - 1, w, h)), (1, h + 1))
        padded.putpixel((0, 0), im.getpixel((0, 0)))
        padded.putpixel((w + 1, 0), im.getpixel((w - 1, 0)))
        padded.putpixel((0, h + 1), im.getpixel((0, h - 1)))
        padded.putpixel((w + 1, h + 1), im.getpixel((w - 1, h - 1)))
        pages[pi].paste(padded, (x, y))
        tile['packed'] = pi, x + 1, y + 1
    return pages


def strips(image_id, tile, opaque):
    w, h = tile.size
    out = []
    for dx in range(0, w, PAGE_W - 2):
        out.append(dict(image=image_id, im=tile.crop((dx, 0, min(dx + PAGE_W - 2, w), h)),
                        dx=dx, opaque=opaque))
    return out


def beer_texture(app):
    """A 240x126 grayscale 'beer' field, tinted by each drink's colour at draw
    time: fine carbonation grain, soft vertical streaks and a few larger
    bubble pockets.  Near-white so the tint stays saturated."""
    rng = np.random.default_rng(11)
    w, h = 240, 126
    acc = np.zeros((h, w), np.float64)
    weights = [0.35, 0.6, 0.9, 1.0]   # let the fine detail dominate, no large flat cells
    for octave, amp in enumerate(weights):
        sw, sh = max(3, w >> (4 - octave)), max(2, h >> (4 - octave))
        base = rng.random((sh, sw))
        up = Image.fromarray((base * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
        acc += np.asarray(up, np.float64) / 255.0 * amp
    acc = acc / sum(weights) * (1.0 / acc.max() if acc.max() > 0 else 1.0)

    # vertical streaks: a column field, smoothed across a little
    cols = rng.random((1, w))
    col_img = Image.fromarray((cols * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
    streaks = np.asarray(col_img.filter(ImageFilter.GaussianBlur(1.6)), np.float64) / 255.0

    # a few soft bubble pockets
    pockets = Image.new('L', (w, h), 0)
    d = __import__('PIL.ImageDraw', fromlist=['ImageDraw']).Draw(pockets)
    for _ in range(48):
        x, y = int(rng.random() * w), int(rng.random() * h)
        r = 1 + int(rng.random() * 6)
        d.ellipse((x - r, y - r, x + r, y + r), fill=int(70 + rng.random() * 110))
    pockets = np.asarray(pockets.filter(ImageFilter.GaussianBlur(1.0)), np.float64) / 255.0

    # fine per-pixel carbonation grain so the pour does not look smeared
    grain = rng.random((h, w))
    grain_img = Image.fromarray((grain * 255).astype(np.uint8))
    grain = np.asarray(grain_img.filter(ImageFilter.GaussianBlur(0.7)), np.float64) / 255.0

    field = 0.73 + 0.17 * acc + 0.06 * streaks + 0.10 * (grain - 0.5) + 0.12 * pockets
    field = np.clip(field, 0.44, 1.0)

    out = np.zeros((h, w, 4), np.uint8)
    g = (field * 255).astype(np.uint8)
    out[:, :, 0] = g
    out[:, :, 1] = g
    out[:, :, 2] = g
    out[:, :, 3] = 255
    return Image.fromarray(out, 'RGBA')


def build_atlas(app, output):
    images, groups = [], {False: [], True: []}
    tiles = []

    def add(name, im, opaque=None):
        if opaque is None:
            opaque = im.getchannel('A').getextrema()[0] == 255
        image_id = len(images)
        images.append(dict(name=name, w=im.width, h=im.height, opaque=opaque))
        groups[opaque].extend(strips(image_id, im, opaque))

    for name, im in foam_frames(app):
        add(name, im)
    for name, im in soda_frames(app):
        add(name, im)
    for i, (name, src, _rgb) in enumerate(DRINKS):
        add('drink_%02d' % i, load(app, src).resize((47, 22), Image.LANCZOS))
    for name, src, size in [('egg', 'egg', (120, 120)),
                            ('bubble', 'bubble16', None), ('steam', 'steam', None),
                            ('halo', 'Halo', None), ('halo_m', 'halo_ms', None),
                            ('halo_s', 'halo_xs', None), ('glow', 'glowdot', None),
                            ('hotglow', 'hotglow', None), ('dirty', 'DirtyHalo', None),
                            ('white', 'white', None),
                            ('cond', 'Condensation', (126, 126)),
                            ('liquid', None, None),
                            ('btn_menu', 'Bmenu', None), ('btn_done', 'Bdone', None),
                            ('btn_ok', 'Bok', None), ('btn_refill', 'BbarB', None)]:
        im = beer_texture(app) if src is None else load(
            app, src + ('.png' if (app / (src + '.png')).exists() else '.jpg'))
        if size:
            im = im.resize(size, Image.LANCZOS)
        add(name, im)

    page_formats = []
    for opaque in (True, False):
        pages = pack(groups[opaque])
        for t in groups[opaque]:
            pi, x, y = t['packed']
            t['packed'] = pi + len(page_formats), x, y
            tiles.append(t)
        for page in pages:
            raw = bytearray()
            if opaque:
                for r, g, b, a in page.getdata():
                    raw += struct.pack('<H', (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3))
            else:
                for r, g, b, a in page.getdata():
                    raw += bytes((r * a // 255, g * a // 255, b * a // 255, a))
            (output / ('page%02d.bin' % len(page_formats))).write_bytes(raw)
            page_formats.append(0 if opaque else 2)

    parts, records = bytearray(), bytearray()
    for image_id, image in enumerate(images):
        start = len(parts) // 16
        mine = sorted((t for t in tiles if t['image'] == image_id), key=lambda t: t['dx'])
        for t in mine:
            pi, x, y = t['packed']
            parts += struct.pack('<8H', pi, x, y, t['im'].width, t['im'].height, t['dx'], 0, 0)
        records += struct.pack('<32s6H', image['name'].encode(), image['w'], image['h'], start,
                               len(parts) // 16 - start, 1 if image['opaque'] else 0, 0)
    header = struct.pack('<4s4H', b'FBA1', len(page_formats), len(images), len(parts) // 16, 0)
    (output / 'sprites.bin').write_bytes(header + bytes(page_formats) + bytes(-len(page_formats) % 4)
                                         + records + parts)
    print('atlas: %d images, %d pages' % (len(images), len(page_formats)))
    return len(page_formats)


def build_background(app, output):
    """A warm bar backdrop: horizontal planks with grain, procedurally drawn."""
    rng = np.random.default_rng(23)
    w, h = 256, 128
    img = Image.new('RGB', (w, h))
    d = __import__('PIL.ImageDraw', fromlist=['ImageDraw']).Draw(img)
    planks = [(0, 26), (26, 54), (54, 82), (82, 128)]
    base = [(74, 46, 28), (66, 40, 24), (80, 51, 31), (59, 36, 22)]
    for (y0, y1), col in zip(planks, base):
        d.rectangle((0, y0, w, y1), fill=col)
    # wavy grain lines inside each plank
    for (y0, y1), col in zip(planks, base):
        span = y1 - y0
        for _ in range(34):
            y = y0 + 2 + int(rng.random() * (span - 4))
            shade = 0.75 + rng.random() * 0.6
            c = tuple(int(min(255, max(0, ch * shade))) for ch in col)
            pts = []
            x = 0
            while x <= w:
                pts.append((x, y + int(2.2 * np.sin(x * 0.045 + y * 0.3 + rng.random() * 0.4))))
                x += 6
            d.line(pts, fill=c, width=1)
    # plank seams
    for y0, _y1 in planks[1:]:
        d.rectangle((0, y0 - 1, w, y0), fill=(28, 16, 9))
        d.rectangle((0, y0, w, y0), fill=(96, 63, 38))
    img = img.filter(ImageFilter.GaussianBlur(0.7))
    a = np.asarray(img).astype(np.float64)
    # gentle vignette so the edges fall off like a lit bar top
    yy, xx = np.mgrid[0:h, 0:w]
    vx = 1.0 - 0.38 * (np.abs(xx - w / 2) / (w / 2)) ** 1.6
    vy = 1.0 - 0.30 * (np.abs(yy - h * 0.35) / (h * 0.85)) ** 1.5
    a *= (vx * vy)[:, :, None]
    out = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGB').convert('RGBA')
    x, y = -1, -1
    raw = bytearray()
    for r, g, b, _a in out.getdata():
        raw += bytes((r, g, b, 255))
    (output / 'bg.bin').write_bytes(raw)
    print('background: planks')


def build_font(output):
    """Render an Arial-metric font (Liberation Sans) into font.bin + pages."""
    from PIL import ImageFont, ImageDraw
    font = ImageFont.truetype('/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf', 22)
    first, last = 32, 126
    glyphs = []
    for code in range(first, last + 1):
        ch = chr(code)
        bbox = font.getbbox(ch)
        w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
        if w <= 0 or h <= 0:
            glyphs.append(dict(code=code, im=np.zeros((1, 1, 4), np.uint8), xo=0, yo=0,
                               adv=int(font.getlength(ch)), w=0, h=0))
            continue
        im = Image.new('RGBA', (w, h))
        ImageDraw.Draw(im).text((-bbox[0], -bbox[1]), ch, font=font, fill=(255, 255, 255, 255))
        adv = int(round(font.getlength(ch)))
        glyphs.append(dict(code=code, im=np.asarray(im, dtype=np.uint8), xo=bbox[0], yo=bbox[1],
                           adv=adv, w=w, h=h))
    tiles = []
    for g in glyphs:
        if not g['w']:
            g['packed'] = (0, 0, 0)
            continue
        tiles.append(dict(im=Image.fromarray(g['im']), name='g%d' % g['code'], glyph=g))
    pages = pack(tiles)
    for t in tiles:
        t['glyph']['packed'] = t['packed']
    records = bytearray()
    for g in glyphs:
        pi, x, y = g.get('packed', (0, 0, 0))
        records += struct.pack('<8H', pi, x, y, g['w'], g['h'], g['xo'] & 0xFFFF,
                               g['yo'] & 0xFFFF, g['adv'] & 0xFFFF)
    sprites = (output / 'sprites.bin').read_bytes()
    start = struct.unpack_from('<H', sprites, 4)[0]
    for i, page in enumerate(pages):
        raw = bytearray()
        for r, g, b, a in page.getdata():
            raw += bytes((r * a // 255, g * a // 255, b * a // 255, a))
        (output / ('page%02d.bin' % (start + i))).write_bytes(raw)
    (output / 'font.bin').write_bytes(struct.pack('<4s3H6x', b'GDF1', len(glyphs), first, len(pages))
                                      + bytes(records))
    print('font: %d glyphs, %d pages' % (len(glyphs), len(pages)))


def build_sounds(app, output):
    for src, dst in SOUNDS.items():
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', str(app / src),
                        '-ac', '1', '-ar', str(RATE), '-f', 'wav', str(output / dst)], check=True)


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: convert_assets.py APP_DIR DATA_DIR')
    app, output = Path(sys.argv[1]), Path(sys.argv[2])
    output.mkdir(parents=True, exist_ok=True)
    build_atlas(app, output)
    build_background(app, output)
    build_font(output)
    build_sounds(app, output)
    files = sorted(p.name for p in output.glob('*') if p.is_file())
    (output / 'files.lst').write_text(''.join('%s %d\n' % (n, (output / n).stat().st_size)
                                             for n in files))
    total = sum((output / n).stat().st_size for n in files)
    print('%d files, %d bytes' % (len(files), total))


if __name__ == '__main__':
    main()
