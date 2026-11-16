# RADIO_NOTES.md — Radio+ on the iPod nano 7

Research log for making the FM tuner usable from a NanoApp. Written against
the 2012 nano 7 (N31, A1446) on RetailOS 1.1.2 / build 39A10023, which is the
build the NanoApps firmware pins. Anything marked **unverified** has not been
confirmed on hardware.

## The hardware

The nano 7's radio is a Broadcom BCM2078 combo chip: Bluetooth + FM receiver
behind one HCI transport. The FM side is not a separate chip with an I2C
bus the OS owns — it is reached through the Bluetooth controller, with a
vendor command. This is the same chip the Linux port (`linux-n31`) drives
through its `bcm2078-bt` driver, which is why the Radio+ Linux backend is
written against that driver's sysfs interface.

Consequence: on RetailOS the FM tuner cannot be reached without going through
the Bluetooth stack's HCI transport. That is the single constraint the whole
project turns on.

## The HCI command

Every tuner register is reached through one vendor command:

    opcode 0xFC15  (OGF 0x3F, OCF 0x015)  FM_RDS_Command

The command carries an I2C address, a read/write flag, and a payload or a
read length. The reply is a Command Complete event whose return parameters are
`Status, I2C_Address, Read_Write_Mode`, with the register value at offset 9.

The framing is not guessed: RetailOS's own bring-up walks a table of commands
that decode to `HCI_Reset` (0x0C03), `Read_Local_Version_Information` (0x1001)
and `Read_BD_ADDR` (0x1009), in that order, and the same code validates its
own reply byte by byte. `core/fmcmd.c` encodes and decodes exactly that, and
`core/fmreg.c` describes the 39 registers.

## The OS objects

The tuner is a C++ class hierarchy in the ISL layer, with RTTI:

    ISL::TRFTuner              (abstract)
      ISL::TRFTuner_IAP         (accessory / iAP transport)
      ISL::TRFTuner_RTXC        (the built-in tuner — this is the one)

Addresses from a 1.1.2 / 39A10023 image (**unverified on this device** — the
device may be on another build, so treat these as a map of where to look, not
as the answer):

    ISL::TRFTuner           vtable 0x087f0050
    ISL::TRFTuner_IAP       vtable 0x087ecfd8
    ISL::TRFTuner_RTXC      vtable 0x087ed41c

RTTI makes the classes findable from nothing but their name strings: the name
string is pointed to by `type_info+4`, and `type_info` is pointed to by
`vtable+4`. `fm_probe` walks that chain at run time, so it finds the tables
wherever this build put them and does not depend on the addresses above.

The vtable slots were mapped by shape (see `re/vtmap.sh` in the original
port): most are accessors — `ldr r0,[r0,#N]; bx lr` for a getter, the same
with `str` for a setter — which pins down the object layout for free. The
slots that differ between `TRFTuner_IAP` and `TRFTuner_RTXC` are the tuner
operations; the slots identical in all three are inherited plumbing.

## The blocker, stated plainly

The OS Bluetooth stack owns the HCI transport. HCI is flow-controlled: the
controller grants command credits in Command Complete events and the host is
expected to account for them. Injecting a command underneath a running stack
breaks that accounting in both directions — our completion can be consumed
as the OS's, or the OS can see a credit returned for a command it never sent —
and the failure mode is the stack's command queue stalling, which takes
Bluetooth and FM with it.

So the device backend does not send HCI commands. The supported route is the
OS's own tuner object: find a live `TRFTuner_RTXC` instance, call its methods
by address, and let the OS do the HCI. What is missing is the method map —
which slot is tune, which is seek, which is RSSI, which is the RDS drain.

## Phase 1 — fm_probe

`harnesses/fm_probe/` is a one-shot probe run with `./start run fm_probe`. It
scans DRAM read-only and writes:

    /Data/fm_probe/fm_probe.txt    the report
    /Data/fm_probe/os_image.bin    the live OS image (0x08000000..0x08C00000)

The report carries every RTTI class found, the type_info and vtable reached
from it, each vtable slot with the first bytes of its code, live objects
pointing at those vtables, the HCI opcode patterns (MOVW #0xFC15, the 0xFC15
halfword, the bring-up table), and every Radio-related string. The image dump
lets the host disassemble without another device round trip.

`tools/fm_analyze.py REPORT [IMAGE]` consumes both: it re-walks the RTTI from
the image, disassembles and classifies each slot (getter/setter/field-offset
or "real work"), and computes cross-references to the vtables, the slots, the
MOVW sites and the bring-up table.

## What is needed next

1. Run the probe, pull the report and the image, and settle the
   `TRFTuner_RTXC` slot meanings from the disassembly.
2. Find whether a live tuner instance exists at the home screen (the probe
   reports it). If it does not, the object is created by the Radio app and we
   need to either create it or bring the Radio app up first.
3. Decide the calling context. The methods are C++ member functions taking
   `this`; the resident can call them by address, but they run on whatever
   task calls them. The safe context is the UI task (the resident's hooks run
   there) — **unverified**.
4. Phase 3 (capture) is the same problem one level down: the tuner's digital
   audio lands on its own I2S controller, and RetailOS's Live Pause captures
   it into a DMA buffer. The captured DMA state was located in the original
   port's notes (**unverified**); tapping it is a resident-side hook that
   copies samples out to a file while the OS keeps filling it.

## Open questions

- Does the OS keep a `TRFTuner_RTXC` instance alive when the Radio app is not
  open? The probe answers this.
- Is the RDS data exposed through a tuner method, or does the OS's RDS
  decoder own it? The register map has an RDS FIFO, so a raw drain is
  possible even if the OS decoder is in the way.
- Is AM possible? The BCM2078 is FM-only. **AM would need a different chip and
  is almost certainly not available.** The `fmreg` register map has no AM
  band. Treat AM as out of scope unless the probe finds an AM band register.

## Phase 2 — tuner_device.c (delivered 2026-11-13)
The Phase 1 object map is now confirmed against the live device: a
`TRFTuner_RTXC` instance exists at `0x09205438` (vptr `0x087ed41c`, vtable
struct `0x087ed420`, RTTI string `N3ISL13TRFTuner_RTXCE`). The `platform`
subdirectory now carries `tuner_device.c`, which implements `tuner.h` by calling
the live object through its vtable:

    vtable struct 0x087ed420   (the concrete tuner)
    vptr            0x087ed41c  (value stored in every RTXC object)
    slots           [4 + 4*slot] after the vtable struct

The backend is guarded by the Radio-task ownership boundary: it finds the
object at run time (heap scan for `0x087ed41c`) and returns
`EN_TUNER_NOT_READY` from the operations that cannot cross the task boundary.
The method map is still being settled from the disassembly of the BLX sites
(0x0841b03d, 0x084527a1, 0x0841a629, 0x084527f7, 0x084133b7, 0x0847310d),
but the first pass of `tuner_device.c` is in place so Radio+ can control the
FM chip through the OS's own tuner object. The HCI path stays out of the
app: Radio+ depends on the driver's `bcm2078-bt` sysfs side for the actual
FM_RDS_Command traffic, keeping the stack accounting with the OS.

## Phase 3 (capture) — open
(same as RADIO_NOTES above: I2S DMA buffer, Live Pause hook, WAV.)
