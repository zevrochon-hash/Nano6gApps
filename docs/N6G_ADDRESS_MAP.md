# Nano 6G address map — review of the external map

An outside contributor supplied a "fully resolved" nano 6G address map
(`docs/n6g_addrs_external.h`). This page records how it lines up with the
constants this tree has actually exercised on hardware, so nobody re-derives
or blindly overwrites the wrong thing.

**Short version: keep it as a lead list, not as truth.** Several entries are
useful and new. At least four are wrong or internally inconsistent.

---

## 1. Where the two maps disagree

| Item | External map | This tree (verified live) | Verdict |
|---|---|---|---|
| Home controller vtable | `0x00000000` — "no patchable C++ vtable on 6G" | `0x086c9658` (`HB_SILVER_HOME_VTABLE`) | **Tree wins.** Hooks at `+0x40`, `+0x184`, `+0x18c` were installed and observed taking control in RAM. |
| Action dispatch | "N/A — app launch via SCSI channel" | slot `+0x18c`, stock `0x080a6b25` | **Tree wins.** The SCSI path is how we *inject* code, not how the OS launches home items. |
| `getresource` entry | `0x08408be0`, prologue `0xe92d5ff0` | `0x08409e86`, prologue `0xb5f8` (`f8 b5 04 46 00 78 1d 46 0e 46`) | **Tree wins.** Our address is byte-verified and already function-patched with a stock-bytes guard. `0x08408be0` may be a different wrapper; do not swap without a live prologue check. |
| Touch post entry | `0x00000000` — "N/A, poll `TOUCH_LIST_HEAD`" | `0x084a27f6`, prologue `0xe92d` | **Tree wins.** Polling the transient list missed roughly 3 of 4 taps. The post entry latches every event. |
| Resident load / staging | `0x089c0000` | `0x09358000` (uncached alias `0x89358000`) | **Tree wins for SCSI staging.** `0x09358000` is the tested window. See the framebuffer conflict below for why `0x089c0000` is suspect anyway. |
| `OUR_ID_BASE` | `0x0dad8000` | `0x0dad8000` | **Agreement.** Good corroboration. |

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
- `system/silver_resident/silver_resident.c` is the authority for the
  `getresource` and post-touch patch sites, including their stock-byte guards.
- `sdk/hb_heap.c` is the authority for 6G allocation.
- If you change a prologue or stock-byte array, the guard will refuse to patch
  and the resident will log a refusal. That is the intended failure mode, not
  a bug to route around.
