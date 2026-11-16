/*
 * hb_heap.c — OS main-heap interface, nano 6G (S5L8723) port.
 *
 * The 7G backs these with OS heap entry points whose addresses we have not
 * derived for the 6G yet. Rather than block the port on that RE, the 6G
 * variant is self-contained: a static arena inside the resident's own memory
 * plus a small coalescing free-list allocator. The resident is heap-loaded
 * by the untether, so the arena never touches live OS heap, never needs an
 * OS entry point, and can't corrupt the OS allocator state.
 *
 * The heap queries report arena occupancy; they exist for trace/diagnostic
 * parity with the 7G API, not for OS heap introspection.
 */

#include "hb_heap.h"

#include <stdbool.h>
#include <stddef.h>

/* Arena size is a per-image choice: the resident's arena lives inside the
 * resident's own loaded span, and that span is allocated from the BOOTSTRAP's
 * arena. So the bootstrap's arena must be strictly larger than the resident's
 * entire span (arena + code/bss), while the resident's arena only has to hold
 * its own allocations (app pack + one app image + icons). With both images at
 * 6 MB the resident could never fit inside the bootstrap — hence the split:
 * bootstrap 6 MB, resident 4 MB. */
#ifndef HB_ARENA_BYTES
#define HB_ARENA_BYTES  (4u * 1024u * 1024u)
#endif

#define ARENA_BYTES  HB_ARENA_BYTES

#define HDR_MAGIC    0x48424147u   /* 'HBAG' */
#define HDR_FREE     0x1u
#define HDR_OVERHEAD 16u           /* size(total incl. hdr) + magic/flag + pad/alignment */

typedef struct arena_hdr {
    uint32_t total;                /* total block size incl. this header   */
    uint32_t magic;                /* HDR_MAGIC | (free ? HDR_FREE : 0)    */
    struct arena_hdr *next_free;   /* free list link (HDR_FREE blocks)     */
    uint32_t pad;                  /* keeps payload 8-byte aligned          */
} arena_hdr_t;

static uint8_t  s_arena[ARENA_BYTES] __attribute__((aligned(8)));
static arena_hdr_t *s_free;
static uint32_t s_used;            /* bytes outstanding (incl. headers)   */
static uint32_t s_high_water;
static bool     s_inited;

static void arena_init(void)
{
    if (s_inited)
        return;
    s_inited = true;
    s_free = (arena_hdr_t *)s_arena;
    s_free->total = ARENA_BYTES;
    s_free->magic = HDR_MAGIC | HDR_FREE;
    s_free->next_free = NULL;
    s_free->pad = 0;
    s_used = 0;
    s_high_water = 0;
}

void *hb_os_alloc(uint32_t size)
{
    arena_hdr_t *best_fit = NULL, **best_fit_link = NULL;
    arena_hdr_t *cur, **link;

    if (!size)
        return NULL;
    arena_init();

    uint32_t want = (size + HDR_OVERHEAD + 7u) & ~7u;
    if (want < size)            /* overflow */
        return NULL;

    link = &s_free;
    while (*link) {
        cur = *link;
        if (cur->total >= want) {
            if (!best_fit || cur->total < best_fit->total) {
                best_fit = cur;
                best_fit_link = link;
                if (cur->total == want)
                    break;
            }
        }
        link = &cur->next_free;
    }
    if (!best_fit)
        return NULL;

    /* detach best_fit from the free list (we hold the link pointing at it) */
    *best_fit_link = best_fit->next_free;

    /* split the block if the remainder can hold a header + 16 bytes */
    if (best_fit->total >= want + HDR_OVERHEAD + 16u) {
        arena_hdr_t *tail = (arena_hdr_t *)((uint8_t *)best_fit + want);
        tail->total = best_fit->total - want;
        tail->magic = HDR_MAGIC | HDR_FREE;
        tail->next_free = s_free;
        tail->pad = 0;
        s_free = tail;
        best_fit->total = want;
    }
    best_fit->magic = HDR_MAGIC;
    best_fit->next_free = NULL;

    s_used += best_fit->total;
    if (s_used > s_high_water)
        s_high_water = s_used;
    return (void *)((uint8_t *)best_fit + HDR_OVERHEAD);
}

void hb_os_free(void *p)
{
    if (!p)
        return;
    arena_hdr_t *hdr = (arena_hdr_t *)((uint8_t *)p - HDR_OVERHEAD);
    if (hdr->magic != HDR_MAGIC)
        return;
    hdr->magic = HDR_MAGIC | HDR_FREE;
    s_used -= hdr->total;

    /* Absorb physically-adjacent free blocks. The free list isn't address
     * sorted, so walk it; each absorbed block is removed from the list.
     * hdr is not IN the list during the walk (it is pushed at the end), so
     * cur != hdr always. */
    arena_hdr_t *cur, **link;
    int pass;
    for (pass = 0; pass < 2; pass++) {
        link = &s_free;
        while (*link) {
            cur = *link;
            uint8_t *cur_end = (uint8_t *)cur + cur->total;
            uint8_t *hdr_end = (uint8_t *)hdr + hdr->total;
            if (cur_end == (uint8_t *)hdr) {          /* cur directly before hdr */
                cur->total += hdr->total;              /* merged block starts at cur */
                *link = cur->next_free;                /* remove cur */
                hdr = cur;                             /* re-anchor merged block */
                continue;
            }
            if (hdr_end == (uint8_t *)cur) {          /* cur directly after hdr */
                hdr->total += cur->total;
                *link = cur->next_free;                /* remove cur */
                continue;
            }
            link = &cur->next_free;
        }
    }
    hdr->next_free = s_free;
    s_free = hdr;
}

uint32_t hb_os_heap_free(void)
{
    arena_init();
    return ARENA_BYTES - s_used;
}

uint32_t hb_os_heap_largest(void)
{
    arena_hdr_t *cur;
    uint32_t best = 0;
    arena_init();
    for (cur = s_free; cur; cur = cur->next_free)
        if (cur->total > best)
            best = cur->total;
    return best > HDR_OVERHEAD ? best - HDR_OVERHEAD : 0;
}
