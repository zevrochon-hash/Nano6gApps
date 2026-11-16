#!/usr/bin/env python3
"""Extract the real vtable structs for TRFTuner_RTXCE, N3ISL12TRFTuner_IAPE,
N3ISL8TRFTunerE (abstract base), and the iAP getPreset method, then
disassemble the first instructions of each slot."""
import struct

IMG = "/home/zev/Desktop/iPod/Build/NanoApps/ports/radioplus/re/out/os_image.bin"
d = open(IMG, "rb").read()
BASE = 0x08000000

def u32(a): return struct.unpack_from("<I", d, a - BASE)[0]
def u16(a): return struct.unpack_from("<H", d, a - BASE)[0]
def cstr(a):
    out = []
    i = a - BASE
    while i < len(d) and d[i] not in (0,):
        out.append(chr(d[i])); i += 1
    return "".join(out)

def find_word(word, lo, hi):
    """returns addresses where the 4-byte word appears, within [lo,hi)"""
    out = []
    i = lo - BASE
    data = d[i:hi - BASE]
    j = 0
    while True:
        k = data.find(word.to_bytes(4, "little"), j)
        if k < 0: break
        out.append(BASE + k)
        j = k + 1
    return out

def disasm(a):
    try:
        from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
        cs = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        ins = next(cs.disasm(d[a - BASE:a - BASE + 4], a))
        return "%s %s" % (ins.mnemonic, ins.op_str)
    except Exception:
        return "?"

# The vtable structs: find by the type_info that the probe identified.
# TRFTuner_RTXCE type_info = 0x0878b62c (at its [0] the vtable struct address is stored? no)
# From the image: vtable struct for RTXC is at 0x087ed420 with [0]=0x0878b62c
# The iAP vtable (12TRFTuner_IAPE) = 0x087ecfd8 has [0]=0x0878b590
# The abstract TRFTuner (8TRFTunerE) = 0x087f0050 has [0]=0x0878bbc0

vtable_structs = {
    "N3ISL13TRFTuner_RTXCE": 0x087ed420,
    "N3ISL12TRFTuner_IAPE": 0x087ecfd8,
    "N3ISL8TRFTunerE (base)": 0x087f0050,
}

for cls, V in vtable_structs.items():
    print("=" * 70)
    print("vtable struct 0x%08x  (%s)" % (V, cls))
    print("  [0] = 0x%08x  (type_info)" % u32(V))
    print("  [4] = 0x%08x  (first method)" % u32(V + 4))
    print("  [8] = 0x%08x  (second method)" % u32(V + 8))
    print("  ...")
    print("  [156] = 0x%08x  (slot 39)" % u32(V + 156))
    print("  methods:")
    for slot in range(40):
        m = u32(V + 4 + 4 * slot)
        if m == 0:
            print("    [%2d] (null)" % slot)
            continue
        dis = disasm(m)
        print("    [%2d] 0x%08x  %s" % (slot, m, dis))

# Also show the abstract base's vtable table (the 16 bytes at 0x087f0050)
print("=" * 70)
print("0x087f0050 abstract TRFTuner: first 16 bytes (the vtable)")
print("  ", " ".join("%08x" % u32(0x087f0050 + 4 * i) for i in range(4)))
