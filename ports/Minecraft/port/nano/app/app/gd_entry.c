/* Geometry Dash nano app SDK entry point */
#include <stdint.h>
#include <string.h>
#include "hb_sdk.h"
#include "hb_shared.h"
#include "hb_heap.h"

#include "../../../../ios_app_path.h"

/* Extern declarations for our port functions */
extern void fb_nano_frame(int w, int h, uint32_t frame);
extern void gfx_init(void);
extern void gd_start_game(void);

/* Runtime state */
static uint32_t ran_before __attribute__((section(".noinit")));

/* Frame callback - called by the SDK */
static void frame_callback(int w, int h, uint32_t frame) {
    hb_fpu_status_t status;
    if (hb_fpu_attach_current_task(&status) < 0) return;
    fb_nano_frame(w, h, frame);
}

/* SDK entry point */
__attribute__((section(".text.entry"), used, noinline))
void *payload_entry(int op, void *fb, int w, int h, hb_shared_t *sh) {
    extern uint32_t __bss_start__[], __bss_end__[];
    (void)fb; (void)w; (void)h;
    
    if (op != 0 || !sh || sh->magic != HB_SHARED_MAGIC || sh->version != HB_SHARED_VERSION)
        return 0;
    
    if (ran_before) {
        /* Restore BSS on relaunch - simplified */
        for (uint32_t *p = __bss_start__; p < __bss_end__; p++)
            *p = 0;
    } else {
        ran_before = 1;
        for (uint32_t *p = __bss_start__; p < __bss_end__; p++)
            *p = 0;
    }
    
    hb_shared = sh;
    return (void *)frame_callback;
}
