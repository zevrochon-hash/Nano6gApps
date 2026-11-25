# Nano 6G address map — review of the external map

An outside contributor supplied a "fully resolved" nano 6G address map
(`docs/n6g_addrs_external.h`). This page records how it lines up with the
constants this tree has actually exercised on hardware, so nobody re-derives
or blindly overwrites the wrong thing.

**Short version: keep it as a lead list, not as truth.** Several entries are
useful and new. At least four are wrong or internally inconsistent.

---

## 1. Where the two maps disagree

| Item | External map | What the live device shows (2026-11-25) | Verdict |
|---|---|---|---|
| Home controller vtable | `0x00000000` — "no patchable C++ vtable on 6G" | `0x086c9658`, valid Itanium vtable (typeinfo `0x086842f0`); stock slots `+0x40`/`+0x17c`/`+0x184`/`+0x18c` read back exactly | **External map wrong.** Slots are ordinary patchable Thumb pointers. |
| Action dispatch | "N/A — app launch via SCSI channel" | slot `+0x18c`, stock `0x080a6b25` | **External map wrong.** SCSI is the injection channel, not the OS launch path. |
| `getresource` entry | `0x08408be0`, prologue `0xe92d5ff0` | `0x08408be0` really does start with that prologue, but its body is a two-pass 128-byte-block copy routine taking a fifth stack argument | **Prologue right, identity unproven.** Not a 4-argument lookup. |
| Tree's old `getresource` | — | `0x08409e86` is **mid-instruction** here (`cmp r1, #0`) | **Tree constant is a 7G leftover.** The stock-bytes guard would refuse to patch, which is why that path is gated off on 6G. |
| Touch post entry | `0x00000000` — "N/A, poll `TOUCH_LIST_HEAD`" | real ARM routine at `0x084a26d6` (`push {r4-r11,lr}`) that walks touch arrays | **External map wrong.** There is a touch routine; the old `0x084a27f6` is mid-instruction on this unit. |
| Touch list `0x08800118` | count `+0x04`, x `+0x3c`, y `+0x48`, node ptr `+0x60` | stable for 60 s, **no DRAM pointers in 512 bytes**, `+0x60` = `0x0dad2117` (a resource id) | **Unconfirmed / likely wrong.** Cannot be a pointer-linked list head as described. |
| Resident load / staging | `0x089c0000` | `0x089c0000` is the framebuffer base; `0x09358000` reads zeroed | **External map wrong.** Keep the tree's staging window. |
| `OUR_ID_BASE` | `0x0dad8000` | `0x0dad8000` | **Agreement.** But it is an id base, not memory. |
| Framebuffer | `0x089c0000`, 240x240x2 | RGB565 pixels at the base, zeros from `0x089dc200` | **Agreement.** |
| Touch mgr `0x086855bc` | "vtable, slots are sub-vtables, not fn ptrs" | table of C++ RTTI/vtable descriptors with Thumb function pointers | **Partly wrong.** The OS is normal Itanium C++ with patchable vtables. |

---

## 2. Internal contradictions in the external map

These are wrong regardless of what this tree does.

### 2.1 The resident load address sits on the framebuffer

```c
#define N6G_FB_BASE            0x089c0000u
#define N6G_RESIDENT_LOAD_ADDR 0x089c0000u   /* "Sits safely above FB end" */
```

`0x089c0000 + 0x1C200 = 0x089dc200`, so the framebuffer occupies
`0x089c0000..0x089dc1ff`. The load address is the framebuffer *base*, not above
its end. Either the comment is wrong or the constant is. A load address would
have to be at least `0x089dc200` to be clear of it.

If someone uses this map as-is, the resident is written straight over the
live framebuffer.

### 2.2 Scratch flags live inside the stack

```
N6G_APP_STACK_TOP  = load + 0x10000   /* 64 KB stack */
N6G_TIMER_ADDR     = load + 0x5c00
N6G_FLAG_ADDR      = load + 0x5ffc
```

A 64 KB stack growing down from `+0x10000` occupies `+0x0000..+0x10000`.
That range contains `+0x5c00`, `+0x5f40`, `+0x5fec`, `+0x5ff0` and `+0x5ffc`.
Any real call depth would overwrite the flags, timer, and touch mailbox.

The numbers may have come from a build where the stack was much smaller than
the comment claims. As written, the layout is not self-consistent.

### 2.3 `N6G_APP_HEAP_BASE == N6G_APP_STACK_TOP`

That pairing is fine for a downward-growing stack against an upward-growing
heap, so this one is a note rather than a bug — but it only works if the
"64 KB stack" claim above is false.

---

## 3. Entries worth keeping

These are plausible and mostly new to this tree. Treat them as leads to verify
with a live read, not as confirmed.

- **Framebuffer geometry** — `240x240`, RGB565, stride `480`. Matches the
  device. `0x089c0000` as the base is worth re-checking, but the geometry is
  right.
- **`N6G_LCD_CTRL_BASE 0x38500000`** — useful, and the accompanying warning is
  correct: do **not** read peripheral registers over SCSI. Unmapped reads data
  abort the device.
- **`N6G_TOUCH_LIST_HEAD 0x08800118`** with `+0x04` count, `+0x3c` x, `+0x48`
  y, `+0x60` first-node pointer, and `pixel = fixed16 >> 4`. This is a real
  lead for reading touch state directly. It does not replace the post-touch
  hook for input latency, but it is useful for polling/diagnostics.
- **`N6G_TOUCH_MGR_VTABLE 0x086855bc`** — worth a look as a *sub-vtable*
  structure. The claim that slots are sub-vtables rather than function
  pointers is a useful caution.
- **`N6G_ADDR_OP_NEW` / `N6G_ADDR_OP_DELETE` TODO** — we already have working
  6G allocator/free entry points (`0x08178df4` / `0x0817d93c`, flags `3`), so
  this is low priority, but it is the right area to disassemble if the OS
  operator-new turns out to differ from the raw allocator.

---

## 4. How to check any of these on real hardware

Before trusting a new address, read its first bytes and compare against the
expected prologue:

- Thumb function: first halfword usually ends in `b5xx` (`push {...,lr}`)
- ARM function: first word `0xe92d5xxx` (`push {r4-r11,lr}`)

Use the project's existing SCSI peek helper rather than raw `dd` or a
platform-specific device path — the helper already resolves the correct iPod
and handles the transport. If a read faults or the prologue does not match,
the address is wrong; stop rather than writing to it.

Do this on a 6G that is known to be in normal mode, and never write to a
peripheral range over SCSI.

---

## 5. Rules for anyone editing constants

- Do not replace a live-verified constant with an unverified one from this
  external map. Add the new value alongside it with a comment and a way to
  select it, then test.
- `sdk/hb_silver_uitask.h` is the authority for home-controller slots.
  Verified live: `0x086c9658`, slots `+0x40`/`+0x17c`/`+0x184`/`+0x18c`.
- `system/silver_resident/silver_resident.c` contains the `getresource` and
  post-touch patch sites, but **its addresses are 7G leftovers on 6G**:
  `0x08409e86` and `0x084a27f6` both land mid-instruction on a live 6G. The
  stock-byte guards therefore refuse to patch, and the features are gated off
  (`HB_6G_HAVE_RESDB 0`, `HB_6G_HAVE_ICONPOLL 0`). Do not treat those
  constants as verified; the real touch routine observed on hardware is at
  `0x084a26d6`.
- `sdk/hb_heap.c` is the authority for 6G allocation.
- If you change a prologue or stock-byte array, the guard will refuse to patch
  and the resident will log a refusal. That is the intended failure mode, not
  a bug to route around.

---

## 6. Live verification run (2026-11-25)

Read-only probes against an attached 6G (`05ac:1266`, 7.3 GB) produced these
results. The shareable header that came out of this run is `n6g_addrs.h`
(outside the repo, on the operator's Desktop); this section is the record.

- Framebuffer: RGB565 pixels at `0x089c0000`, content through `0x089dbbff`,
  zero from `0x089dc200`. Geometry/size confirmed.
- Home vtable `0x086c9658`: valid Itanium header (typeinfo `0x086842f0`) and
  all four stock slot values matched exactly.
- `0x08408be0`: prologue `0xe92d5ff0` confirmed; body is a 128-byte-block,
  two-pass copy routine with a fifth stack argument, so its identity as
  `getresource` is not proven.
- `0x084a26d6`: real ARM touch routine (`push {r4-r11,lr}`), reads count at
  `+0x20` and arrays at `+0x30`/`+0x34`, writes through `+0x40`.
- `0x08800118`: identical for 60 s (no framebuffer change either), no DRAM
  pointers in 512 bytes, `+0x60` = `0x0dad2117`. Not a pointer list head.
- C++ RTTI strings found in the image: `TTouchGestureMgr`, `TSilverDaisyView`,
  `TWheelGestureMsg`, `TTextView`, `TTimerMsg` — the OS uses normal C++
  vtables, not encrypted ObjC-style dispatch.
- Allocator `0x08178df4` and free `0x0817d93c` are real Thumb entries.
- Transport note: the SCSI stub only answers **512-byte** reads. Requesting
  256 or 128 bytes fails and looks exactly like a bad address. Always read a
  full 512 and slice.
