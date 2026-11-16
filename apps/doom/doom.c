/* doom.c — Doom port for iPod nano 7 (GLES 1.1 surface app).
 *       Fullscreen GL view: gl_app_init() one-time setup, gl_app_frame(w,h,frame)
 *       every VSync. Touch = hb_surface_touch_read(). Buttons =
 *       hb_button_pressed(). Sound = hb_audio_play_wav().
 *
 *       Demo level data + textures: doom_data.c (embedded).
 *
 *       Rendering: GLES 1.1 via the SDK's GL surface. The resident
 *       injects a fullscreen GL view; we clear the screen and draw
 *       the level every frame.
 */

#include "doom.h"

#include "hb_sdk.h"
#include "hb_gl_surface.h"
#include "gl.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>

bool s_music_playing = false;

game_t g;
player_t g_player;

/* ========================= input ========================= */

static int s_touch_last_x, s_touch_last_y;
static bool s_just_tap;
static int s_swipe_dx, s_swipe_dy;

/* Process touch input. Returns true when the player fired. */
static bool process_touch(void)
{
    hb_spoint_t tp;
    hb_surface_touch_read(&tp);

    if (tp.down) {
        s_touch_last_x = tp.x;
        s_touch_last_y = tp.y;
    }

    /* Detect tap: finger down and up within a short time. */
    static uint32_t s_tap_held_since = 0;
    static bool     s_tap_held = false;
    if (tp.down) {
        if (!s_tap_held) {
            s_tap_held = true;
            s_tap_held_since = 0;
        }
        s_just_tap = false;
    } else {
        if (s_tap_held) {
            s_just_tap = true;
            s_tap_held = false;
        }
        s_swipe_dx = 0;
        s_swipe_dy = 0;
    }

    /* Accumulate swipe for look-around. */
    s_swipe_dx += (s_touch_last_x - tp.x);
    s_swipe_dy += (s_touch_last_y - tp.x);
    if (fabsf(s_swipe_dx) > 2) {
        g_player.angle += s_swipe_dx * 0.5f;
        s_swipe_dx = 0;
    }
    if (fabsf(s_swipe_dy) > 2) {
        g_player.pitch += s_swipe_dy * 0.5f;
        s_swipe_dy = 0;
    }

    return s_just_tap;
}

/* Check physical buttons. Returns which button was pressed (or 0). */
static int process_buttons(void)
{
    if (hb_button_pressed(HB_BTN_PLAY_PAUSE)) return 1;
    if (hb_button_pressed(HB_BTN_VOL_UP))     return 2;
    if (hb_button_pressed(HB_BTN_VOL_DOWN))   return 3;
    if (hb_button_pressed(HB_BTN_HOME))       return 4;
    if (hb_button_pressed(HB_BTN_POWER))      return 5;
    return 0;
}

/* ========================= rendering ========================= */

/* Draw a 16x16 sprite. */
static void draw_sprite(int x, int y, int tex_idx)
{
    if (tex_idx >= 0 && tex_idx < NUM_TEXTURES) {
        draw_textured_quad(x, y, 16, 16, 64, 64, s_texid[tex_idx]);
    }
}

/* Draw the map: walls + sprites. */
static void draw_map(void)
{
    const map_t *m = &g.map;
    int i;

    gl_clear(13, 11, 20);

    /* Draw each wall as a horizontal or vertical line. */
    for (i = 0; i < m->num_lines; i++) {
        const line_t *ln = &m->lines[i];

        int mx = (ln->vx[0] + ln->vx[2]) / 2;
        int my = (ln->vx[1] + ln->vx[3]) / 2;
        uint8_t v = 64;  /* fallback texture colour */

        if (ln->tex_num == TEX_BRICK) {
            v = g_textures[TEX_BRICK].data[(my % 64) * 64 + (mx % 64)];
        } else if (ln->tex_num == TEX_METAL) {
            v = g_textures[TEX_METAL].data[(my % 64) * 64 + (mx % 64)];
        } else if (ln->tex_num == TEX_WOOD) {
            v = g_textures[TEX_WOOD].data[(my % 64) * 64 + (mx % 64)];
        } else if (ln->tex_num == TEX_METAL2) {
            v = g_textures[TEX_METAL2].data[(my % 64) * 64 + (mx % 64)];
        } else if (ln->tex_num == TEX_GRASS) {
            v = g_textures[TEX_GRASS].data[(my % 64) * 64 + (mx % 64)];
        } else if (ln->tex_num == TEX_WATER) {
            v = g_textures[TEX_WATER].data[(my % 64) * 64 + (mx % 64)];
        } else if (ln->tex_num == TEX_SKULL) {
            v = g_textures[TEX_SKULL].data[(my % 64) * 64 + (mx % 64)];
        } else if (ln->tex_num == TEX_SUN) {
            v = g_textures[TEX_SUN].data[(my % 64) * 64 + (mx % 64)];
        } else if (ln->tex_num == TEX_CANDY) {
            v = g_textures[TEX_CANDY].data[(my % 64) * 64 + (mx % 64)];
        }

        uint16_t col = gray8(v);

        int x0 = ln->vx[0], y0 = ln->vx[1];
        int x1 = ln->vx[2], y1 = ln->vx[3];

        /* Bresenham line draw. */
        int dx = abs(x1 - x0), dy = abs(y1 - y0);
        int xx = 1, xy = 1, xe = x0, ye = y0;
        int sx = 1, sy = 1;
        if (x0 > x1) { sx = -1; xe = x1; }
        if (y0 > y1) { sy = -1; ye = y1; }
        int err = dx - dy;
        while (1) {
            if (x0 >= 0 && x0 < SCR_W && y0 >= 0 && y0 < SCR_H) {
                /* draw pixel */
                uint8_t c = (uint8_t)(col & 0xFF);
                (void)c;
            }
            if (x0 == xe && y0 == ye) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
    }

    /* Draw sprites. */
    for (i = 0; i < m->num_sprites; i++) {
        const sprite_t *sp = &m->sprites[i];
        draw_sprite(sp->x - 8, sp->y - 8, sp->tex_num);
    }
}

/* Draw the HUD. */
static void draw_hud(void)
{
    /* Health bar (bottom left). */
    int x = 10, y = SCR_H - 10;
    int bh = g_player.health / 20;
    if (bh > 10) bh = 10;
    glColor4ub(0, 180, 0, 255);
    glBegin(GL_TRIANGLE_STRIP);
    glVertex2i(x, y - bh);
    glVertex2i(x + 120, y - bh);
    glVertex2i(x, y);
    glVertex2i(x + 120, y);
    glEnd();
    glColor4ub(255, 255, 255, 255);
    glBegin(GL_TRIANGLE_STRIP);
    glVertex2i(x, y - bh);
    glVertex2i(x + 120 * (g_player.health / 100.0f * 120), y - bh);
    glVertex2i(x, y);
    glVertex2i(x + 120 * (g_player.health / 100.0f * 120), y);
    glEnd();

    /* Ammo (bottom right). */
    x = SCR_W - 130;
    y = SCR_H - 10;
    int ah = g_player.ammo;
    if (ah > 10) ah = 10;
    glColor4ub(0, 0, 200, 255);
    glBegin(GL_TRIANGLE_STRIP);
    glVertex2i(x, y - ah);
    glVertex2i(x + 20, y - ah);
    glVertex2i(x, y);
    glVertex2i(x + 20, y);
    glEnd();
    glColor4ub(255, 255, 255, 255);
    glBegin(GL_TRIANGLE_STRIP);
    glVertex2i(x, y - ah);
    glVertex2i(x + 20 * (g_player.ammo / (float)g_player.max_ammo), y - ah);
    glVertex2i(x, y);
    glVertex2i(x + 20 * (g_player.ammo / (float)g_player.max_ammo), y);
    glEnd();

    /* Crosshair (center). */
    glColor4ub(255, 255, 255, 255);
    glBegin(GL_LINES);
    glVertex2i(SCR_W / 2 - 2, SCR_H / 2);
    glVertex2i(SCR_W / 2 + 2, SCR_H / 2);
    glVertex2i(SCR_W / 2, SCR_H / 2 - 2);
    glVertex2i(SCR_W / 2, SCR_H / 2 + 2);
    glEnd();
}

/* Draw the menu screen. */
static void draw_menu(void)
{
    gl_clear(13, 11, 20);

    glColor4ub(255, 255, 255, 255);

    /* Menu items. */
    const char *items[] = { "START GAME", "DEMO", "OPTIONS", "QUIT" };
    int sel = s_menu_sel;
    int y = 50;
    int i;
    for (i = 0; i < 4; i++) {
        if (i == sel) {
            glColor4ub(255, 0, 0, 255);
            glBegin(GL_TRIANGLE_STRIP);
            glVertex2i(20, y - 10);
            glVertex2i(200, y - 10);
            glVertex2i(20, y + 10);
            glVertex2i(200, y + 10);
            glEnd();
            glColor4ub(255, 255, 255, 255);
        }
        y += 36;
    }

    y += 20;
    glColor4ub(128, 128, 128, 255);
}

/* ========================= game logic ========================= */

static void update_game(void)
{
    if (!g.ready) return;

    /* Input. */
    if (process_touch()) {
        if (g_player.ammo > 0) {
            g_player.ammo--;
            g_player.score += 10;
            s_just_tap = false;
            const char *snd = "sounds/fire.wav";
            hb_audio_play_wav(snd, 0x0FFF);
            s_music_playing = true;
        }
    }

    int btn = process_buttons();
    if (btn == 1) {
        g.paused = !g.paused;
        s_music_playing = false;
    } else if (btn == 2) {
        if (s_menu_sel > 0) s_menu_sel--;
    } else if (btn == 3) {
        if (s_menu_sel < 3) s_menu_sel++;
    } else if (btn == 4) {
        if (s_menu_sel == 0) {
            g.ready = true;
            g.paused = false;
            reset_game(g.active_map);
            s_music_playing = false;
        }
    } else if (btn == 5) {
        g.ready = false;
        s_music_playing = false;
    }
}

/* ========================= main entry points ========================= */

void gl_app_init(void)
{
    gl_init_textures();
    build_textures();          /* defined in doom_data.c */
    int i;
    for (i = 0; i < NUM_TEXTURES; i++) {
        gl_load_texture(i, g_textures[i].data);
    }
    reset_game(g_demo_level);
    g.ready = false;
    s_menu_sel = 0;
    s_just_tap = false;
    s_music_playing = false;
}

void gl_app_frame(int w, int h, uint32_t frame)
{
    (void)w; (void)h; (void)frame;

    if (!g.ready) {
        draw_menu();
        return;
    }

    if (g.paused) {
        draw_menu();
        return;
    }

    update_game();
    draw_map();
    draw_hud();

    g.frame++;
}
