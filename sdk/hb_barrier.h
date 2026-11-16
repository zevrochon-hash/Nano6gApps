/*
 * hb_barrier.h — memory barriers + CPSR access for the iPod nano 6 (S5L8723,
 * ARM1176JZF-S, ARMv6K).
 *
 * The nano 7 SDK uses ARMv7/Thumb-2 instructions (`dsb sy`, `dmb sy`,
 * `mrs Rd, cpsr`) which the ARM1176 does not have.  ARMv6K provides the
 * legacy CP15 barrier operations instead, but they are ARM-mode only, so
 * each helper is compiled as an ARM function with target("arm") and called
 * through the normal Thumb/ARM interworking (the compiler emits blx).
 *
 * Do NOT use `dsb sy`/`dmb sy`/`isb sy` in nano 6 code: the assembler
 * rejects them for -mcpu=arm1176jzf-s in Thumb mode.
 */
#ifndef HB_BARRIER_H
#define HB_BARRIER_H

#include <stdint.h>

/* Data synchronisation barrier: all prior memory accesses complete first. */
void hb_dsb(void);

/* Data memory barrier: ordering only, no completion wait. */
void hb_dmb(void);

/* Read CPSR (ARM mode; needed by the lazy-VFP ownership check). */
uint32_t hb_read_cpsr(void);

#endif /* HB_BARRIER_H */
