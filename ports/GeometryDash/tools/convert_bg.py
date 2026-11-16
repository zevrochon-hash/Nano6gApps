#!/usr/bin/env python3
"""Convert the original's background gradient into a nano texture page.

GD 1.0 ships one 512x512 grayscale mosaic (game_bg_01_001.png, CgBI) that the
game multiplies by each level's background colour. The image is dark at the top
and bright near the horizon, exactly how the original screen looks, so it is
stored unflipped. Output is a 256x128 RGBA8888 page (bg.bin) the game tints with
the level's bg colour and stretches once across the canvas.
"""
import struct
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from cgbi import load_cgbi

PAGE_W, PAGE_H = 256, 128


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else 'game_bg_01_001.png'
    out = sys.argv[2] if len(sys.argv) > 2 else 'bg.bin'
    im = load_cgbi(src).convert('RGBA')
    im = im.resize((PAGE_W, PAGE_H))
    with open(out, 'wb') as f:
        f.write(im.tobytes())
    print(f'{out}: {PAGE_W}x{PAGE_H} RGBA8888 from {src}')


if __name__ == '__main__':
    main()
