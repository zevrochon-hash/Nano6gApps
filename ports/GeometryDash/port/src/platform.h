#ifndef GD_PLATFORM_H
#define GD_PLATFORM_H
#include <stdint.h>
#define port_checkpoint(stage,detail,value) ((void)0)
uint64_t plat_time_us(void);
void *plat_read_file(const char *path, uint32_t *size, int save);
int plat_write_file(const char *path, const void *data, uint32_t size);
void plat_log(const char *fmt, ...);
void plat_log_flush(void);      /* write the log ring to the device now */
/* Effects: the original's click / jump / death / completion sounds. */
enum { GD_SND_CLICK, GD_SND_JUMP, GD_SND_DIE, GD_SND_WIN, GD_SND_COUNT };
void plat_sound(int sound, float gain);
/* Music: start/stop the level track by file name (NULL stops). */
int  plat_music_play(const char *name);
void plat_music_stop(void);
uint32_t plat_seed(void);

/* Touches in the game's logical canvas. */
typedef struct { int count; struct { float x, y; } pt[2]; } PlatTouches;
int game_init(float w, float h);
void game_frame(float dt, const PlatTouches *touches);
void game_redraw(void);          /* draw the last step again (call instead of game_frame) */
void game_gl_lost(void);
uint32_t game_bg_color(void);
int game_wants_exit(void);
#endif
