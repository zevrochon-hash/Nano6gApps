#!/usr/bin/env python3
"""Convert the original Geometry Dash 1.0 app into nano texture pages + sound.

The original keeps its art in one GJ_GameSheet atlas (GJ_GameSheet.plist + .png) and
its music/sfx as mp3/caf files. The nano's GL driver is only proven with 256x128
pages, so every sprite is cut out and repacked; opaque art becomes RGB565 pages and
the rest keeps premultiplied RGBA8888. sprites.bin uses the same FBA1 layout the
Flappy Bird / Doodle Jump ports load, so port/src/sprites.c works unchanged.

Requires Pillow. No game data is embedded here.
"""
import argparse
import plistlib
import struct
import re
import subprocess
import sys
from pathlib import Path
from PIL import Image

PAGE_W, PAGE_H = 256, 128

# sheet frame -> image name in sprites.bin
SPRITES = [
    ('player_01_001.png', 'player_cube'),
    ('player_01_2_001.png', 'player_cube2'),
    ('playerSquare_001.png', 'player_square'),
    ('ship_01_001.png', 'ship'),
    ('square_01_001.png', 'square01'),
    ('square_02_001.png', 'square02'),
    ('square_03_001.png', 'square03'),
    ('square_04_001.png', 'square04'),
    ('square_05_001.png', 'square05'),
    ('square_06_001.png', 'square06'),
    ('square_07_001.png', 'square07'),
    ('spike_01_001.png', 'spike01'),
    ('spike_02_001.png', 'spike02'),
    ('d_spikes_01_001.png', 'dspikes01'),
    ('d_spikes_02_001.png', 'dspikes02'),
    ('d_spikes_03_001.png', 'dspikes03'),
    ('d_spikes_04_001.png', 'dspikes04'),
    ('pit_01_001.png', 'pit01'),
    ('pit_02_001.png', 'pit02'),
    ('pit_03_001.png', 'pit03'),
    ('rod_01_001.png', 'rod01'),
    ('rod_02_001.png', 'rod02'),
    ('rod_03_001.png', 'rod03'),
    ('rod_ball_01_001.png', 'rodball01'),
    ('rod_ball_02_001.png', 'rodball02'),
    ('rod_ball_03_001.png', 'rodball03'),
    ('chain_01_001.png', 'chain01'),
    ('plank_01_001.png', 'plank01'),
    ('bump_01_001.png', 'bump01'),
    ('ring_01_001.png', 'ring01'),
    ('portal_01_back_001.png', 'portal01back'),
    ('portal_02_back_001.png', 'portal02back'),
    ('portal_03_back_001.png', 'portal03back'),
    ('portal_04_back_001.png', 'portal04back'),
    ('portal_01_front_001.png', 'portal01'),
    ('portal_02_front_001.png', 'portal02'),
    ('portal03_front_001.png', 'portal03'),
    ('portal_04_front_001.png', 'portal04'),
    ('star_001.png', 'star'),
    ('floorLine_001.png', 'floorline'),
    ('GJ_playBtn_001.png', 'playbtn'),
    ('GJ_playBtn2_001.png', 'playbtn2'),
    # The original draws a soft white rim behind every solid object. Without
    # it the tinted art loses its outline against the level colour.
    ('square_01_glow_001.png', 'square01glow'),
    ('square_02_glow_001.png', 'square02glow'),
    ('square_03_glow_001.png', 'square03glow'),
    ('square_04_glow_001.png', 'square04glow'),
    ('square_06_glow_001.png', 'square06glow'),
    ('square_07_glow_001.png', 'square07glow'),
    ('spike_01_glow_001.png', 'spike01glow'),
    ('spike_02_glow_001.png', 'spike02glow'),
    ('plank_01_glow_001.png', 'plank01glow'),
    ('bump_01_glow_001.png', 'bump01glow'),
    ('ring_01_glow_001.png', 'ring01glow'),
]

# original sound -> nano wav name
SOUNDS = {
    'playSound_01.caf': 'click.wav',
    'quitSound_01.caf': 'quit.wav',
    'endStart_02.caf': 'end.wav',
    'explode_11.caf': 'explode.wav',
    'achievement_01.caf': 'achieve.wav',
}
MUSIC = ['StereoMadness.mp3', 'BackOnTrack.mp3', 'Polargeist.mp3', 'DryOut.mp3',
         'BaseAfterBase.mp3', 'CantLetGo.mp3', 'Jumper.mp3']
MUSIC_OUT = ['stereo.wav', 'back.wav', 'polar.wav', 'dry.wav', 'base.wav', 'cant.wav', 'jumper.wav']
RATE = 16000


def pack(tiles):
    """Shelf-pack tiles (each with a 1 px extruded border) into 256x128 pages.

    A tile is one rectangular strip of an image; wide art is split into several
    strips (the nano's pages are only 256 px across) and each becomes its own
    piece, with dx recording where the strip sits in the original image."""
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
    """Split one image into page-wide strips (a list of pack tiles)."""
    w, h = tile.size
    out = []
    for dx in range(0, w, PAGE_W - 2):
        out.append(dict(image=image_id, im=tile.crop((dx, 0, min(dx + PAGE_W - 2, w), h)),
                        dx=dx, opaque=opaque))
    return out


def parse_rect(text):
    """"{{x,y},{w,h}}" (the 2013 plists store rects as ASCII strings)."""
    nums = [int(n) for n in re.findall(r'-?\d+', text)]
    if len(nums) != 4:
        raise ValueError('unexpected rect: ' + text)
    return nums


def load_frames(app):
    sheet = app / 'GJ_GameSheet.png'
    meta = plistlib.loads((app / 'GJ_GameSheet.plist').read_bytes())
    frames = meta['frames']
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from cgbi import load_cgbi
    im = load_cgbi(sheet)
    out = {}
    for key, name in SPRITES:
        f = frames.get(key)
        if not f:
            raise SystemExit('missing sheet frame: ' + key)
        x, y, w, h = parse_rect(f['frame'])
        if f.get('rotated'):
            # Format 2 stores a rotated frame as h x w in the atlas; rotate it
            # back to the sprite's own w x h orientation.
            tile = im.crop((x, y, x + h, y + w)).transpose(Image.Transpose.ROTATE_90)
        else:
            tile = im.crop((x, y, x + w, y + h))
        out[name] = tile
    # The GUI pieces: the 1.0 logo (in the launch sheet), the progress-bar
    # frame and the four rounded colour buttons (standalone CgBI images).
    launch = load_cgbi(app / 'GJ_LaunchSheet.png')
    lmeta = plistlib.loads((app / 'GJ_LaunchSheet.plist').read_bytes())
    for key, name in (('GJ_logo_001.png', 'gdlogo'),
                      ('RobTopLogoBig_001.png', 'robtoplogo')):
        f = lmeta['frames'].get(key)
        if not f:
            continue
        x, y, w, h = parse_rect(f['frame'])
        if f.get('rotated'):
            out[name] = launch.crop((x, y, x + h, y + w)).transpose(Image.Transpose.ROTATE_90)
        else:
            out[name] = launch.crop((x, y, x + w, y + h))
    for name, file in (('progressbar', 'GJ_progressBar_001.png'),
                       ('gui_green', 'GJ_button_01.png'),
                       ('gui_cyan', 'GJ_button_02.png'),
                       ('gui_pink', 'GJ_button_03.png'),
                       ('gui_gray', 'GJ_button_04.png')):
        p = app / file
        if p.exists():
            out[name] = load_cgbi(p)
    # The floor grid is a separate 128x128 tile in the 1.0 app, not in the sheet.
    # It carries the ground's own gradient plus the faint square pattern.
    ground = app / 'groundSquare_001.png'
    if ground.exists():
        out['groundsquare'] = load_cgbi(ground)
    return out


def convert(app, output):
    output.mkdir(parents=True, exist_ok=True)
    frames = load_frames(app)
    # The floor tile spans a full page height, so it cannot use the shelf packer's
    # 1 px border. Give it a page of its own (it is opaque and only needs the top
    # of the gradient, which the ground clamps to).
    ground = frames.pop('groundsquare', None)

    # The original block/spike art is a white/grey mask with a translucent black
    # gradient: the engine tints it with the level's object colour. Keep the alpha
    # (the port multiplies it by the level's ground colour at draw time); forcing
    # it opaque loses the gradient and makes the blocks look flat and white.
    groups = {False: [], True: []}
    images = []
    for name in sorted(frames):
        tile = frames[name]
        opaque = tile.getchannel('A').getextrema()[0] == 255
        image_id = len(images)
        images.append(dict(name=name, w=tile.width, h=tile.height, opaque=opaque))
        groups[opaque].extend(strips(image_id, tile, opaque))

    page_formats, tiles = [], []
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
    if ground is not None:
        page = Image.new('RGBA', (PAGE_W, PAGE_H))
        page.paste(ground.crop((0, 0, PAGE_W, PAGE_H)), (0, 0))
        pi = len(page_formats)
        raw = bytearray()
        for r, g, b, a in page.getdata():
            raw += struct.pack('<H', (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3))
        (output / ('page%02d.bin' % pi)).write_bytes(raw)
        page_formats.append(0)
        image_id = len(images)
        images.append(dict(name='groundsquare', w=ground.width, h=ground.height, opaque=True))
        tiles.append(dict(image=image_id, im=ground, dx=0, packed=(pi, 0, 0), opaque=True))
    if len(page_formats) > 40:
        raise SystemExit('texture budget exceeded')

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
    files = ['sprites.bin'] + ['page%02d.bin' % i for i in range(len(page_formats))]

    # music + sfx: mp3/caf -> mono 16-bit wav the OS audio stack can stream
    for src, dst in zip(MUSIC, MUSIC_OUT):
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', str(app / src),
                        '-ac', '1', '-ar', str(RATE), '-f', 'wav', str(output / dst)], check=True)
        files.append(dst)
    for src, dst in SOUNDS.items():
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', str(app / src),
                        '-ac', '1', '-ar', str(RATE), '-f', 'wav', str(output / dst)], check=True)
        files.append(dst)
    (output / 'files.lst').write_text(''.join('%s %d\n' % (p, (output / p).stat().st_size) for p in files))
    tex = sum(65536 if f == 0 else 131072 for f in page_formats)
    print('%d images, %d pages (%d opaque), %d KiB texture, %d sounds'
          % (len(images), len(page_formats), page_formats.count(0), tex // 1024, len(files) - 1 - len(page_formats)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    convert(args.app, args.output)
