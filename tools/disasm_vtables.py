#!/usr/bin/env python3
"""Disassemble the vtable methods for TRFTuner_RTXC (0x087ed420) and the
abstract base (0x087f0054), and the iAP tuner vtable (0x087ecfdc).
Identify the real-work methods (BLX sites)."""
import struct

IMG = "/home/zev/Desktop/iPod/Build/NanoApps/ports/radioplus/re/out/os_image.bin"
d = open(IMG, "rb").read()
BASE = 0x08000000

def u32(a): return struct.unpack_from("<I", d, a - BASE)[0]
def disasm(a, n=8):
    try:
        from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
        cs = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        out = []
        for ins in cs.disasm(d[a - BASE:a - BASE + 4 * n], a):
            out.append("%s %s" % (ins.mnemonic, ins.op_str))
        return out
    except Exception as e:
        return ["(disasm error: %s)" % e]

# vtable struct addresses
vtables = {
    "N3ISL13TRFTuner_RTXCE (concrete)": 0x087ed420,
    "N3ISL12TRFTuner_IAPE": 0x087ecfdc,
    "N3ISL8TRFTunerE (abstract base)": 0x087f0054,
}

for cls, V in vtables.items():
    print("=" * 72)
    print("%s  vtable struct @ 0x%08x" % (cls, V))
    print("  [0] type_info = 0x%08x" % u32(V))
    print("  methods (40 slots):")
    for slot in range(40):
        a = V + 4 + 4 * slot
        m = u32(a)
        if m == 0:
            print("    [%2d] (null)" % slot)
            continue
        # check if this slot is a BLX (the first instruction is BLX)
        ins = disasm(m, 1)
        prefix = "BLX" if ins and "BLX" in ins[0] else "   "
        dis = disasm(m, 2)
        print("    [%2d] 0x%08x  %s %s" % (slot, m, prefix, dis[0] if dis else ""))
