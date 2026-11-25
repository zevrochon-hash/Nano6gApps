/*
 * n6g_addrs_external.h — REFERENCE SNAPSHOT ONLY. NOT BUILD INPUT.
 *
 * This is a third-party 6G address map received from an outside contributor.
 * It is kept verbatim for reference because several entries are useful, but
 * parts of it contradict constants this tree has verified live on hardware.
 *
 * Read docs/N6G_ADDRESS_MAP.md before trusting any value here.
 *
 * Known problems (see that document for details):
 *   - N6G_RESIDENT_LOAD_ADDR equals N6G_FB_BASE, but the comment claims it is
 *     "above FB end". The framebuffer spans 0x089c0000..0x089dc1ff, so a load
 *     address of 0x089c0000 would land on top of the framebuffer.
 *   - The flag/scratch offsets (+0x5c00..+0x5ffc) fall inside the 64 KB stack
 *     region that grows down from +0x10000, so they can be clobbered.
 *   - N6G_ADDR_GETRESOURCE (0x08408be0) does not match the entry this tree
 *     patches and has verified (0x08409e86, prologue 0xb5f8).
 *   - "No patchable home-controller vtable" contradicts live-verified hooks at
 *     0x086c9658 (+0x40, +0x184, +0x18c).
 */

#ifndef N6G_ADDRS_EXTERNAL_REFERENCE_ONLY
#error "docs/n6g_addrs_external.h is a reference snapshot, not build input. See docs/N6G_ADDRESS_MAP.md."
#endif

#pragma once
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* FRAMEBUFFER                                                         */
/* ------------------------------------------------------------------ */
/* [CONFIRMED-LIVE] Framebuffer at 0x089c0000.
 * Pixel data confirmed at 0x089c0200+ from live scan.
 * 240x240 RGB565 = 0x1C200 bytes, ends at 0x089dc200. */
#define N6G_FB_BASE     0x089c0000u
#define N6G_FB_W        240
#define N6G_FB_H        240
#define N6G_FB_BPP      2               /* RGB565 */
#define N6G_FB_SIZE     (N6G_FB_W * N6G_FB_H * N6G_FB_BPP)  /* 0x1C200 */
#define N6G_FB_STRIDE   (N6G_FB_W * N6G_FB_BPP)             /* 480 bytes */

/* LCD controller peripheral base (S5L8723).
 * [CONFIRMED-STATIC] 0x38500000 has 2 code refs in OSOS display driver.
 * DO NOT read this via SCSI -- unmapped peripheral regs cause data abort. */
#define N6G_LCD_CTRL_BASE   0x38500000u

/* ------------------------------------------------------------------ */
/* RESIDENT LOAD ADDRESS + SCRATCH REGIONS                            */
/* ------------------------------------------------------------------ */
/* [CONFIRMED-LIVE] 0x089c0000 reads all-zeros. Sits safely above FB end. */
#define N6G_RESIDENT_LOAD_ADDR      0x089c0000u

#define N6G_FLAG_ADDR               (N6G_RESIDENT_LOAD_ADDR + 0x5ffcu)
#define N6G_SCREENSHOT_FLAG_ADDR    (N6G_RESIDENT_LOAD_ADDR + 0x5ff0u)
#define N6G_STATUSBAR_FLAG_ADDR     (N6G_RESIDENT_LOAD_ADDR + 0x5fecu)
#define N6G_TOUCH_MB_ADDR           (N6G_RESIDENT_LOAD_ADDR + 0x5f40u)
#define N6G_TIMER_ADDR              (N6G_RESIDENT_LOAD_ADDR + 0x5c00u)
#define N6G_APP_STACK_TOP           (N6G_RESIDENT_LOAD_ADDR + 0x10000u) /* 64KB stack */
#define N6G_APP_HEAP_BASE           (N6G_RESIDENT_LOAD_ADDR + 0x10000u)
#define N6G_APP_HEAP_SIZE           0x30000u  /* 192KB for app heap */
#define N6G_GETRES_TRAMP_ADDR       (N6G_RESIDENT_LOAD_ADDR + 0x7200u)
#define N6G_APP_PACK_ADDR           (N6G_RESIDENT_LOAD_ADDR + 0x40000u)

/* ------------------------------------------------------------------ */
/* TOUCH SYSTEM                                                        */
/* ------------------------------------------------------------------ */
/* [CONFIRMED-LIVE] with finger on screen during dump. */
#define N6G_TOUCH_LIST_HEAD         0x08800118u
#define N6G_TOUCH_COUNT_OFF         0x04u   /* *(head+4) = num active touches */
#define N6G_TOUCH_X_OFF             0x3cu   /* node+0x3c = x (>>4 for pixels) */
#define N6G_TOUCH_Y_OFF             0x48u   /* node+0x48 = y (>>4 for pixels) */
#define N6G_TOUCH_NODE_PTR_OFF      0x60u   /* head+0x60 = ptr to first node */

/* Touch coord decode: pixel = fixed16 >> 4 */
#define N6G_TOUCH_TO_PX(v)          ((v) >> 4)

/* ------------------------------------------------------------------ */
/* RESOURCE DATABASE                                                   */
/* ------------------------------------------------------------------ */
/* [CONFIRMED-LIVE] First word matches PUSH {r4-r11,lr} = 0xe92d5ff0. */
#define N6G_ADDR_GETRESOURCE        0x08408be0u
#define N6G_GETRES_PROLOGUE0        0x5ff0u  /* low halfword of first ARM instr */
#define N6G_OUR_ID_BASE             0x0dad8000u

/* ------------------------------------------------------------------ */
/* TTouchMgr (informational only)                                     */
/* ------------------------------------------------------------------ */
/* [CONFIRMED-LIVE] but N6G vtable slots are sub-vtables, not fn ptrs.
 * Direct slot-hooking NOT applicable. Use TOUCH_LIST_HEAD polling instead. */
#define N6G_TOUCH_MGR_VTABLE        0x086855bcu  /* informational only */

/* ------------------------------------------------------------------ */
/* NOT APPLICABLE ON N6G (documented for porting reference)           */
/* ------------------------------------------------------------------ */
/* N6G uses encrypted ObjC-style dispatch (0x0dad0000 region).
 * There is no patchable C++ vtable for the home controller.
 * App launch is done via SCSI channel instead. */
#define N6G_HOME_VTABLE             0x00000000u  /* N/A on N6G */
#define N6G_HOME_DISPATCH_ORIG      0x00000000u  /* N/A on N6G */
#define N6G_ADDR_POSTTOUCH          0x00000000u  /* N/A -- use TOUCH_LIST_HEAD */

/* operator new/delete: [TODO] scan 0x084b9d00 area with Thumb disasm */
#define N6G_ADDR_OP_NEW             0x00000000u  /* TODO */
#define N6G_ADDR_OP_DELETE          0x00000000u  /* TODO */
