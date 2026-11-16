/*
 * doom.h — compact Freedoom engine for the iPod nano 7G.
 *
 * Everything is integer / fixed-point: the build uses the soft-float ABI and
 * we do not want libm or __aeabi_fdiv in the per-pixel path.
 *
 * The logical screen is landscape 432x240.  The panel is portrait 240x432, so
 * a finished frame is rotated onto it (cx,cy) -> (239-cy, cx).
 */
#ifndef DOOM_H
#define DOOM_H

#include <stdint.h>

#define GW          432
#define GH          240
#define VIEW_H      197          /* 3D view rows; status bar below       */
#define HUD_Y       197
#define HUD_H       43
#define HORIZON     (VIEW_H / 2) /* 98                                    */
#define FOCAL       216          /* GW/2 -> ~90 degree horizontal FOV      */
#define CX          (GW / 2)

#define MAXV        2048
#define MAXL        1600
#define MAXSEC      256
#define MAXIDX      4096
#define MAXTH       400
#define MAXTEX      192
#define MAXFLAT     96
#define MAXSPR      320
#define MAXGFX      160
#define MAXMOBJ     400
#define MAXPRJ      40
#define MAXDOOR     32
#define PLANE_CACHE 24

#define PACK_MAGIC  0x314d444eu  /* "NDM1" little-endian */

/* ------------------------------------------------------------------ pack */

typedef struct {
    char     tag[4];
    uint32_t off, size;
} __attribute__((packed)) pak_ent_t;

typedef struct {
    char     name[8];
    uint16_t w, h;
    uint32_t off, pad;
} __attribute__((packed)) tex_ent_t;

typedef struct {
    char     name[8];
    uint32_t off;
} __attribute__((packed)) flat_ent_t;

typedef struct {
    char     name[8];
    uint16_t w, h;
    int16_t  left, top;
    uint32_t off, pad;
} __attribute__((packed)) pic_ent_t;

/* --------------------------------------------------------------- map data */

typedef struct {
    uint16_t v1, v2, flags, special, tag, fsec, bsec;
    uint16_t ftop, fmid, fbot, btop, bmid, bbot;
    int16_t  fxoff, bxoff;
    int16_t  len;
} line_t;

typedef struct {
    int16_t  floor_h, ceil_h;
    uint16_t floorpic, ceilpic, light;
    int16_t  special, tag;
    uint16_t line_start, line_count;
} sector_t;

typedef struct {
    int16_t  x, y;
    uint16_t angle, type;
    int16_t  flags;
    uint16_t sector, baseid;
} mthing_t;

typedef struct {
    uint16_t kind, hp;
    uint8_t  letter, radius, height, speed;
} tmeta_t;

typedef struct vertex_s { int16_t x, y; } vertex_t;

/* ------------------------------------------------------------- art tables */

typedef struct {
    char     name[8];
    uint16_t w, h;
    uint32_t off;
    const uint8_t *pix;
} tex_t;

typedef struct {
    char     name[8];
    uint32_t off;
    const uint8_t *pix;
} flat_t;

typedef struct {
    char     name[8];
    uint16_t w, h;
    int16_t  left, top;
    uint32_t off;
    const uint8_t *pix;
} pic_t;

/* -------------------------------------------------------------- runtime */

typedef struct {
    int32_t  x, y;           /* 16.16 world position                       */
    int32_t  z;              /* floor height << 16                         */
    int16_t  angle;
    uint8_t  kind;           /* 0 deco, 1 monster, 2 item, 4 key           */
    uint8_t  baseid;
    uint8_t  sector;
    uint8_t  state;          /* 0 idle 1 chase 2 attack 3 pain 4 dead      */
    uint8_t  active;
    uint8_t  removed;
    uint8_t  death_letter;
    uint8_t  radius, height, speed;
    int16_t  hp, maxhp;
    int16_t  timer;
    int16_t  anim;
    int16_t  type;
    uint8_t  was_dead;
} mobj_t;

typedef struct {
    int32_t x, y, z;         /* 16.16                                      */
    int32_t vx, vy;          /* 16.16 units/sec                            */
    int32_t vz;
    int16_t life;
    int16_t damage;
    uint8_t active;
    uint8_t kind;            /* 0 = imp fireball, 1 = caco fireball        */
    uint8_t sector;
} prj_t;

typedef struct {
    uint16_t sector;
    int16_t  open_h;
    int16_t  state;          /* 0 closed 1 opening 2 open                  */
    int16_t  key;
} door_t;

typedef struct {
    int down;
    int x, y;                /* current finger, canvas coords              */
    int press_x, press_y;
    int dx, dy;              /* movement since previous frame              */
    int tap;                 /* released this frame with little movement    */
    int tap_x, tap_y;
    int btn_fire;            /* edge: play/pause or vol+                   */
    int btn_use;             /* edge: vol down                             */
} input_t;

typedef struct {
    int32_t  x, y;           /* 16.16                                      */
    int16_t  angle;
    int16_t  sector;
    int16_t  floor_z;
    int16_t  health, armor;
    int16_t  shells, bullets;
    uint8_t  keys;
    uint8_t  weapon;         /* 0 pistol, 1 shotgun                        */
    uint8_t  dead;
    int16_t  fire_cd;
    int16_t  fire_anim;
    int16_t  bob;
    int16_t  damage_flash;
    int16_t  face_timer;
} player_t;

/* ----------------------------------------------------------------- globals */

extern uint32_t *g_cv;               /* 432x240 canvas                     */
extern uint16_t *g_depth;            /* 432*VIEW_H wall depth              */
extern const uint8_t *g_pak;
extern uint32_t g_pak_size;
extern uint32_t g_pal[256];
extern uint32_t g_shade[32][256];
extern int32_t  g_sin[1024];
extern int32_t  g_cos[1024];
extern int16_t  g_atan[257];

extern tex_t   g_tex[MAXTEX];    extern int g_ntex;
extern flat_t  g_flat[MAXFLAT];  extern int g_nflat;
extern pic_t   g_spr[MAXSPR];    extern int g_nspr;
extern pic_t   g_gfx[MAXGFX];    extern int g_ngfx;

extern vertex_t g_vert[MAXV];    extern int g_nvert;
extern line_t   g_line[MAXL];    extern int g_nline;
extern sector_t g_sec[MAXSEC];   extern int g_nsec;
extern sector_t g_sec0[MAXSEC];
extern uint16_t g_lidx[MAXIDX];  extern int g_nidx;
extern mthing_t g_thing[MAXTH];  extern int g_nthing;
extern tmeta_t  g_meta[MAXTH];
extern int16_t  g_line_other[MAXL];  /* other sector for two-sided lines   */
extern int16_t  g_line_len[MAXL];
extern int16_t  g_line_door[MAXL];

extern char    g_sprbase[64][5];
extern int     g_nbase;
extern int16_t g_sprmap[64][26][9];
extern int     g_pisg[2], g_shtg[2];
extern int     g_pisf, g_shtf, g_puff, g_puff_b;
extern int     g_poss_base, g_sarg_base;
extern int     g_bal1[2], g_bal7[2];
extern int     g_face[8], g_keyspr[6];
extern int     g_num[10], g_ysnum[10];
extern int     g_stbar, g_titlepic, g_mdoom;
extern int     g_font[64];

extern mobj_t   g_mobj[MAXMOBJ]; extern int g_nmobj;
extern prj_t    g_prj[MAXPRJ];
extern door_t   g_door[MAXDOOR]; extern int g_ndoor;
extern player_t g_player;
extern int      g_tilt;
extern void (*g_accel_fn)(int32_t out[3]);

extern int g_state;               /* see ST_*                           */
extern int g_menu_sel;
extern uint32_t g_now_ms;
extern uint32_t g_state_ms;
extern uint32_t g_game_ms;
extern int g_demo;
extern int g_demo_tips;
extern char g_message[64];
extern uint32_t g_message_ms;
extern int g_exit_line;

extern int g_sky_flat;
extern int g_frame;

enum {
    ST_TITLE = 0,
    ST_MENU,
    ST_CONTROLS,
    ST_ABOUT,
    ST_GAME,
    ST_PAUSE,
    ST_DEAD,
    ST_WIN
};

enum {
    SFX_PISTOL = 0, SFX_SHOTGUN, SFX_DOOR, SFX_ITEM, SFX_WEAPON,
    SFX_PAIN, SFX_DEATH, SFX_OOF, SFX_SWITCH, SFX_POSSIT, SFX_POSDTH,
    SFX_BGSIT, SFX_BGDTH, SFX_SARGSIT, SFX_SARGDTH, SFX_FIRXPL,
    SFX_BAREXP, SFX_NOWAY, SFX_PLROOF, SFX_FIRSHT, SFX_CLAW,
    SFX_POSACT, SFX_SGTSIT, SFX_COUNT
};

/* engine.c */
int  doom_load_pack(const uint8_t *pak, uint32_t size);
void doom_frame(const input_t *in);
void doom_start_game(int demo);
void doom_present(uint32_t *fb);
void doom_quit_to_menu(void);
void doom_sound(int id);

/* render.c */
void r_draw_world(void);
void r_draw_sprites(void);
void r_draw_weapon(void);
void r_clear(void);

/* ui.c */
void ui_draw_hud(void);
void ui_draw_title(void);
void ui_draw_menu(void);
void ui_draw_controls(void);
void ui_draw_about(void);
void ui_draw_pause(void);
void ui_draw_dead(void);
void ui_draw_win(void);
void ui_draw_demo_tips(void);
void ui_text(int x, int y, const char *s, int scale, uint32_t rgb);
int  ui_text_width(const char *s, int scale);
void ui_pic(const pic_t *p, int x, int y, int scale, uint32_t tint);
void ui_flat_bg(void);

/* helper math */
static inline int32_t dsin(int a) { return g_sin[(a >> 6) & 1023]; }
static inline int32_t dcos(int a) { return g_cos[(a >> 6) & 1023]; }
int atan2_16(int y, int x);
int isqrt32(uint32_t v);
int rnd(void);

#endif
