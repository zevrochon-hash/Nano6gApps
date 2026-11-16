#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

void gfx_set_frame_offset(float x, float y);
void gfx_forget_all(void);
void sprites_forget(uint32_t seed);
uint32_t plat_seed(void);
void plat_log(const char *format, ...);
void *plat_read_file(const char *path, int *size, int default_size);
void plat_write_file(const char *path, const void *data, int size);
int game_wants_exit(void);
void game_debug_dump(const char *names);
int game_init(float panel_w, float panel_h);
void game_frame(float dt, const void *touches);
void game_redraw(void);
void game_gl_lost(void);
unsigned int game_bg_color(void);

#endif