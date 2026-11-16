/*
 * radio — Radio+ for the iPod nano 7.
 *
 * A polished FM radio NanoApp built on the NanoApps LVGL surface. The tuner is
 * Apple's own ISL::TRFTuner_RTXC object (located by fm_probe), called through
 * its vtable so the OS keeps owning the HCI transport and the Radio task's
 * object lifetime. Nothing here builds an HCI packet.
 *
 * Layout (240x432):
 *   ┌────────────────────────────────┐
 *   │ RADIO+              STEREO  PWR│  header, tint accent
 *   │                                │
 *   │        1 0 1 . 5 0              │  frequency, montserrat 48
 *   │              MHz               │
 *   │                                │
 *   │  SIGNAL  [██████████████░░░░]  │  RSSI bar
 *   │  RDS     Station Name          │  scrolling PS
 *   │                                │
 *   │   ( TUNE- )  ( TUNE+ )         │  circular buttons
 *   │   ( SEEK- )  ( SEEK+ )         │
 *   │   ( MUTE  )  ( POWER )         │
 *   └────────────────────────────────┘
 *
 * Controls: VOL UP/DOWN tune, HOME exits, touch hits the buttons.
 */

#include "hb_sdk.h"
#include "hb_prefs.h"
#include "lvgl/lvgl.h"

/* ---- tuner backend (ISL::TRFTuner_RTXC, from fm_probe) ---- */

#define RTXC_VTABLE  0x087ed420u   /* type_info word; methods start +4 */
#define RTXC_VPTR    0x087ed41cu   /* vptr stored in every RTXC object */

#define V(slot) (*(const uint32_t *)(RTXC_VTABLE + 4u + 4u * (slot)))

static uint32_t s_obj;
static bool     s_ready;
static bool     s_power;
static bool     s_mute;
static uint32_t s_khz   = 101500;   /* 101.50 MHz */
static bool     s_stereo_state;
static int      s_rssi;
static int      s_snr;
static char     s_rds_text[64];

/* Find the live RTXC object: scan the heap for the vptr. */
static uint32_t find_rtxc_obj(void)
{
    for (uint32_t a = 0x08a00000u; a + 4u <= 0x0c000000u; a += 4u) {
        if (*(const volatile uint32_t *)(uintptr_t)a == RTXC_VPTR)
            return a;
    }
    return 0;
}

static void tuner_init(void)
{
    s_obj   = find_rtxc_obj();
    s_ready = (s_obj != 0);
}

static void tuner_power(bool on)
{
    if (!s_ready) return;
    ((void (*)(uint32_t, uint32_t))V(6))(s_obj, on ? 1u : 0u);
    s_power = on;
}

static void tuner_tune(uint32_t khz)
{
    if (!s_ready) return;
    ((void (*)(uint32_t, uint32_t))V(1))(s_obj, khz);
    s_khz = khz;
}

static void tuner_seek(bool up)
{
    if (!s_ready) return;
    ((void (*)(uint32_t, uint32_t))V(10))(s_obj, up ? 1u : 0u);
}

static void tuner_mute(bool on)
{
    s_mute = on;
}

/* ---- UI ---- */

static lv_obj_t *s_freq;
static lv_obj_t *s_mhz;
static lv_obj_t *s_stereo;
static lv_obj_t *s_rssi_bar;
static lv_obj_t *s_rssi_lbl;
static lv_obj_t *s_rds;
static lv_obj_t *s_status;
static lv_obj_t *s_pwr_btn;

static lv_obj_t *s_btn_tune_down;
static lv_obj_t *s_btn_tune_up;
static lv_obj_t *s_btn_seek_down;
static lv_obj_t *s_btn_seek_up;
static lv_obj_t *s_btn_mute;
static lv_obj_t *s_btn_power;

static const uint32_t COL_BG     = 0x0a0c12;
static const uint32_t COL_PANEL  = 0x141824;
static const uint32_t COL_ACCENT = 0x35e0c8;
static const uint32_t COL_TEXT   = 0xf2f4fa;
static const uint32_t COL_DIM    = 0x8a93a6;
static const uint32_t COL_WARN   = 0xffb454;
static const uint32_t COL_RED    = 0xff5470;

static void fmt_freq(char *out, uint32_t khz)
{
    uint32_t mhz = khz / 1000u;
    uint32_t dec = khz % 1000u;
    out[0] = '0' + (mhz / 100u) % 10u;
    out[1] = '0' + (mhz / 10u) % 10u;
    out[2] = '0' + mhz % 10u;
    out[3] = '.';
    out[4] = '0' + (dec / 100u) % 10u;
    out[5] = '0' + (dec / 10u) % 10u;
    out[6] = 0;
}

static void refresh_ui(void)
{
    char f[8];
    fmt_freq(f, s_khz);
    lv_label_set_text(s_freq, f);
    lv_label_set_text(s_mhz, "MHz");

    lv_label_set_text(s_stereo, s_stereo_state ? "STEREO" : "MONO");
    lv_obj_set_style_text_color(s_stereo,
        lv_color_hex(s_stereo_state ? COL_ACCENT : COL_DIM), 0);

    lv_bar_set_value(s_rssi_bar, s_rssi, LV_ANIM_ON);
    char r[24];
    int n = 0;
    const char *lbl = "SIGNAL ";
    while (lbl[n]) r[n] = lbl[n], n++;
    const char *val = s_rssi > 60 ? "STRONG" : s_rssi > 30 ? "GOOD" : "WEAK";
    for (int i = 0; val[i]; i++) r[n++] = val[i];
    r[n] = 0;
    lv_label_set_text(s_rssi_lbl, r);

    if (s_rds_text[0]) {
        lv_label_set_text(s_rds, s_rds_text);
    } else {
        lv_label_set_text(s_rds, s_power ? "No RDS station" : "Tuner off");
    }

    lv_label_set_text(s_status, s_ready ? "ISL::TRFTuner_RTXC" : "no tuner object");
    lv_obj_set_style_text_color(s_status,
        lv_color_hex(s_ready ? COL_DIM : COL_RED), 0);

    lv_label_set_text(s_pwr_btn, s_power ? "POWER" : "POWER");
    lv_obj_set_style_bg_color(s_pwr_btn,
        lv_color_hex(s_power ? COL_ACCENT : COL_PANEL), 0);
    lv_obj_set_style_text_color(s_pwr_btn,
        lv_color_hex(s_power ? COL_BG : COL_TEXT), 0);
}

/* ---- buttons ---- */

enum {
    A_TUNE_DOWN = 1,
    A_TUNE_UP,
    A_SEEK_DOWN,
    A_SEEK_UP,
    A_MUTE,
    A_POWER,
};

static void step_freq(int dir)
{
    int32_t k = (int32_t)s_khz + dir * 100;   /* 100 kHz steps */
    if (k < 87500)  k = 107900;
    if (k > 107900) k = 87500;
    tuner_tune((uint32_t)k);
}

static void on_btn(lv_event_t *e)
{
    int code = (int)(uintptr_t)lv_event_get_user_data(e);
    switch (code) {
    case A_TUNE_DOWN: step_freq(-1); break;
    case A_TUNE_UP:   step_freq(+1); break;
    case A_SEEK_DOWN: tuner_seek(false); break;
    case A_SEEK_UP:   tuner_seek(true);  break;
    case A_MUTE:      tuner_mute(!s_mute); break;
    case A_POWER:
        s_power = !s_power;
        tuner_power(s_power);
        break;
    }
    refresh_ui();
}

static lv_obj_t *make_btn(lv_obj_t *parent, int x, int y,
                          const char *label, int code, uint32_t bg)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 92, 62);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(btn, 31, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_opa(btn, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, on_btn, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)code);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(l);
    return btn;
}

/* ---- per-frame ---- */

static uint32_t s_last_ms = 0;

static void on_tick(void)
{
    uint32_t now = hb_time_uptime_ms();

    /* Physical buttons: VOL = tune. HOME is the OS exit policy. */
    static uint32_t s_last_vol = 0;
    if (now - s_last_ms >= 200) {
        s_last_ms = now;
        if (hb_button_pressed(HB_BTN_VOL_UP) && now - s_last_vol > 200) {
            step_freq(+1);
            s_last_vol = now;
        } else if (hb_button_pressed(HB_BTN_VOL_DOWN) && now - s_last_vol > 200) {
            step_freq(-1);
            s_last_vol = now;
        }
        refresh_ui();
    }
}

HB_APP_ENTRY(payload_entry)
{
    hb_trace_init();

    tuner_init();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Header bar. */
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 240, 40);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "RADIO+");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_pos(title, 12, 8);

    s_stereo = lv_label_create(hdr);
    lv_label_set_text(s_stereo, "MONO");
    lv_obj_set_style_text_font(s_stereo, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_stereo, lv_color_hex(COL_DIM), 0);
    lv_obj_set_pos(s_stereo, 150, 12);

    /* Frequency display. */
    s_freq = lv_label_create(scr);
    lv_obj_set_style_text_color(s_freq, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(s_freq, &lv_font_montserrat_48, 0);
    lv_obj_set_pos(s_freq, 8, 56);

    s_mhz = lv_label_create(scr);
    lv_obj_set_style_text_color(s_mhz, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(s_mhz, &lv_font_montserrat_16, 0);
    lv_label_set_text(s_mhz, "MHz");
    lv_obj_set_pos(s_mhz, 200, 130);

    /* Signal bar. */
    s_rssi_lbl = lv_label_create(scr);
    lv_label_set_text(s_rssi_lbl, "SIGNAL ");
    lv_obj_set_style_text_color(s_rssi_lbl, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(s_rssi_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_rssi_lbl, 16, 180);

    s_rssi_bar = lv_bar_create(scr);
    lv_obj_set_size(s_rssi_bar, 208, 10);
    lv_obj_set_pos(s_rssi_bar, 16, 206);
    lv_bar_set_range(s_rssi_bar, 0, 100);
    lv_obj_set_style_bg_color(s_rssi_bar, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_color(s_rssi_bar, lv_color_hex(COL_ACCENT),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_rssi_bar, 5, 0);
    lv_obj_set_style_radius(s_rssi_bar, 5, LV_PART_INDICATOR);

    /* RDS station name. */
    s_rds = lv_label_create(scr);
    lv_label_set_text(s_rds, "No RDS station");
    lv_obj_set_style_text_color(s_rds, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(s_rds, &lv_font_montserrat_20, 0);
    lv_obj_set_width(s_rds, 208);
    lv_label_set_long_mode(s_rds, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(s_rds, 16, 232);

    /* Status line. */
    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "ISL::TRFTuner_RTXC");
    lv_obj_set_style_text_color(s_status, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_status, 16, 262);

    /* Button grid. */
    int bx1 = 24, bx2 = 124;
    int by1 = 292, by2 = 362;
    s_btn_tune_down = make_btn(scr, bx1, by1, "TUNE -", A_TUNE_DOWN, COL_PANEL);
    s_btn_tune_up   = make_btn(scr, bx2, by1, "TUNE +", A_TUNE_UP,   COL_PANEL);
    s_btn_seek_down = make_btn(scr, bx1, by2, "SEEK -", A_SEEK_DOWN, COL_PANEL);
    s_btn_seek_up   = make_btn(scr, bx2, by2, "SEEK +", A_SEEK_UP,   COL_PANEL);

    /* Mute + power row. */
    s_btn_mute  = make_btn(scr, bx1, 392, "MUTE",  A_MUTE,  COL_PANEL);
    s_btn_power = make_btn(scr, bx2, 392, "POWER", A_POWER, COL_PANEL);

    /* Power on at start. */
    tuner_power(true);
    refresh_ui();

    hb_lv_set_frame_cb(on_tick);
}
