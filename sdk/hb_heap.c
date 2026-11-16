/*
 * hb_heap.c — OS main-heap interface, nano 6G (S5L8723) port.
 *
 * The 6G exposes its OS allocator as a Thumb entry point at 0x08178df4,
 * with a flags-taking free at 0x0817d93cu. We bind to them directly here
 * instead of carrying a static arena in .bss — that arena inflated every
 * image's .bss by megabytes and broke the resident's 16-bit immediates
 * in compact stages like disk_bootstrap.
 */

#include "hb_heap.h"
#include <stdint.h>
#include <stddef.h>

typedef void *(*hb6_os_alloc_t)(uint32_t size);
typedef void  (*hb6_os_free_t)(void *p, uint32_t flags);
#define ADDR_OS_ALLOC     0x08178df4u   /* void *alloc(uint32_t size) */
#define ADDR_OS_FREE      0x0817d93cu   /* void  free(void *p, uint32_t flags) */

void *hb_os_alloc(uint32_t size)
{
    if (!size)
        return NULL;
    return ((hb6_os_alloc_t)(uintptr_t)(ADDR_OS_ALLOC | 1u))(size);
}

void hb_os_free(void *p)
{
    if (!p)
        return;
    ((hb6_os_free_t)(uintptr_t)(ADDR_OS_FREE | 1u))(p, 3);
}

/* These two report arena occupancy only; the 6G doesn't have a verified
 * heap-size query, so report 0/large-enough values callers tolerate.
 * hb_reloc_file logs RELARENA with these but never gates on them. */
uint32_t hb_os_heap_free(void)    { return 0u; }
uint32_t hb_os_heap_largest(void) { return 0u; }
