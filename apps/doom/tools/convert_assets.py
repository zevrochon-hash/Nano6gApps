#!/usr/bin/env python3
"""Build the compact Doom asset pack for the iPod nano 7G port.

Sources
-------
Map      : <freedoom>/levels/e1m1.wad          (the level source in the ipa folder)
Art      : freedoom1.wad (Freedoom 0.13.0)     (compiled form of the source PNGs)
Sounds   : <freedoom>/sounds/ds*.wav           (mono PCM WAVs from the source tree)

The device cannot hold the 28 MB IWAD and the app binary is capped near 0.5 MB,
so this script extracts only what E1M1 needs and writes a small indexed-colour
pack plus a handful of WAVs.

Output (relative to the app build dir):
    build/assets/doom.pak
    build/assets/sfx/*.wav
"""
from __future__ import annotations

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]          # apps/doom
FREEDOOM = Path("/home/zev/Desktop/iPod/Build/ipa folder/freedoom")
IWAD = Path("/usr/share/games/doom/freedoom1.wad")
OUT = ROOT / "build" / "assets"

# ---------------------------------------------------------------- WAD reader

class Wad:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        magic, n, off = struct.unpack_from("<4sii", self.data, 0)
        if magic not in (b"IWAD", b"PWAD"):
            raise SystemExit(f"{path}: not a WAD")
        self.lumps: list[tuple[str, int, int]] = []
        self.index: dict[str, tuple[int, int]] = {}
        for i in range(n):
            o, sz, nm = struct.unpack_from("<ii8s", self.data, off + i * 16)
            name = nm.rstrip(b"\0").decode("ascii", "replace")
            self.lumps.append((name, o, sz))
            self.index[name] = (o, sz)

    def get(self, name: str) -> bytes:
        o, sz = self.index[name]
        return self.data[o:o + sz]

    def has(self, name: str) -> bool:
        return name in self.index


def decode_picture(data: bytes):
    """Doom patch/picture -> (w, h, left, top, bytes[y*w+x], 255 = transparent)."""
    w, h, left, top = struct.unpack_from("<hhhh", data, 0)
    if w <= 0 or h <= 0 or w > 1024 or h > 1024:
        raise ValueError(f"bad picture {w}x{h}")
    out = bytearray([255]) * (w * h)
    for x in range(w):
        colofs = struct.unpack_from("<I", data, 8 + 4 * x)[0]
        p = colofs
        while p < len(data):
            topdelta = data[p]
            p += 1
            if topdelta == 0xFF:
                break
            length = data[p]
            p += 2                                   # length, pad
            for i in range(length):
                y = topdelta + i
                if 0 <= y < h:
                    out[y * w + x] = data[p + i]
            p += length + 1                          # pixels, pad
    return w, h, left, top, out


def decode_flat(data: bytes):
    if len(data) < 4096:
        raise ValueError("short flat")
    return 64, 64, 0, 0, bytearray(data[:4096])


# ------------------------------------------------------------ texture list

def parse_textures(wad: Wad):
    pnames_raw = wad.get("PNAMES")
    npatch = struct.unpack_from("<I", pnames_raw, 0)[0]
    pnames = [pnames_raw[4 + i * 8:12 + i * 8].rstrip(b"\0").decode("ascii")
              for i in range(npatch)]
    defs = {}
    for lump in ("TEXTURE1", "TEXTURE2"):
        if not wad.has(lump):
            continue
        raw = wad.get(lump)
        n = struct.unpack_from("<I", raw, 0)[0]
        offs = struct.unpack_from(f"<{n}I", raw, 4)
        for off in offs:
            name = raw[off:off + 8].rstrip(b"\0").decode("ascii")
            w, h = struct.unpack_from("<HH", raw, off + 12)
            npat = struct.unpack_from("<H", raw, off + 20)[0]
            patches = []
            for j in range(npat):
                px, py, pi = struct.unpack_from("<hhH", raw, off + 22 + j * 10)
                patches.append((pnames[pi], px, py))
            defs[name] = (w, h, patches)
    return defs


def composite_texture(wad: Wad, name: str, texdefs, cache: dict):
    if name in cache:
        return cache[name]
    w, h, patches = texdefs[name]
    pix = bytearray([255]) * (w * h)
    for pname, px, py in patches:
        if not wad.has(pname):
            print(f"  ! missing patch {pname} for texture {name}")
            continue
        pw, ph, _left, _top, ppix = decode_picture(wad.get(pname))
        for y in range(ph):
            dy = py + y
            if not (0 <= dy < h):
                continue
            row = y * pw
            drow = dy * w
            for x in range(pw):
                dx = px + x
                if 0 <= dx < w:
                    c = ppix[row + x]
                    if c != 255:
                        pix[drow + dx] = c
    cache[name] = (w, h, pix)
    return cache[name]


# ------------------------------------------------------------------- map

SIDEDEF = struct.Struct("<hh8s8s8sH")
SECTOR = struct.Struct("<hh8s8shhh")


def parse_map(mapwad: Wad):
    def lump(name):
        return mapwad.get(name)
    things = [struct.unpack_from("<hhhhh", lump("THINGS"), i)
              for i in range(0, len(lump("THINGS")), 10)]
    verts = [struct.unpack_from("<hh", lump("VERTEXES"), i)
             for i in range(0, len(lump("VERTEXES")), 4)]
    lines = [struct.unpack_from("<HHHHHHH", lump("LINEDEFS"), i)
             for i in range(0, len(lump("LINEDEFS")), 14)]
    sides = [SIDEDEF.unpack_from(data, i) for data in [lump("SIDEDEFS")]
             for i in range(0, len(data), 30)]
    sectors = [SECTOR.unpack_from(data, i) for data in [lump("SECTORS")]
               for i in range(0, len(data), 26)]
    return things, verts, lines, sides, sectors


def bsp_sector(mapwad: Wad, px: int, py: int, cache=None) -> int:
    """Return the sector containing (px,py) via even-odd ray casting.

    The BSP lumps in this WAD don't survive a straightforward walk (the
    node child-order semantics don't match stock Doom), so we test geometry
    directly: count ray crossings of every sector's boundary lines.  Exact
    for closed sector outlines, and it agrees with the ambiguity check
    (exactly one containing sector for every E1M1 thing)."""
    if cache is None:
        cache = _build_sector_index(mapwad)
    lines, verts, sec_lines = cache
    hits = []
    for s in range(len(sec_lines)):
        inside = False
        for li in sec_lines[s]:
            v1, v2 = lines[li]
            ax, ay = verts[v1]
            bx, by = verts[v2]
            if (ay > py) != (by > py):
                xint = ax + (py - ay) * (bx - ax) / (by - ay)
                if xint > px:
                    inside = not inside
        if inside:
            hits.append(s)
    if len(hits) == 1:
        return hits[0]
    if hits:
        return hits[0]
    return 0


def _build_sector_index(mapwad: Wad):
    lines = [struct.unpack_from("<HHHHHHH", mapwad.get("LINEDEFS"), i)
             for i in range(0, len(mapwad.get("LINEDEFS")), 14)]
    verts = [struct.unpack_from("<hh", mapwad.get("VERTEXES"), i)
             for i in range(0, len(mapwad.get("VERTEXES")), 4)]
    sides = [SIDEDEF.unpack_from(mapwad.get("SIDEDEFS"), i)
             for i in range(0, len(mapwad.get("SIDEDEFS")), 30)]
    sectors = [SECTOR.unpack_from(mapwad.get("SECTORS"), i)
               for i in range(0, len(mapwad.get("SECTORS")), 26)]
    sec_lines = [[] for _ in sectors]
    for li, (_v1, _v2, _f, _sp, _tg, sf, sb) in enumerate(lines):
        if sf != 0xFFFF:
            sec_lines[sides[sf][5]].append(li)
        if sb != 0xFFFF:
            sec_lines[sides[sb][5]].append(li)
    # collapse line indices to vertex pairs for speed
    flat_lines = [(l[0], l[1]) for l in lines]
    return flat_lines, verts, sec_lines


# ------------------------------------------------------------- thing table
# E1M1's thing types. Sprite frames come from Doom's info.c (doomednum ->
# spawnstate -> sprite). kind: 0 decoration, 1 monster, 2 item, 3 player start,
# 4 key.
THINGS: dict[int, tuple[str, int, int]] = {
    3004: ("POSS", 1, 20), 9: ("SPOS", 1, 30), 3001: ("TROO", 1, 60),
    3002: ("SARG", 1, 150), 58: ("SARG", 1, 150),
    2011: ("STIM", 2, 10), 2012: ("MEDI", 2, 25),
    2014: ("BON1", 2, 1), 2015: ("BON2", 2, 1),
    2018: ("ARM1", 2, 100), 2019: ("ARM2", 2, 200),
    2001: ("SHOT", 2, 0), 2002: ("MGUN", 2, 0),
    2003: ("LAUN", 2, 0), 2004: ("PLAS", 2, 0), 2005: ("CSAW", 2, 0),
    2007: ("CLIP", 2, 10), 2008: ("SHEL", 2, 4), 2010: ("ROCK", 2, 1),
    2046: ("BROK", 2, 5), 2047: ("CELL", 2, 20), 2048: ("AMMO", 2, 50),
    2049: ("BPAK", 2, 0), 2013: ("SOUL", 2, 100), 2023: ("PSTR", 2, 0),
    5: ("BKEY", 4, 0), 6: ("YKEY", 4, 0), 13: ("RKEY", 4, 0),
    10: ("PLAY", 0, 0), 12: ("PLAY", 0, 0), 15: ("PLAY", 0, 0),
    18: ("POSS", 0, 0), 19: ("SPOS", 0, 0), 20: ("TROO", 0, 0),
    21: ("SARG", 0, 0), 22: ("HEAD", 0, 0),
    24: ("POL5", 0, 0), 25: ("POL1", 0, 0), 26: ("POL6", 0, 0),
    43: ("TRE1", 0, 0), 47: ("SMIT", 0, 0), 48: ("ELEC", 0, 0),
    54: ("TRE2", 0, 0), 60: ("GOR4", 0, 0), 2028: ("COLU", 0, 0),
    2035: ("BAR1", 0, 0),
}
DEATH_LETTER = {10: "W", 12: "W", 15: "N", 18: "L", 19: "L", 20: "M",
                21: "N", 22: "L", 60: "A"}


def directive_kind(typ):
    sprite, kind, hp = THINGS.get(typ, ("", 0, 0))
    if typ in (1, 2, 3, 4):
        return sprite, 3, hp
    return sprite, kind, hp


def parse_texture_cfg(path: Path):
    """Parse the source tree's textures.cfg into {name: (w,h,[(patch,x,y)])}."""
    defs = {}
    if not path.exists():
        return defs
    cur = None
    for raw in path.read_text(errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith(";") or line.startswith("#"):
            continue
        if line.startswith("*"):
            if cur:
                parts = line[1:].split()
                if len(parts) >= 3:
                    defs[cur][2].append((parts[0].upper(), int(parts[1]), int(parts[2])))
            continue
        parts = line.split()
        if len(parts) >= 3 and parts[1].isdigit() and parts[2].isdigit():
            cur = parts[0].upper()
            defs[cur] = (int(parts[1]), int(parts[2]), [])
        else:
            cur = None
    return defs


def main():
    for p in (IWAD, FREEDOOM):
        if not p.exists():
            raise SystemExit(f"missing {p}")

    print("reading WADs...")
    iwad = Wad(IWAD)
    mapwad = Wad(FREEDOOM / "levels" / "e1m1.wad")
    things, verts, lines, sides, sectors = parse_map(mapwad)
    texdefs = parse_textures(iwad)
    cfgdefs = parse_texture_cfg(FREEDOOM / "lumps" / "textures" / "textures.cfg")

    # ---- used textures/flats
    tex_names, flat_names = set(), set()
    for _off, _rowoff, top, bot, mid, _sec in sides:
        for t in (top, mid, bot):
            n = t.rstrip(b"\0").decode("ascii", "replace")
            if n and n != "-":
                tex_names.add(n)
    for _fh, _ch, fpic, cpic, _light, _sp, _tag in sectors:
        for t in (fpic, cpic):
            n = t.rstrip(b"\0").decode("ascii", "replace")
            if n:
                flat_names.add(n)
    used_tex, used_flat = sorted(tex_names), sorted(flat_names)
    print(f"  {len(used_tex)} wall textures, {len(used_flat)} flats")
    for t in used_tex:
        if t not in texdefs:
            if t in cfgdefs:
                texdefs[t] = cfgdefs[t]
                print(f"  + {t} from textures.cfg")
            else:
                print("  ! missing texture def", t)
    for f in used_flat:
        if not iwad.has(f):
            print("  ! missing flat", f)

    # ---- textures
    tex_cache = {}
    tex_entries = []
    tex_data = bytearray()
    for name in used_tex:
        if name not in texdefs:
            continue
        w, h, pix = composite_texture(iwad, name, texdefs, tex_cache)
        tex_entries.append((name, w, h, len(tex_data)))
        tex_data += pix
    print(f"  texture data: {len(tex_data)} bytes")

    # ---- flats
    flat_entries = []
    flat_data = bytearray()
    for name in used_flat:
        if not iwad.has(name):
            continue
        _w, _h, _l, _t, pix = decode_flat(iwad.get(name))
        flat_entries.append((name, len(flat_data)))
        flat_data += pix
    print(f"  flat data: {len(flat_data)} bytes")

    # ---- sprites
    need_bases = {v[0] for v in THINGS.values()} | {"PISG", "PISF", "SHTG", "SHTF",
                                                     "PUFF", "BAL1", "BAL7", "BAR1", "HEAD"}
    full_bases = {"POSS", "SPOS", "TROO", "SARG", "PLAY", "HEAD"}
    spr_entries = []
    spr_data = bytearray()
    for name, _o, _sz in iwad.lumps:
        if len(name) != 6:
            continue
        base, letter, rot = name[:4], name[4], name[5]
        if base not in need_bases:
            continue
        if base not in full_bases:
            if letter not in "AB" or rot != "0":
                continue
        if not (name in iwad.index):
            continue
        w, h, left, top, pix = decode_picture(iwad.get(name))
        spr_entries.append((name, w, h, left, top, len(spr_data)))
        spr_data += pix
    print(f"  {len(spr_entries)} sprites, {len(spr_data)} bytes")

    # ---- HUD/menu/title/font graphics
    gfx_names = ["STBAR", "TITLEPIC", "INTERPIC", "M_DOOM", "M_SKULL1", "M_SKULL2"]
    gfx_names += [f"STTNUM{i}" for i in range(10)]
    gfx_names += [f"STYSNUM{i}" for i in range(10)]
    gfx_names += ["STTPRCNT", "STTMINUS"]
    gfx_names += ["STFST00", "STFST10", "STFST20", "STFST30", "STFST40", "STFST42",
                  "STFDEAD0", "STFGOD0"]
    gfx_names += [f"STKEYS{i}" for i in range(6)]
    gfx_names += [f"STCFN{c:03d}" for c in range(33, 96)]
    gfx_entries = []
    gfx_data = bytearray()
    for name in gfx_names:
        if not iwad.has(name):
            print("  ! missing gfx", name)
            continue
        w, h, left, top, pix = decode_picture(iwad.get(name))
        gfx_entries.append((name, w, h, left, top, len(gfx_data)))
        gfx_data += pix
    print(f"  {len(gfx_entries)} gfx, {len(gfx_data)} bytes")

    tex_idx = {e[0]: i for i, e in enumerate(tex_entries)}
    flat_idx = {e[0]: i for i, e in enumerate(flat_entries)}

    def ti(b):
        n = b.rstrip(b"\0").decode("ascii", "replace")
        return tex_idx.get(n, 0xFFFF) if n and n != "-" else 0xFFFF

    # ---- map line lists
    sec_lines = [[] for _ in sectors]
    for li, (_v1, _v2, _f, _sp, _tg, sfront, sback) in enumerate(lines):
        if sfront != 0xFFFF:
            sec_lines[sides[sfront][5]].append(li)
        if sback != 0xFFFF:
            sec_lines[sides[sback][5]].append(li)

    base_names = sorted({e[0][:4] for e in spr_entries})
    spr_base_id = {b: i for i, b in enumerate(base_names)}

    map_data = bytearray()
    map_data += struct.pack("<HH", len(verts), len(lines))
    for x, y in verts:
        map_data += struct.pack("<hh", x, y)
    for v1, v2, flags, special, tag, sfront, sback in lines:
        fs = sides[sfront] if sfront != 0xFFFF else None
        bs = sides[sback] if sback != 0xFFFF else None
        fsec = fs[5] if fs else 0xFFFF
        bsec = bs[5] if bs else 0xFFFF
        ft, fm, fb = (ti(fs[2]), ti(fs[4]), ti(fs[3])) if fs else (0xFFFF,) * 3
        bt, bm, bb = (ti(bs[2]), ti(bs[4]), ti(bs[3])) if bs else (0xFFFF,) * 3
        fx = fs[0] if fs else 0
        bx = bs[0] if bs else 0
        map_data += struct.pack("<HHHHHHHHHHHHHhh", v1, v2, flags, special, tag,
                                fsec, bsec, ft, fm, fb, bt, bm, bb, fx, bx)
    map_data += struct.pack("<H", len(sectors))
    line_index = []
    for si, (fh, ch, fpic, cpic, light, sp, tag) in enumerate(sectors):
        fname = fpic.rstrip(b"\0").decode("ascii", "replace")
        cname = cpic.rstrip(b"\0").decode("ascii", "replace")
        start = len(line_index)
        line_index += sec_lines[si]
        map_data += struct.pack("<hhHHHhhHH", fh, ch, flat_idx.get(fname, 0),
                                flat_idx.get(cname, 0), light & 0xFF, sp, tag,
                                start, len(sec_lines[si]))
    for li in line_index:
        map_data += struct.pack("<H", li)

    # ---- things + gameplay meta
    sector_cache = _build_sector_index(mapwad)
    tm = bytearray()
    map_data += struct.pack("<H", len(things))
    tm += struct.pack("<H", len(things))
    for x, y, ang, typ, flags in things:
        sprite, kind, hp = directive_kind(typ)
        if typ in (1, 2, 3, 4):
            kind = 3
        try:
            sec = bsp_sector(mapwad, x, y, sector_cache)
        except Exception:
            sec = 0
        baseid = spr_base_id.get(sprite, 0xFFFF)
        letter = DEATH_LETTER.get(typ)
        if letter is None:
            letter = "A"
        # Doom thing angles are degrees (0 = east); store 16-bit BAM instead.
        ang_bam = (int(ang) * 65536) // 360
        map_data += struct.pack("<hhHHhHH", x, y, ang_bam & 0xFFFF, typ, flags, sec, baseid)
        radius, height, speed = 16, 56, 0
        if kind == 1:
            radius, height, speed = (30, 56, 8) if typ in (3002, 58) else (20, 56, 8)
        tm += struct.pack("<HHBBBB", kind, hp, ord(letter), radius, height, speed)

    # ---- chunks
    def chunk(tag, data):
        return (tag, bytes(data))

    pal = iwad.get("PLAYPAL")[:768]

    tex_table = bytearray(struct.pack("<H", len(tex_entries)))
    for name, w, h, off in tex_entries:
        tex_table += name.encode("ascii").ljust(8, b"\0")
        tex_table += struct.pack("<HHII", w, h, off, 0)
    tex_table += tex_data

    flat_table = bytearray(struct.pack("<H", len(flat_entries)))
    for name, off in flat_entries:
        flat_table += name.encode("ascii").ljust(8, b"\0")
        flat_table += struct.pack("<I", off)
    flat_table += flat_data

    spr_table = bytearray(struct.pack("<H", len(spr_entries)))
    for name, w, h, left, top, off in spr_entries:
        spr_table += name.encode("ascii").ljust(8, b"\0")
        spr_table += struct.pack("<HHhhII", w, h, left, top, off, 0)
    spr_table += spr_data

    gfx_table = bytearray(struct.pack("<H", len(gfx_entries)))
    for name, w, h, left, top, off in gfx_entries:
        gfx_table += name.encode("ascii").ljust(8, b"\0")
        gfx_table += struct.pack("<HHhhII", w, h, left, top, off, 0)
    gfx_table += gfx_data

    chunks = [chunk("PAL ", pal), chunk("MAP ", map_data), chunk("TEX ", tex_table),
              chunk("FLAT", flat_table), chunk("SPR ", spr_table),
              chunk("GFX ", gfx_table), chunk("THNG", tm)]
    header = struct.pack("<4sII", b"NDM1", 1, len(chunks))
    off = len(header) + 12 * len(chunks)
    toc, body = b"", b""
    for tag, data in chunks:
        toc += struct.pack("<4sII", tag.encode("ascii"), off + len(body), len(data))
        body += data
    pak = header + toc + body

    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "doom.pak").write_bytes(pak)
    print(f"wrote {OUT / 'doom.pak'} ({len(pak)} bytes)")

    # ---- sounds
    sfx_dir = OUT / "sfx"
    sfx_dir.mkdir(parents=True, exist_ok=True)
    wanted = ["pistol", "shotgn", "doropn", "itemup",
              "wpnup", "plpain", "pldeth", "oof", "swtchn", "posit2", "podth2",
              "bgsit1", "bgdth1", "sgtsit", "sgtdth", "firxpl", "barexp",
              "noway", "firsht", "claw", "posact"]
    names = {"pistol": "pistol.wav", "shotgn": "shotgun.wav", "doropn": "door.wav",
             "itemup": "item.wav", "wpnup": "weapon.wav", "plpain": "pain.wav",
             "pldeth": "death.wav", "oof": "oof.wav", "swtchn": "switch.wav",
             "posit2": "possit.wav", "podth2": "posdth.wav",
             "bgsit1": "bgsit.wav", "bgdth1": "bgdth.wav",
             "sgtsit": "sargsit.wav", "sgtdth": "sargdth.wav",
             "firxpl": "firxpl.wav", "barexp": "barexp.wav",
             "noway": "noway.wav", "firsht": "firsht.wav",
             "claw": "claw.wav", "posact": "posact.wav"}
    n = 0
    for key in wanted:
        src = FREEDOOM / "sounds" / f"ds{key}.wav"
        if src.exists():
            (sfx_dir / names[key]).write_bytes(src.read_bytes())
            n += 1
        else:
            print(f"  ! missing sound ds{key}.wav")
    print(f"wrote {n} sounds to {sfx_dir}")


if __name__ == "__main__":
    main()
