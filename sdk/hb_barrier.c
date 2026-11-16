/*
 * hb_barrier.c — ARMv6K implementations of the nano 7 SDK's ARMv7 barriers.
 * See hb_barrier.h for why these are separate ARM-mode functions.
 */
#include "hb_barrier.h"

/* Legacy ARMv6 CP15 barriers.  r0 is scratch (the CP15 barrier operations
 * ignore the register value; 0 is the conventional choice). */
__attribute__((target("arm"), noinline))
void hb_dsb(void)
{
    __asm__ volatile("mcr p15, 0, r0, c7, c10, 4" ::: "r0", "memory");
}

__attribute__((target("arm"), noinline))
void hb_dmb(void)
{
    __asm__ volatile("mcr p15, 0, r0, c7, c10, 5" ::: "r0", "memory");
}

__attribute__((target("arm"), noinline))
uint32_t hb_read_cpsr(void)
{
    uint32_t v;
    __asm__ volatile("mrs %0, cpsr" : "=r"(v));
    return v;
}
