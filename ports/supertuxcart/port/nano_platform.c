/* Nano frame adapter for the landscape SuperTuxKart mini-port. */
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include "hb_sdk.h"
#include "platform.h"
#include "game.h"
#include "gfx.h"

uint64_t plat_time_us(void) { return hb_time_uptime_us(); }
uint32_t plat_seed(void) { return (uint32_t)hb_time_uptime_us(); }
void *plat_read_file(const char *path, uint32_t *size, int save) { (void)path; (void)save; if (size) *size = 0; return 0; }
int plat_write_file(const char *path, const void *data, uint32_t size) { (void)path; (void)data; (void)size; return -1; }
void plat_log(const char *format, ...) { va_list args; va_start(args, format); vprintf(format, args); va_end(args); putchar('\n'); }
void plat_sound(int sound, float gain) { (void)sound; (void)gain; }
void port_crumb(const char *tag, uint32_t a, uint32_t b) { hb_trace_log(tag, a, b); }

void stc_nano_frame(int w, int h, uint32_t frame)
{
    /* Portrait panel, landscape game. gfx rotates the 320x240 canvas into it. */
    gfx_begin_frame(w, h, 320.f, 240.f, 1, .05f, .12f, .07f);
    stc_frame(w, h, frame);
    stc_draw();
    gfx_end_frame();
}
