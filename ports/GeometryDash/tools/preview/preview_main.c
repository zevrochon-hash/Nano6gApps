/* preview_main.c — drive the real game code and save screenshots of every screen. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "platform.h"
#include "gfx.h"
#include "game.h"

float g_preview_logical_w = 480.0f, g_preview_logical_h = 270.0f;
void gfx_write_ppm(const char *path);

static const char *sDataDir = ".";
static char sSaveDir[512];
static int sSoundLog;

uint64_t plat_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ull + (uint64_t)tv.tv_usec;
}

static void *read_file(const char *dir, const char *name, uint32_t *size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *data = malloc((size_t)n + 1);
    if (fread(data, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(data); return NULL; }
    fclose(f);
    ((char *)data)[n] = 0;
    *size = (uint32_t)n;
    return data;
}

void *plat_read_file(const char *name, uint32_t *size, int save)
{
    if (save) return read_file(sSaveDir, name, size);
    return read_file(sDataDir, name, size);
}

int plat_write_file(const char *name, const void *data, uint32_t size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", sSaveDir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(data, 1, size, f);
    fclose(f);
    return 0;
}

void plat_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
void plat_log_flush(void) {}
void plat_sound(int sound, float gain) { if (sSoundLog) fprintf(stderr, "[sound %d %.2f]\n", sound, gain); }
int plat_music_play(const char *name) { if (sSoundLog) fprintf(stderr, "[music %s]\n", name); return 0; }
void plat_music_stop(void) { if (sSoundLog) fprintf(stderr, "[music stop]\n"); }
uint32_t plat_seed(void) { return 12345; }

static void tap(float x, float y)
{
    PlatTouches t;
    memset(&t, 0, sizeof t);
    t.count = 0;
    game_frame(1.0f / 60.0f, &t);       /* release */
    t.count = 1;
    t.pt[0].x = x; t.pt[0].y = y;
    game_frame(1.0f / 60.0f, &t);       /* press */
    t.count = 0;
    game_frame(1.0f / 60.0f, &t);       /* release */
}

static void idle_frames(int n)
{
    for (int i = 0; i < n; i++) {
        PlatTouches t;
        memset(&t, 0, sizeof t);
        game_frame(1.0f / 60.0f, &t);
    }
}

static void shot(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.ppm", sSaveDir, name);
    gfx_write_ppm(path);
    printf("wrote %s (%dx%d)\n", path, (int)g_preview_logical_w, (int)g_preview_logical_h);
}

static void idle(void)
{
    PlatTouches t;
    memset(&t, 0, sizeof t);
    game_frame(0.0f, &t);
    printf("state=%d level=%d px=%.0f py=%.0f mode=%d objects=%d\n",
           G.state, G.level, G.px, G.py, G.mode, G.nobjects);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <data dir> <out dir> [sound]\n", argv[0]);
        return 2;
    }
    sDataDir = argv[1];
    snprintf(sSaveDir, sizeof sSaveDir, "%s", argv[2]);
    sSoundLog = argc > 3;
    G.best[0] = 62;
    G.best[1] = 100;

    if (game_init(480, 270)) {
        fprintf(stderr, "game_init failed\n");
        return 1;
    }
    G.best[0] = 62;
    G.best[1] = 100;

    /* 1. main menu */
    idle(); shot("01_menu");

    /* 2. menu with practice on and a different level selected */
    tap(40.0f, 140.0f);                 /* left arrow -> last level */
    tap(40.0f, 140.0f);                 /* -> level 5 */
    tap(54.0f, 257.0f);                 /* practice toggle */
    idle(); shot("02_menu_practice");

    /* 3. settings */
    tap(457.0f, 20.0f);
    G.settings_row = 1;
    G.state_t = 1.0f;
    idle(); shot("03_settings");
    tap(240.0f, 234.0f);                /* back to menu row */

    /* 4. start Stereo Madness and take a gameplay shot */
    G.level = 0;
    tap(240.0f, 130.0f);                /* play */
    G.px = 700.0f;
    G.py = 12.0f;
    G.vy = 0;
    G.grounded = 1;
    G.camx = G.px - 160.0f;
    G.camy = 0;
    G.state_t = 2.0f;
    idle(); shot("04_play_stereo");

    /* 5. Back on Track palette */
    G.state = 0;
    G.state_t = 1.0f;
    G.level = 1;
    tap(240.0f, 130.0f);
    G.px = 900.0f;
    G.py = 80.0f;
    G.vy = 120.0f;
    G.grounded = 0;
    G.camy = 12.0f;
    G.camx = G.px - 160.0f;
    G.state_t = 2.0f;
    idle(); shot("05_play_btrack");

    /* 5b. chain decorations (the rotated sheet frames) */
    G.px = 3150.0f;
    G.py = 100.0f;
    G.camy = 30.0f;
    G.camx = G.px - 160.0f;
    G.state_t = 2.2f;
    idle(); shot("05b_play_chains");

    /* 5c. a gravity portal (a rotated sheet frame) */
    G.state = 0;
    G.state_t = 1.0f;
    G.level = 6;
    tap(240.0f, 130.0f);
    G.px = 3300.0f;
    G.py = 150.0f;
    G.camy = 55.0f;
    G.camx = G.px - 160.0f;
    G.state_t = 2.0f;
    idle(); shot("05c_play_portal");

    /* 6. ship mode */
    G.state = 0;
    G.state_t = 1.0f;
    G.level = 1;
    tap(240.0f, 130.0f);
    G.px = 900.0f;
    G.py = 110.0f;
    G.vy = 0;
    G.camx = G.px - 160.0f;
    G.camy = 12.0f;
    G.mode = 1;
    G.prot = -6.0f;
    G.state_t = 2.5f;
    idle(); shot("06_play_ship");

    /* 6b. the ship high in a tall section: the per-level ceiling and the
     * vertical camera must let it reach the top of the level */
    G.state = 0;
    G.state_t = 1.0f;
    G.level = 0;
    tap(240.0f, 130.0f);
    G.px = 8100.0f;
    G.py = 360.0f;
    G.vy = 0.0f;
    G.mode = 1;
    G.prot = 0.0f;
    G.camx = G.px - 160.0f;
    G.camy = G.py - 105.0f;
    G.state_t = 2.5f;
    idle(); shot("06b_play_ship_high");

    /* 7. pause */
    G.state = 4;
    G.state_t = 1.0f;
    idle(); shot("07_paused");

    /* 8. death explosion */
    G.state = 2;
    G.state_t = 0.22f;
    G.nshards = GD_SHARDS;
    for (int i = 0; i < GD_SHARDS; i++) {
        float a = (float)i * (6.2831853f / GD_SHARDS);
        G.shard[i].x = G.px + cosf(a) * 22.0f;
        G.shard[i].y = G.py + sinf(a) * 22.0f;
        G.shard[i].vx = 0;
        G.shard[i].vy = 0;
        G.shard[i].life = 0.45f;
    }
    G.flash = 0.03f;
    idle(); shot("08_dead");

    /* 9. level complete */
    G.practice = 0;
    G.state = 3;
    G.state_t = 1.0f;
    G.nshards = 0;
    G.flash = 0;
    G.won = 1;
    G.attempt = 7;
    idle(); shot("09_complete");

    /* 10. gameplay in a green level with settings row highlight */
    G.state = 5;
    G.settings_row = 3;
    G.state_t = 1.0f;
    idle(); shot("10_settings_reset");

    if (getenv("GD_SIM")) {
        /* Run the real play loop for a while: verifies movement, collisions,
         * automatic restart and the save file without a device. Practice was
         * left on by the menu scene; turn it off so the real death/restart
         * path (ST_DEAD) is exercised, not the instant checkpoint respawn. */
        G.state = 0;
        G.level = 0;
        G.practice = 0;
        tap(240.0f, 130.0f);
        int deaths = 0, last = G.state;
        for (int i = 0; i < 1800; i++) {
            PlatTouches t;
            memset(&t, 0, sizeof t);
            if (G.state == 1 && G.grounded && (i % 26) == 0) {
                t.count = 1;
                t.pt[0].x = 240.0f;
                t.pt[0].y = 130.0f;
            }
            game_frame(1.0f / 60.0f, &t);
            if (last != 2 && G.state == 2) deaths++;
            last = G.state;
        }
        printf("sim: state=%d level=%d attempt=%d best=%d px=%.0f deaths=%d\n",
               G.state, G.level, G.attempt, G.best[0], G.px, deaths);

        /* Pause-menu interaction: pause button, then each row. */
        G.state = 1;
        G.state_t = 0.5f;
        G.px = 700.0f;
        G.py = 12.0f;
        G.vy = 0.0f;
        G.grounded = 1;

        /* A tap that starts and ends between two 60 Hz steps must still count:
         * the resident reads touches much faster than the game steps. */
        {
            PlatTouches t;
            memset(&t, 0, sizeof t);
            t.count = 1; t.pt[0].x = 458.0f; t.pt[0].y = 19.0f;
            game_frame(0.0f, &t);
            t.count = 0;
            game_frame(0.0f, &t);
            game_frame(1.0f / 60.0f, &t);
            printf("quick pause: state=%d\n", G.state);
        }
        G.state = 1;
        G.state_t = 0.5f;

        tap(458.0f, 19.0f);
        printf("pause: state=%d\n", G.state);
        idle_frames(20);
        tap(240.0f, 90.0f);
        printf("resume: state=%d\n", G.state);
        tap(458.0f, 19.0f);
        idle_frames(20);
        tap(240.0f, 128.0f);
        printf("restart: state=%d px=%.0f\n", G.state, G.px);
        tap(458.0f, 19.0f);
        idle_frames(20);
        tap(240.0f, 166.0f);
        printf("practice: state=%d practice=%d\n", G.state, G.practice);
        tap(240.0f, 204.0f);
        printf("exit: state=%d\n", G.state);

        /* The ship ceiling now comes from the level, not the old 198. */
        G.state = 0;
        G.level = 0;
        G.practice = 0;
        tap(240.0f, 130.0f);
        printf("ceil: level0=%.0f\n", G.ceil_y);
        G.mode = 1;
        G.px = 8010.0f;
        G.py = G.ceil_y + 40.0f;
        G.vy = 200.0f;
        idle_frames(1);
        printf("ship top: state=%d py=%.0f camy=%.0f ceil=%.0f\n", G.state, G.py, G.camy, G.ceil_y);

        /* A real climb: hold the ship up and check the camera keeps pace. */
        G.state = 0;
        G.level = 0;
        tap(240.0f, 130.0f);
        G.mode = 1;
        G.px = 8010.0f;
        G.py = 165.0f;
        G.vy = 0.0f;
        G.camy = 0.0f;
        for (int i = 0; i < 180; i++) {
            PlatTouches t;
            memset(&t, 0, sizeof t);
            t.count = 1;
            t.pt[0].x = 240.0f;
            t.pt[0].y = 130.0f;
            game_frame(1.0f / 60.0f, &t);
            if (G.state != 1) break;   /* the ceiling is lethal now */
        }
        printf("ship climb: state=%d py=%.0f camy=%.0f ceil=%.0f\n", G.state, G.py, G.camy, G.ceil_y);
    }

    printf("textures=%d bytes=%u draws=%d quads=%d\n",
           gfx_stat_textures, (unsigned)gfx_stat_tex_bytes,
           gfx_stat_draws, gfx_stat_quads);
    return 0;
}
