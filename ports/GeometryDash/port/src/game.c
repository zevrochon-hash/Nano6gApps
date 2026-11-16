/* Geometry Dash 1.0: the real levels, art and music from the original iOS app.
 *
 * The level data is the original's own: objects with their id, position and
 * rotation, extracted from the armv7 executable (see tools/convert_levels.py).
 * Coordinates are the original's pixels; y is the sprite centre and the ground
 * top is y=0. The game runs in a fixed 480x270 logical canvas that the platform
 * rotates onto the nano's portrait panel.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "platform.h"
#include "gfx.h"
#include "sprites.h"
#include "game.h"

GDGame G;

/* logical canvas */
#define CW 480.0f
#define CH 270.0f
#define GROUND_SY 212.0f          /* screen y of the world's ground line */
#define MIN_CEIL_Y 240.0f         /* never make a level's sky shorter than this */
#define PLAYER_HALF 12.0f
/* GD's own numbers: 0.958 u/frame^2 gravity, 11.18 u/frame jump, 311.58 u/s speed,
 * 15 u/s terminal, at 30 px per unit and 60 Hz. */
#define GRAVITY 3450.0f
#define JUMP_V 671.0f
#define TERM_V 480.0f
#define SPEED 312.0f
#define SHIP_UP 1120.0f
#define SHIP_DN 850.0f
#define SHIP_MAX 330.0f
#define PAD_V 512.0f
#define ORB_V 352.0f

/* The original's palette: the player is the default green cube, everything that
 * is not a portal/pad/orb takes the level's object colour (its ground colour). */
#define PLAYER_RGB 0xFF00FF00u
#define GD_GREEN   0xFF7ADE2Du

enum { GD_MODE_CUBE, GD_MODE_SHIP, GD_MODE_BALL, GD_MODE_UFO };
enum { ST_MENU, ST_PLAY, ST_DEAD, ST_DONE, ST_PAUSED, ST_SETTINGS };

/* object ids that do something */
#define ID_BLOCK_FIRST 1
#define ID_BLOCK_LAST  7
#define ID_SPIKE1      8
#define ID_PIT         9
#define ID_PORTAL1     10
#define ID_ROD1        15
#define ID_DSPIKE1     18
#define ID_BUMP        35
#define ID_RING        36
#define ID_SPIKE2      39
#define ID_PLANK       40
#define ID_CHAIN       41

static GDObject objs[GD_MAX_OBJECTS];
static uint8_t used[GD_MAX_OBJECTS];   /* portals/orbs already fired this attempt */
static uint32_t obj_tint;              /* the level's object colour, cached once */
static int prev_touch, touch_press;
static float press_x, press_y;
static float prev_py;

/* ---- level data ------------------------------------------------------------------ */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int compare_objects_by_x(const void *a, const void *b)
{
    const GDObject *oa = (const GDObject *)a, *ob = (const GDObject *)b;
    if (oa->x != ob->x) return oa->x - ob->x;
    if (oa->y != ob->y) return oa->y - ob->y;
    return oa->id - ob->id;
}

/* The per-frame collision and draw loops only need a small window around the
 * player or the camera; keep the level sorted so that window is a binary
 * search instead of a 2,000+ item scan every frame. */
static int obj_lower_bound(float x)
{
    int lo = 0, hi = G.nobjects;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if ((float)objs[mid].x < x) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* Level objects use whole-degree rotations. Keep a table so a static saw or
 * chain does not call cosf/sinf every frame; the player's smooth angle still
 * uses the real trig functions. */
static float s_cos[360], s_sin[360];
static int s_trig_ready;
static void cos_sin_angle(float rot, float *c, float *s)
{
    if (rot == floorf(rot)) {
        long r = (long)lrintf(rot) % 360L;
        if (r < 0) r += 360L;
        if (!s_trig_ready) {
            for (int i = 0; i < 360; i++) {
                float rad = (float)i * 3.14159265f / 180.0f;
                s_cos[i] = cosf(rad);
                s_sin[i] = sinf(rad);
            }
            s_trig_ready = 1;
        }
        *c = s_cos[r];
        *s = s_sin[r];
        return;
    }
    float rad = rot * 3.14159265f / 180.0f;
    *c = cosf(rad);
    *s = sinf(rad);
}

static int load_level_data(void)
{
    uint32_t size = 0;
    uint8_t *data = plat_read_file("levels.bin", &size, 0);
    if (!data || size < 8 || memcmp(data, "GDLS", 4) || rd16(data + 4) != 2) {
        free(data);
        return -1;
    }
    int n = rd16(data + 6);
    if (n < 1 || n > GD_MAX_LEVELS) {
        free(data);
        return -1;
    }
    G.nlevels = n;
    G.level_data = data;
    uint32_t p = 8;
    for (int i = 0; i < n; i++) {
        int nl = data[p++];
        memcpy(G.levels[i].name, data + p, nl);
        G.levels[i].name[nl] = 0;
        p += nl;
        int sl = data[p++];
        memcpy(G.levels[i].song, data + p, sl);
        G.levels[i].song[sl] = 0;
        p += sl;
        memcpy(G.levels[i].bg, data + p, 3);
        p += 3;
        memcpy(G.levels[i].gr, data + p, 3);
        p += 3;
        G.levels[i].count = rd16(data + p);
        p += 2;
        G.levels[i].offset = p;
        p += (uint32_t)G.levels[i].count * 7;
        G.levels[i].length = 0;
    }
    return 0;
}

static uint8_t keep[GD_MAX_OBJECTS];

/* Hollow out fully enclosed solid-block rectangles, leaving the outer shell.
 * The filled black walls were hundreds of blocks the player could never see
 * the inside of; this keeps the silhouette but turns the interior into empty
 * space, which is both cheaper and closer to a "shell" wall. */
static int solid_block_at_sorted(const GDObject *arr, int n, int x, int y)
{
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if ((float)arr[mid].x < (float)x) lo = mid + 1;
        else hi = mid;
    }
    for (int i = lo; i < n && arr[i].x == x; i++) {
        uint8_t id = arr[i].id;
        if (arr[i].y == y && id >= ID_BLOCK_FIRST && id <= ID_BLOCK_LAST) return 1;
    }
    return 0;
}

static int hollow_solid_blocks(int n)
{
    for (int i = 0; i < n; i++) {
        uint8_t id = objs[i].id;
        keep[i] = 1;
        if (id < ID_BLOCK_FIRST || id > ID_BLOCK_LAST) continue;
        int x = objs[i].x, y = objs[i].y;
        if (solid_block_at_sorted(objs, n, x - 30, y) &&
            solid_block_at_sorted(objs, n, x + 30, y) &&
            solid_block_at_sorted(objs, n, x, y - 30) &&
            solid_block_at_sorted(objs, n, x, y + 30))
            keep[i] = 0;
    }
    int w = 0;
    for (int i = 0; i < n; i++) {
        if (keep[i]) {
            if (w != i) objs[w] = objs[i];
            w++;
        }
    }
    return w;
}

static void load_level_objects(int index)
{
    GDLevel *l = &G.levels[index];
    const uint8_t *p = G.level_data + l->offset;
    int n = l->count;
    if (n > GD_MAX_OBJECTS) n = GD_MAX_OBJECTS;
    for (int i = 0; i < n; i++) {
        objs[i].id = p[0];
        objs[i].x = (int16_t)rd16(p + 1);
        objs[i].y = (int16_t)rd16(p + 3);
        objs[i].rot = (int16_t)rd16(p + 5);
        p += 7;
    }
    qsort(objs, (size_t)n, sizeof objs[0], compare_objects_by_x);
    /* hollow out enclosed solid rectangles so the filled brick walls cost the
     * edge of the shell, not every block inside it */
    n = hollow_solid_blocks(n);
    G.nobjects = n;
    G.objects = objs;
    int maxx = 0;
    float max_top = 0.0f;
    for (int i = 0; i < n; i++) {
        if (objs[i].x > maxx) maxx = objs[i].x;
        uint8_t id = objs[i].id;
        if ((id >= ID_BLOCK_FIRST && id <= ID_BLOCK_LAST) || id == ID_PLANK) {
            float hh = 15.0f;
            if (id == ID_PLANK && objs[i].rot % 180 == 0) hh = 7.0f;
            float top = (float)objs[i].y + hh;
            if (top > max_top) max_top = top;
        }
    }
    l->length = maxx + 60;
    /* The camera can climb as high as the level actually goes. The old fixed
     * ceiling stopped the ship halfway up tall ship sections. */
    G.ceil_y = max_top + 30.0f;
    if (G.ceil_y < MIN_CEIL_Y) G.ceil_y = MIN_CEIL_Y;
    obj_tint = 0xFF000000u | ((uint32_t)l->gr[0] << 16) |
               ((uint32_t)l->gr[1] << 8) | (uint32_t)l->gr[2];
}

/* ---- sprites --------------------------------------------------------------------- */

static void bind_sprites(void)
{
    G.spr_cube = spr_find("player_cube");
    G.spr_cube2 = spr_find("player_cube2");
    G.spr_spark = G.spr_cube2;   /* the tiny white glint used at rod tips */
    G.spr_cubeface = spr_find("player_square");
    G.spr_ship = spr_find("ship");
    for (int i = 0; i < 7; i++) {
        char n[16];
        snprintf(n, sizeof n, "square%02d", i + 1);
        G.spr_square[i] = spr_find(n);
    }
    G.spr_spike[0] = spr_find("spike01");
    G.spr_spike[1] = spr_find("spike02");
    for (int i = 0; i < 4; i++) {
        char n[16];
        snprintf(n, sizeof n, "dspikes%02d", i + 1);
        G.spr_dspike[i] = spr_find(n);
    }
    G.spr_pit[0] = spr_find("pit01");
    for (int i = 0; i < 3; i++) {
        char n[16];
        snprintf(n, sizeof n, "rod%02d", i + 1);
        G.spr_rod[i] = spr_find(n);
    }
    G.spr_chain = spr_find("chain01");
    G.spr_plank = spr_find("plank01");
    G.spr_bump = spr_find("bump01");
    G.spr_ring = spr_find("ring01");
    for (int i = 0; i < 4; i++) {
        char n[16];
        snprintf(n, sizeof n, "portal%02d", i + 1);
        G.spr_portal[i] = spr_find(n);
        snprintf(n, sizeof n, "portal%02dback", i + 1);
        G.spr_portal_back[i] = spr_find(n);
    }
    G.spr_floor = spr_find("floorline");
    G.spr_shadow = spr_find("groundshadow");
    G.spr_play = spr_find("playbtn");
    G.spr_play2 = spr_find("playbtn2");
    G.spr_star = spr_find("star");
    G.spr_ground = spr_find("groundsquare");
    G.spr_logo = spr_find("gdlogo");
    G.spr_progress = spr_find("progressbar");
    G.spr_gui[0] = spr_find("gui_green");
    G.spr_gui[1] = spr_find("gui_cyan");
    G.spr_gui[2] = spr_find("gui_pink");
    G.spr_gui[3] = spr_find("gui_gray");
    for (int i = 0; i < 7; i++) {
        char n[20];
        snprintf(n, sizeof n, "square%02dglow", i + 1);
        G.spr_square_glow[i] = spr_find(n);
    }
    G.spr_spike_glow[0] = spr_find("spike01glow");
    G.spr_spike_glow[1] = spr_find("spike02glow");
    G.spr_plank_glow = spr_find("plank01glow");
    G.spr_bump_glow = spr_find("bump01glow");
    G.spr_ring_glow = spr_find("ring01glow");
}

static int object_sprite(uint8_t id)
{
    if (id >= ID_BLOCK_FIRST && id <= ID_BLOCK_LAST) return G.spr_square[id - 1];
    switch (id) {
    case ID_SPIKE1: return G.spr_spike[0];
    case ID_SPIKE2: return G.spr_spike[1];
    case ID_PIT:    return G.spr_pit[0];
    case ID_PORTAL1: case ID_PORTAL1 + 1: case ID_PORTAL1 + 2: case ID_PORTAL1 + 3:
        return G.spr_portal[id - ID_PORTAL1];
    case ID_ROD1: case ID_ROD1 + 1: case ID_ROD1 + 2:
        return G.spr_rod[id - ID_ROD1];
    case ID_DSPIKE1: case ID_DSPIKE1 + 1: case ID_DSPIKE1 + 2: case ID_DSPIKE1 + 3:
        return G.spr_dspike[id - ID_DSPIKE1];
    case ID_BUMP:  return G.spr_bump;
    case ID_RING:  return G.spr_ring;
    case ID_PLANK: return G.spr_plank;
    case ID_CHAIN: return G.spr_chain;
    default: return -1;
    }
}

/* ---- saving ---------------------------------------------------------------------- */

/* best.dat: one byte per level, then one byte per setting. Older saves only
 * have the first eight bytes, so anything missing just keeps its default. */
static void load_best(void)
{
    uint32_t size = 0;
    /* save=1: best.dat is our own file, not a shipped asset, so it is not in files.lst */
    uint8_t *data = plat_read_file("best.dat", &size, 1);
    if (data && size >= sizeof G.best) memcpy(G.best, data, sizeof G.best);
    else memset(G.best, 0, sizeof G.best);
    /* attempts are session-only: every app opening starts fresh */
    memset(G.attempts, 0, sizeof G.attempts);
    G.music_on = G.sound_on = 1;
    if (data && size >= sizeof G.best + 3) {
        G.music_on = data[sizeof G.best] != 0;
        G.sound_on = data[sizeof G.best + 1] != 0;
        G.practice = data[sizeof G.best + 2] != 0;
    }
    free(data);
}

static void save_best(void)
{
    uint8_t buf[sizeof G.best + 3 + sizeof G.attempts];
    memcpy(buf, G.best, sizeof G.best);
    buf[sizeof G.best] = (uint8_t)G.music_on;
    buf[sizeof G.best + 1] = (uint8_t)G.sound_on;
    buf[sizeof G.best + 2] = (uint8_t)G.practice;
    memcpy(buf + sizeof G.best + 3, G.attempts, sizeof G.attempts);
    plat_write_file("best.dat", buf, sizeof buf);
}

/* ---- game ------------------------------------------------------------------------- */

static void start_level(int index)
{
    G.level = index;
    load_level_objects(index);
    G.mode = GD_MODE_CUBE;
    G.px = 0;
    G.py = PLAYER_HALF;
    G.vy = 0;
    G.prot = 0;
    G.grounded = 1;
    G.gravity_sign = 1;
    memset(used, 0, sizeof used);
    G.camx = 0;
    G.camy = 0;
    G.cp_set = 1;
    G.cpx = 0;
    G.cpy = PLAYER_HALF;
    G.cp_grav = 1;
    G.cp_mode = GD_MODE_CUBE;
    G.pad_anim = G.orb_anim = 0;
    G.pad_idx = G.orb_idx = -1;
    G.nshards = 0;
    G.flash = 0;
    /* The OS audio stack panics if a second long track is spawned while the
     * first is still loading, so a restart right after a start keeps the music
     * running instead of retriggering it. */
    plat_log("start level %d song %s music_t=%.1f", index, G.levels[index].song, G.music_t);
    plat_log_flush();
    if (G.time - G.music_t > 2.0f) {
        if (G.music_on) plat_music_play(G.levels[index].song);
        G.music_t = G.time;
    }
    if (G.attempts[index] < 9999) G.attempts[index]++;
    G.attempt = G.attempts[index];
    save_best();
    G.state = ST_PLAY;
    G.state_t = 0;
}

static void die(void)
{
    if (G.state != ST_PLAY) return;
    /* the best is the furthest point reached, so it is recorded on every death */
    if (!G.practice) {
        int pct = (int)(100.0f * G.px / (float)G.levels[G.level].length);
        if (pct < 0) pct = 0;
        if (pct > 99) pct = 99;
        if (pct > G.best[G.level]) G.best[G.level] = pct;
        save_best();
    }
    if (G.practice && G.cp_set) {
        /* practice mode: back to the last checkpoint instead of the start */
        G.px = G.cpx;
        G.py = G.cpy;
        G.vy = 0;
        G.mode = G.cp_mode;
        G.gravity_sign = G.cp_grav;
        G.grounded = 0;
        G.prot = 0;
        G.camy = 0;
        memset(used, 0, sizeof used);   /* make one-time portals fire again after a practice death */
        G.nshards = 0;
        G.state = ST_PLAY;
        G.state_t = 0;
        { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.5f); }
        return;
    }
    /* the original bursts into a handful of icon-coloured shards, flashes white
     * for a moment, then restarts the attempt on its own */
    G.nshards = GD_SHARDS;
    for (int i = 0; i < GD_SHARDS; i++) {
        float a = (float)i * (6.2831853f / GD_SHARDS) + (float)(G.attempt & 7) * 0.31f;
        float speed = 120.0f + (float)((i * 37 + G.attempt * 11) % 90);
        G.shard[i].x = G.px;
        G.shard[i].y = G.py;
        G.shard[i].vx = cosf(a) * speed;
        G.shard[i].vy = sinf(a) * speed + 60.0f;
        G.shard[i].life = 0.55f + 0.05f * (float)(i % 4);
        G.shard[i].color = PLAYER_RGB;
    }
    G.flash = 0.09f;
    G.state = ST_DEAD;
    G.state_t = 0;
    { if (G.sound_on) plat_sound(GD_SND_DIE, 1.0f); }
    plat_music_stop();
}

static void finish_level(void)
{
    G.state = ST_DONE;
    G.state_t = 0;
    { if (G.sound_on) plat_sound(GD_SND_WIN, 1.0f); }
    plat_music_stop();
    if (!G.practice) {
        G.best[G.level] = 100;
        save_best();
    }
    G.nshards = 0;
    G.flash = 0.0f;
}

/* Returns 1 when the object killed the player. */
static int collide(const GDObject *o, float pl, float pr, float pb, float pt)
{
    uint8_t id = o->id;
    float hw = 15.0f, hh = (id == ID_PLANK) ? 7.0f : 15.0f;
    /* a quarter turn swaps the box; the art itself is drawn rotated */
    if (o->rot % 180 != 0) { float t = hw; hw = hh; hh = t; }
    float ol = o->x - hw, or_ = o->x + hw, ob = o->y - hh, ot = o->y + hh;
    if (pr <= ol || pl >= or_ || pt <= ob || pb >= ot) return 0;
    if (id == ID_SPIKE1 || id == ID_SPIKE2 || (id >= ID_DSPIKE1 && id <= ID_DSPIKE1 + 3)) {
        /* the original's hazard hitbox is much smaller than the art: a spike only
         * kills well inside its outline, so grazing the tip is survivable. */
        float m = 9.0f;
        return (pr > ol + m && pl < or_ - m && pt > ob + m && pb < ot - m);
    }
    /* only real blocks stop the player; pits, rods, chains and decorations do not */
    if (!((id >= ID_BLOCK_FIRST && id <= ID_BLOCK_LAST) || id == ID_PLANK)) return 0;
    /* land on the face the gravity pulls towards; a side hit is a wall and kills */
    if (G.gravity_sign > 0) {
        if (G.vy <= 0 && prev_py + PLAYER_HALF >= ot - 4.0f) {
            G.py = ot + PLAYER_HALF;
            G.vy = 0;
            G.grounded = 1;
            G.prot = 0;
            return 0;
        }
    } else {
        if (G.vy >= 0 && prev_py - PLAYER_HALF <= ob + 0.5f) {
            G.py = ob - PLAYER_HALF;
            G.vy = 0;
            G.grounded = 1;
            G.prot = 0;
            return 0;
        }
    }
    return 1;
}

static int pit_at(float x)
{
    for (int i = obj_lower_bound(x - 16.0f); i < G.nobjects; i++) {
        if ((float)objs[i].x >= x + 16.0f) break;
        if (objs[i].id == ID_PIT) return 1;
    }
    return 0;
}

static void update_play(float dt, const PlatTouches *touch)
{
    int pressed = touch->count > 0 || touch_press;
    int just = touch_press;
    /* the pause button sits in the top-right corner; give it a generous,
     * slightly inward hit area so a thumb on the edge still lands */
    if (just && press_x > CW - 60.0f && press_y < 46.0f) {
        G.state = ST_PAUSED;
        G.state_t = 0;
        { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.5f); }
        return;
    }
    prev_py = G.py;

    G.px += SPEED * dt;

    if (G.mode == GD_MODE_CUBE) {
        G.vy -= G.gravity_sign * GRAVITY * dt;
        if (G.vy * G.gravity_sign < -TERM_V) G.vy = -TERM_V * G.gravity_sign;
        if (pressed && G.grounded) {
            G.vy = JUMP_V * G.gravity_sign;
            G.grounded = 0;
        }
        G.py += G.vy * dt;
        if (!G.grounded) G.prot += 300.0f * dt * G.gravity_sign;
    } else if (G.mode == GD_MODE_SHIP) {
        G.vy += (pressed ? SHIP_UP : -SHIP_DN) * dt;
        if (G.vy > SHIP_MAX) G.vy = SHIP_MAX;
        if (G.vy < -SHIP_MAX) G.vy = -SHIP_MAX;
        G.py += G.vy * dt;
        /* smooth, damped rotation like the original (max 22.5 deg) */
        float target_rot = G.vy * 0.08f;
        G.prot += (target_rot - G.prot) * 2.5f * dt;
        if (G.prot > 22.5f) G.prot = 22.5f;
        if (G.prot < -22.5f) G.prot = -22.5f;
        if (G.py - PLAYER_HALF < 0.0f) { die(); return; }
        /* the ship is a hull, not a balloon: the level ceiling is a boundary */
        if (G.py + PLAYER_HALF > G.ceil_y) { die(); return; }
    } else if (G.mode == GD_MODE_BALL) {
        G.vy -= G.gravity_sign * GRAVITY * dt;
        if (just) G.gravity_sign = -G.gravity_sign;
        G.py += G.vy * dt;
        if (G.py - PLAYER_HALF < 0) { G.py = PLAYER_HALF; G.vy = 0; G.grounded = 1; }
        G.prot += (G.gravity_sign > 0 ? -8.0f : 8.0f) * dt * 60.0f;
    } else { /* ufo */
        G.vy -= GRAVITY * dt;
        if (just) G.vy = JUMP_V;
        G.py += G.vy * dt;
        G.prot = G.vy * 0.12f;
    }

    G.grounded = 0;

    /* the ground (only while gravity pulls down); the pits are gaps in it */
    if (G.mode != GD_MODE_SHIP && G.gravity_sign > 0 && G.py - PLAYER_HALF <= 0.0f) {
        if (pit_at(G.px)) {
            if (G.py < -40.0f) { die(); return; }
        } else {
            G.py = PLAYER_HALF;
            G.vy = 0;
            G.grounded = 1;
            /* the cube lands on a flat face, like the original */
            G.prot = roundf(G.prot / 90.0f) * 90.0f;
        }
    }
    /* flipped gravity sticks to the level's ceiling instead */
    if (G.mode != GD_MODE_SHIP && G.gravity_sign < 0 && G.py + PLAYER_HALF >= G.ceil_y &&
        !pit_at(G.px)) {
        G.py = G.ceil_y - PLAYER_HALF;
        G.vy = 0;
        G.grounded = 1;
        G.prot = roundf(G.prot / 90.0f) * 90.0f;
    }

    float pl = G.px - PLAYER_HALF, pr = G.px + PLAYER_HALF;
    float pb = G.py - PLAYER_HALF, pt = G.py + PLAYER_HALF;
    for (int i = obj_lower_bound(G.px - 70.0f); i < G.nobjects; i++) {
        const GDObject *o = &objs[i];
        if ((float)o->x > G.px + 70.0f) break;
        if (o->id >= ID_PORTAL1 && o->id <= ID_PORTAL1 + 3) {
            /* a portal fires once, when the player crosses it at its own height.
             * 10/11 flip gravity, 12 is the cube portal, 13 the ship portal. */
            if (!used[i] && o->x <= G.px && o->x > G.px - SPEED * dt * 2.0f - 2.0f &&
                fabsf(G.py - o->y) < 80.0f) {
                used[i] = 1;
                if (o->id == 10 || o->id == 11) {
                    G.gravity_sign = -G.gravity_sign;
                } else {
                    int m = (o->id == 12) ? GD_MODE_CUBE : GD_MODE_SHIP;
                    if (m != G.mode) { G.mode = m; G.vy = 0; }
                }
                { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.6f); }
            }
            continue;
        }
        if (o->id == ID_BUMP) {
            /* the yellow jump pad fires whenever the player touches it */
            if (pr > o->x - 14 && pl < o->x + 14 && pb < o->y + 14 && pt > o->y - 14) {
                G.vy = 1000.0f * G.gravity_sign;
                G.grounded = 0;
                G.pad_idx = i;
                G.pad_anim = 1.0f;
                { if (G.sound_on) plat_sound(GD_SND_JUMP, 0.5f); }
            }
        } else if (o->id == ID_RING) {
            /* the jump orb only fires on a fresh tap */
            if (just && pr > o->x - 16 && pl < o->x + 16 && pt > o->y - 16 && pb < o->y + 16) {
                G.vy = 900.0f * G.gravity_sign;
                G.grounded = 0;
                G.orb_idx = i;
                G.orb_anim = 1.0f;
                { if (G.sound_on) plat_sound(GD_SND_JUMP, 0.5f); }
            }
        } else if (collide(o, pl, pr, pb, pt)) {
            die();
            return;
        }
    }

    if (G.px >= G.levels[G.level].length) finish_level();
    G.camx = G.px - 160.0f;
    if (G.camx < 0) G.camx = 0;

    /* Vertical camera: follow a real climb, but hold still for an ordinary jump
     * (the target stays at or below zero until the player is well above the
     * ground, and a dead zone stops it twitching on small height changes). */
    float target = G.py - 105.0f;
    if (target < 0.0f) target = 0.0f;
    if (target > G.camy + 6.0f || target < G.camy - 6.0f)
        G.camy += (target - G.camy) * 3.0f * dt;
    if (G.camy < 0.0f) G.camy = 0.0f;

    G.time += dt;
    if (G.pad_anim > 0.0f) G.pad_anim -= dt * 2.2f;
    if (G.orb_anim > 0.0f) G.orb_anim -= dt * 2.2f;
    if (G.pad_anim < 0.0f) G.pad_anim = 0.0f;
    if (G.orb_anim < 0.0f) G.orb_anim = 0.0f;


    /* practice mode drops a checkpoint on the ground every 600 px */
    if (G.practice && G.grounded && G.px - G.cpx > 600.0f) {
        G.cpx = G.px;
        G.cpy = G.py;
        G.cp_grav = G.gravity_sign;
        G.cp_mode = G.mode;
        G.cp_set = 1;
    }
}

/* ---- the original's font --------------------------------------------------------- */

typedef struct { uint16_t page, x, y, w, h, xo, yo, adv; } Glyph;

static Glyph *fg;
static int fcount, ffirst, fnpages;
static int fpage[8];

static int font_load(void)
{
    uint32_t size = 0;
    uint8_t *d = plat_read_file("font.bin", &size, 0);
    if (!d || size < 16 || memcmp(d, "GDF1", 4)) { free(d); return -1; }
    fcount = d[4] | d[5] << 8;
    ffirst = d[6] | d[7] << 8;
    fnpages = d[8] | d[9] << 8;
    if (fcount < 1 || fcount > 200 || fnpages < 1 || fnpages > 8 ||
        size != 16 + 16u * fcount) {
        free(d);
        return -1;
    }
    fg = malloc(sizeof(Glyph) * fcount);
    if (!fg) { free(d); return -1; }
    for (int i = 0; i < fcount; i++) {
        const uint8_t *p = d + 16 + 16u * i;
        fg[i].page = p[0] | p[1] << 8;
        fg[i].x = p[2] | p[3] << 8;
        fg[i].y = p[4] | p[5] << 8;
        fg[i].w = p[6] | p[7] << 8;
        fg[i].h = p[8] | p[9] << 8;
        fg[i].xo = (int16_t)(p[10] | p[11] << 8);
        fg[i].yo = (int16_t)(p[12] | p[13] << 8);
        fg[i].adv = p[14] | p[15] << 8;
    }
    free(d);
    int base = sprites_pages();
    for (int i = 0; i < fnpages; i++) {
        char path[24];
        uint32_t psz = 0;
        snprintf(path, sizeof path, "page%02d.bin", base + i);
        uint8_t *px = plat_read_file(path, &psz, 0);
        if (!px || psz != 256u * 128u * 4u) {
            plat_log("GeometryDash: font page %s missing", path);
            return -1;
        }
        fpage[i] = gfx_tex_create(256, 128, GFX_FMT_RGBA8888, px, 1, 0);
        free(px);
        if (!fpage[i]) return -1;
    }
    return 0;
}

/* The original's background: one grayscale gradient, tinted with the level colour. */
static int bg_load(void)
{
    uint32_t size = 0;
    uint8_t *d = plat_read_file("bg.bin", &size, 0);
    if (!d || size != 256u * 128u * 4u) {
        free(d);
        return -1;
    }
    G.bg_tex = gfx_tex_create(256, 128, GFX_FMT_RGBA8888, d, 1, 0);
    free(d);
    return G.bg_tex ? 0 : -1;
}

static float text_width(const char *s, float scale)
{
    float w = 0;
    if (!fg) return 0;
    for (const char *p = s; *p; p++) {
        int c = (unsigned char)*p;
        if (c < ffirst || c >= ffirst + fcount) { w += 8 * scale; continue; }
        w += fg[c - ffirst].adv * scale;
    }
    return w;
}

static void draw_text(const char *s, float x, float y, float scale, uint32_t argb)
{
    float cx = x;
    if (!fg) return;
    gfx_set_blend(GFX_BLEND_ALPHA);
    for (const char *p = s; *p; p++) {
        int c = (unsigned char)*p;
        if (c < ffirst || c >= ffirst + fcount) { cx += 8 * scale; continue; }
        const Glyph *g = &fg[c - ffirst];
        if (g->w && g->h && g->page < fnpages && fpage[g->page]) {
            float px = cx + g->xo * scale, py = y + g->yo * scale;
            float gw = g->w * scale, gh = g->h * scale;
            float xy[8] = {px, py, px + gw, py, px + gw, py + gh, px, py + gh};
            float uv[8] = {g->x, g->y, g->x + g->w, g->y,
                           g->x + g->w, g->y + g->h, g->x, g->y + g->h};
            gfx_quad(fpage[g->page], 256, 128, xy, uv, argb);
        }
        cx += g->adv * scale;
    }
}

/* ---- drawing --------------------------------------------------------------------- */

static uint32_t rgb(int r, int g, int b) { return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }

/* Premultiplied translucent colour, as the renderer's blend expects. */
static uint32_t rgba(uint32_t color, float a)
{
    if (a <= 0.0f) return 0;
    if (a > 1.0f) a = 1.0f;
    uint32_t A = (uint32_t)(a * 255.0f + 0.5f);
    uint32_t r = ((color >> 16) & 255u) * A / 255u;
    uint32_t g = ((color >> 8) & 255u) * A / 255u;
    uint32_t b = (color & 255u) * A / 255u;
    return (A << 24) | (r << 16) | (g << 8) | b;
}

static void draw_rect(float x, float y, float w, float h, uint32_t argb)
{
    float xy[8] = {x, y, x + w, y, x + w, y + h, x, y + h};
    float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    gfx_quad(0, 1, 1, xy, uv, argb);
}

static void draw_spr(int spr, float cx, float cy, float scale, float rot, uint32_t tint)
{
    if (spr < 0) return;
    int w = spr_w(spr), h = spr_h(spr);
    if (w <= 0 || h <= 0) return;
    float hw = w * 0.5f * scale, hh = h * 0.5f * scale;
    float xy[8];
    if (rot == 0.0f) {
        xy[0] = cx - hw; xy[1] = cy - hh;
        xy[2] = cx + hw; xy[3] = cy - hh;
        xy[4] = cx + hw; xy[5] = cy + hh;
        xy[6] = cx - hw; xy[7] = cy + hh;
    } else {
        float c, s;
        cos_sin_angle(rot, &c, &s);
        float corners[4][2] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
        for (int i = 0; i < 4; i++) {
            float x = corners[i][0], y = corners[i][1];
            xy[i * 2] = cx + x * c - y * s;
            xy[i * 2 + 1] = cy + x * s + y * c;
        }
    }
    if (tint == 0) spr_quad(spr, xy, 1.0f);
    else spr_quad_color(spr, xy, 1.0f, tint);
}

static void draw_spr_rect(int spr, float x0, float y0, float x1, float y1, uint32_t tint)
{
    if (spr < 0) return;
    float q[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
    if (tint == 0) spr_quad(spr, q, 1.0f);
    else spr_quad_color(spr, q, 1.0f, tint);
}

/* The original keeps a white halo behind its objects. The plist trims each
 * halo to its opaque pixels and records how far its centre moved from the
 * sprite centre; apply that offset (and rotate it with the object). */
static void draw_glow(int spr, float cx, float cy, float scale, float rot, float dx, float dy)
{
    if (spr < 0) return;
    float ox = dx * scale, oy = dy * scale;
    if (rot != 0.0f) {
        float c, s;
        cos_sin_angle(rot, &c, &s);
        float rx = ox * c - oy * s, ry = ox * s + oy * c;
        ox = rx; oy = ry;
    }
    draw_spr(spr, cx + ox, cy + oy, scale, rot, 0);
}

/* halo sprite and its screen-space centre offset for a world object */
static void object_glow(uint8_t id, int *spr, float *ox, float *oy)
{
    static const float square_off[7][2] = {
        {0, 0}, {0, -14}, {-2, -2}, {-13, -13}, {0, 0}, {0, -2}, {0, 0}
    };
    *spr = -1; *ox = *oy = 0.0f;
    if (id >= ID_BLOCK_FIRST && id <= ID_BLOCK_LAST) {
        int i = id - ID_BLOCK_FIRST;
        if (G.spr_square_glow[i] >= 0) {
            *spr = G.spr_square_glow[i];
            *ox = square_off[i][0];
            *oy = square_off[i][1];
        }
    } else if (id == ID_SPIKE1) {
        *spr = G.spr_spike_glow[0]; *oy = 1.0f;
    } else if (id == ID_SPIKE2) {
        *spr = G.spr_spike_glow[1];
    } else if (id == ID_PLANK) {
        *spr = G.spr_plank_glow;
    } else if (id == ID_BUMP) {
        *spr = G.spr_bump_glow; *oy = -2.0f;
    } else if (id == ID_RING) {
        *spr = G.spr_ring_glow;
    }
}

static uint32_t ground_rgb(void)
{
    GDLevel *l = &G.levels[G.level];
    return rgb(l->gr[0], l->gr[1], l->gr[2]);
}

static uint32_t player_rgb(void) { return PLAYER_RGB; }

/* The original's icon is three sprites: a coloured face, the body outline with
 * its transparent window, and the small centre detail. Drawing all three is
 * what turns the sprite from a hollow outline into the familiar cube. */
static void draw_cube_icon(float cx, float cy, float rot, float alpha, int ship)
{
    uint32_t c1 = alpha >= 1.0f ? player_rgb() : rgba(player_rgb(), alpha);
    if (ship || G.spr_cube < 0) {
        draw_spr(G.spr_ship, cx, cy, 1.0f, rot, c1);
        return;
    }
    uint32_t c2 = alpha >= 1.0f ? player_rgb() : rgba(player_rgb(), alpha);
    draw_spr(G.spr_cubeface, cx, cy, 1.0f, rot, c1);
    draw_spr(G.spr_cube, cx, cy, 1.0f, rot, c1);
    draw_spr(G.spr_cube2, cx, cy, 1.0f, rot, c2);
}

/* The GD font has no drop shadow of its own; the original draws text over a
 * darker copy, which is what makes the HUD readable on any level colour. */
static void draw_text_shadow(const char *s, float cx, float y, float scale, uint32_t argb)
{
    float w = text_width(s, scale);
    draw_text(s, cx - w * 0.5f + 1.5f, y + 2.0f, scale, rgba(0x000000, 0.65f));
    draw_text(s, cx - w * 0.5f, y, scale, argb);
}

static void draw_text_centered(const char *s, float cx, float y, float scale, uint32_t argb)
{
    draw_text(s, cx - text_width(s, scale) * 0.5f, y, scale, argb);
}

/* A Geometry Dash style button: dark base, green face, light top edge. */
static void draw_button(float x, float y, float w, float h, const char *label, uint32_t face)
{
    int gui = -1;
    if (face == 0xFF7ADE2Du) gui = G.spr_gui[0];
    else if (face == 0xFF4EA61Fu) gui = G.spr_gui[0];
    else if (face == 0xFF5B5B66u) gui = G.spr_gui[3];
    if (gui >= 0) {
        draw_rect(x + 2, y + 3, w, h, rgba(0x000000, 0.45f));
        draw_spr_rect(gui, x, y, x + w, y + h, face == 0xFF4EA61Fu ? 0xFFC8E0B0u : 0);
    } else {
        draw_rect(x + 2, y + 3, w, h, rgba(0x000000, 0.45f));
        draw_rect(x, y, w, h, rgba(0x000000, 0.55f));
        draw_rect(x + 2, y + 2, w - 4, h - 4, face);
        draw_rect(x + 2, y + 2, w - 4, 2, rgba(0xFFFFFF, 0.35f));
    }
    float scale = 0.62f;
    while (scale > 0.3f && text_width(label, scale) > w - 12.0f) scale -= 0.04f;
    draw_text_shadow(label, x + w * 0.5f, y + h * 0.5f - 9.0f * scale / 0.62f, scale, 0xFFFFFFFFu);
}

/* ---- background, ground and world ------------------------------------------------ */

static void draw_bg_at(float scroll)
{
    GDLevel *l = &G.levels[G.level];
    uint32_t bg = rgb(l->bg[0], l->bg[1], l->bg[2]);
    if (!G.bg_tex) {
        gfx_set_blend(GFX_BLEND_NONE);
        draw_rect(0, 0, CW, CH, bg);
        return;
    }
    /* The 1.0 background is one 512px mosaic, stretched across the whole canvas
     * exactly once; the level colour tints it. */
    gfx_set_blend(GFX_BLEND_NONE);
    float xy[8] = {0, 0, CW, 0, CW, CH, 0, CH};
    float uv[8] = {0, 0, 256, 0, 256, 128, 0, 128};
    (void)scroll;
    gfx_quad(G.bg_tex, 256, 128, xy, uv, bg);
}

static void draw_bg(void) { draw_bg_at(G.camx); }

static void draw_ground(void)
{
    float gy = GROUND_SY + G.camy;
    /* when the camera has climbed into a ship section, the ground is off-screen */
    if (gy - 2.0f > CH) return;
    uint32_t gc = ground_rgb();
    int tile = G.spr_ground;
    if (tile >= 0) {
        /* groundSquare_001 is the original's own 128 px ground tile: white at
         * the ground line, a faint checker, then a dark gradient downwards. */
        float s = 0.5f;
        float tw = (float)spr_w(tile) * s;
        float th = (float)spr_h(tile) * s;
        float off = fmodf(G.camx, tw);
        if (off < 0.0f) off += tw;
        gfx_set_blend(GFX_BLEND_NONE);
        for (float x = -off; x < CW + tw; x += tw)
            draw_spr(tile, x + tw * 0.5f, gy + th * 0.5f, s, 0.0f, gc);
    } else {
        gfx_set_blend(GFX_BLEND_NONE);
        draw_rect(0, gy, CW, CH - gy, gc);
    }
    /* bright lip at the top of the ground, like the original's white floor line */
    gfx_set_blend(GFX_BLEND_ALPHA);
    draw_rect(0, gy - 2, CW, 2, rgba(0xFFFFFF, 0.92f));
    draw_rect(0, gy, CW, 1, rgba(0x000000, 0.35f));
}

static uint32_t object_tint(uint8_t id)
{
    /* blocks, spikes, pits, planks and chains use the level's object colour */
    if ((id >= ID_BLOCK_FIRST && id <= ID_BLOCK_LAST) ||
        id == ID_SPIKE1 || id == ID_SPIKE2 || id == ID_PIT ||
        (id >= ID_DSPIKE1 && id <= ID_DSPIKE1 + 3) ||
        id == ID_PLANK || id == ID_CHAIN)
        return obj_tint;
    /* saws stay steel, portals/pads/orbs keep their built-in colours */
    return 0;
}

static void draw_shards(void)
{
    if (!G.nshards) return;
    gfx_set_blend(GFX_BLEND_ALPHA);
    for (int i = 0; i < G.nshards; i++) {
        float life = G.shard[i].life;
        if (life <= 0.0f) continue;
        float a = life * 1.6f;
        if (a > 1.0f) a = 1.0f;
        float sx = G.shard[i].x - G.camx;
        float sy = GROUND_SY - G.shard[i].y + G.camy;
        uint32_t col = G.shard[i].color ? G.shard[i].color : PLAYER_RGB;
        draw_rect(sx - 3.0f, sy - 3.0f, 6.0f, 6.0f, rgba(col, a));
    }
}

static void draw_world(void)
{
    draw_bg();
    draw_ground();
    gfx_set_blend(GFX_BLEND_ALPHA);

    /* "ATTEMPT n" scrolls with the level, as in the original */
    if (G.state == ST_PLAY || G.state == ST_PAUSED) {
        float ay = GROUND_SY - 132.0f + G.camy;
        if (ay > -40.0f && ay < CH + 40.0f) {
            char abuf[32];
            snprintf(abuf, sizeof abuf, "ATTEMPT %d", G.attempt);
            draw_text_shadow(abuf, 225.0f - G.camx, ay, 1.15f,
                             rgba(0xFFFFFF, 0.32f));
        }
    }

    for (int i = obj_lower_bound(G.camx - 160.0f); i < G.nobjects; i++) {
        const GDObject *o = &objs[i];
        if ((float)o->x - G.camx > CW + 160.0f) break;
        float sx = o->x - G.camx;
        float sy = GROUND_SY - o->y + G.camy;
        /* the camera now climbs with the level; skip drawing things the screen
         * cannot see instead of always rendering the full vertical stack */
        if (sy < -150.0f || sy > CH + 150.0f) continue;
        if (o->id >= ID_PORTAL1 && o->id <= ID_PORTAL1 + 3) {
            /* portals are drawn as a back and a front half; centering both
             * halves on the object origin made the portal collapse */
            int pi = o->id - ID_PORTAL1;
            static const float back_off[4] = {-11.0f, -10.0f, -10.0f, -10.0f};
            static const float front_off[4] = {5.0f, 5.0f, 6.0f, 6.0f};
            if (G.spr_portal_back[pi] >= 0)
                draw_spr(G.spr_portal_back[pi], sx + back_off[pi], sy, 1.0f, 0.0f, 0);
            if (G.spr_portal[pi] >= 0)
                draw_spr(G.spr_portal[pi], sx + front_off[pi], sy, 1.0f, 0.0f, 0);
            continue;
        }
        int spr = object_sprite(o->id);
        if (spr < 0) continue;
        float scale = 1.0f;
        if (o->id == ID_BUMP || o->id == ID_RING) {
            /* the pads and orbs breathe, and swell when they fire */
            scale = 1.0f + 0.12f * sinf(G.time * 5.0f + o->x * 0.05f);
            if (o->id == ID_BUMP && i == G.pad_idx && G.pad_anim > 0.0f) scale += 0.5f * G.pad_anim;
            if (o->id == ID_RING && i == G.orb_idx && G.orb_anim > 0.0f) scale += 0.4f * G.orb_anim;
        }
        int glow; float gox, goy;
        object_glow(o->id, &glow, &gox, &goy);
        if (glow >= 0) draw_glow(glow, sx, sy, scale, (float)o->rot, gox, goy);
        draw_spr(spr, sx, sy, scale, (float)o->rot, object_tint(o->id));
        if (o->id >= ID_ROD1 && o->id <= ID_ROD1 + 2) {
            /* the decorative rods are little signal posts: a tiny white glint
             * at the tip makes them read as scenery, not as hazards */
            int h = spr_h(spr);
            int r = o->rot % 360;
            if (r < 0) r += 360;
            float tip = (r == 180) ? sy + h * 0.5f + 3.0f : sy - h * 0.5f - 3.0f;
            if (G.spr_square_glow[3] >= 0)
                draw_spr(G.spr_square_glow[3], sx, tip, 2.1f, 0.0f, 0);
            if (G.spr_spark >= 0)
                draw_spr(G.spr_spark, sx, tip, 1.0f, 0.0f, 0);
        }
    }

    /* the player */
    if (G.mode == GD_MODE_SHIP)
        draw_cube_icon(G.px - G.camx, GROUND_SY - G.py + G.camy, G.prot, 1.0f, 1);
    else
        draw_cube_icon(G.px - G.camx, GROUND_SY - G.py + G.camy - 3, G.prot, 1.0f, 0);

    if (G.state != ST_DONE) draw_shards();
    if (G.flash > 0.0f) {
        float a = G.flash / 0.09f * 0.8f;
        draw_rect(0, 0, CW, CH, rgba(0xFFFFFF, a));
    }
}

/* ---- HUD and screens ------------------------------------------------------------- */

static void draw_pause_icon(void)
{
    if (G.spr_gui[0] >= 0) {
        draw_spr(G.spr_gui[0], CW - 22, 19, 0.7f, 0.0f, 0);
        draw_rect(CW - 28, 13, 5, 12, 0xFFFFFFFFu);
        draw_rect(CW - 21, 13, 5, 12, 0xFFFFFFFFu);
        return;
    }
    draw_rect(CW - 34, 6, 28, 26, rgba(0x000000, 0.45f));
    draw_rect(CW - 26, 12, 5, 14, 0xFFFFFFFFu);
    draw_rect(CW - 17, 12, 5, 14, 0xFFFFFFFFu);
}

static void draw_hud(void)
{
    GDLevel *l = &G.levels[G.level];
    float t = (float)G.px / (float)l->length;
    char buf[48];
    if (t < 0) t = 0;
    if (t > 1) t = 1;

    draw_pause_icon();

    /* the original's thin progress bar across the top */
    float bx = 92.0f, by = 5.0f, bw = 296.0f, bh = 12.0f;
    gfx_set_blend(GFX_BLEND_ALPHA);
    if (G.spr_progress >= 0) {
        draw_spr_rect(G.spr_progress, bx, by, bx + bw, by + bh, 0xFF0A0A12u);
        if (t > 0.0f) {
            float fw = bw * t - 2.0f;
            if (fw < 1.0f) fw = 1.0f;
            gfx_set_clip(bx + 1.0f, by + 1.0f, fw, bh - 2.0f);
            draw_spr_rect(G.spr_progress, bx, by, bx + bw, by + bh, 0xFF00E000u);
            gfx_set_clip(0, 0, 0, 0);
        }
    } else {
        draw_rect(bx - 2, by - 2, bw + 4, bh + 4, rgba(0x000000, 0.55f));
        draw_rect(bx, by, bw, bh, rgba(0x101018, 0.85f));
        if (t > 0.0f) {
            draw_rect(bx, by, bw * t, bh, 0xFF00F000u);
            draw_rect(bx, by, bw * t, 2, rgba(0xFFFFFF, 0.38f));
        }
        draw_rect(bx, by, bw, 1, rgba(0xFFFFFF, 0.55f));
        draw_rect(bx, by + bh - 1, bw, 1, rgba(0x000000, 0.6f));
    }
    snprintf(buf, sizeof buf, "%d%%", (int)(t * 100.0f + 0.5f));
    draw_text_shadow(buf, CW * 0.5f, by + bh + 2.0f, 0.42f, 0xFFFFFFFFu);

    if (G.practice) {
        draw_text_shadow("PRACTICE", 8.0f + text_width("PRACTICE", 0.4f) * 0.5f, 12.0f,
                         0.4f, 0xFF7ADE2Du);
    }
}

static void draw_menu(void)
{
    GDLevel *l = &G.levels[G.level];
    char buf[48];
    draw_bg_at(G.time * 26.0f);

    /* the original's logo from the launch sheet */
    draw_rect(0, 0, CW, 54, rgba(0x000000, 0.18f));
    if (G.spr_logo >= 0)
        draw_spr(G.spr_logo, CW * 0.5f, 28.0f, 0.9f, 0.0f, 0);
    else
        draw_text_shadow("GEOMETRY DASH", CW * 0.5f, 7.0f, 1.2f, 0xFFFFFFFFu);

    /* level card */
    float px = 78.0f, py = 64.0f, pw = CW - 156.0f, ph = 128.0f;
    draw_rect(px, py, pw, ph, rgba(0x000000, 0.42f));
    draw_rect(px, py, pw, 2, ground_rgb());
    draw_rect(px, py + ph - 2, pw, 2, ground_rgb());
    draw_text_shadow(l->name, CW * 0.5f, py + 12.0f, 0.9f, 0xFFFFFFFFu);
    snprintf(buf, sizeof buf, "LEVEL %d OF %d", G.level + 1, G.nlevels);
    draw_text_shadow(buf, CW * 0.5f, py + 42.0f, 0.42f, 0xFFD8D8D8u);
    snprintf(buf, sizeof buf, "BEST %d%%", G.best[G.level]);
    draw_text_shadow(buf, CW * 0.5f, py + 56.0f, 0.48f, 0xFF7ADE2Du);

    /* the big green play button from the original */
    if (G.spr_play >= 0)
        draw_spr(G.spr_play, CW * 0.5f, py + 101.0f, 0.62f, 0.0f, 0);
    else
        draw_button(CW * 0.5f - 44.0f, py + 78.0f, 88.0f, 36.0f, "PLAY", 0xFF7ADE2Du);

    /* level arrows */
    draw_spr(G.spr_gui[3], 38.0f, 140.0f, 1.2f, 0.0f, 0);
    draw_spr(G.spr_gui[3], CW - 38.0f, 140.0f, 1.2f, 0.0f, 0);
    draw_text_centered("<", 38.0f, 120.0f, 1.15f, 0xFFFFFFFFu);
    draw_text_centered(">", CW - 38.0f, 120.0f, 1.15f, 0xFFFFFFFFu);

    /* bottom bar: level pips and the practice toggle */
    draw_rect(0, CH - 30, CW, 30, rgba(0x000000, 0.45f));
    for (int i = 0; i < G.nlevels; i++)
        draw_rect(CW * 0.5f - G.nlevels * 8.0f + i * 16.0f, CH - 21, 10, 10,
                  i == G.level ? 0xFFFFFFFFu : rgba(0xFFFFFF, 0.28f));
    snprintf(buf, sizeof buf, "PRACTICE %s", G.practice ? "ON" : "OFF");
    draw_button(6, CH - 26, 150, 22, buf, G.practice ? 0xFF4EA61Fu : 0xFF5B5B66u);

    /* settings gear, upper right like the original */
    draw_spr(G.spr_gui[3], CW - 24.0f, 24.0f, 0.7f, 0.0f, 0);
    draw_rect(CW - 26, 14, 4, 4, 0xFFFFFFFFu);
    draw_rect(CW - 26, 30, 4, 4, 0xFFFFFFFFu);
    draw_rect(CW - 34, 22, 4, 4, 0xFFFFFFFFu);
    draw_rect(CW - 18, 22, 4, 4, 0xFFFFFFFFu);
    draw_rect(CW - 30, 18, 12, 12, 0xFFFFFFFFu);
    draw_rect(CW - 26, 22, 4, 4, rgba(0x000000, 0.65f));
}

/* ---- settings -------------------------------------------------------------------- */

static const char *settings_rows[] = {
    "PRACTICE MODE", "MUSIC", "SOUND FX", "RESET PROGRESS", "BACK TO MENU"
};
static const char *settings_subs[] = {
    "START FROM CHECKPOINTS", "LEVEL SOUNDTRACK", "UI AND DEATH SOUNDS",
    "CLEAR BESTS AND ATTEMPTS", "RETURN TO THE LEVEL SELECT"
};
#define SETTINGS_ROWS 5
#define SET_ROW_X 62.0f
#define SET_ROW_W (CW - 124.0f)
#define SET_ROW_TOP 70.0f
#define SET_ROW_STEP 37.0f
#define SET_ROW_H 32.0f

static float settings_reset_arm;   /* seconds left to confirm a progress reset */

/* One rectangle shared by the drawing and the touch test, so a row can never
 * look like it is somewhere else than it responds. */
static void settings_row_rect(int i, float *x, float *y, float *w, float *h)
{
    *x = SET_ROW_X;
    *w = SET_ROW_W;
    *h = SET_ROW_H;
    *y = SET_ROW_TOP + (float)i * SET_ROW_STEP;
}

static void draw_settings(void)
{
    char buf[48];
    draw_menu();
    gfx_set_blend(GFX_BLEND_ALPHA);
    draw_rect(0, 0, CW, CH, rgba(0x000000, 0.80f));
    draw_rect(18, 10, CW - 36, CH - 20, rgba(0x0A1220, 0.94f));
    draw_rect(18, 10, CW - 36, 3, 0xFF7ADE2Du);
    draw_rect(18, CH - 13, CW - 36, 3, 0xFF7ADE2Du);
    draw_text_shadow("SETTINGS", CW * 0.5f, 22.0f, 1.0f, 0xFFFFFFFFu);
    draw_text_centered("AUDIO AND GAME OPTIONS", CW * 0.5f, 46.0f, 0.34f,
                       rgba(0xFFFFFF, 0.55f));

    for (int i = 0; i < SETTINGS_ROWS; i++) {
        float x, y, w, h;
        settings_row_rect(i, &x, &y, &w, &h);
        int hot = (G.settings_row == i);
        if (i == 4) {
            draw_button(x, y, w, h, settings_rows[i], 0xFF7ADE2Du);
            continue;
        }
        int on = 0;
        if (i == 0) on = G.practice;
        else if (i == 1) on = G.music_on;
        else if (i == 2) on = G.sound_on;
        int armed = (i == 3 && settings_reset_arm > 0.0f);

        draw_rect(x + 2, y + 3, w, h, rgba(0x000000, 0.45f));
        draw_rect(x, y, w, h, hot ? rgba(0x1A2A3A, 0.96f) : rgba(0x101A26, 0.92f));
        draw_rect(x, y, w, 1, rgba(0xFFFFFF, 0.10f));
        draw_rect(x, y, 3, h, armed ? 0xFFFF6B6Bu : (on ? 0xFF7ADE2Du : rgba(0xFFFFFF, 0.18f)));

        draw_text(settings_rows[i], x + 14.0f, y + 5.0f, 0.50f,
                  (i == 3) ? (armed ? 0xFFFFB0B0u : 0xFFFF8A8Au) : 0xFFFFFFFFu);
        draw_text(armed ? "TAP AGAIN TO CONFIRM" : settings_subs[i],
                  x + 14.0f, y + 19.0f, 0.30f,
                  armed ? rgba(0xFFFF8A8A, 0.95f) : rgba(0xFFFFFF, 0.45f));

        if (i < 3) {
            float pw = 62.0f, ph = 20.0f;
            float px = x + w - pw - 10.0f, py = y + (h - ph) * 0.5f;
            draw_rect(px, py, pw, ph, on ? 0xFF4EA61Fu : rgba(0x2A2A33, 0.92f));
            draw_rect(px, py, pw, 2, on ? rgba(0xFFFFFF, 0.25f) : rgba(0xFFFFFF, 0.08f));
            snprintf(buf, sizeof buf, "%s", on ? "ON" : "OFF");
            draw_text_centered(buf, px + pw * 0.5f, py + 4.0f, 0.42f,
                               on ? 0xFFFFFFFFu : 0xFF9A9A9Au);
        } else if (armed) {
            draw_text_centered("SURE?", x + w - 44.0f, y + 10.0f, 0.42f, 0xFFFF8A8Au);
        } else {
            draw_text_centered("CLEAR", x + w - 44.0f, y + 10.0f, 0.42f, 0xFFFF8A8Au);
        }
    }
}

static void draw_dead(void)
{
    char buf[32];
    draw_world();
    gfx_set_blend(GFX_BLEND_ALPHA);
    float a = G.state_t / 0.85f;
    if (a > 0.82f) a = 0.82f;
    draw_rect(0, 0, CW, CH, rgba(0x000000, a));
    snprintf(buf, sizeof buf, "ATTEMPT %d", G.attempt);
    draw_text_shadow(buf, CW * 0.5f, 20.0f, 0.55f, rgba(0xFFFFFF, 0.65f));
}

static float fit_text_scale(const char *s, float base, float max_w)
{
    float sc = base;
    while (sc > 0.40f && text_width(s, sc) > max_w) sc -= 0.05f;
    return sc;
}

static void draw_done(void)
{
    char buf[48];
    float t = G.state_t;
    float grow = t < 0.22f ? t / 0.22f : 1.0f;
    if (grow < 0.0f) grow = 0.0f;
    float star_scale = 0.90f + 0.35f * grow;
    float px = CW * 0.5f - 165.0f, py = 56.0f, pw = 330.0f, ph = 148.0f;
    const char *title = G.practice ? "PRACTICE COMPLETE!" : "LEVEL COMPLETE!";
    float title_scale = fit_text_scale(title, 0.82f, pw - 22.0f) * (0.85f + 0.15f * grow);

    draw_world();
    gfx_set_blend(GFX_BLEND_ALPHA);
    draw_rect(0, 0, CW, CH, rgba(0x000000, 0.68f));

    /* small, clean completion panel */
    draw_rect(px, py, pw, ph, rgba(0x101820, 0.88f));
    draw_rect(px, py, pw, 2, 0xFF7ADE2Du);
    draw_rect(px, py + ph - 2, pw, 2, 0xFF7ADE2Du);
    if (G.spr_star >= 0)
        draw_spr(G.spr_star, CW * 0.5f, 78.0f, star_scale, 0.0f, 0xFFFFD200u);
    draw_text_shadow(title, CW * 0.5f, 102.0f, title_scale, 0xFFFFFFFFu);
    draw_text_shadow(G.levels[G.level].name, CW * 0.5f, 124.0f, 0.52f, 0xFFD8D8D8u);
    snprintf(buf, sizeof buf, "100%%   ATTEMPT %d", G.attempt);
    draw_text_shadow(buf, CW * 0.5f, 144.0f, 0.46f, 0xFF7ADE2Du);
    draw_button(CW * 0.5f - 80.0f, 166.0f, 160.0f, 26.0f, "CONTINUE", 0xFF7ADE2Du);
}

#define PAUSE_X 120.0f
#define PAUSE_W 240.0f
#define PAUSE_TOP 76.0f
#define PAUSE_STEP 42.0f
#define PAUSE_H 34.0f

static void pause_row_rect(int i, float *x, float *y, float *w, float *h)
{
    *x = PAUSE_X;
    *w = PAUSE_W;
    *h = PAUSE_H;
    *y = PAUSE_TOP + (float)i * PAUSE_STEP;
}

static void draw_paused(void)
{
    char buf[48];
    draw_world();
    draw_hud();
    gfx_set_blend(GFX_BLEND_ALPHA);
    draw_rect(0, 0, CW, CH, rgba(0x000000, 0.72f));
    draw_rect(CW * 0.5f - 150.0f, 18.0f, 300.0f, CH - 36.0f, rgba(0x0A1220, 0.55f));
    draw_rect(CW * 0.5f - 150.0f, 18.0f, 300.0f, 3, 0xFF7ADE2Du);
    draw_text_shadow("PAUSED", CW * 0.5f, 28.0f, 1.15f, 0xFFFFFFFFu);
    snprintf(buf, sizeof buf, "%s   %d%%", G.levels[G.level].name,
             (int)(100.0f * G.px / (float)G.levels[G.level].length + 0.5f));
    draw_text_centered(buf, CW * 0.5f, 56.0f, 0.38f, rgba(0xFFFFFF, 0.60f));

    for (int i = 0; i < 4; i++) {
        float x, y, w, h;
        pause_row_rect(i, &x, &y, &w, &h);
        if (i == 2) {
            snprintf(buf, sizeof buf, "PRACTICE %s", G.practice ? "ON" : "OFF");
            draw_button(x, y, w, h, buf, G.practice ? 0xFF4EA61Fu : 0xFF5B5B66u);
        } else {
            draw_button(x, y, w, h, i == 0 ? "RESUME" : i == 1 ? "RESTART" : "EXIT TO MENU",
                        0xFF7ADE2Du);
        }
    }
}

/* ---- frame ----------------------------------------------------------------------- */

int game_init(float w, float h)
{
    memset(&G, 0, sizeof G);
    G.panel_w = w;
    G.panel_h = h;
    G.gravity_sign = 1;
    G.objects = objs;
    G.music_t = -10.0f;
    if (load_level_data()) {
        plat_log("GeometryDash: levels.bin missing or wrong version");
        return -1;
    }
    if (sprites_load()) {
        plat_log("GeometryDash: sprites missing");
        return -1;
    }
    bind_sprites();
    if (font_load()) plat_log("GeometryDash: font missing, text disabled");
    else plat_log("GeometryDash: font loaded");
    if (bg_load()) plat_log("GeometryDash: background missing, flat colour");
    else plat_log("GeometryDash: background loaded");
    load_best();
    G.state = ST_MENU;
    G.level = 0;
    return 0;
}

void game_frame(float dt, const PlatTouches *touch)
{
    /* The resident calls us every couple of milliseconds but only steps the
     * game at 60 Hz. Latch a fresh press here, on every call, so a tap that
     * begins and ends between two game steps is not lost. */
    int down = touch->count > 0;
    if (down && !prev_touch) {
        touch_press = 1;
        press_x = touch->pt[0].x;
        press_y = touch->pt[0].y;
    }
    prev_touch = down;

    if (dt > 0.0f) {
        if (dt > 0.05f) dt = 0.05f;
        G.state_t += dt;

        if (G.state == ST_MENU) {
            if (touch_press) {
                float x = press_x, y = press_y;
                if (x > CW - 56.0f && y < 44.0f) {
                    G.state = ST_SETTINGS;
                    G.state_t = 0;
                    G.settings_row = -1;
                    settings_reset_arm = 0;
                    { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.5f); }
                } else if (x < 66.0f && y > 100.0f && y < 180.0f) {
                    G.level = (G.level + G.nlevels - 1) % G.nlevels;
                    { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.5f); }
                } else if (x > CW - 66.0f && y > 100.0f && y < 180.0f) {
                    G.level = (G.level + 1) % G.nlevels;
                    { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.5f); }
                } else if (x < 160.0f && y > CH - 32.0f) {
                    G.practice = !G.practice;
                    save_best();
                    plat_log("menu: practice -> %d", G.practice);
                    plat_log_flush();
                    { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.5f); }
                } else if (x > 80.0f && x < CW - 80.0f && y > 60.0f && y < 200.0f) {
                    start_level(G.level);
                }
            }
        } else if (G.state == ST_SETTINGS) {
            if (settings_reset_arm > 0.0f) {
                settings_reset_arm -= dt;
                if (settings_reset_arm < 0.0f) settings_reset_arm = 0.0f;
            }
            if (touch_press) {
                int row = -1;
                for (int i = 0; i < SETTINGS_ROWS; i++) {
                    float rx, ry, rw, rh;
                    settings_row_rect(i, &rx, &ry, &rw, &rh);
                    if (press_x >= rx && press_x <= rx + rw &&
                        press_y >= ry && press_y <= ry + rh) { row = i; break; }
                }
                G.settings_row = row;
                if (row == 0) {
                    G.practice = !G.practice;
                    { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.5f); }
                    save_best();
                } else if (row == 1) {
                    G.music_on = !G.music_on;
                    { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.5f); }
                    save_best();
                } else if (row == 2) {
                    G.sound_on = !G.sound_on;
                    { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.5f); }
                    save_best();
                } else if (row == 3) {
                    if (settings_reset_arm > 0.0f) {
                        memset(G.best, 0, sizeof G.best);
                        memset(G.attempts, 0, sizeof G.attempts);
                        G.attempt = 0;
                        settings_reset_arm = 0;
                        save_best();
                    } else {
                        settings_reset_arm = 2.5f;
                    }
                    { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.5f); }
                } else if (row == 4) {
                    G.state = ST_MENU;
                    G.state_t = 0;
                    settings_reset_arm = 0;
                    { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.5f); }
                }
            }
        } else if (G.state == ST_PLAY) {
            update_play(dt, touch);
        } else if (G.state == ST_PAUSED) {
            if (touch_press) {
                float x = press_x, y = press_y;
                int row = -1;
                if (x > CW - 60.0f && y < 46.0f) {
                    row = 0;                     /* the pause icon resumes too */
                } else {
                    for (int i = 0; i < 4; i++) {
                        float rx, ry, rw, rh;
                        pause_row_rect(i, &rx, &ry, &rw, &rh);
                        if (x >= rx && x <= rx + rw && y >= ry && y <= ry + rh) {
                            row = i; break;
                        }
                    }
                }
                if (row == 0) {
                    G.state = ST_PLAY;
                    G.state_t = 0;
                    { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.5f); }
                } else if (row == 1) {
                    start_level(G.level);
                } else if (row == 2) {
                    G.practice = !G.practice;
                    save_best();
                    plat_log("pause: practice -> %d", G.practice);
                    plat_log_flush();
                    { if (G.sound_on) plat_sound(GD_SND_CLICK, 0.5f); }
                } else if (row == 3) {
                    G.state = ST_MENU;
                    G.state_t = 0;
                    plat_music_stop();
                }
            }
        } else if (G.state == ST_DEAD) {
            /* the original restarts on its own; a tap just cuts the wait short */
            if (G.state_t > 0.85f || (G.state_t > 0.30f && down))
                start_level(G.level);
        } else if (G.state == ST_DONE) {
            if (G.state_t > 0.2f && down) {
                G.state = ST_MENU;
                plat_music_stop();
            }
        }

        /* cosmetic timers run in every state */
        if (G.flash > 0.0f) {
            G.flash -= dt;
            if (G.flash < 0.0f) G.flash = 0.0f;
        }
        if (G.state == ST_DEAD || G.state == ST_DONE) {
            for (int i = 0; i < G.nshards; i++) {
                if (G.shard[i].life <= 0.0f) continue;
                G.shard[i].x += G.shard[i].vx * dt;
                G.shard[i].y += G.shard[i].vy * dt;
                G.shard[i].vy -= 700.0f * dt;
                G.shard[i].life -= dt;
            }
        }
        touch_press = 0;
    }

    GDLevel *l = &G.levels[G.level];
    float r = l->bg[0] / 255.f, g = l->bg[1] / 255.f, b = l->bg[2] / 255.f;
    gfx_begin_frame((int)G.panel_w, (int)G.panel_h, CW, CH, 1, r, g, b);
    spr_frame_begin();
    if (G.state == ST_MENU) draw_menu();
    else if (G.state == ST_SETTINGS) draw_settings();
    else if (G.state == ST_DEAD) draw_dead();
    else if (G.state == ST_DONE) draw_done();
    else if (G.state == ST_PAUSED) draw_paused();
    else { draw_world(); draw_hud(); }
    gfx_end_frame();
}

void game_redraw(void) { spr_frame_replay(); }

void game_gl_lost(void) { gfx_forget_all(); }

uint32_t game_bg_color(void) { return rgb(G.levels[G.level].bg[0], G.levels[G.level].bg[1], G.levels[G.level].bg[2]); }

int game_wants_exit(void) { return 0; }
