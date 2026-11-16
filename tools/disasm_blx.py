import struct
d=open('ports/radioplus/re/out/os_image.bin','rb').read()
BASE=0x08000000
def u32(a): return struct.unpack_from('<I', d, a-BASE)[0]
sites = {0x0841b03d:'slot6', 0x084527a1:'slot7', 0x0841a629:'slot8', 0x084527f7:'slot9', 0x084133b7:'slot10', 0x0847310d:'slot13'}
try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
    cs = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    for a,name in sites.items():
        print('--- %s @%08x ---' % (name,a))
        for off in range(0, 40, 4):
            addr=a-off
            ins=next(cs.disasm(d[addr-BASE:addr-BASE+4], addr))
            print('  %08x: %s' % (addr, ins))
except Exception as e:
    print("capstone err:", e)
