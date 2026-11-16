#!/usr/bin/env python3
"""Analyse a fm_probe report + OS image dump.

Usage:
    fm_analyze.py REPORT [IMAGE]

REPORT is /Data/fm_probe/fm_probe.txt pulled from the iPod. IMAGE is the
optional /Data/fm_probe/os_image.bin (the live OS image as it sits in DRAM at
0x08000000). With the image, every vtable slot is disassembled and classified
and cross-references are computed; without it, the report alone is summarised.

The classification is deliberately conservative: a slot is only called a
getter/setter when the first instructions are unambiguously one, because a
wrong name here is worse than no name.
"""

import argparse
import json
import re
import struct
from pathlib import Path

from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB

BASE = 0x08000000

NAMES = [
    "N3ISL8TRFTunerE",
    "N3ISL11IPodRFTunerE",
    "N3ISL12TRFTuner_IAPE",
    "N3ISL13TRFTuner_RTXCE",
    "N3ISL21IPodRFTunerPresetListE",
    "18TRFTunerPresetList",
    "16CIapLingoRFTuner",
    "24TSilverRadioTunerBarView",
    "23TCSettings_RadioRegions",
    "27TSilverCntlrTransitionAddonI23TCSettings_RadioRegionsE",
]


# ---------------------------------------------------------------- report ---

def parse_report(path):
    """Pull the live-object addresses and the pattern hits out of the report."""
    text = Path(path).read_text(errors="replace")
    out = {"objects": [], "movw": [], "half": [], "bringup": [], "names": []}
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("obj=0x"):
            out["objects"].append(int(s[4:], 16))
        elif s.startswith("movw_fc15 0x"):
            out["movw"].append(int(s.split()[1], 16))
        elif s.startswith("fc15_half 0x"):
            out["half"].append(int(s.split()[1], 16))
        elif s.startswith("bringup_table 0x"):
            out["bringup"].append(int(s.split()[1], 16))
        elif s.startswith("-- "):
            out["names"].append(s[3:])
    return out


# ----------------------------------------------------------------- image ---

class Image:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        self.end = BASE + len(self.data)
        self.cs = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        self.cs.detail = False

    def word(self, addr):
        o = addr - BASE
        if o < 0 or o + 4 > len(self.data):
            return None
        return struct.unpack_from("<I", self.data, o)[0]

    def half(self, addr):
        o = addr - BASE
        if o < 0 or o + 2 > len(self.data):
            return None
        return struct.unpack_from("<H", self.data, o)[0]

    def read(self, addr, n):
        o = addr - BASE
        if o < 0 or o + n > len(self.data):
            return None
        return self.data[o:o + n]

    def find_string(self, s):
        needle = s.encode() + b"\0"
        i = self.data.find(needle)
        return BASE + i if i >= 0 else None

    def find_word(self, val, lo=BASE, hi=None):
        hi = hi or self.end
        needle = struct.pack("<I", val)
        out, i = [], 0
        while True:
            i = self.data.find(needle, i, hi - BASE)
            if i < 0:
                break
            if i % 4 == 0:
                out.append(BASE + i)
            i += 1
        return out

    def callers(self, target, limit=40):
        """Every BL/BLX whose computed target is `target`."""
        target &= ~1
        found = []
        data = self.data
        for off in range(0, len(data) - 4, 2):
            hw1 = struct.unpack_from("<H", data, off)[0]
            if (hw1 & 0xF800) != 0xF000:
                continue
            hw2 = struct.unpack_from("<H", data, off + 2)[0]
            if (hw2 & 0xC000) != 0xC000:
                continue
            kind = 0xD if (hw2 & 0x1000) else 0xC
            s = (hw1 >> 10) & 1
            imm10 = hw1 & 0x3FF
            j1 = (hw2 >> 13) & 1
            j2 = (hw2 >> 11) & 1
            imm11 = hw2 & 0x7FF
            i1 = 1 - (j1 ^ s)
            i2 = 1 - (j2 ^ s)
            imm = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1)
            if s:
                imm -= 1 << 25
            pc = BASE + off + 4
            tgt = pc + imm
            if kind == 0xC:
                tgt &= ~3
            if tgt == target:
                found.append((BASE + off, "BLX" if kind == 0xC else "BL"))
                if len(found) >= limit:
                    break
        return found

    def xrefs(self, addr, limit=24):
        """Literal-pool / data references to `addr` (word-sized)."""
        return self.find_word(addr, BASE, min(self.end, BASE + 0x01000000))[:limit]

    def disasm(self, addr, n=8, count=6):
        addr &= ~1
        code = self.read(addr, n)
        if not code:
            return []
        return list(self.cs.disasm(code, addr))[:count]


def classify(img, slot, maxlen=32):
    """Name a vtable slot from its first instructions, or return None."""
    code = img.read(slot & ~1, maxlen)
    if not code:
        return None
    ins = list(img.cs.disasm(code, slot & ~1))
    if not ins:
        return None

    # getter: ldr r0,[r0,#N] ... bx lr / pop {pc}
    for i, x in enumerate(ins[:3]):
        if x.mnemonic in ("ldr", "ldrb", "ldrh") and x.op_str.startswith("r0, [r0"):
            m = re.search(r"#(0x[0-9a-f]+|\d+)", x.op_str)
            off = int(m.group(1), 0) if m else 0
            tail = " ".join(f"{y.mnemonic} {y.op_str}" for y in ins[i + 1:i + 3])
            kind = {"ldr": "u32", "ldrb": "u8", "ldrh": "u16"}[x.mnemonic]
            if "bx lr" in tail or "pop {pc}" in tail or "pop {r4" in tail:
                return f"get_{kind}+0x{off:x}"
            return f"peek_{kind}+0x{off:x}?"
    # setter: str rX,[r0,#N] ... bx lr / pop {pc}
    for i, x in enumerate(ins[:3]):
        if x.mnemonic in ("str", "strb", "strh") and "[r0" in x.op_str:
            m = re.search(r"#(0x[0-9a-f]+|\d+)", x.op_str)
            off = int(m.group(1), 0) if m else 0
            tail = " ".join(f"{y.mnemonic} {y.op_str}" for y in ins[i + 1:i + 3])
            kind = {"str": "u32", "strb": "u8", "strh": "u16"}[x.mnemonic]
            if "bx lr" in tail or "pop {pc}" in tail or "pop {r4" in tail:
                return f"set_{kind}+0x{off:x}"
            return f"poke_{kind}+0x{off:x}?"
    # field address: add r0,r0,#N ... bx lr
    if ins[0].mnemonic == "add" and ins[0].op_str.startswith("r0, r0"):
        m = re.search(r"#(0x[0-9a-f]+|\d+)", ins[0].op_str)
        if m:
            return f"addr+0x{int(m.group(1), 0):x}"
    # constant: mov r0,#imm ... bx lr
    if ins[0].mnemonic in ("mov", "movs") and ins[0].op_str.startswith("r0, #"):
        return f"const {ins[0].op_str.split(', ')[1]}"
    return None


def rtti_walk(img, name, slots=40):
    """Find the vtables for one RTTI class name."""
    s = img.find_string(name)
    if s is None:
        return None
    out = []
    for ti_ref in img.find_word(s):
        ti = ti_ref - 4
        vptr = img.word(ti)
        if vptr is None or not (BASE <= vptr < img.end):
            continue
        for vt_ref in img.find_word(ti):
            slots_addr = vt_ref + 4
            s0 = img.word(slots_addr)
            if s0 is not None and s0 != 0 and not (BASE <= (s0 & ~1) < img.end):
                continue
            out.append({"str": s, "ti": ti, "vt": slots_addr - 8,
                        "slots": slots_addr, "n": slots})
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("report", type=Path)
    ap.add_argument("image", type=Path, nargs="?")
    ap.add_argument("--json", type=Path)
    args = ap.parse_args()

    rep = parse_report(args.report)
    print(f"report: {args.report}  names={len(rep['names'])} "
          f"objects={len(rep['objects'])} movw={len(rep['movw'])} "
          f"bringup={len(rep['bringup'])}")

    result = {"report": str(args.report), "objects": [hex(a) for a in rep["objects"]],
              "vtables": [], "hci": rep}

    if args.image:
        img = Image(args.image)
        print(f"image:  {args.image}  {len(img.data)} bytes at 0x{BASE:08x}\n")

        seen = set()
        for name in NAMES:
            vts = rtti_walk(img, name)
            if not vts:
                print(f"-- {name}: not found")
                continue
            for v in vts:
                if v["vt"] in seen:
                    continue
                seen.add(v["vt"])
                print(f"-- {name}")
                print(f"   str=0x{v['str']:08x} ti=0x{v['ti']:08x} "
                      f"vt=0x{v['vt']:08x}")
                entry = {"name": name, "vt": hex(v["vt"]),
                         "slots": [], "xrefs": [hex(a) for a in img.xrefs(v["vt"])],
                         "callers": []}
                run_zero = 0
                for i in range(v["n"]):
                    a = img.word(v["slots"] + 4 * i)
                    if a is None:
                        break
                    if a == 0:
                        run_zero += 1
                        if i >= 4 and run_zero >= 3:
                            print(f"   [{i:2d}] ... (rest zero)")
                            break
                        print(f"   [{i:2d}] 0x00000000  pure/end")
                        entry["slots"].append({"i": i, "addr": 0, "cls": "pure"})
                        continue
                    run_zero = 0
                    cls = classify(img, a)
                    ins = img.disasm(a, 16, 3)
                    text = " | ".join(f"{x.mnemonic} {x.op_str}" for x in ins)
                    print(f"   [{i:2d}] 0x{a:08x}  {cls or '':18s} {text}")
                    entry["slots"].append({"i": i, "addr": hex(a), "cls": cls,
                                           "code": text})
                    if i < 8:
                        entry["callers"] += [
                            {"slot": i, "at": hex(c), "kind": k}
                            for c, k in img.callers(a, 8)]
                result["vtables"].append(entry)
                print()

        print("== cross-references ==")
        for name in NAMES:
            s = img.find_string(name)
            if s:
                print(f"  {name}: str 0x{s:08x}  refs {[hex(a) for a in img.xrefs(s)]}")
        for a in rep["movw"][:8]:
            print(f"  movw_fc15 0x{a:08x}  callers "
                  f"{[(hex(c), k) for c, k in img.callers(a, 8)]}")
        for a in rep["bringup"][:8]:
            print(f"  bringup_table 0x{a:08x}  refs {[hex(x) for x in img.xrefs(a)]}")

    print("\n== live objects (from report) ==")
    for a in rep["objects"]:
        print(f"  0x{a:08x}")

    if args.json:
        args.json.write_text(json.dumps(result, indent=2) + "\n")
        print(f"\nwrote {args.json}")


if __name__ == "__main__":
    main()
