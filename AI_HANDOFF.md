# AI handoff for NanoApps 6G work

This page is for any AI or human picking up work in this repository.

## What this repository is

This repo is a **NanoApps port/work tree focused on the iPod nano 6G**, derived from the NanoApps 7G project.

It now builds cleanly from a fresh clone, including:

- firmware bootstrap assets
- release IPSWs
- the disk ZIP containing `Apps/`, `Data/`, and `Library/`
- vendored build dependencies under `tools/dependencies/`

Important: the repository contains code and release builders for the original 7G-style flow, but the **current 6G device state is not equivalent to a finished 7G install**.

## Current status at a glance

### What works

- The source tree builds from a fresh clone.
- `tools/build_firmware.py` works.
- `tools/build_release.py` works.
- `system/disk_bootstrap` now fits the deferred-stage 16-bit immediate limits.
- The 6G resident can be loaded into RAM for testing.
- 6G home-controller hooks have been observed live in RAM.
- A 3-folder disk bundle can be produced: `Apps/`, `Data/`, `Library/`.

### What does not work yet on a real nano 6G

- The nano 6G does **not currently have a persistent boot-time loader in flash**.
- Because of that, simply copying `Apps/`, `Data/`, and `Library/` to the disk is currently **inert** on the tested 6G device state.
- Rebooting or unplugging clears the RAM-loaded resident and restores stock behavior.
- Homebrew icons do **not** yet survive reboot or appear automatically on a stock-booting 6G from disk alone.

## Most important facts discovered so far

### 1) The big build failure was fixed

The previous blocker was:

`resident exceeds deferred-stage 16-bit immediates`

That is now fixed by two main changes:

- `sdk/hb_heap.c` now uses the 6G OS allocator directly instead of embedding a huge static heap arena in `.bss`
- relocatable apps now use `sdk/hb_reloc.ld`, which packs sections tightly instead of leaving huge gaps in the image span

As a result, `system/disk_bootstrap/build/disk_bootstrap.hbapp` shrank enough for the deferred stage builder.

### 2) Fresh clones were missing required vendored dependencies

The repo originally omitted `tools/dependencies/` content needed by real builds. That has been restored.

This matters because the build expects:

- `tools/dependencies/ipodhax`
- `tools/dependencies/silverutil`
- `tools/dependencies/fontawesome`

Do not remove those just because they look vendored or large; the release build needs them.

### 3) The current install story on 6G is not finished

The user-facing goal is still:

1. copy `Apps/`, `Data/`, `Library/` to disk
2. reboot/eject
3. see homebrew icons on the native Home Screen

That is **not currently achieved on 6G** without a persistent loader path.

### 4) An external 6G address map was reviewed, and parts of it are wrong

A contributor supplied a "fully resolved" 6G address map. It is kept at
`docs/n6g_addrs_external.h` (reference only, deliberately `#error`s if included
in a build). The review is `docs/N6G_ADDRESS_MAP.md`.

Headline: **do not treat it as truth.** Useful new leads (touch list layout,
framebuffer geometry, LCD controller base) are mixed with entries that
contradict constants verified live on hardware:

- it says there is no patchable home-controller vtable; this tree has verified
  hooks at `0x086c9658` taking control in RAM
- it says the post-touch entry is not applicable; this tree patches
  `0x084a27f6` and polling the touch list alone missed most taps
- its `getresource` address differs from the byte-verified one this tree
  patches (`0x08409e86`)
- its resident load address is identical to its framebuffer base, so following
  it would overwrite the framebuffer

If you are tempted to "modernise" constants from that file, read the review
first. Verify a prologue live before trusting any address.

## Repository-specific notes

### Files worth reading first

- `README.md` — general project overview, but some of it is still written from the original 7G perspective
- `AI_HANDOFF.md` — this file; prefer this for current 6G status
- `docs/N6G_ADDRESS_MAP.md` — review of the external 6G address map, with a conflict table
- `docs/n6g_addrs_external.h` — the external map itself, reference only, not build input
- `tools/build_firmware.py` — builds firmware/bootstrap assets, never flashes a device
- `tools/build_release.py` — full release build
- `tools/build_firmware_bootstrap.py`
- `tools/build_deferred_stage.py`
- `sdk/hb_heap.c` — 6G heap strategy now uses OS alloc/free entry points
- `sdk/hb_reloc.ld` — compact relocatable linker layout
- `system/disk_bootstrap/disk_bootstrap.c`
- `system/disk_bootstrap/Makefile`
- `system/silver_resident/silver_resident.c`
- `system/silver_resident/Makefile`

### Current important 6G implementation details

- 6G code execution via SCSI/RAM testing was found to be stable only when staged through uncached alias `0x89358000` (alias of `0x09358000`)
- `hb_icache_invalidate` was made a no-op on 6G because an earlier SVC-based path crashed
- direct OS allocator/free addresses currently used on 6G:
  - alloc: `0x08178df4`
  - free: `0x0817d93c` with flags `3`

These are already reflected in code; do not casually “simplify” them away.

## Device-state caveat

There is **no iPod connected right now** according to the latest operator context. Assume this repository work should remain host-side unless explicitly told otherwise.

Also, previous work found that the specific nano 6G device being tested is fragile:

- it can white-screen or reboot easily
- repeated flash attempts were unreliable
- the user explicitly asked to stop repeated flash attempts for now

So if future work involves actual hardware again:

- avoid unnecessary writes
- do not assume a persistent flashed bootstrap exists
- do not assume reboot persistence unless re-verified on-device

## What is already on GitHub

Public repo:

- `https://github.com/zevrochon-hash/Nano6gApps`

Key known commits:

- `eea5a9c` — initial public 6G port snapshot
- `a47cbe5` — fix firmware build by shrinking bootstrap span to fit deferred-stage immediates
- `6e2abd8` — restore vendored build dependencies and stop ignoring required Font Awesome assets
- `eeb5efd` — add this handoff page and correct stale 6G status notes
- the commit that adds `docs/` — external 6G address map plus the review in `docs/N6G_ADDRESS_MAP.md`

## Best next tasks

If you are continuing the 6G effort, the highest-value tasks are probably:

1. **Make the README accurately describe 6G status**
   - Right now parts of it still read as if this repo were a finished 7G project.
   - If you edit it, be careful not to throw away useful upstream documentation.

2. **Figure out a practical non-flash 6G workflow**
   - Since persistent flash bootstrap is not currently available, investigate whether the 7G-style “push resident each boot” experience can be adapted to the 6G using the existing SCSI path.

3. **Continue 6G icon registration/home-screen integration research**
   - Hooking was observed live in RAM, but icons still did not appear in the desired persistent end-user flow.

4. **Verify the new touch-list lead**
   - `0x08800118` (count `+0x04`, x `+0x3c`, y `+0x48`, first node `+0x60`,
     pixel = fixed16 >> 4) is plausible but unverified. Confirm it with a live
     read before building anything on it. Keep the post-touch hook for input;
     polling alone was unreliable.

5. **Keep release/build reproducibility intact**
   - Do not break fresh-clone builds while experimenting.

## Cross-platform guidance for future AI workers

Do not assume the next person is on Linux or macOS.

When writing notes or instructions:

- prefer naming scripts/files over giving shell-specific commands
- refer to paths and entry points clearly
- say “create a Python virtual environment and install `tools/requirements-release.txt`” instead of assuming one shell syntax
- say “use the Rust toolchain version pinned in `firmware/rust-toolchain.toml`” instead of assuming a specific local Rust setup
- if you do provide commands, make them easy to translate

## Things not to do casually

- Do not delete `tools/dependencies/` because it looks like third-party cruft.
- Do not assume the three-folder install flow already works on 6G.
- Do not assume README claims inherited from upstream are accurate for this fork.
- Do not attempt risky device flashing unless the operator explicitly wants that.

## Short truth-in-advertising summary

This repository is in a much better state than before:

- **builds are fixed**
- **fresh clones work**
- **release artifacts generate correctly**

But the core product goal for the nano 6G is still unfinished:

- **drag-and-drop folders alone do not yet make homebrew apps appear on the 6G Home Screen on the currently tested device state**
