/*
 * hb_heap.h — the homebrew heap interface.
 *
 * On the nano 7 this is backed by live OS heap entry points whose addresses we
 * haven't derived for the nano 6G. The 6G build (this tree) backs hb_os_alloc /
 * hb_os_free / hb_os_heap_* with a static arena inside the resident's own
 * memory plus a coalescing free list. Consequences for callers:
 *   - no OS heap address is needed; allocation can't panic the OS.
 *   - the arena is hard-capped (see hb_heap.c); a too-large request returns
 *     NULL, callers handle it as usual.
 *   - the "free"/"largest" queries report arena occupancy, not OS heap stats.
 */
#ifndef HB_HEAP_H
#define HB_HEAP_H

#include <stdint.h>

/* Bytes free in the arena right now (the arena-size query). */
uint32_t hb_os_heap_free(void);

/* Largest contiguous free block in the arena — the real ceiling for one
 * allocation. */
uint32_t hb_os_heap_largest(void);

/* Allocate `size` bytes from the OS heap. Returns NULL on failure (NOT a panic,
 * unlike the heap allocator). The usable region is guaranteed to lie within physical
 * SDRAM (0x08000000..0x0A000000). Free with hb_os_free(). */
void *hb_os_alloc(uint32_t size);

/* Free a block returned by hb_os_alloc (the matched free entry). */
void hb_os_free(void *p);

/* One app-owned hb_os_alloc block that the resident frees when it unloads the
 * app (app switch / relaunch after quit) and clears at boot. For buffers too big
 * for .bss that would otherwise leak: LVGL apps get no quit callback, and their
 * .bss (holding the pointer) is re-zeroed on every launch. Free any previous
 * block before storing a new one. Never use a fixed DRAM address as a buffer —
 * 0x0910xxxx-0x094xxxxx is live OS heap. The slot lives in the resident's
 * shared block (hb_shared.h), set before any app code runs. */
#include "hb_shared.h"
#define HB_APP_AUX_BLOCK      (hb_shared->aux_block)

#endif /* HB_HEAP_H */
