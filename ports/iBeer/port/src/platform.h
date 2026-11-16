#ifndef IBEER_PLATFORM_H
#define IBEER_PLATFORM_H
#include <stdint.h>

/* Compiled out on the host harness; the nano layer reports checkpoints. */
#define port_checkpoint(stage, detail, value) ((void)0)

uint64_t plat_time_us(void);
void *plat_read_file(const char *path, uint32_t *size, int save);
int plat_write_file(const char *path, const void *data, uint32_t size);
void plat_log(const char *fmt, ...);
void plat_log_flush(void);

/* 3-axis accelerometer in milli-g (1 g = 1000). Returns 0 when unavailable. */
int plat_accel(int32_t out[3]);

enum {
    IB_SND_POUR,      /* liquid draining */
    IB_SND_BURP,      /* shake */
    IB_SND_FIZZ,      /* bubbles / soda */
    IB_SND_BREAK,     /* glass tap */
    IB_SND_CLICK,     /* UI */
    IB_SND_BLIP,      /* UI 2 */
    IB_SND_COUNT
};
void plat_sound(int sound, float gain);

uint32_t plat_seed(void);

/* Touches in the game's logical canvas. */
typedef struct { int count; struct { float x, y; } pt[2]; } PlatTouches;

int  game_init(float w, float h);
void game_frame(float dt, const PlatTouches *touches);
void game_redraw(void);
void game_gl_lost(void);
uint32_t game_bg_color(void);
int  game_wants_exit(void);
#endif
