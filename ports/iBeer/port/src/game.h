#ifndef IBEER_GAME_H
#define IBEER_GAME_H
#include <stdint.h>

#define IB_MAX_DRINKS 18
#define IB_MAX_BUBBLES 48
#define IB_MAX_DRIPS 24
#define IB_MAX_STEAM 12

typedef struct {
    const char *name;
    uint8_t rgb[3];      /* liquid colour */
    uint8_t foam[3];     /* foam / head colour */
    uint8_t fizz;        /* bubble rate 0..1 */
    uint8_t hot;         /* draws steam */
} IBDrink;

typedef struct {
    float x, y, r, vy, vx, life;
} IBParticle;

typedef struct {
    float x, y, vy, len, life;
} IBDrip;

typedef struct {
    int state;            /* 0 play, 1 menu, 2 help */
    float state_t;
    int drink;
    /* liquid */
    float level;          /* 0..1 of the glass */
    float tilt;           /* surface angle, radians */
    float tilt_v;
    float tilt_target;
    float slosh;          /* surface wave offset */
    float slosh_v;
    float pour;           /* current pour rate (for sound/visual) */
    int pouring;
    float surface_y;      /* cached surface height at the glass centre */
    /* effects */
    float foam_t;
    int foam_frame;
    float fizz_t;
    float shake;          /* smoothed shake energy */
    float burp_cd;
    float splash;         /* touch splash energy */
    float splash_x;
    float condensation;   /* 0..1, grows with pouring */
    float shatter;        /* >0: shattered-glass overlay countdown */
    int cracked;          /* the glass has broken (once per fill) */
    float foam_boost;     /* 0..1 the head froths up when shaken */
    float foam_cd;        /* fizz sound cooldown */
    float time;
    /* accelerometer */
    float ax, ay, az;
    float pax, pay, paz;
    int accel_ok;
    /* particles */
    IBParticle bubble[IB_MAX_BUBBLES];
    IBParticle steam[IB_MAX_STEAM];
    IBDrip drip[IB_MAX_DRIPS];
    int nbubble, ndrip, nsteam;
    /* layout */
    float panel_w, panel_h;
    /* sprites */
    int spr_foam[16], spr_soda[8], spr_bubble, spr_steam, spr_cond, spr_liquid;
    int spr_halo, spr_halo_m, spr_halo_s, spr_glow, spr_hotglow, spr_dirty, spr_white;
    int spr_btn_menu, spr_btn_done, spr_btn_ok, spr_btn_refill;
    int spr_egg;
    int spr_drink[IB_MAX_DRINKS];
    int foam_count, soda_count;
    int bg_tex, font_ok;
    int cond_px, cond_py, cond_pw, cond_ph;   /* frost texture piece in its page */
    int liq_px, liq_py, liq_pw, liq_ph;       /* bubble texture piece in its page */
    uint32_t frame;
    /* save */
    int dirty;
} IBGame;

extern IBGame G;

int  game_init(float w, float h);
void game_frame(float dt, const PlatTouches *touch);
void game_redraw(void);
void game_gl_lost(void);
uint32_t game_bg_color(void);
int game_wants_exit(void);
#endif
