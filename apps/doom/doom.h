/* doom.h — Doom port for iPod nano 7: shared type definitions.
 *       GLES 1.1 surface app. Touch: hb_surface_touch_read().
 *       Buttons: hb_button_pressed(). Audio: hb_audio_play_wav().
 */

#ifndef DOOM_H
#define DOOM_H

#include "hb_sdk.h"
#include "hb_surface_input.h"
#include "hb_gl_surface.h"
#include "gl.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

/* ========================= constants ========================= */

#define SCR_W     320          /* Doom internal render width           */
#define SCR_H     200          /* Doom internal render height          */
#define TEX_W     128          /* base texture tile width (power of 2) */
#define TEX_H     128

#define MAX_MAPS    4
#define MAX_LINES   256
#define MAX_SPRITES 128
#define MAX_SECTORS 64
#define MAX_NODES   512
#define MAX_TEXTURES 64
#define MAX_NAMES   32

/* =============== textures (256x128, embedded as C arrays) ============ */

enum {
    TEX_BRICK,   /* 0 */
    TEX_METAL,   /* 1 */
    TEX_WOOD,   /* 2 */
    TEX_METAL2,  /* 3 */
    TEX_GRASS,   /* 4 */
    TEX_WATER,   /* 5 */
    TEX_FOLIAGE, /* 6 */
    TEX_SKULL,   /* 7 */
    TEX_SUN,     /* 8 */
    TEX_CANDY,   /* 9 */
    NUM_TEXTURES
};

/* Texture header: id, w, h (pixels), tile flag */
typedef struct {
    uint8_t  id;
    uint8_t  w, h;
    uint8_t  flags;
    uint8_t  pad[3];
    uint8_t  data[TEX_W * TEX_H];   /* 16384 bytes */
} texture_t;

/* ============ map types (compact demo format) ============ */

/* A line (wall segment). 4 vertex x,y in map coords. */
typedef struct {
    int16_t vx[4];
    uint8_t tex_num;
    uint8_t dir;        /* 0 = horizontal (top/bottom), 1 = vertical */
    uint8_t flags;      /* 0 = normal, 1 = two-sided, 2 = special (exit) */
} line_t;

/* A sector (floor/ceiling region). */
typedef struct {
    int16_t floor_h;
    int16_t ceil_h;
    uint8_t floor_pal;
    uint8_t ceiling_pal;
} sector_t;

/* A sprite (enemy, item, weapon). */
typedef struct {
    int16_t x, y;
    uint8_t tex_num;
    uint8_t frame;
    uint8_t frame_count;
    uint8_t frame_duration;
} sprite_t;

/* A BSP node. */
typedef struct {
    int16_t x, y, w, h;
    int16_t div_dir;    /* 0 = x, 1 = y, 2 = angle */
    int16_t div_val;
    int16_t front_child, back_child;
} node_t;

/* The map. */
typedef struct {
    char     name[MAX_NAMES];
    uint8_t  format;
    uint16_t width;
    uint16_t height;
    uint16_t num_lines;
    uint16_t num_sectors;
    uint16_t num_sprites;
    uint16_t num_nodes;
    uint16_t texture_count;
    line_t   lines[MAX_LINES];
    sector_t sectors[MAX_SECTORS];
    sprite_t sprites[MAX_SPRITES];
    node_t   nodes[MAX_NODES];
    texture_t textures[NUM_TEXTURES];
    uint16_t colormap[256];
} map_t;

/* Externs: shared data defined in doom_data.c */
extern texture_t  g_textures[];
extern map_t       g_maps[];
extern int         g_num_maps;
extern int         g_demo_level;

/* ============ renderer state ============ */

extern GLuint s_texid[MAX_TEXTURES];
extern int    s_tex_width[MAX_TEXTURES];
extern int    s_tex_height[MAX_TEXTURES];

/* ============ input state ============ */


/* ============ sound state ============ */

extern bool s_music_playing;

/* ============ GLES 1.1 function declarations (from sdk/gl.h) ============ */
extern void    glClear(GLbitfield mask);
extern void    glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
extern void    glClearDepthf(GLclampf depth);
extern void    glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
extern void    glBegin(GLenum mode);
extern void    glEnd(void);
extern void    glVertex2i(GLint x, GLint y);
extern void    glTexCoord2f(GLfloat s, GLfloat t);
extern void    glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
extern void    glEnable(GLenum cap);
extern void    glDisable(GLenum cap);
extern void    glBlendFunc(GLenum sfactor, GLenum dfactor);
extern void    glBindTexture(GLenum target, GLuint texture);
extern void    glGenTextures(GLsizei n, GLuint *textures);
extern void    glTexImage2D(GLenum target, GLint level, GLint internalformat,
                            GLsizei width, GLsizei height, GLint border,
                            GLenum format, GLenum type, const GLvoid *pixels);
extern void    glTexParameteri(GLenum target, GLenum pname, GLint param);
extern void    glTexParameterf(GLenum target, GLenum pname, GLfloat param);

/* Game state (shared across doom.c) */
extern int s_menu_sel;          /* menu selection index */
extern void    build_textures(void);  /* defined in doom_data.c */

/* ============ player state. ============ */

/* Player state. */
typedef struct {
    float x, y;
    float angle;        /* view angle in degrees */
    float pitch;        /* look up/down, 0 = level */
    int health;
    int ammo;
    int max_ammo;
    int weapon;
    int score;
} player_t;

/* ============ game state ============ */

typedef struct {
    int      active_map;
    int      frame;
    int      paused;
    bool     ready;
    map_t    map;
} game_t;

extern game_t g;

/* ============ helpers ============ */

static inline int clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}


/* Convert 8-bit grayscale to RGB565 (16-bit). */
static inline uint16_t gray8(uint8_t v)
{
    return ((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3);
}

/* Convert RGB888 to RGB565 (16-bit). */
static inline uint16_t rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

/* Draw a textured quad with GLES 1.1. Screen-space quad; uv in pixels. */
static void draw_textured_quad(int x, int y, int w, int h,
                               int tex_w, int tex_h,
                               GLuint tex)
{
    if (tex == 0) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub(255, 255, 255, 255);
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2i(x, y);
    glTexCoord2f((float)tex_w, 0.0f);
    glVertex2i(x + w, y);
    glTexCoord2f(0.0f, (float)tex_h);
    glVertex2i(x, y + h);
    glTexCoord2f((float)tex_w, (float)tex_h);
    glVertex2i(x + w, y + h);
    glEnd();
}

/* Clear the screen. */
static void gl_clear(int r, int g, int b)
{
    glClearColor((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

/* Load a texture into GLES. Does not delete existing textures. */
static void gl_load_texture(int tex_idx, const uint8_t *pixels)
{
    if (tex_idx < 0 || tex_idx >= MAX_TEXTURES) return;
    if (s_texid[tex_idx] != 0) {
        glDeleteTextures(1, &s_texid[tex_idx]);
    }
    glGenTextures(1, &s_texid[tex_idx]);
    glBindTexture(GL_TEXTURE_2D, s_texid[tex_idx]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 TEX_W, TEX_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

/* Initialize GLES textures. */
static void gl_init_textures(void)
{
    int i;
    for (i = 0; i < NUM_TEXTURES; i++) {
        s_texid[i] = 0;
        s_tex_width[i] = TEX_W;
        s_tex_height[i] = TEX_H;
    }
}

/* Initialize a map from a source map. */
static void copy_map(map_t *dst, const map_t *src)
{
    memcpy(dst, src, sizeof(map_t));
    dst->texture_count = src->texture_count;
}

/* Player state. */
typedef struct {
    float x, y;
    float angle;        /* view angle in degrees */
    float pitch;        /* look up/down, 0 = level */
    int health;
    int ammo;
    int max_ammo;
    int weapon;
    int score;

extern player_t g_player;
static void move_player(player_t *p, float dx, float dy, float da)
{
    float speed = 4.0f;
    float turn = 3.0f;

    p->angle += da * turn;
    while (p->angle >= 360.0f) p->angle -= 360.0f;
    while (p->angle < 0.0f) p->angle += 360.0f;

    if (fabsf(dx) > 0.001f || fabsf(dy) > 0.001f) {
        float len = sqrtf(dx * dx + dy * dy) * speed;
        float dir = p->angle * 3.14159265f / 180.0f;
        float nx = p->x + cosf(dir) * len;
        float ny = p->y + sinf(dir) * len;

        /* Simple bounding-box collision against map lines.
         * This is a placeholder — a real port would use BSP traversal. */
        const map_t *m = &g.map;
        int i;
        for (i = 0; i < m->num_lines; i++) {
            const line_t *ln = &m->lines[i];
            int j;
            for (j = 0; j < 4; j++) {
                int px = (int)(nx + ln->vx[j] - p->x);
                int py = (int)(ny + ln->vx[j] - p->y);
                if (fabsf((float)px) < 16.0f && fabsf((float)py) < 16.0f) {
                    nx = p->x;
                    ny = p->y;
                    break;
                }
            }
        }
        p->x = nx;
        p->y = ny;
    }
}

/* Player update (movement + shooting + collisions). */
static void update_player(void)
{
    float dx = 0.0f, dy = 0.0f, da = 0.0f;

    /* Movement defaults: trackball horizontal = look, vertical = move */
    (void)dx; (void)dy; (void)da;

    /* Shoot: just fired flag is set by input handling. */
}

/* Game state: reset level. */
static void reset_game(int level)
{
    memset(&g, 0, sizeof(g));
    g.active_map = level;
    g.frame = 0;
    g.paused = false;
    /* copy map data */
    g.map = g_maps[level];
    copy_map(&g.map, &g_maps[level]);
    /* setup player */
    g_player.x = 320.0f;
    g_player.y = 200.0f;
    g_player.angle = 0.0f;
    g_player.pitch = 0.0f;
    g_player.health = 100;
    g_player.ammo = 20;
    g_player.max_ammo = 20;
    g_player.weapon = 0;
    g_player.score = 0;
    g.ready = true;
}

/* Return true if the player is on a special (exit) line. */
static bool is_exit_line(int line_idx)
{
    const map_t *m = &g.map;
    return m->lines[line_idx].flags & 2;
}

#endif /* DOOM_H */
