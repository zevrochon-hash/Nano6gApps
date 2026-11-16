#!/usr/bin/env python3
"""HCI/FM opcode analyzer for the nano 7 OS image (12 MB, IMG_LO..DUMP_END).
  - every 0xFC15 halfword site with 12 bytes context
  - bring-up pattern 03 0C 01 10 09 10 (and LE-word variants) with hits
  - capstone disassembly around the first N sites
"""
import struct
import numpy as np
from capstone import *
HAVE_CM = True
try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
    from capstone.arm import *
except ImportError:
    HAVE_CM = False

IMG = "/home/zev/Desktop/iPod/Build/NanoApps/ports/radioplus/re/out/os_image.bin"
IMG_LO, IMG_HI = 0x08000000, 0x09100000
DUMP_END = 0x08C00000

d = open(IMG, "rb").read()
need = DUMP_END - IMG_LO
assert len(d) >= need, (len(d), need)

out = []

# ---- 1. all 0xFC15 halfword sites with context ----
half = np.frombuffer(d, dtype=np.uint16, count=len(d) // 2)
idx = np.nonzero(half == 0xFC15)[0]
sites = [IMG_LO + 2 * i for i in idx]
out.append("== 0xFC15 halfword sites (%d) ==" % len(sites))
for a in sites:
    lo = max(0, a - IMG_LO - 8); hi = min(len(d), a - IMG_LO + 16)
    out.append("%08x  %s" % (a, " ".join("%02x" % b for b in d[lo:hi])))

# ---- 2. bring-up pattern 03 0C 01 10 09 10 (and LE variants) ----
pat = bytes([0x03, 0x0C, 0x01, 0x10, 0x09, 0x10])
p2 = bytes([0x0C, 0x03, 0x01, 0x10, 0x09, 0x10])   # 0x030C,0x0101,0x0901 LE words
hits = sorted({IMG_LO + i for i in range(len(d) - 5) if d[i:i+6] == pat})
hits += sorted({IMG_LO + i for i in range(len(d) - 5) if d[i:i+6] == p2})
out.append("\n== bring-up pattern hits (%d) ==" % len(set(hits)))
for a in set(hits):
    out.append("%08x  %s" % (a, " ".join("%02x" % b for b in d[a-IMG_LO:a-IMG_LO+12])))

# ---- 3. command/OGF=0x3F references within 4KB of each site ----
out.append("\n== OGF=0x3F (packed 16-bit) opcodes within 4KB of sites ==")
seen = set()
for a in sites:
    lo = max(0, a - IMG_LO - 0x1000); hi = min(len(d), a - IMG_LO + 0x1000)
    chunk = d[lo:hi]
    for i in range(0, len(chunk) - 1, 2):
        o = struct.unpack_from("<H", chunk, i)[0]
        if (o >> 10) == 0x3F and (o & 0x3FF) not in seen:
            seen.add(o & 0x3FF)
            out.append("  %08x raw=0x%04x ocf=0x%03x name=%s" % (
                a, o, o & 0x3FF,
                {
                    0x001: "FM_RDS_Command+?" , 0x003: "FM_SetStationInfo",
                    0x009: "FM_SetRadioStation", 0x015: "FM_RDS_Command",
                    0x021: "FM_GetRadioSetting", 0x025: "FM_SetRadioSetting",
                    0x031: "FM_RDS_PopChannel", 0x035: "FM_RDS_Scan",
                    0x041: "FM_RDS_SetPreEdit", 0x051: "FM_GetTunerSettings",
                    0x061: "FM_SetTunerSettings",
                }.get(o & 0x3FF, "?")))

# ---- 4. disassembly around first 16 sites ----
if HAVE_CM:
    cs = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    out.append("\n== disassembly around first 16 sites ==")
    for a in sites[:16]:
        out.append("--- site %08x ---" % a)
        lo = max(0, a - IMG_LO - 0x100); hi = min(len(d), a - IMG_LO + 0x180)
        for i in range(lo, hi - 4, 4):
            try:
                ins = next(cs.disasm(d[lo:hi][i - lo:i - lo + 4], a + (i - lo)))
                out.append("  %08x: %s" % (a + (i - lo), ins))
            except StopIteration:
                pass

open("/home/zev/Desktop/iPod/Build/NanoApps/ports/radioplus/re/out/hci_sites.txt", "w").write("\n".join(out))
print("sites:", len(sites), "| wrote hci_sites.txt")
