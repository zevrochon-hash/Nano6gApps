#!/usr/bin/env python3
"""Build hbpk_6g_3apps_v2.pack — the 3-app 6G test fixture with REAL icons.

Differences from the original hbpk_6g_3apps.pack:
  * icons are canonical 32-byte-header RGBA8888 bitmaps (the form the 6G
    bitmap cache / renderer actually consume), not the obsolete 0x0 "IPMB"
    blobs whose w/h sat at +4/+8 and read as 0x0 at +0x10/+0x14;
  * icon size is 105x105, which is what the stock 6G home items use
    (e.g. Photos X=0x0dad02ef -> a 105x105 bitmap; the derived overlays
    X-0x25 / X+0x12 are 164x154);
  * label_id is 0 (the old 0x0dad07xx ids fall inside the stock bitmap
    range and labels are not published on the 6G, so they were a hazard).

Header layout (confirmed against a working .nanoapp icon.bin):
    +0x00 0x00011888   +0x04 0x002001c0   +0x08 0   +0x0c 0
    +0x10 w            +0x14 h            +0x18 id  +0x1c w*h*4
    +0x20 RGBA8888 pixels (alpha last)

Usage:  python3 tools/fixtures/mk6g_fixture.py [out.pack]
"""
import os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
MKAPP = os.path.join(TOOLS, "mkapp.py")
CODE = os.path.join(HERE, "hbpk_6g_3apps.pack")

W = H = 105
APPS = [
    # id,     sbid,       name,    RGB
    (0x7000, "hb.test0", "test0", (0x2f, 0x6f, 0xff)),   # blue
    (0x7001, "hb.test1", "test1", (0x2f, 0xbf, 0x4f)),   # green
    (0x7002, "hb.test2", "test2", (0xe0, 0x3f, 0x3f)),   # red
]
ICON_ID0 = 0x0dad0761          # first icon id register_bitmaps will assign


def make_icon(propid, rgb):
    px = bytearray()
    r, g, b = rgb
    for y in range(H):
        for x in range(W):
            border = x < 5 or y < 5 or x >= W - 5 or y >= H - 5
            centre = 42 <= x < 63 and 42 <= y < 63
            if border or centre:
                px += bytes((255, 255, 255, 255))
            else:
                px += bytes((r, g, b, 255))
    hdr = struct.pack("<8I", 0x00011888, 0x002001c0, 0, 0,
                      W, H, propid, W * H * 4)
    return hdr + bytes(px)


def extract_code(pack_path, out_path):
    """Reuse the 64-byte code section of the original fixture."""
    d = open(pack_path, "rb").read()
    _, _, count = struct.unpack_from("<III", d, 0)
    off = struct.unpack_from("<I", d, 12)[0]
    secn = struct.unpack_from("<I", d, off + 48)[0]
    for s in range(secn):
        kind, so, sl = struct.unpack_from("<III", d, off + 52 + s * 12)
        if kind == 4:
            open(out_path, "wb").write(d[off + so:off + so + sl])
            return
    raise SystemExit("no code section in " + pack_path)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "hbpk_6g_3apps_v2.pack")
    tmp = tempfile.mkdtemp(prefix="hb6fixture_")
    code = os.path.join(tmp, "code.bin")
    extract_code(CODE, code)

    apps = []
    for i, (aid, sbid, name, rgb) in enumerate(APPS):
        icon = os.path.join(tmp, "icon%d.bin" % i)
        open(icon, "wb").write(make_icon(ICON_ID0 + i, rgb))
        app = os.path.join(tmp, "app%d.app" % i)
        subprocess.check_call([
            sys.executable, MKAPP, "bundle", app,
            "--id", hex(aid), "--kind", "0", "--sbid", sbid, "--name", name,
            "--label-id", "0", "--icon", icon, "--code", code,
        ])
        apps.append(app)

    subprocess.check_call([sys.executable, MKAPP, "pack", out] + apps)
    print("icons: %dx%d RGBA, %d bytes each" % (W, H, 32 + W * H * 4))


if __name__ == "__main__":
    main()
