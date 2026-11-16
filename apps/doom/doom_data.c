/* doom_data.c — embedded demo level + procedural textures for doom.nanoapp.
 *       doom.c and doom_sfx.c both include doom.h for shared types.
 *       Texture generation: procedural, full-screen tiles.
 *       Demo map: a compact, playable level (walls, sectors, sprites, nodes).
 */

#include "doom.h"

/* Add the missing texture IDs used by the demo. */
enum {
    TEX_PISTOL = 10,
    TEX_HEALTH = 11,
    TEX_KEY    = 12,
};

/* ================== procedural texture generators ================== */

/* Texture table — procedural, filled by build_textures(). */
texture_t g_textures[NUM_TEXTURES] = {
    { .id = 0, .w = 64, .h = 64, .flags = 0, .data = { 0 } },
    { .id = 1, .w = 64, .h = 64, .flags = 0, .data = { 0 } },
    { .id = 2, .w = 64, .h = 64, .flags = 0, .data = { 0 } },
    { .id = 3, .w = 64, .h = 64, .flags = 0, .data = { 0 } },
    { .id = 4, .w = 64, .h = 64, .flags = 0, .data = { 0 } },
    { .id = 5, .w = 64, .h = 64, .flags = 0, .data = { 0 } },
    { .id = 6, .w = 32, .h = 32, .flags = 0, .data = { 0 } },
    { .id = 7, .w = 64, .h = 64, .flags = 0, .data = { 0 } },
    { .id = 8, .w = 64, .h = 64, .flags = 0, .data = { 0 } },
    { .id = 9, .w = 64, .h = 64, .flags = 0, .data = { 0 } },
};

/* Procedural texture generator: draws into a 64x64 tile and we tile it. */
static void gen_brick(uint8_t *dst, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int bx = i / 16, by = j / 16;
            int ox = (i % 16) - 8;
            int oy = (j % 16) - 8;
            int dist = (int)sqrtf((float)(ox * ox + oy * oy));
            uint8_t v = (dist < 6) ? 200 : 150;
            if ((bx + by) & 1) v = (v + 40) / 2;
            dst[j * w + i] = v;
        }
    }
}

static void gen_metal(uint8_t *dst, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int v = 128 + ((i * 3 + j * 5) % 64);
            dst[j * w + i] = (uint8_t)v;
        }
    }
}

static void gen_wood(uint8_t *dst, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int plank = j / 8;
            uint8_t v = (plank == 0) ? 180 : (plank == 1) ? 120 : 140;
            dst[j * w + i] = v;
        }
    }
}

static void gen_grass(uint8_t *dst, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            dst[j * w + i] = (j / 8) ? 60 : 80;
        }
    }
}

static void gen_water(uint8_t *dst, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int v = 40 + ((j * 3 + i * 7) % 20);
            dst[j * w + i] = (uint8_t)v;
        }
    }
}

static void gen_sun(uint8_t *dst, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int cx = w / 2, cy = h / 2;
            int dx = i - cx, dy = j - cy;
            int dist = (int)sqrtf((float)(dx * dx + dy * dy));
            uint8_t v = (dist < 20) ? 220 : (dist < 30) ? 150 : 40;
            dst[j * w + i] = v;
        }
    }
}

static void gen_candy(uint8_t *dst, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int t = (i * 7 + j * 13) % 255;
            dst[j * w + i] = (uint8_t)(t * 20 / 255);
        }
    }
}

static void gen_candy2(uint8_t *dst, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int stripe = (i / 8) % 2;
            dst[j * w + i] = (uint8_t)(stripe ? 160 : 40);
        }
    }
}

static void gen_pistol(uint8_t *dst, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int v = (i < w / 3) ? 180 : (i < w * 2 / 3) ? 140 : 100;
            dst[j * w + i] = (uint8_t)v;
        }
    }
}

static void gen_health(uint8_t *dst, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int cx = w / 2, cy = h / 2;
            int dx = i - cx, dy = j - cy;
            int dist = (int)sqrtf((float)(dx * dx + dy * dy));
            uint8_t v = (dist < 14) ? 130 : (dist < 20) ? 80 : 40;
            dst[j * w + i] = v;
        }
    }
}

static void gen_key(uint8_t *dst, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int v = (j < h / 3) ? 170 : (i < w / 2) ? 140 : 120;
            dst[j * w + i] = (uint8_t)v;
        }
    }
}

static void gen_skull(uint8_t *dst, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int cx = w / 2, cy = h / 3;
            int dx = i - cx, dy = j - cy;
            int dist = (int)sqrtf((float)(dx * dx + dy * dy));
            uint8_t v = (dist < 16) ? 140 : (dist < 22) ? 90 : 50;
            dst[j * w + i] = v;
        }
    }
}

static void gen_enemy(uint8_t *dst, int w, int h)
{
    gen_skull(dst, w, h);
}

/* Fill the texture table with procedural textures. */
void build_textures(void)
{
    gen_brick(g_textures[TEX_BRICK].data, TEX_W, TEX_H);
    gen_metal(g_textures[TEX_METAL].data, TEX_W, TEX_H);
    gen_wood(g_textures[TEX_WOOD].data, TEX_W, TEX_H);
    gen_metal(g_textures[TEX_METAL2].data, TEX_W, TEX_H);
    gen_grass(g_textures[TEX_GRASS].data, TEX_W, TEX_H);
    gen_water(g_textures[TEX_WATER].data, TEX_W, TEX_H);
    gen_skull(g_textures[TEX_SKULL].data, TEX_W, TEX_H);
    gen_sun(g_textures[TEX_SUN].data, TEX_W, TEX_H);
    gen_candy(g_textures[TEX_CANDY].data, TEX_W, TEX_H);
    gen_pistol(g_textures[TEX_PISTOL].data, TEX_W, TEX_H);
    gen_health(g_textures[TEX_HEALTH].data, TEX_W, TEX_H);
    gen_key(g_textures[TEX_KEY].data, TEX_W, TEX_H);
}

/* ================== demo map ================== */

static map_t s_demo_map = {
    .name = "DEMO1",
    .format = 1,
    .width = 64,
    .height = 64,
    .num_lines = 12,
    .num_sectors = 3,
    .num_sprites = 5,
    .num_nodes = 3,
    .texture_count = NUM_TEXTURES,
    .lines = {
        { .vx = {0, 64, 64, 0},    .tex_num = TEX_BRICK,   .dir = 0, .flags = 0 },
        { .vx = {64, 64, 0, 0},    .tex_num = TEX_METAL,   .dir = 0, .flags = 0 },
        { .vx = {0, 0, 64, 64},    .tex_num = TEX_WOOD,    .dir = 0, .flags = 0 },
        { .vx = {64, 0, 0, 64},    .tex_num = TEX_METAL2,  .dir = 0, .flags = 0 },
        { .vx = {0, 32, 32, 32},   .tex_num = TEX_GRASS,   .dir = 0, .flags = 0 },
        { .vx = {32, 64, 32, 32},  .tex_num = TEX_WATER,   .dir = 0, .flags = 0 },
        { .vx = {32, 32, 64, 32},  .tex_num = TEX_BRICK,   .dir = 0, .flags = 0 },
        { .vx = {0, 32, 32, 32},   .tex_num = TEX_METAL,   .dir = 0, .flags = 0 },
        { .vx = {16, 0, 16, 64},   .tex_num = TEX_METAL2,  .dir = 1, .flags = 0 },
        { .vx = {32, 16, 32, 48},  .tex_num = TEX_SKULL,   .dir = 0, .flags = 0 },
        { .vx = {48, 16, 48, 48},  .tex_num = TEX_SUN,     .dir = 1, .flags = 0 },
        { .vx = {48, 48, 64, 48},  .tex_num = TEX_CANDY,   .dir = 0, .flags = 2 },  /* exit */
    },
    .sectors = {
        { .floor_h = 0,  .ceil_h = 128, .floor_pal = 0, .ceiling_pal = 0 },
        { .floor_h = 0,  .ceil_h = 128, .floor_pal = 4, .ceiling_pal = 0 },
        { .floor_h = 0,  .ceil_h = 128, .floor_pal = 7, .ceiling_pal = 0 },
    },
    .sprites = {
        { .x = 32, .y = 32, .tex_num = TEX_PISTOL, .frame = 0, .frame_count = 0, .frame_duration = 0 },
        { .x = 32, .y = 32, .tex_num = TEX_HEALTH, .frame = 0, .frame_count = 0, .frame_duration = 0 },
        { .x = 32, .y = 32, .tex_num = TEX_KEY,    .frame = 0, .frame_count = 0, .frame_duration = 0 },
        { .x = 32, .y = 32, .tex_num = TEX_SKULL,  .frame = 0, .frame_count = 0, .frame_duration = 0 },
        { .x = 32, .y = 32, .tex_num = TEX_SUN,    .frame = 0, .frame_count = 0, .frame_duration = 0 },
    },
    .nodes = {
        { .x = 0, .y = 0, .w = 64, .h = 64, .div_dir = 0, .div_val = 32,
          .front_child = 0, .back_child = 1 },
        { .x = 0, .y = 0, .w = 32, .h = 64, .div_dir = 0, .div_val = 16,
          .front_child = 2, .back_child = 3 },
        { .x = 0, .y = 0, .w = 16, .h = 64, .div_dir = 0, .div_val = 8,
          .front_child = -1, .back_child = -1 },
        { .x = 0, .y = 0, .w = 16, .h = 64, .div_dir = 1, .div_val = 32,
          .front_child = -1, .back_child = -1 },
        { .x = 32, .y = 0, .w = 32, .h = 64, .div_dir = 0, .div_val = 48,
          .front_child = -1, .back_child = -1 },
    },
    .colormap = { 0 },
};

map_t g_maps[MAX_MAPS] = { s_demo_map, {0}, {0} };
int g_num_maps = 1;
int g_demo_level = 0;

/* Fill colormap with a grayscale gradient. */
void build_colormap(map_t *map)
{
    int i;
    for (i = 0; i < 256; i++) {
        map->colormap[i] = gray8((uint8_t)i);
    }
}
