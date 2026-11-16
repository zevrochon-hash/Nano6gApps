# Radio+ nano 7 — Phase 1 findings (from live FM_PROBE)

## Device
- iPod nano 7 (N31), RetailOS 1.1.2 / 39A10023, boot-report magic RTP2
  (0x086c9b84+0x2c), resident_version 1, ABI 2, live flag "DISK", kind GL_SURFACE.

## Apple Radio objects located (Itanium ABI RTTI walk)
| class | str | type_info | vtable | vtable struct (real @ +4) | vptr value |
|---|---|---|---|---|---|
| N3ISL8TRFTunerE (abstract base) | 0x087948c5 | 0x0878bbc0 | 0x087f0050 | 0x087f0054 | 0x087f0050 |
| N3ISL11IPodRFTunerE | 0x08793d93 | (no ti landed) | — | — | — |
| N3ISL12TRFTuner_IAPE | 0x08793e77 | 0x0878b590 | 0x087ecfd8 | 0x087ecfdc | 0x087ecfd8 |
| N3ISL13TRFTuner_RTXCE (concrete) | 0x08793f25 | 0x0878b62c | 0x087ed41c | 0x087ed420 | 0x087ed41c |

- vtable slot values are the method addresses; the concrete tuner's real-work
  implementations sit at the BLX sites:
  0x0841b03d, 0x084527a1, 0x0841a629, 0x084527f7, 0x084133b7, 0x0847310d
- The abstract base (0x087f0050) copies the same method layout, so the whole
  TRFTuner interface is recoverable from the vtable slots.

## HCI / FM opcodes (from the 17 MB image dump)
- FM_RDS_Command vendor opcode = 0xFC15  (OGF=0x3F, OCF=0x015) — a bare halfword
  that occurs 96 times in the image: the dispatch/table sites and the command
  builder references. The command encoding is OGF<<10 | OCF.
- "bring-up" sequence 03 0C 01 10 09 10: not present in the image as searched —
  the FM bring-up opcodes are still unidentified (the recovered table is the
  concrete tuner's method implementations, which are the better target).
- All other HCI opcodes visible in the image are data; 0xFC15 is the FM command.

## Live objects (scan_objects, heap 0x08a00000..0x0c000000)
iAP tuner (0x08d17c64, 0x08d17c8c), 18TRFTunerPresetList (0x08d17c64,
0x08d17c8c, 0x0b4147d4, 0x0b4147ec), 23TCSettings_RadioRegions (four instances).

Live concrete tuner object found by image scan (host-side): RTFTuner_RTXC at
0x09205438, vptr 0x087ed41c, vtable struct 0x087ed420. The object is owned by
the Radio task.

## Image dump
- 17825792 bytes: 0x08000000..0x09100000 (OS image + homebrew window).
- Full report at ports/radioplus/re/out/fm_probe.txt (57 KB).
- os_image.bin is available for static disassembly (capstone ARM).

## Phase 2 plan
Build platform/tuner_device.c implementing tuner.h by calling the live
ISL::TRFTuner_RTXC object through its vtable (tune/seek/state/RDS), guarded by
en_tuner_can_raw() and the Radio-task ownership question. Key risk: the object
is owned by the Radio task; a call from the app UI-task context may be unsafe
and must be checked on hardware. AM is not supported (BCM2078 is FM-only).
