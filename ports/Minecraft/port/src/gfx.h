#ifndef GFX_H
#define GFX_H

void gfx_init(void);
void gfx_shutdown(void);
void gfx_loop(void);
void gfx_clear(unsigned int color);
void gfx_set_title(const char *title);

#endif