/* iBeer — a nano port of Hottrix's iBeer 4.1.
 *
 * The original drives a 45x45 GL fluid mesh from the accelerometer; this port
 * reproduces the same behaviour with a cheaper model: the liquid is a stack of
 * wall-following trapezoid bands textured with the original's Condensation
 * gradient, the surface is a plane tilted by the filtered accelerometer roll,
 * and the head is the original's 66-frame foam strip animation.
 *
 * Coordinates are logical pixels, origin top-left, y down. 240x360 (2:3, as the
 * original's 320x480 screen).
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gfx.h"
#include "sprites.h"
#include "platform.h"
#include "game.h"

#define CW 240.0f
#define CH 432.0f          /* full panel: the beer IS the screen */

#define GLASS_CX (CW * 0.5f)
#define LIQ_TOP 0.0f
#define LIQ_BOT CH
#define LIQ_H CH

#define MAX_TILT 1.40f          /* ~80 deg; past this the glass cannot hold liquid */
#define POUR_ANGLE 0.16f        /* extra tilt past the rim before it streams */
#define POUR_RATE 0.55f         /* glass-heights per second at full pour */
#define LEVEL_MIN 0.02f

#define ROWS 8
#define FOAM_H 38.0f        /* height of the foam head */
#define FOAM_DEPTH 9.0f     /* how far the head dips into the beer */

IBGame G;

static float surface_for(float level);

/* ------------------------------------------------------------------ drinks */

static const IBDrink DRINKS[IB_MAX_DRINKS] = {
    {"Beer",            {196, 152,  62}, {252, 244, 214}, 0.55, 0},
    {"Lite Beer",       {206, 176, 110}, {250, 246, 224}, 0.65, 0},
    {"Gudness",         {116,  82,  50}, {232, 220, 196}, 0.35, 0},
    {"Amber Weizen",    {198, 126,  54}, {250, 240, 208}, 0.70, 0},
    {"Mouthwash",       {118, 178,  92}, {226, 246, 214}, 0.60, 0},
    {"Milk",            {236, 230, 214}, {252, 250, 244}, 0.10, 0},
    {"Chocolate Milk",  {126,  80,  50}, {214, 190, 166}, 0.15, 0},
    {"Strawberry Milk", {228, 150, 140}, {250, 224, 216}, 0.15, 0},
    {"Alien Milk",      {116, 202, 108}, {214, 246, 200}, 0.30, 0},
    {"Cola Soda",       { 82,  42,  30}, {226, 196, 158}, 0.90, 0},
    {"Lime Soda",       {150, 192, 128}, {236, 248, 220}, 0.85, 0},
    {"Water",           {168, 198, 220}, {238, 246, 250}, 0.25, 0},
    {"Energy Soda",     {204, 132,  50}, {250, 236, 200}, 0.80, 0},
    {"Red Wine",        {126,  46,  60}, {226, 176, 180}, 0.20, 0},
    {"Rose Wine",       {198, 122, 132}, {250, 214, 218}, 0.20, 0},
    {"White Wine",      {214, 202, 158}, {250, 246, 224}, 0.35, 0},
    {"Champagne",       {224, 200, 128}, {252, 248, 226}, 0.95, 0},
    {"Coffee",          {108,  72,  46}, {214, 186, 158}, 0.20, 1},
};

/* ------------------------------------------------------------------ font */

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
        size != 16 + 16u * fcount) { free(d); return -1; }
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
        if (!px || psz != 256u * 128u * 4u) { free(px); return -1; }
        fpage[i] = gfx_tex_create(256, 128, GFX_FMT_RGBA8888, px, 1, 0);
        free(px);
        if (!fpage[i]) return -1;
    }
    return 0;
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
    if (!fg) return;
    /* a soft dark shadow/offset so text stays readable on beer and wood */
    {
        float ox = 0.9f, oy = 1.1f;
        float cx = x;
        gfx_set_blend(GFX_BLEND_ALPHA);
        for (const char *p = s; *p; p++) {
            int c = (unsigned char)*p;
            if (c < ffirst || c >= ffirst + fcount) { cx += 8 * scale; continue; }
            const Glyph *g = &fg[c - ffirst];
            if (g->w && g->h && g->page < fnpages && fpage[g->page]) {
                float px = cx + g->xo * scale + ox, py = y + g->yo * scale + oy;
                float gw = g->w * scale, gh = g->h * scale;
                float xy[8] = {px, py, px + gw, py, px + gw, py + gh, px, py + gh};
                float uv[8] = {g->x, g->y, g->x + g->w, g->y,
                               g->x + g->w, g->y + g->h, g->x, g->y + g->h};
                gfx_quad(fpage[g->page], 256, 128, xy, uv, 0xB0000000u);
            }
            cx += g->adv * scale;
        }
    }
    float cx = x;
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

static void draw_text_c(const char *s, float cx, float y, float scale, uint32_t argb)
{
    draw_text(s, cx - text_width(s, scale) * 0.5f, y, scale, argb);
}

/* ------------------------------------------------------------------ helpers */

static uint32_t rgb(int r, int g, int b, int a)
{
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    a = a < 0 ? 0 : a > 255 ? 255 : a;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* no glass to constrain the liquid: the interior is the whole canvas */
static float half_w(float y)
{
    (void)y;
    return CW * 0.5f;
}

static void quad(float x0, float y0, float x1, float y1, uint32_t argb)
{
    float xy[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
    float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    gfx_quad(0, 1, 1, xy, uv, argb);
}

static void sprite(int id, float x0, float y0, float x1, float y1, float alpha, uint32_t tint)
{
    if (id < 0 || alpha <= 0.f) return;
    float xy[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
    if (tint == 0xFFFFFFu) spr_quad(id, xy, alpha);
    else spr_quad_color(id, xy, alpha, tint);
}

/* ------------------------------------------------------------------ init */

int game_init(float w, float h)
{
    memset(&G, 0, sizeof G);
    G.panel_w = w;
    G.panel_h = h;
    G.level = 0.86f;
    G.state = 0;
    G.drink = 0;
    G.surface_y = surface_for(G.level);
    G.condensation = 0.35f;

    if (sprites_load()) {
        plat_log("iBeer: sprites missing");
        return -1;
    }
    if (font_load()) plat_log("iBeer: font missing, text disabled");

    uint32_t size = 0;
    uint8_t *d = plat_read_file("bg.bin", &size, 0);
    if (!d || size != 256u * 128u * 4u) { free(d); plat_log("iBeer: bg missing"); return -1; }
    G.bg_tex = gfx_tex_create(256, 128, GFX_FMT_RGBA8888, d, 1, 0);
    free(d);
    if (!G.bg_tex) return -1;

    for (int i = 0; i < 16; i++) {
        char n[16];
        snprintf(n, sizeof n, "foam_%02d", i);
        G.spr_foam[i] = spr_find(n);
        if (G.spr_foam[i] < 0) break;
        G.foam_count = i + 1;
    }
    for (int i = 0; i < 8; i++) {
        char n[16];
        snprintf(n, sizeof n, "soda_%02d", i);
        G.spr_soda[i] = spr_find(n);
        if (G.spr_soda[i] < 0) break;
        G.soda_count = i + 1;
    }
    G.spr_bubble = spr_find("bubble");
    G.spr_steam = spr_find("steam");
    G.spr_cond = spr_find("cond");
    G.spr_liquid = spr_find("liquid");
    G.spr_halo = spr_find("halo");
    G.spr_halo_m = spr_find("halo_m");
    G.spr_halo_s = spr_find("halo_s");
    G.spr_glow = spr_find("glow");
    G.spr_hotglow = spr_find("hotglow");
    G.spr_dirty = spr_find("dirty");
    G.spr_white = spr_find("white");
    G.spr_egg = spr_find("egg");
    G.spr_btn_menu = spr_find("btn_menu");
    G.spr_btn_done = spr_find("btn_done");
    G.spr_btn_ok = spr_find("btn_ok");
    G.spr_btn_refill = spr_find("btn_refill");
    for (int i = 0; i < IB_MAX_DRINKS; i++) {
        char n[16];
        snprintf(n, sizeof n, "drink_%02d", i);
        G.spr_drink[i] = spr_find(n);
    }

    /* the frost and bubble textures' atlas pieces */
    {
        int page, px, py, pw, ph;
        if (G.spr_cond >= 0 && spr_piece_rect(G.spr_cond, &page, &px, &py, &pw, &ph) == 0) {
            G.cond_px = px; G.cond_py = py; G.cond_pw = pw; G.cond_ph = ph;
        }
        if (G.spr_liquid >= 0 && spr_piece_rect(G.spr_liquid, &page, &px, &py, &pw, &ph) == 0) {
            G.liq_px = px; G.liq_py = py; G.liq_pw = pw; G.liq_ph = ph;
        }
    }

    /* restore the last drink + level (16-byte record) */
    uint32_t ssize = 0;
    uint8_t *sd = plat_read_file("save.bin", &ssize, 1);
    if (sd && ssize >= 4) {
        int dr = sd[0];
        int lv = sd[1];
        if (dr >= 0 && dr < IB_MAX_DRINKS) G.drink = dr;
        if (lv > 0 && lv <= 100) G.level = (float)lv / 100.f;
        free(sd);
    }
    plat_log("iBeer: ready drink=%d level=%.2f foam=%d soda=%d cond=%d liq=%d pages=%d",
             G.drink, G.level, G.foam_count, G.soda_count, G.spr_cond, G.spr_liquid,
             sprites_pages());
    return 0;
}

static void save_state(void)
{
    uint8_t buf[16];
    memset(buf, 0, sizeof buf);
    buf[0] = (uint8_t)G.drink;
    buf[1] = (uint8_t)(G.level * 100.f + 0.5f);
    plat_write_file("save.bin", buf, sizeof buf);
}

/* ------------------------------------------------------------------ physics */

static void spawn_bubble(float x, float y, float r)
{
    if (G.nbubble >= IB_MAX_BUBBLES) return;
    IBParticle *p = &G.bubble[G.nbubble++];
    p->x = x; p->y = y; p->r = r;
    p->vy = -(14.0f + 26.0f * (float)rand() / (float)RAND_MAX);
    p->vx = 0.f;
    p->life = 1.f;
}

static void spawn_steam(void)
{
    if (G.nsteam >= IB_MAX_STEAM) return;
    IBParticle *p = &G.steam[G.nsteam++];
    float hw = half_w(G.surface_y);
    p->x = GLASS_CX + (float)(rand() % 60 - 30);
    p->y = G.surface_y - 6.0f;
    p->r = 5.0f + 4.0f * (float)rand() / (float)RAND_MAX;
    p->vy = -(10.0f + 10.0f * (float)rand() / (float)RAND_MAX);
    p->vx = (float)(rand() % 8 - 4) * 0.4f;
    p->life = 1.f;
    (void)hw;
}

static void spawn_drip(void)
{
    if (G.ndrip >= IB_MAX_DRIPS) return;
    IBDrip *d = &G.drip[G.ndrip++];
    d->x = (float)(rand() % (int)CW);
    d->y = 20.0f + (float)(rand() % 30);
    d->vy = 6.0f;
    d->len = 3.0f;
    d->life = 1.f;
}

static void update(float dt)
{
    G.time += dt;
    if (G.burp_cd > 0.f) G.burp_cd -= dt;
    if (G.shatter > 0.f) G.shatter -= dt;

    /* --- tilt from the accelerometer, low-pass filtered as in the original --- */
    int32_t a[3];
    if (plat_accel(a)) {
        float x = (float)a[0], y = (float)a[1], z = (float)a[2];
        G.ax = G.ax * 0.95f + x * 0.05f;
        G.ay = G.ay * 0.95f + y * 0.05f;
        G.az = G.az * 0.90f + z * 0.10f;
        float dx = G.ax - G.pax, dy = G.ay - G.pay, dz = G.az - G.paz;
        float jerk = sqrtf(dx * dx + dy * dy + dz * dz);
        G.pax = G.ax; G.pay = G.ay; G.paz = G.az;
        if (jerk > 900.f) G.shake += (jerk - 900.f) * 0.02f;
        G.accel_ok = 1;
    }
    G.shake *= 0.90f;
    /* shaking froths the beer up — the original's party trick */
    G.foam_boost += G.shake * dt * 1.6f;
    if (G.foam_boost > 1.f) G.foam_boost = 1.f;
    G.foam_boost -= dt * 0.22f;
    if (G.foam_boost < 0.f) G.foam_boost = 0.f;
    if (G.foam_cd > 0.f) G.foam_cd -= dt;
    if (G.foam_boost > 0.5f && G.foam_cd <= 0.f) {
        plat_sound(IB_SND_FIZZ, 0.5f);
        G.foam_cd = 3.0f;
    }
    if (G.shake > 1.35f && G.burp_cd <= 0.f) {
        plat_sound(IB_SND_BURP, 0.9f);
        G.burp_cd = 2.6f;
        G.shake = 0.f;
        G.slosh_v += 3.2f;
    }

    /* roll about the screen axis. The nano's X reports the opposite sign to the
     * iPhone's input; negate to match the original's "ax = 1000*sin(phi)"
     * convention. (See ports/doodlejump and ports/templerun.) */
    float s = -G.ax / 1000.f;
    if (s > 1.f) s = 1.f;
    if (s < -1.f) s = -1.f;
    G.tilt_target = asinf(s) * 1.35f;
    if (G.tilt_target > MAX_TILT) G.tilt_target = MAX_TILT;
    if (G.tilt_target < -MAX_TILT) G.tilt_target = -MAX_TILT;

    /* the surface lags and rings like liquid in a glass */
    float k = 9.0f, damp = 4.2f;
    G.tilt_v += ((G.tilt_target - G.tilt) * k - G.tilt_v * damp) * dt;
    G.tilt += G.tilt_v * dt;

    /* a travelling wave along the surface, excited by touch and burps */
    G.slosh_v += (-G.slosh * 6.0f - G.slosh_v * 2.4f) * dt;
    G.slosh += G.slosh_v * dt;

    /* --- pouring --- */
    float ys = surface_for(G.level);
    float tt = tanf(G.tilt);
    /* pour when the surface line reaches the top edge of the screen —
     * i.e. you are tipping the phone into your mouth */
    float y_high = ys - fabsf(half_w(ys) * tt);
    float over = -y_high;
    int pouring = over > 0.f && G.level > LEVEL_MIN;
    if (pouring) {
        float rate = POUR_RATE * (0.35f + over / 40.f);
        if (rate > 1.4f) rate = 1.4f;
        G.level -= rate * dt;
        if (G.level <= LEVEL_MIN) {
            G.level = LEVEL_MIN;
            if (!G.cracked) {   /* the last drop is gone: the glass shatters */
                G.cracked = 1;
                G.shatter = 2.6f;
                plat_sound(IB_SND_BREAK, 0.9f);
            }
        }
        G.pour = rate;
        G.condensation += dt * 0.55f;
        if (G.condensation > 1.f) G.condensation = 1.f;
        /* the head slides down the slope as it drains */
        if (!G.pouring) plat_sound(IB_SND_POUR, 0.8f);
        G.pouring = 1;
    } else {
        G.pouring = 0;
        G.pour *= 0.8f;
    }
    G.surface_y = surface_for(G.level);

    /* --- foam animation --- */
    float fps = pouring ? 22.f : 10.f;
    G.foam_t += dt * fps;
    while (G.foam_t >= 1.f) {
        G.foam_t -= 1.f;
        G.foam_frame++;
    }

    /* --- fizz --- */
    const IBDrink *dk = &DRINKS[G.drink];
    G.fizz_t += dt * (0.4f + dk->fizz * 2.2f) * (1.f + G.foam_boost * 2.5f);
    if (G.fizz_t >= 1.f) {
        G.fizz_t -= 1.f;
        if (G.level > LEVEL_MIN) {
            float hw = half_w(G.surface_y + 6.f);
            float x = GLASS_CX + (float)(rand() % 100 - 50) * 0.8f;
            if (x < GLASS_CX - hw + 3.f) x = GLASS_CX - hw + 3.f;
            if (x > GLASS_CX + hw - 3.f) x = GLASS_CX + hw - 3.f;
            spawn_bubble(x, LIQ_BOT - 4.f, 2.4f + 3.2f * (float)rand() / (float)RAND_MAX);
        }
    }

    /* --- steam --- */
    if (dk->hot && G.level > LEVEL_MIN && (rand() % 6) == 0) spawn_steam();

    /* --- condensation drips --- */
    if (G.condensation > 0.45f && (rand() % 90) == 0) spawn_drip();

    /* --- integrate particles --- */
    for (int i = 0; i < G.nbubble; i++) {
        IBParticle *p = &G.bubble[i];
        p->y += p->vy * dt;
        p->x += p->vx * dt + sinf(G.time * 5.f + p->y * 0.15f) * 6.f * dt;
        p->vy -= 12.f * dt;
        if (p->vy < -46.f) p->vy = -46.f;
        if (p->y < G.surface_y + 14.f) p->life -= dt * 5.f;
        if (p->life <= 0.f || p->y < LIQ_TOP) { G.bubble[i] = G.bubble[--G.nbubble]; i--; }
    }
    for (int i = 0; i < G.nsteam; i++) {
        IBParticle *p = &G.steam[i];
        p->y += p->vy * dt;
        p->x += p->vx * dt + sinf(G.time * 1.7f + (float)i) * 8.f * dt;
        p->r += dt * 5.f;
        p->life -= dt * 0.42f;
        if (p->life <= 0.f) { G.steam[i] = G.steam[--G.nsteam]; i--; }
    }
    for (int i = 0; i < G.ndrip; i++) {
        IBDrip *d = &G.drip[i];
        d->y += d->vy * dt;
        d->vy += 26.f * dt;
        d->len = 3.0f + (d->vy * 0.12f);
        if (d->len > 12.f) d->len = 12.f;
        if (d->y > CH - 6.f) { G.drip[i] = G.drip[--G.ndrip]; i--; }
    }
}

/* ------------------------------------------------------------------ drawing */


/* premultiplied colour from a channel triple */
static uint32_t pm3(const uint8_t *c, float f, int a)
{
    int r = (int)(c[0] * f), g = (int)(c[1] * f), b = (int)(c[2] * f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return rgb(r * a / 255, g * a / 255, b * a / 255, a);
}

/* surface height for a fill level: a full glass puts the head at the top edge */
static float surface_for(float level)
{
    if (level > 1.f) level = 1.f;
    if (level < 0.f) level = 0.f;
    return CH - level * (CH - FOAM_H);
}

/* the liquid surface height at x: a plane tilted by the roll plus a wave */
static float surf_y(float x, float ys, float tt, float wave)
{
    return ys - tt * (x - GLASS_CX) + wave * sinf(G.time * 2.6f + x * 0.021f);
}

/* a soft blob from the original's glow art */
static void blob(int id, float cx, float cy, float rx, float ry, float a, uint32_t tint)
{
    if (id < 0 || a <= 0.f) return;
    sprite(id, cx - rx, cy - ry, cx + rx, cy + ry, a, tint);
}

static void draw_liquid(void)
{
    if (G.level <= LEVEL_MIN) return;
    const IBDrink *dk = &DRINKS[G.drink];
    float ys = G.surface_y;
    float tt = tanf(G.tilt);
    float wave = G.slosh * 2.6f;

    /* the body: wall-following bands, each sampling the beer field so the
     * tinted grain runs down the whole screen with no seams */
    const int B = 40;
    for (int j = 0; j < B; j++) {
        float t0 = (float)j / B, t1 = (float)(j + 1) / B;
        float yb = ys + (LIQ_BOT - ys) * t1;
        float yl = (j == 0) ? surf_y(0.f, ys, tt, wave) : ys + (LIQ_BOT - ys) * t0;
        float yr = (j == 0) ? surf_y(CW, ys, tt, wave) : yl;
        if (yl > yb) yl = yb;
        if (yr > yb) yr = yb;
        if (yl >= yb && yr >= yb) continue;

        /* depth: a bright shoulder where the light comes through, deep below */
        float d = t0;
        float f = 1.10f - 0.46f * d - 0.26f * d * d;
        float xy[8] = {0.f, yl, CW, yr, CW, yb, 0.f, yb};
        if (G.spr_liquid >= 0 && G.liq_pw > 0) {
            float x0 = (float)G.liq_px, y0 = (float)G.liq_py;
            float pw = (float)G.liq_pw, ph = (float)G.liq_ph;
            float uv[8] = {x0, y0 + (yl / CH) * ph, x0 + pw, y0 + (yr / CH) * ph,
                           x0 + pw, y0 + (yb / CH) * ph, x0, y0 + (yb / CH) * ph};
            spr_quad_uv(G.spr_liquid, xy, uv, 1.f, pm3(dk->rgb, f, 255));
        } else {
            float no_uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            gfx_set_blend(GFX_BLEND_ALPHA);
            gfx_quad(0, 1, 1, xy, no_uv, pm3(dk->rgb, f, 255));
        }
    }

    /* light pooling under the surface, as if a lamp is behind the drink */

    /* (no separate meniscus line: the foam head IS the surface line) */
}

static void draw_foam(void)
{
    if (G.level <= LEVEL_MIN) return;
    const IBDrink *dk = &DRINKS[G.drink];
    int soda = dk->fizz > 0.55f && G.soda_count > 0;
    int n = soda ? G.soda_count : G.foam_count;
    if (n <= 0) return;
    int base = G.foam_frame % n;
    uint32_t tint = pm3(dk->foam, 1.02f, 255);

    float ys = G.surface_y;
    float tt = tanf(G.tilt);
    float wave = G.slosh * 2.6f;

    /* two tiles of the original's foam strip across the panel; the second
     * samples a different frame so the join is not a repeat */
    const float fh = FOAM_H * (1.f + 0.85f * G.foam_boost);
    const float tw = CW * 0.5f;
    for (int i = 0; i < 2; i++) {
        int fi = (base + (i ? n / 2 : 0)) % n;
        int id = soda ? G.spr_soda[fi] : G.spr_foam[fi];
        int page, px, py, pw, ph;
        if (id < 0 || spr_piece_rect(id, &page, &px, &py, &pw, &ph) != 0) continue;
        float x0 = i * tw, x1 = x0 + tw;
        float s0 = surf_y(x0, ys, tt, wave), s1 = surf_y(x1, ys, tt, wave);
        float xy[8] = {x0, s0 - fh, x1, s1 - fh,
                       x1, s1 + FOAM_DEPTH, x0, s0 + FOAM_DEPTH};
        float uv[8] = {(float)px, (float)py, (float)px + pw, (float)py,
                       (float)px + pw, (float)py + ph, (float)px, (float)py + ph};
        spr_quad_uv(id, xy, uv, 1.f, tint);
    }

    /* a crown of froth so the head is not a straight cut */
    uint32_t froth = pm3(dk->foam, 1.f, 235);
    for (int i = 0; i < 8; i++) {
        float x = 14.f + i * (CW - 28.f) / 7.f + 5.f * sinf(G.time * 1.1f + i * 1.9f);
        float s = surf_y(x, ys, tt, wave);
        float r = (8.f + 3.5f * sinf(i * 2.1f + G.time * 0.8f)) * (1.f + G.foam_boost * 0.8f);
        blob(G.spr_glow, x, s - fh + 10.f, r * 1.35f, r * 0.8f, 0.5f + 0.3f * G.foam_boost, froth);
    }
    /* a soft shadow under the head so it sits *on* the beer */
    if (G.spr_glow >= 0) {
        float sy0 = surf_y(0.f, ys, tt, wave) + FOAM_DEPTH, sy1 = surf_y(CW, ys, tt, wave) + FOAM_DEPTH;
        float xy[8] = {0.f, sy0, CW, sy1, CW, sy1 + 13.f, 0.f, sy0 + 13.f};
        gfx_set_blend(GFX_BLEND_ALPHA);
        spr_quad_color(G.spr_glow, xy, 0.55f, pm3(dk->rgb, 0.42f, 200));
    }

    /* froth licking down into the beer */
    for (int i = 0; i < 6; i++) {
        float x = 20.f + i * (CW - 40.f) / 5.f + 4.f * sinf(G.time * 0.9f + i * 2.6f);
        float s = surf_y(x, ys, tt, wave);
        float r = 6.f + 3.f * sinf(i * 1.7f);
        blob(G.spr_glow, x, s + 4.f, r * 1.25f, r * 0.75f, 0.38f, froth);
    }
}

/* foam and beer spilling over the top edge as you tip it to drink */
static void draw_spill(void)
{
    if (!G.pouring) return;
    const IBDrink *dk = &DRINKS[G.drink];
    float tt = tanf(G.tilt);
    if (fabsf(tt) < 0.05f) return;
    /* where the surface line crosses the top edge of the panel */
    float xc = GLASS_CX + G.surface_y / tt;
    if (xc < -30.f || xc > CW + 30.f) return;
    uint32_t froth = pm3(dk->foam, 1.f, 240);
    for (int i = 0; i < 5; i++) {
        float ph = fmodf(G.time * 1.5f + i * 0.21f, 1.f);
        float y = 20.f * (1.f - ph) - 6.f;
        float x = xc + sinf(i * 1.3f + G.time * 1.4f) * 13.f;
        blob(G.spr_glow, x, y, 9.f + 5.f * (1.f - ph), 6.f, 0.8f * (1.f - ph), froth);
    }
    /* the beer tongue right at the lip */
    float xy[8] = {xc - 16.f, 0.f, xc + 16.f, 0.f, xc + 12.f, 9.f, xc - 12.f, 9.f};
    float no_uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    gfx_set_blend(GFX_BLEND_ALPHA);
    gfx_quad(0, 1, 1, xy, no_uv, pm3(dk->rgb, 1.25f, 150));
}

static void draw_bubbles(void)
{
    for (int i = 0; i < G.nbubble; i++) {
        IBParticle *p = &G.bubble[i];
        float r = p->r;
        float a = p->life > 0.6f ? 1.f : p->life / 0.6f;
        sprite(G.spr_bubble, p->x - r, p->y - r, p->x + r, p->y + r, a, 0xFFFFFFFFu);
    }
}

static void draw_steam(void)
{
    for (int i = 0; i < G.nsteam; i++) {
        IBParticle *p = &G.steam[i];
        float r = p->r;
        float a = p->life * 0.5f;
        sprite(G.spr_steam, p->x - r, p->y - r, p->x + r, p->y + r, a, 0xFFFFFFFFu);
    }
}

static void draw_condensation(void)
{
    if (G.condensation < 0.05f) return;
    /* wet spots on the "glass": a fixed scatter of soft sheen drops,
     * grown and brightened by the condensation level */
    int n = (int)(G.condensation * 22.f);
    float a = 0.30f + G.condensation * 0.40f;
    for (int i = 0; i < n; i++) {
        float hx = sinf(1234.5f + i * 12.9898f) * 43758.5453f;
        float hy = sinf(1234.5f + i * 78.233f) * 28001.8384f;
        hx -= floorf(hx);
        hy -= floorf(hy);
        float x = 8.f + hx * (CW - 16.f);
        float y = 8.f + hy * (CH - 16.f);
        float r = 1.2f + (sinf(i * 3.7f + 9.f) * 0.5f + 0.5f) * 3.6f;
        /* tiny crisp droplets, like water beading on glass */
        sprite(G.spr_bubble, x - r, y - r, x + r, y + r, a, 0xFFFFFFFFu);
    }
    for (int i = 0; i < G.ndrip; i++) {
        IBDrip *d = &G.drip[i];
        /* a soft wet streak running down, not a hard white bar */
        sprite(G.spr_glow, d->x - 2.6f, d->y - d->len * 2.4f, d->x + 2.6f, d->y,
               0.55f, rgb(205, 222, 238, 130));
    }
}

/* the glass breaking when the last drop pours out: the original's shattered
 * glass overlay, stretched over the glass area and fading slowly */
static void draw_shatter(void)
{
    if (G.shatter <= 0.f || G.spr_egg < 0) return;
    float a = G.shatter > 1.6f ? 1.f : G.shatter / 1.6f;
    gfx_set_blend(GFX_BLEND_ALPHA);
    sprite(G.spr_egg, 0.f, 0.f, CW, CH, a, 0xFFFFFFFFu);
}


/* Bmenu/Bdone/Bok have the legend baked into the art; BbarB does not. */
static void draw_button(int spr, const char *label, float x, float y, float w, float h,
                        uint32_t tint, uint32_t text, int baked_legend)
{
    if (spr >= 0) {
        sprite(spr, x, y, x + w, y + h, 1.0f, tint);
    } else {
        float xy[8] = {x, y, x + w, y, x + w, y + h, x, y + h};
        float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        gfx_set_blend(GFX_BLEND_ALPHA);
        gfx_quad(0, 1, 1, xy, uv, tint);
    }
    if (baked_legend) return;
    float scale = 0.62f;
    draw_text_c(label, x + w * 0.5f, y + h * 0.5f - 9.f * scale, scale, text);
}

static void draw_ui(void)
{
    const IBDrink *dk = &DRINKS[G.drink];

    /* top-left menu, top-right refill, small drink tag at the bottom */
    draw_button(G.spr_btn_menu, "MENU", 8.f, 12.f, 62.f, 22.f, rgb(210, 178, 120, 255),
                rgb(40, 26, 12, 255), 1);
    draw_button(G.spr_btn_refill, "REFILL", CW - 70.f, 12.f, 62.f, 22.f,
                rgb(210, 178, 120, 255), rgb(40, 26, 12, 255), 0);

    /* the beer IS the level indicator — just name the drink quietly */
    draw_text_c(dk->name, CW * 0.5f, CH - 22.f, 0.55f, rgb(255, 248, 230, 205));

    if (G.accel_ok == 0) {
        draw_text_c("no tilt sensor", CW * 0.5f, 42.f, 0.5f, rgb(255, 220, 200, 160));
    }
}

static void draw_menu(void)
{
    /* dim the play field */
    float xy[8] = {0, 0, CW, 0, CW, CH, 0, CH};
    float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    gfx_set_blend(GFX_BLEND_ALPHA);
    gfx_quad(0, 1, 1, xy, uv, rgb(8, 5, 3, 190));

    draw_text_c("SELECT A DRINK", CW * 0.5f, 26.f, 0.8f, rgb(255, 246, 224, 240));

    const int cols = 3;
    const float cw = 72.f, ch = 54.f;
    const float x0 = (CW - cols * cw) * 0.5f;
    const float y0 = 56.f;
    for (int i = 0; i < IB_MAX_DRINKS; i++) {
        int cx = i % cols, cy = i / cols;
        float x = x0 + cx * cw, y = y0 + cy * ch;
        if (y + ch > CH - 46.f) break;
        int sel = i == G.drink;
        float bxy[8] = {x + 2.f, y + 2.f, x + cw - 2.f, y + 2.f,
                        x + cw - 2.f, y + ch - 2.f, x + 2.f, y + ch - 2.f};
        gfx_set_blend(GFX_BLEND_ALPHA);
        gfx_quad(0, 1, 1, bxy, uv, sel ? rgb(250, 224, 160, 235) : rgb(52, 34, 20, 235));
        if (G.spr_drink[i] >= 0)
            sprite(G.spr_drink[i], x + cw * 0.5f - 23.f, y + 6.f,
                   x + cw * 0.5f + 23.f, y + 26.f, 1.f, 0xFFFFFFu);
        draw_text_c(DRINKS[i].name, x + cw * 0.5f, y + 30.f, 0.40f,
                    sel ? rgb(70, 40, 10, 255) : rgb(255, 240, 214, 235));
    }
    draw_button(G.spr_btn_done, "DONE", CW * 0.5f - 40.f, CH - 38.f, 80.f, 24.f,
                rgb(210, 178, 120, 255), rgb(40, 26, 12, 255), 1);
}

static void draw(void)
{
    G.frame++;
    spr_frame_begin();

    /* background */
    if (G.bg_tex) {
        float xy[8] = {0, 0, CW, 0, CW, CH, 0, CH};
        float uv[8] = {0, 0, 256, 0, 256, 128, 0, 128};
        gfx_set_blend(GFX_BLEND_NONE);
        gfx_quad(G.bg_tex, 256, 128, xy, uv, 0xFFFFFFFFu);
    }

    const char *sk = getenv("IB_SKIP");
#define SKIP(n) (sk && strchr(sk, n))
    if (!SKIP('l')) draw_liquid();
    if (!SKIP('b')) draw_bubbles();
    if (!SKIP('f')) draw_foam();
    if (!SKIP('p')) draw_spill();
    if (!SKIP('s')) draw_steam();
    if (!SKIP('c')) draw_condensation();
    draw_shatter();
#undef SKIP

    if (G.state == 1) draw_menu();
    else draw_ui();
}

/* ------------------------------------------------------------------ input */

static void handle_touch(const PlatTouches *t, int down_edge)
{
    if (!down_edge || t->count < 1) return;
    float x = t->pt[0].x, y = t->pt[0].y;

    if (G.state == 1) {
        /* menu: drink grid or DONE */
        if (y > CH - 50.f && y < CH - 6.f && x > CW * 0.5f - 50.f && x < CW * 0.5f + 50.f) {
            G.state = 0;
            plat_sound(IB_SND_CLICK, 0.7f);
            return;
        }
        const int cols = 3;
        const float cw = 72.f, ch = 54.f;
        const float x0 = (CW - cols * cw) * 0.5f;
        const float y0 = 56.f;
        if (x < x0 || y < y0) return;
        int cx = (int)((x - x0) / cw), cy = (int)((y - y0) / ch);
        if (cx < 0 || cx >= cols) return;
        int i = cy * cols + cx;
        if (i >= 0 && i < IB_MAX_DRINKS && y0 + cy * ch + ch <= CH - 46.f) {
            G.drink = i;
            G.level = 0.92f;
            G.cracked = 0;
            G.shatter = 0.f;
            G.foam_frame = 0;
            G.foam_boost = 0.f;
            G.foam_cd = 0.f;
            G.slosh_v += 2.0f;
            G.state = 0;
            save_state();
            plat_sound(IB_SND_BLIP, 0.8f);
        }
        return;
    }

    /* top bar buttons */
    if (y > 6.f && y < 40.f) {
        if (x < 74.f) {
            G.state = 1;
            plat_sound(IB_SND_CLICK, 0.7f);
            return;
        }
        if (x > CW - 74.f) {
            G.level = 0.92f;
            G.cracked = 0;
            G.shatter = 0.f;
            G.foam_boost = 0.f;
            G.foam_cd = 0.f;
            G.slosh_v += 2.4f;
            G.condensation = 0.2f;
            plat_sound(IB_SND_FIZZ, 0.7f);
            save_state();
            return;
        }
    }

    /* a tap anywhere in the beer stirs it */
    if (y > LIQ_TOP && y < LIQ_BOT) {
        G.splash_x = x;
        G.splash = 1.f;
        G.slosh_v += (x < GLASS_CX ? 1.6f : -1.6f);
        plat_sound(IB_SND_BREAK, 0.35f);
    }
}

/* ------------------------------------------------------------------ frame */

void game_frame(float dt, const PlatTouches *touches)
{
    static int prev_down;
    int down = touches && touches->count > 0;
    int edge = down && !prev_down;
    prev_down = down;

    if (dt > 0.f) update(dt);
    if (edge) handle_touch(touches, 1);

    gfx_begin_frame((int)G.panel_w, (int)G.panel_h, CW, CH, 0, 0.04f, 0.03f, 0.02f);
    draw();
    gfx_end_frame();

    if (G.state == 0 && (G.frame % 180) == 0) save_state();
}

void game_redraw(void)
{
    gfx_begin_frame((int)G.panel_w, (int)G.panel_h, CW, CH, 0, 0.04f, 0.03f, 0.02f);
    draw();
    gfx_end_frame();
}

void game_gl_lost(void)
{
    gfx_forget_all();
    sprites_forget();
    G.bg_tex = 0;
    if (fg) { free(fg); fg = NULL; }
    memset(fpage, 0, sizeof fpage);
    fnpages = 0;
}

uint32_t game_bg_color(void) { return 0xFF241408u; }
int game_wants_exit(void) { return 0; }
