/* NanoApps SDK adapter for Geometry Dash port */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "../src/platform.h"
#include "../src/game.h"
#include "../src/gfx.h"

extern void gd_draw_full(void);

#define DATA_DIR ""

uint64_t plat_time_us(void) {
    return 0; // Hardware timer implementation needed
}

uint32_t plat_seed(void) {
    return 12345;
}

void *plat_read_file(const char *name, uint32_t *size, int save) {
    (void)save;
    if (size) *size = 0;
    return NULL;
}

int plat_write_file(const char *name, const void *data, uint32_t size) {
    (void)data; (void)name; (void)size;
    return -1;
}

void plat_log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

void plat_sound(int sound, float gain) {
    (void)sound; (void)gain;
}

void fb_nano_frame(int w, int h, uint32_t frame) {
    (void)w; (void)h; (void)frame;

    static int initialized = 0;
    if (!initialized) {
        gfx_init();
        gd_start_game();
        initialized = 1;
    }

    // Get input from SDK
    // For now, we simulate a touch if the SDK reports any touch
    G.input_pressed = true; // Replace with actual SDK touch check

    gd_update();
    gd_draw_full();

    gfx_shutdown();
}

void abort(void) {
    while(1); 
}

void exit(int status) {
    (void)status;
    while(1);
}
