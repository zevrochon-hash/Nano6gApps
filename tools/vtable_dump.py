#!/usr/bin/env python3
"""Extract the TRFTuner_RTXC and TRFTuner vtables from the pulled image,
disassemble the vtable slots, and identify the methods.
"""
import struct, subprocess, os

IMG = "/home/zev/Desktop/iPod/Build/NanoApps/ports/radioplus/re/out/os_image.bin"
d = open(IMG, "rb").read()
BASE = 0x08000000

def u32(a):
    return struct.unpack_from("<I", d, a - BASE)[0]

def u16(a):
    return struct.unpack_from("<H", d, a - BASE)[0]

def find_all(word, lo=0x8000000, hi=0x8c00000):
    out = []
    off = lo - BASE
    data = d[off:hi - BASE]
    i = 0
    while True:
        j = data.find(word.to_bytes(4, "little"), i)
        if j < 0: break
        out.append(BASE + j)
        i = j + 1
    return out

def disasm_word(a, n=1):
    """disassemble one 32-bit word at address a (capstone)"""
    try:
        from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
        cs = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        ins = next(cs.disasm(d[a - BASE:a - BASE + 4], a))
        return ins.mnemonic + " " + ins.op_str
    except Exception:
        return "?"

# The vtables (values) found by the probe
vtable_values = {
    "N3ISL8TRFTunerE (abstract TRFTuner)": 0x087f0050,
    "N3ISL12TRFTuner_IAPE": 0x087ecfd8,
    "N3ISL13TRFTuner_RTXCE": 0x087ed41c,
}

# Wait — the probe's names said: 8TRFTunerE vt=0x087f0050, 12TRFTuner_IAPE vt=0x087ecfd8, 13TRFTuner_RTXCE vt=0x087ed41c
# Let me check the actual slots and values from the report
report = open("/home/zev/Desktop/iPod/Build/NanoApps/ports/radioplus/re/out/fm_probe.txt").read()
import re
def sect(name):
    return report.split(name + "\n")[1].split("\n")[0]

# Find the vtable value for each class
# The report lines: "   ti=... vt=..."; extract
print("=== vtable values from report ===")
for line in report.splitlines():
    if line.startswith("   ti="):
        print(line)

print("\n=== vtables in image (address where the vtable VALUE appears) ===")
# The vtable VALUE 0x087ed41c is stored at some address V; the vtable struct at V has [0]=type_info
for cls, val in vtable_values.items():
    hits = find_all(val)
    print(f"{cls}: vtable value 0x{val:08x} stored at {hits}")

print("\n=== method addresses (vtable slot values) ===")
for cls, val in vtable_values.items():
    hits = find_all(val)
    for V in hits:
        print(f"\n-- {cls} vtable at 0x{V:08x} --")
        for slot in range(40):
            addr = V + 4 * slot
            sv = u32(addr)
            if sv == 0:
                print(f"  [{slot:2d}] (null)")
                continue
            # first instruction at the slot
            first = disasm_word(sv)
            print(f"  [{slot:2d}] 0x{sv:08x}  {first}")
