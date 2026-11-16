/*
 * ui.c — status bar, title/menu screens, overlays and the Doom font.
 */
#include "doom.h"

#define TINT_ORIG 0xFFFFFFFFu

static void fill_rect(int x, int y, int w, int h, uint32_t c)
{
    int xx, yy;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GW) w = GW - x;
    if (y + h > GH) h = GH - y;
    if (w <= 0 || h <= 0) return;
    for (yy = 0; yy < h; yy++) {
        uint32_t *p = g_cv + (long)(y + yy) * GW + x;
        for (xx = 0; xx < w; xx++) p[xx] = c;
    }
}

static void outline(int x, int y, int w, int h, uint32_t c)
{
    fill_rect(x, y, w, 1, c);
    fill_rect(x, y + h - 1, w, 1, c);
    fill_rect(x, y, 1, h, c);
    fill_rect(x + w - 1, y, 1, h, c);
}

static void darken(int amount)
{
    int i, n = GW * GH;
    for (i = 0; i < n; i++) {
        uint32_t c = g_cv[i];
        uint32_t r = ((c >> 16) & 255) * amount >> 8;
        uint32_t g = ((c >> 8) & 255) * amount >> 8;
        uint32_t b = (c & 255) * amount >> 8;
        g_cv[i] = (r << 16) | (g << 8) | b;
    }
}

void ui_pic(const pic_t *p, int x, int y, int scale, uint32_t tint)
{
    int sx, sy, xx, yy;
    if (!p || !p->pix || scale <= 0) return;
    for (sy = 0; sy < p->h; sy++) {
        int py = y + sy * scale;
        for (sx = 0; sx < p->w; sx++) {
            uint8_t pix = p->pix[sy * p->w + sx];
            uint32_t c;
            int px = x + sx * scale;
            if (pix == 255) continue;
            if (px < 0 || px + scale <= 0 || py < 0 || py + scale <= 0) continue;
            if (px >= GW || py >= GH) continue;
            c = (tint == TINT_ORIG) ? g_pal[pix] : tint;
            for (yy = 0; yy < scale; yy++)
                for (xx = 0; xx < scale; xx++)
                    if (px + xx >= 0 && px + xx < GW && py + yy >= 0 && py + yy < GH)
                        g_cv[(py + yy) * GW + px + xx] = c;
        }
    }
}

static void ui_pic_f(const pic_t *p, int x, int y, int scale16, uint32_t tint)
{
    int w, h, xx, yy, ustep, vstep;
    if (!p || !p->pix || scale16 <= 0) return;
    w = ((int32_t)p->w * scale16) >> 16;
    h = ((int32_t)p->h * scale16) >> 16;
    if (w <= 0 || h <= 0) return;
    ustep = (int)(((int64_t)p->w << 16) / w);
    vstep = (int)(((int64_t)p->h << 16) / h);
    for (yy = 0; yy < h; yy++) {
        int py = y + yy;
        int ty = yy * vstep >> 16;
        if (py < 0 || py >= GH) continue;
        for (xx = 0; xx < w; xx++) {
            int px = x + xx;
            int tx = xx * ustep >> 16;
            uint8_t pix;
            if (px < 0 || px >= GW) continue;
            pix = p->pix[ty * p->w + tx];
            if (pix != 255)
                g_cv[py * GW + px] = (tint == TINT_ORIG) ? g_pal[pix] : tint;
        }
    }
}

void ui_text(int x, int y, const char *s, int scale, uint32_t rgb)
{
    int cx = x;
    while (*s) {
        char c = *s++;
        pic_t *p;
        int idx;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c < 33 || c > 95) { cx += 4 * scale; continue; }
        idx = g_font[c - 33];
        if (idx < 0 || idx >= g_ngfx) { cx += 4 * scale; continue; }
        p = &g_gfx[idx];
        ui_pic(p, cx + scale, y + scale, scale, 0x000000);
        ui_pic(p, cx, y, scale, rgb);
        cx += (p->w + 1) * scale;
    }
}

int ui_text_width(const char *s, int scale)
{
    int cx = 0;
    while (*s) {
        char c = *s++;
        int idx;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c < 33 || c > 95) { cx += 4 * scale; continue; }
        idx = g_font[c - 33];
        if (idx < 0 || idx >= g_ngfx) { cx += 4 * scale; continue; }
        cx += (g_gfx[idx].w + 1) * scale;
    }
    return cx;
}

/* --------------------------------------------------------------- backdrop */

void ui_flat_bg(void)
{
    int y, x;
    const int top[3] = { 0x24, 0x0e, 0x0e };
    const int bot[3] = { 0x0a, 0x05, 0x05 };
    for (y = 0; y < GH; y++) {
        int r = top[0] + (bot[0] - top[0]) * y / GH;
        int g = top[1] + (bot[1] - top[1]) * y / GH;
        int b = top[2] + (bot[2] - top[2]) * y / GH;
        uint32_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        for (x = 0; x < GW; x++) g_cv[y * GW + x] = c;
    }
}

/* ------------------------------------------------------------------ title */

void ui_draw_title(void)
{
    int blink = (g_now_ms / 450) & 1;
    /* Freedoom ships full-bleed title art exactly like the classic Doom title
     * screen (logo, marine, "PHASE 1"), so show it unmolested and put the
     * blinking press-any-key prompt in a black strip at the bottom. */
    if (g_titlepic >= 0) {
        ui_pic_f(&g_gfx[g_titlepic], 0, -8, 88474, TINT_ORIG);
        fill_rect(0, 196, GW, 44, 0x050505);
        fill_rect(0, 194, GW, 2, 0x7a1515);
        ui_text(CX - ui_text_width("E1M1  KNEE-DEEP IN THE DEAD", 1) / 2, 201,
                "E1M1  KNEE-DEEP IN THE DEAD", 1, 0xc05a10);
        if (blink)
            ui_text(CX - ui_text_width("TOUCH TO START", 2) / 2, 215,
                    "TOUCH TO START", 2, 0xffd040);
        return;
    }
    /* fallback: pure Doom-style screen */
    ui_flat_bg();
    ui_text(CX - ui_text_width("FREEDOOM", 5) / 2, 48, "FREEDOOM", 5, 0xb82e3f);
    ui_text(CX - ui_text_width("E1M1", 3) / 2, 108, "E1M1", 3, 0xd04030);
    if (blink)
        ui_text(CX - ui_text_width("TOUCH TO START", 2) / 2, 168,
                "TOUCH TO START", 2, 0xffd040);
}

/* ------------------------------------------------------------------- menu */

static void menu_row(int i, const char *label)
{
    int y = 118 + i * 28;
    int w = ui_text_width(label, 2);
    if (i == g_menu_sel) {
        fill_rect(CX - w / 2 - 16, y - 4, w + 32, 24, 0x3a0c0c);
        outline(CX - w / 2 - 16, y - 4, w + 32, 24, 0x9a3030);
    } else {
        fill_rect(CX - w / 2 - 16, y - 4, w + 32, 24, 0x140808);
    }
    ui_text(CX - w / 2, y, label, 2, 0xf0d8a0);
}

void ui_draw_menu(void)
{
    ui_flat_bg();
    if (g_mdoom >= 0) {
        pic_t *p = &g_gfx[g_mdoom];
        int w = (int)((int64_t)p->w * 120000 >> 16);
        ui_pic_f(p, CX - w / 2, 8, 120000, TINT_ORIG);
    }
    ui_text(CX - ui_text_width("KNEE-DEEP IN THE DEAD", 1) / 2, 74,
            "KNEE-DEEP IN THE DEAD", 1, 0xc06018);
    menu_row(0, "NEW GAME");
    menu_row(1, "PLAY DEMO");
    menu_row(2, "CONTROLS");
    menu_row(3, "ABOUT");
    ui_text(CX - ui_text_width("TAP A LINE", 1) / 2, 228,
            "TAP A LINE", 1, 0x806848);
}

/* ----------------------------------------------------------------- screens */

void ui_draw_controls(void)
{
    static const char *lines[] = {
        "LEFT  DRAG    MOVE / STRAFE",
        "RIGHT DRAG    TURN",
        "UP/DOWN DRAG  WALK FWD/BACK",
        "TAP           FIRE",
        "PLAY/PAUSE    FIRE",
        "VOL+          FIRE",
        "VOL-          USE / OPEN",
        "PAUSE ICON    PAUSE GAME",
        "PAUSE -> TILT AIM TOGGLE"
    };
    int i;
    ui_flat_bg();
    ui_text(CX - ui_text_width("CONTROLS", 3) / 2, 14, "CONTROLS", 3, 0xd04030);
    for (i = 0; i < 9; i++)
        ui_text(46, 54 + i * 17, lines[i], 1, 0xd8d0b8);
    ui_text(CX - ui_text_width("TAP TO RETURN", 1) / 2, 220,
            "TAP TO RETURN", 1, 0xffd040);
}

void ui_draw_about(void)
{
    ui_flat_bg();
    ui_text(CX - ui_text_width("ABOUT", 3) / 2, 18, "ABOUT", 3, 0xd04030);
    ui_text(54, 68, "DOOM FOR IPOD NANO 7G", 1, 0xf0e0b0);
    ui_text(54, 88, "USES FREEDOOM ASSETS", 1, 0xc8c0a8);
    ui_text(54, 108, "MAP: E1M1 HANGAR", 1, 0xc8c0a8);
    ui_text(54, 128, "SPRITES AND TEXTURES FROM", 1, 0x9f9888);
    ui_text(54, 142, "FREEDOOM 0.13", 1, 0x9f9888);
    ui_text(54, 166, "TOUCH + BUTTONS", 1, 0xc8c0a8);
    ui_text(54, 184, "LANDSCAPE 432x240", 1, 0xc8c0a8);
    ui_text(CX - ui_text_width("TAP TO RETURN", 1) / 2, 220,
            "TAP TO RETURN", 1, 0xffd040);
}

/* ---------------------------------------------------------------- overlays */

void ui_draw_pause(void)
{
    int w;
    static const struct { int y; const char *label; int tilt; } rows[4] = {
        { 96,  "RESUME",     0 },
        { 128, "RESTART",    0 },
        { 160, "MAIN MENU",  0 },
        { 192, "TILT AIM: ", 1 },
    };
    darken(110);
    w = ui_text_width("PAUSED", 3);
    ui_text(CX - w / 2, 38, "PAUSED", 3, 0xffd040);
    for (int i = 0; i < 4; i++) {
        int y = rows[i].y;
        char label[24];
        int k = 0;
        const char *base = rows[i].label;
        uint32_t col;
        int bcol;
        while (base[k]) { label[k] = base[k]; k++; }
        if (rows[i].tilt) {
            label[k++] = 'O'; label[k++] = 'N';
            if (!g_tilt) { label[k - 2] = 'O'; label[k - 1] = 'F'; label[k++] = 'F'; }
        }
        label[k] = 0;
        col = rows[i].tilt ? (g_tilt ? 0x60e0ff : 0x808080) : 0xf0d8a0;
        bcol = rows[i].tilt ? 0x30a0c0 : 0x9a3030;
        fill_rect(CX - 120, y, 240, 26, 0x240808);
        outline(CX - 120, y, 240, 26, (uint32_t)bcol);
        ui_text(CX - ui_text_width(label, 2) / 2, y + 4, label, 2, col);
    }
    ui_text(CX - ui_text_width("VOL- USE    VOL+ FIRE", 1) / 2, 224,
            "VOL- USE    VOL+ FIRE", 1, 0x706050);
}

void ui_draw_dead(void)
{
    int w;
    darken(70);
    fill_rect(0, 30, GW, 70, 0x300000);
    w = ui_text_width("YOU DIED", 5);
    ui_text(CX - w / 2, 42, "YOU DIED", 5, 0xff3020);
    ui_text(CX - ui_text_width("THE DEMONS WIN THIS TIME", 1) / 2, 92,
            "THE DEMONS WIN THIS TIME", 1, 0xc09080);
    fill_rect(CX - 110, 140, 220, 32, 0x240808); outline(CX - 110, 140, 220, 32, 0xb04030);
    fill_rect(CX - 110, 178, 220, 32, 0x140808); outline(CX - 110, 178, 220, 32, 0x603030);
    ui_text(CX - ui_text_width("TAP TO RESTART", 2) / 2, 148, "TAP TO RESTART", 2, 0xffd040);
    ui_text(CX - ui_text_width("MAIN MENU", 2) / 2, 186, "MAIN MENU", 2, 0xc8b080);
}

void ui_draw_win(void)
{
    darken(90);
    ui_text(CX - ui_text_width("LEVEL COMPLETE", 3) / 2, 50, "LEVEL COMPLETE", 3, 0xffd040);
    ui_text(CX - ui_text_width("YOU ESCAPED E1M1", 2) / 2, 104, "YOU ESCAPED E1M1", 2, 0xd8c898);
    ui_text(CX - ui_text_width("TAP FOR MENU", 2) / 2, 168, "TAP FOR MENU", 2, 0xa8d0ff);
}

void ui_draw_demo_tips(void)
{
    fill_rect(8, 8, 188, 82, 0x080808);
    outline(8, 8, 188, 82, 0x805020);
    ui_text(16, 14, "DEMO CONTROLS", 1, 0xffd040);
    ui_text(16, 30, "RIGHT DRAG  AIM", 1, 0xd8d0b8);
    ui_text(16, 44, "LEFT DRAG   MOVE/STRAFE", 1, 0xd8d0b8);
    ui_text(16, 58, "DRAG UP     WALK FORWARD", 1, 0xd8d0b8);
    ui_text(16, 72, "TAP FIRE   VOL- OPEN DOOR", 1, 0xd8d0b8);
}

/* -------------------------------------------------------------------- HUD */

static void draw_num(int x, int y, int v, int nd, int scale16)
{
    char buf[8];
    int i;
    if (v < 0) v = 0;
    for (i = 0; i < nd; i++) { buf[i] = (char)('0' + v % 10); v /= 10; }
    for (i = nd - 1; i >= 0; i--) {
        int idx = g_num[buf[i] - '0'];
        int ax = x + (nd - 1 - i) * ((13 * scale16) >> 16) + (nd - 1 - i) * 1;
        if (idx >= 0) ui_pic_f(&g_gfx[idx], ax, y, scale16, TINT_ORIG);
    }
}

static void red_flash(int strength)
{
    int i, n = GW * VIEW_H;
    int add = (strength * 90) / 180;    /* 0..90 red boost           */
    int kg = 255 - strength / 2;        /* dim green/blue            */
    int kb = 255 - strength;
    for (i = 0; i < n; i++) {
        uint32_t c = g_cv[i];
        int r = (int)((c >> 16) & 255) + add;
        int g = (int)((c >> 8) & 255) * kg >> 8;
        int b = (int)(c & 255) * kb >> 8;
        if (r > 255) r = 255;
        g_cv[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
}

void ui_draw_hud(void)
{
    int scale = 88474;
    int ammo, hx;
    if (g_player.damage_flash > 0)
        red_flash(g_player.damage_flash);

    if (g_stbar >= 0)
        ui_pic_f(&g_gfx[g_stbar], 0, HUD_Y, scale, TINT_ORIG);
    else
        fill_rect(0, HUD_Y, GW, HUD_H, 0x2a2018);

    /* digit boxes (432px wide after 1.35x scale):
       AMMO 3..62 | HEALTH 68..151 | FRAGS 153..194 | face 196..236 | ARMOR 238..324 */
    ammo = (g_player.weapon == 1) ? g_player.shells : g_player.bullets;
    draw_num(5,   HUD_Y + 3, ammo,            3, scale);
    draw_num(82,  HUD_Y + 3, g_player.health, 3, scale);
    draw_num(250, HUD_Y + 3, g_player.armor,  3, scale);

    /* face */
    {
        int fi = 0;
        if (g_player.dead) fi = 6;
        else if (g_player.health <= 20) fi = 5;
        else if (g_player.health <= 40) fi = 4;
        else if (g_player.health <= 60) fi = 3;
        else if (g_player.health <= 80) fi = 2;
        else if (g_player.health <= 95) fi = 1;
        if (g_player.health > 150) fi = 7;
        if (g_face[fi] >= 0)
            ui_pic_f(&g_gfx[g_face[fi]], 199, HUD_Y + 2, scale, TINT_ORIG);
    }
    /* keys: stacked in the narrow column right of the armor box */
    if (g_player.keys & 1) {
        int k = g_keyspr[0];
        if (k >= 0) ui_pic_f(&g_gfx[k], 330, HUD_Y + 3,  120000, TINT_ORIG);
    }
    if (g_player.keys & 2) {
        int k = g_keyspr[1];
        if (k >= 0) ui_pic_f(&g_gfx[k], 330, HUD_Y + 15, 120000, TINT_ORIG);
    }
    if (g_player.keys & 4) {
        int k = g_keyspr[2];
        if (k >= 0) ui_pic_f(&g_gfx[k], 330, HUD_Y + 27, 120000, TINT_ORIG);
    }

    /* pause button */
    fill_rect(GW - 38, 6, 30, 22, 0x100808);
    outline(GW - 38, 6, 30, 22, 0xa05030);
    ui_text(GW - 32, 10, "II", 2, 0xffd040);

    if (g_demo) {
        fill_rect(6, VIEW_H - 15, 40, 13, 0x300000);
        outline(6, VIEW_H - 15, 40, 13, 0x804020);
        ui_text(11, VIEW_H - 13, "DEMO", 1, 0xff8020);
    }
    (void)hx;
}
