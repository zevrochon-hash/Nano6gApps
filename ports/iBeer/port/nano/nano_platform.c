/* NanoApps SDK adapter for the iBeer port.
 *
 * Assets come from /Data/iBeer (pages, atlas, font, background, sounds). Tilt
 * comes from the OS accelerometer task; touches come from the resident's
 * latched mailbox. The game is paced to 60 Hz as in the other GL ports.
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "hb_sdk.h"
#include "hb_heap.h"
#include "hb_surface_input.h"
#include "../src/platform.h"
#include "../src/gfx.h"
#include "../src/game.h"

#define DATA_DIR "/Data/iBeer"
extern void *memalign(size_t, size_t);
extern void port_log_flush(const char *path);
static struct { char name[48]; uint32_t size; } files[80];
static int file_count, initialized, failed;
uint8_t ib_fast_redraw;
#define STEP_US 16667u

uint64_t plat_time_us(void)
{
    static uint32_t last;
    static uint64_t high;
    uint32_t now = hb_time_uptime_us();
    if (now < last) high += 1ull << 32;
    last = now;
    return high | now;
}

uint32_t plat_seed(void)
{
    uint32_t t = hb_time_uptime_us();
    return (t >> 4) & 0x7fffffffu;
}

static void *read_file(const char *path, uint32_t expected, uint32_t *size)
{
    if (expected > 8u * 1024u * 1024u) return NULL;
    uint32_t cap = ((expected + 1u + 4095u) & ~4095u) + 4096u;
    unsigned char *data = memalign(64, cap);
    if (!data) return NULL;
    uint32_t got = hb_fs_read(path, data, cap);
    if (!got || got > expected) { free(data); return NULL; }
    data[got] = 0;
    if (size) *size = got;
    return data;
}

static int manifest(void)
{
    uint32_t size;
    char *text = read_file(DATA_DIR "/files.lst", 8192, &size), *p;
    if (!text) return -1;
    file_count = 0;
    for (p = text; *p && file_count < 80;) {
        unsigned len = 0;
        uint32_t n = 0;
        while (*p && *p != ' ' && *p != '\n' && len < 47) files[file_count].name[len++] = *p++;
        files[file_count].name[len] = 0;
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') {
            n = n * 10 + (unsigned)(*p++ - '0');
            if (n > 8u * 1024u * 1024u) { free(text); return -1; }
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
        if (len && n) { files[file_count].size = n; file_count++; }
    }
    free(text);
    return file_count ? 0 : -1;
}

void *plat_read_file(const char *name, uint32_t *size, int save)
{
    char path[128];
    uint32_t expected = 16;
    if (strstr(name, "..") || name[0] == '/') return NULL;
    if (!save) {
        int i;
        for (i = 0; i < file_count; i++) if (!strcmp(files[i].name, name)) break;
        if (i == file_count) return NULL;
        expected = files[i].size;
    }
    snprintf(path, sizeof path, DATA_DIR "/%s", name);
    return read_file(path, expected, size);
}

int plat_write_file(const char *name, const void *data, uint32_t size)
{
    char path[128];
    if (!size || size > 65536 || strstr(name, "..") || name[0] == '/') return -1;
    uint32_t cap = (size + 4095u) & ~4095u;
    void *buf = memalign(64, cap);
    if (!buf) return -1;
    memset(buf, 0, cap);
    memcpy(buf, data, size);
    snprintf(path, sizeof path, DATA_DIR "/%s", name);
    hb_fs_mkdir(DATA_DIR);
    int result = hb_fs_write(path, buf, size) ? 0 : -1;
    if (result) plat_log("write %s failed", path);
    free(buf);
    return result;
}

void plat_log_flush(void) { port_log_flush(DATA_DIR "/log.txt"); }

void plat_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');
}

/* The OS accelerometer task keeps a 3-axis sample in DRAM; the scale is 1 g
 * = 1000. Axis convention is portrait, screen toward the user. */
int plat_accel(int32_t out[3])
{
    hb_accel_read_milli_g(out);
    return 1;
}

/* The OS audio stack is single-shot: a second spawn while a long track is
 * still loading clobbers the shared descriptor and panics the device. Keep
 * every trigger separated, and never let a UI tap retrigger the pour. */
static uint64_t last_audio;
static uint64_t music_busy_until;

static int audio_ready(void)
{
    uint64_t now = plat_time_us();
    if (last_audio && now - last_audio < 2500000ull) return 0;
    last_audio = now;
    return 1;
}

void plat_sound(int sound, float gain)
{
    static const char *names[IB_SND_COUNT] = {
        "pour.wav", "burp.wav", "fizz.wav", "break.wav", "click.wav", "blip.wav"
    };
    (void)gain;
    if (sound < 0 || sound >= IB_SND_COUNT) return;
    if (plat_time_us() < music_busy_until) return;
    if (!audio_ready()) return;
    char path[128];
    snprintf(path, sizeof path, DATA_DIR "/%s", names[sound]);
    hb_audio_play_wav_main(path, 0x6000);
    /* a 187 KB track takes a moment; keep taps from spawning a second worker */
    music_busy_until = plat_time_us() + 1200000ull;
}

void port_crumb(const char *tag, uint32_t a, uint32_t b) { hb_trace_log(tag, a, b); }

/* Keep the screen lit: the OS does not see the touches the resident hands us. */
static void keep_awake(void)
{
    static uint64_t last;
    uint64_t now = plat_time_us();
    if (last && now - last < 10000000ull) return;
    last = now;
    typedef void *(*instance_fn)(void);
    typedef void (*event_fn)(void *, int);
    void *manager = ((instance_fn)(0x0842ae80u | 1u))();
    if (manager) ((event_fn)(0x084069d8u | 1u))(manager, 4);
}

void ib_nano_frame(int w, int h, uint32_t frame)
{
    static uint64_t last, previous_end;
    static uint32_t previous_frame;
    static int32_t owed;
    if (w < 1 || h < 1 || failed) return;
    if (initialized && frame < previous_frame) { game_gl_lost(); initialized = 0; last = 0; }
    previous_frame = frame;
    if (!initialized) {
        if (manifest() || game_init((float)w, (float)h)) {
            plat_log("iBeer initialization failed");
            port_log_flush(DATA_DIR "/log.txt");
            failed = 1;
            return;
        }
        initialized = 1;
        owed = 0;
        plat_log("ready: heap=%u largest=%u texture=%u", hb_os_heap_free(),
                 hb_os_heap_largest(), gfx_stat_tex_bytes);
        port_log_flush(DATA_DIR "/log.txt");
    }
    keep_awake();
    uint64_t now = plat_time_us();
    uint32_t period = last ? (uint32_t)(now - last) : STEP_US;
    if (period > 100000u) period = 100000u;
    last = now;
    owed += (int32_t)period;
    int steps = owed < (int32_t)(STEP_US / 2u) ? 0 : owed < (int32_t)(STEP_US * 3u / 2u) ? 1 : 2;
    owed -= steps * (int32_t)STEP_US;
    if (owed > (int32_t)STEP_US) owed = (int32_t)STEP_US;

    hb_spoint_t finger;
    hb_surface_touch_read(&finger);
    PlatTouches touch = { 0 };
    if (finger.down) {
        touch.count = 1;
        /* panel pixels -> logical canvas (240x432, full bleed) */
        touch.pt[0].x = finger.x * 240.0f / (float)w;
        touch.pt[0].y = finger.y * 432.0f / (float)h;
    }
    for (int i = 0; i < steps; i++) game_frame(STEP_US * 1e-6f, &touch);
    if (!steps) game_frame(0.0f, &touch);
    previous_end = plat_time_us();
    if (ib_fast_redraw) {
        static uint64_t slot;
        if (!slot || now > slot + STEP_US || now + STEP_US < slot) slot = now;
        slot += STEP_US;
        uint64_t until = slot - 2000;
        while (plat_time_us() < until) {}
    }
    (void)previous_end;
}
