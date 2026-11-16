#ifndef GD_GAME_H
#define GD_GAME_H
#include <stdint.h>

#define GD_MAX_LEVELS 8
#define GD_MAX_OBJECTS 3000
#define GD_SHARDS 18

/* One level object, exactly as levels.bin stores it (7 bytes on disk). */
typedef struct { uint8_t id; int16_t x, y; int16_t rot; } GDObject;

typedef struct {
    char name[24];
    char song[24];
    uint8_t bg[3], gr[3];
    int count;
    uint32_t offset;        /* byte offset of the object array in levels.bin */
    int length;             /* level length in pixels */
} GDLevel;

typedef struct {
    int state;              /* 0 menu, 1 playing, 2 dead, 3 complete, 4 paused */
    int level, mode;        /* mode: 0 cube, 1 ship */
    int attempt;
    uint8_t best[GD_MAX_LEVELS];
    uint16_t attempts[GD_MAX_LEVELS];
    float px, py, vy, prot; /* player centre position, vertical speed, rotation */
    float camx, camy;       /* camera offset; camy follows a climb, not a jump */
    float ceil_y;           /* this level's ceiling, from its tallest solid */
    int grounded;
    int gravity_sign;
    float panel_w, panel_h;
    float state_t;          /* time in the current state */
    int won;
    /* practice mode */
    int practice;
    /* settings menu */
    int music_on, sound_on;
    int settings_row;     /* which settings row a tap landed on */
    float cpx, cpy, cvy;    /* the last checkpoint */
    int cp_grav, cp_mode;
    int cp_set;
    /* pad / orb animations */
    int pad_idx, orb_idx;
    float pad_anim, orb_anim;
    float time;             /* global clock for the animations */
    float music_t;          /* when the music last started, to avoid a hot restart */
    /* death explosion: the shards and the brief white flash */
    struct { float x, y, vx, vy, life; uint32_t color; } shard[GD_SHARDS];
    int nshards;
    float flash;            /* brief white flash on death */
    GDLevel levels[GD_MAX_LEVELS];
    GDObject *objects;      /* the current level's objects */
    int nobjects, nlevels;
    uint8_t *level_data;    /* the whole levels.bin, kept for level switches */
    /* sprites */
    int spr_cube, spr_cube2, spr_cubeface, spr_ship, spr_square[7], spr_spike[2], spr_dspike[4], spr_pit[3];
    int spr_rod[3], spr_chain, spr_plank, spr_bump, spr_ring, spr_portal[4], spr_portal_back[4];
    int spr_star, spr_floor, spr_shadow, spr_play, spr_play2, spr_ground, spr_spark;
    int spr_logo, spr_progress, spr_gui[4];
    int spr_square_glow[7], spr_spike_glow[2], spr_plank_glow, spr_bump_glow, spr_ring_glow;
    int bg_tex;             /* the original's background gradient, tinted per level */
    uint32_t frame;
} GDGame;

extern GDGame G;

int game_init(float w, float h);
void game_frame(float dt, const PlatTouches *touch);
void game_redraw(void);
void game_gl_lost(void);
uint32_t game_bg_color(void);
int game_wants_exit(void);
#endif
