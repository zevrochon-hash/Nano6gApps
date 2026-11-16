/*
 * preview.c — host build harness for the nano Doom engine.
 *
 * Loads build/assets/doom.pak, renders a handful of scripted views into the
 * logical 432x240 canvas and dumps them as PPM files (converted to PNG by
 * the caller).  The point is to iterate on the renderer without the device.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/doom.h"

void doom_sound(int id) { (void)id; }

static void dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int x, y;
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", GW, GH);
    for (y = 0; y < GH; y++) {
        for (x = 0; x < GW; x++) {
            uint32_t c = g_cv[y * GW + x];
            uint8_t rgb[3];
            rgb[0] = (uint8_t)(c >> 16);
            rgb[1] = (uint8_t)(c >> 8);
            rgb[2] = (uint8_t)(c);
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    printf("wrote %s\n", path);
}

static void render_frame(uint32_t *fb)
{
    /* replicate what doom_frame does for ST_GAME, but headless */
    r_draw_world();
    r_draw_sprites();
    r_draw_weapon();
    ui_draw_hud();
    (void)fb;
}

static void dump_state(const char *name, int state)
{
    input_t in;
    memset(&in, 0, sizeof(in));
    g_state = state;
    g_now_ms = 5000;
    doom_frame(&in);
    dump_ppm(name);
}

static void dump_view(const char *name, int angle_delta, int dx, int dy)
{
    input_t in;
    memset(&in, 0, sizeof(in));
    doom_start_game(1);          /* demo setup: shotgun, blue key, 200hp */
    g_player.angle = (int16_t)(g_player.angle + angle_delta);
    g_player.x += dx << 16;
    g_player.y += dy << 16;
    g_state = ST_GAME;
    render_frame(0);
    dump_ppm(name);
}

static void nzero(void *p, int n)
{
    uint8_t *q = (uint8_t *)p;
    while (n-- > 0) *q++ = 0;
}

static int torture_test(void)
{
    input_t in;
    unsigned rs = 12345;
    int i;
    doom_start_game(1);
    g_state = ST_GAME;
    for (i = 0; i < 1500; i++) {
        nzero(&in, sizeof(in));
        rs = rs * 1103515245u + 12345u;
        in.down = (int)((rs >> 3) & 1);
        in.press_x = 300; in.press_y = 120;             /* right half: turn */
        in.x = 300 + (int)((rs >> 20) & 63);
        in.y = 120 + (int)((rs >> 24) & 31);
        in.dx = (int)((rs >> 10) & 7) - 3;
        in.dy = (int)((rs >> 13) & 7) - 3;
        in.press_y = in.y;
        if (i % 13 == 0) { in.tap = 1; in.tap_x = 120; in.tap_y = 120; }
        if (i % 97 == 20) in.btn_fire = 1;
        if (i % 211 == 40) in.btn_use = 1;
        doom_frame(&in);
        if (g_state == ST_DEAD || g_state == ST_WIN) {
            printf("torture: player ended frame %d with state %d (hp %d)\n",
                   i, g_state, g_player.health);
            break;
        }
    }
    printf("torture: survived to frame %d state=%d hp=%d armor=%d pos=(%ld,%ld)\n",
           i, g_state, g_player.health, g_player.armor,
           (long)(g_player.x >> 16), (long)(g_player.y >> 16));
    return 0;
}

static int exit_test(void)
{
    input_t in;
    memset(&in, 0, sizeof(in));
    doom_start_game(0);          /* normal game (no cheats) */
    g_state = ST_GAME;
    /* teleport next to the exit line (special 11) */
    g_player.x = -(400 << 16);
    g_player.y = (1296 << 16);
    if (g_exit_line >= 0)
        g_player.sector = (int16_t)g_line[g_exit_line].fsec;
    g_player.floor_z = g_sec[g_player.sector].floor_h;
    nzero(&in, sizeof(in));
    in.btn_use = 1;
    doom_frame(&in);
    printf("exit test: state after use = %d (ST_WIN=%d)\n", g_state, ST_WIN);
    nzero(&in, sizeof(in));
    doom_frame(&in);             /* renders the win screen */
    dump_ppm("build/preview-out/win.ppm");
    return 0;
}

int main(int argc, char **argv)
{
    const char *pak_path = argc > 1 ? argv[1] : "build/assets/doom.pak";
    const char *outdir = argc > 2 ? argv[2] : ".";
    char out[512];
    FILE *f = fopen(pak_path, "rb");
    uint8_t *pak;
    long size;

    if (!f) { fprintf(stderr, "no pack %s\n", pak_path); return 1; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    pak = (uint8_t *)malloc((size_t)size);
    if (fread(pak, 1, (size_t)size, f) != (size_t)size) return 1;
    fclose(f);

    g_cv = (uint32_t *)malloc(GW * GH * sizeof(uint32_t));
    g_depth = (uint16_t *)malloc(GW * VIEW_H * sizeof(uint16_t));
    memset(g_cv, 0, GW * GH * sizeof(uint32_t));

    g_now_ms = 1000;
    if (doom_load_pack(pak, (uint32_t)size) != 0) {
        fprintf(stderr, "pack load failed\n");
        return 2;
    }
    printf("pack ok: %d textures, %d flats, %d sprites, %d gfx, %d map lines\n",
           g_ntex, g_nflat, g_nspr, g_ngfx, g_nline);

    if (argc > 3 && strcmp(argv[3], "torture") == 0) {
        torture_test();
        return 0;
    }
    if (argc > 3 && strcmp(argv[3], "exit") == 0) {
        exit_test();
        return 0;
    }
    if (argc > 3 && strcmp(argv[3], "flash") == 0) {
        input_t in;
        memset(&in, 0, sizeof(in));
        doom_start_game(1);
        g_player.damage_flash = 150;
        g_state = ST_GAME;
        r_draw_world();
        r_draw_sprites();
        r_draw_weapon();
        ui_draw_hud();
        dump_ppm("build/preview-out/flash.ppm");
        return 0;
    }
    if (argc > 3 && strcmp(argv[3], "dead") == 0) {
        input_t in;
        memset(&in, 0, sizeof(in));
        doom_start_game(1);
        g_player.dead = 1;
        g_state = ST_DEAD;
        r_draw_world();
        r_draw_sprites();
        ui_draw_hud();
        ui_draw_dead();
        dump_ppm("build/preview-out/dead.ppm");
        return 0;
    }

    snprintf(out, sizeof(out), "%s/title.ppm", outdir);
    dump_state(out, ST_TITLE);
    snprintf(out, sizeof(out), "%s/menu.ppm", outdir);
    dump_state(out, ST_MENU);
    snprintf(out, sizeof(out), "%s/controls.ppm", outdir);
    dump_state(out, ST_CONTROLS);
    snprintf(out, sizeof(out), "%s/about.ppm", outdir);
    dump_state(out, ST_ABOUT);
    snprintf(out, sizeof(out), "%s/game_start.ppm", outdir);
    dump_view(out, 0, 0, 0);
    snprintf(out, sizeof(out), "%s/game_right.ppm", outdir);
    dump_view(out, 8192, 0, 0);
    snprintf(out, sizeof(out), "%s/game_back.ppm", outdir);
    dump_view(out, 32768, 0, 0);
    snprintf(out, sizeof(out), "%s/game_left.ppm", outdir);
    dump_view(out, -8192, 0, 0);
    snprintf(out, sizeof(out), "%s/pause.ppm", outdir);
    {
        input_t in;
        memset(&in, 0, sizeof(in));
        doom_start_game(1);
        g_state = ST_PAUSE;
        r_draw_world();
        r_draw_sprites();
        ui_draw_hud();
        ui_draw_pause();
        dump_ppm(out);
    }
    return 0;
}
