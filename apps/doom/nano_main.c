/*
 * nano_main.c — iPod nano 7G entry point for the Freedoom port.
 *
 * Landscape Doom on a portrait 240x432 panel: we render into a logical
 * 432x240 canvas (see doom.h) and doom_present() rotates it onto the
 * framebuffer passed to us by the raw-surface runtime (row stride 240).
 *
 * Storage layout on device:
 *   /Data/Doom/doom.pak        assets (see tools/convert_assets.py)
 *   /Data/Doom/sfx/<name>.wav  sounds
 */
#include "hb_raw_surface.h"
#include "hb_sdk.h"
#include "hb_heap.h"

#include "src/doom.h"

#define PAK_PATH "/Data/Doom/doom.pak"

/* raw-surface apps have no hb_wake_lock (that shim is LVGL-only), so reset the
 * OS idle clock like a touch every ~8 s, using the same two OS entry points the
 * LVGL path uses. */
#define ADDR_SYSMODEL_GETINST   0x0842ae80u
#define ADDR_SYSMODEL_SENDEVENT 0x084069d8u
#define HB_KEVENT_TOUCHACTIVITY 4
static void wake_poke(void)
{
    typedef void *(*gi_t)(void);
    typedef void  (*se_t)(void *, int);
    void *m = ((gi_t)(ADDR_SYSMODEL_GETINST | 1u))();
    if (m) ((se_t)(ADDR_SYSMODEL_SENDEVENT | 1u))(m, HB_KEVENT_TOUCHACTIVITY);
}

static const char *const SFX_PATH[SFX_COUNT] = {
    [SFX_PISTOL] = "/Data/Doom/sfx/pistol.wav",
    [SFX_SHOTGUN] = "/Data/Doom/sfx/shotgun.wav",
    [SFX_DOOR]    = "/Data/Doom/sfx/door.wav",
    [SFX_ITEM]    = "/Data/Doom/sfx/item.wav",
    [SFX_WEAPON]  = "/Data/Doom/sfx/weapon.wav",
    [SFX_PAIN]    = "/Data/Doom/sfx/pain.wav",
    [SFX_DEATH]   = "/Data/Doom/sfx/death.wav",
    [SFX_OOF]     = "/Data/Doom/sfx/oof.wav",
    [SFX_SWITCH]  = "/Data/Doom/sfx/switch.wav",
    [SFX_POSSIT]  = "/Data/Doom/sfx/possit.wav",
    [SFX_POSDTH]  = "/Data/Doom/sfx/posdth.wav",
    [SFX_BGSIT]   = "/Data/Doom/sfx/bgsit.wav",
    [SFX_BGDTH]   = "/Data/Doom/sfx/bgdth.wav",
    [SFX_SARGSIT] = "/Data/Doom/sfx/sargsit.wav",
    [SFX_SARGDTH] = "/Data/Doom/sfx/sargdth.wav",
    [SFX_FIRXPL]  = "/Data/Doom/sfx/firxpl.wav",
    [SFX_BAREXP]  = "/Data/Doom/sfx/barexp.wav",
    [SFX_NOWAY]   = "/Data/Doom/sfx/noway.wav",
    [SFX_PLROOF]  = "/Data/Doom/sfx/oof.wav",
    [SFX_FIRSHT]  = "/Data/Doom/sfx/firsht.wav",
    [SFX_CLAW]    = "/Data/Doom/sfx/claw.wav",
    [SFX_POSACT]  = "/Data/Doom/sfx/posact.wav",
    [SFX_SGTSIT]  = "/Data/Doom/sfx/sargsit.wav",
};

static uint8_t   s_pack_ok;
static uint32_t  s_sound_ms[SFX_COUNT];
static int       s_down_prev;
static int       s_press_cx, s_press_cy;
static int       s_prev_cx, s_prev_cy;
static int       s_moved;
static uint32_t  s_press_ms;
static int       s_btn_fire_prev, s_btn_use_prev;

static void nzero(void *p, int n)
{
    uint8_t *q = (uint8_t *)p;
    while (n-- > 0) *q++ = 0;
}

static int iabs(int v) { return v < 0 ? -v : v; }

void doom_sound(int id)
{
    uint32_t now;
    if (id < 0 || id >= SFX_COUNT || !SFX_PATH[id]) return;
    now = hb_time_uptime_ms();
    if (now - s_sound_ms[id] < 60) return;      /* same sound retriggering */
    s_sound_ms[id] = now;
    hb_audio_play_wav_main(SFX_PATH[id], 0x5fff);
}

/* ------------------------------------------------------------ surface setup */

void hb_raw_init(int w, int h)
{
    (void)w; (void)h;
    nzero(s_sound_ms, sizeof(s_sound_ms));
    g_accel_fn = hb_accel_read_raw;    /* optional tilt-to-look (pause menu) */
    wake_poke();

    g_cv = (uint32_t *)hb_os_alloc(GW * GH * sizeof(uint32_t));
    g_depth = (uint16_t *)hb_os_alloc(GW * VIEW_H * sizeof(uint16_t));

    if (g_cv && g_depth) {
        uint32_t size = hb_fs_size(PAK_PATH);
        if (size >= 1024 && size < 8u * 1024 * 1024) {
            uint8_t *pak = (uint8_t *)hb_os_alloc(size);
            if (pak && hb_fs_read(PAK_PATH, pak, size) == size) {
                if (doom_load_pack(pak, size) == 0)
                    s_pack_ok = 1;
                else
                    hb_os_free(pak);
            } else if (pak) {
                hb_os_free(pak);
            }
        }
    }

    g_now_ms = hb_time_uptime_ms();
    if (s_pack_ok) {
        doom_start_game(0);          /* sets ST_GAME with fresh player state */
        g_state = ST_TITLE;          /* but boot to the title screen          */
        g_state_ms = g_now_ms;
    }
}

/* ------------------------------------------------------------- per frame */

void hb_raw_frame(const hb_spoint_t *t)
{
    int cx, cy;
    input_t in;
    uint32_t *fb;

    nzero(&in, sizeof(in));
    g_now_ms = hb_time_uptime_ms();
    g_frame++;
    if ((g_frame % 480) == 0) wake_poke();   /* ~every 8 s at 60 fps */

    if (!s_pack_ok) {
        /* plain dark-red screen: pack missing or failed to load */
        fb = hb_raw_fb();
        if (fb) hb_raw_fill(0x280008);
        return;
    }

    /* portrait touch (px,py) -> landscape canvas (lx,ly) */
    cx = t->y;
    cy = 239 - t->x;

    if (t->down) {
        if (!s_down_prev) {
            s_press_cx = cx; s_press_cy = cy;
            s_prev_cx = cx;  s_prev_cy = cy;   /* dx/dy = 0 on the first frame */
            s_moved = 0;
            s_press_ms = g_now_ms;
        } else {
            s_moved += iabs(cx - s_prev_cx) + iabs(cy - s_prev_cy);
        }
        in.down = 1;
        in.x = cx; in.y = cy;
        in.press_x = s_press_cx; in.press_y = s_press_cy;
        in.dx = cx - s_prev_cx; in.dy = cy - s_prev_cy;
        s_prev_cx = cx; s_prev_cy = cy;
    } else {
        if (s_down_prev && s_moved < 12 && g_now_ms - s_press_ms < 500) {
            in.tap = 1;             /* released quickly with little drift */
            in.tap_x = s_prev_cx; in.tap_y = s_prev_cy;
        }
        in.x = s_prev_cx; in.y = s_prev_cy;
    }
    s_down_prev = t->down;

    /* transport buttons: play/pause and vol+ fire, vol- uses / opens */
    {
        int f = hb_button_pressed(HB_BTN_PLAY_PAUSE) || hb_button_pressed(HB_BTN_VOL_UP);
        int u = hb_button_pressed(HB_BTN_VOL_DOWN);
        if (f && !s_btn_fire_prev) in.btn_fire = 1;
        if (u && !s_btn_use_prev) in.btn_use = 1;
        s_btn_fire_prev = f; s_btn_use_prev = u;
    }

    doom_frame(&in);
    doom_present(hb_raw_fb());
}
