/* Minimal NanoApps SDK adapter for Geometry Dash port */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "../src/platform.h"
#include "../src/game.h"
#include "../src/gfx.h"

#define DATA_DIR ""

/* Platform time */
static uint64_t last_time = 0;

uint64_t plat_time_us(void) {
    /* Simple time - in real implementation would use hardware timer */
    return 0;
}

uint32_t plat_seed(void) {
    /* Seed random number generator */
    return 12345;
}

void *plat_read_file(const char *name, uint32_t *size, int save) {
    (void)save;
    /* Minimal implementation - return NULL */
    if (size) *size = 0;
    return NULL;
}

int plat_write_file(const char *name, const void *data, uint32_t size) {
    (void)data;
    /* Minimal implementation - return -1 (fail) */
    (void)name;
    (void)size;
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
    (void)sound;
    (void)gain;
    /* No sound support */
}

void port_crumb(const char *tag, uint32_t a, uint32_t b) {
    (void)tag;
    (void)a;
    (void)b;
}

/* Keep awake - minimal implementation */
static uint64_t last_keep_awake = 0;

void keep_awake(void) {
    /* Do nothing */
}

void fb_nano_frame(int w, int h, uint32_t frame) {
    /* Minimal frame function */
    (void)w;
    (void)h;
    (void)frame;
    
    /* Initialize once */
    static int initialized = 0;
    if (!initialized) {
        gfx_init();
        gd_start_game();
        initialized = 1;
    }
    
    /* Update and draw */
    gd_update();
    gd_draw();
    
    /* End frame */
    gfx_shutdown();
    (void)frame;
}