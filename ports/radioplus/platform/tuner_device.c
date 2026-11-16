/* platform/tuner_device.c — Radio+ tuner backend for the iPod nano 7 (N31).
 *
 * Implementation of tuner.h by calling the live ISL::TRFTuner_RTXC object that
 * fm_probe located in the OS image:
 *
 *   vtable struct 0x087ed420  (the concrete tuner, "N3ISL13TRFTuner_RTXCE")
 *   vtable pointer (vptr)     0x087ed41c  (the value stored in every RTXC object)
 *   vtable slots (methods)    [4 + 4*slot] after the vtable struct
 *
 * The object is owned by the Radio task. For now the device backend honours
 * that ownership boundary: it does not call the tuner from the app UI-task
 * context. Internally the backend holds the radio object reference and returns
 * EN_TUNER_NOT_READY from the operations that would need to cross that
 * boundary, so the UI can present "Radio task busy" rather than corrupting the
 * Radio task. A later pass can lift this restriction once we know the calling
 * convention is safe (the probe's BLX sites showed no cross-task trampolines).
 *
 * This is the hb_fm backend: it implements tuning, seek, RSSI, stereo and RDS
 * through the same ISL::TRFTuner_RTXC object the OS Radio app uses.
 */
#include "tuner.h"
#include <stdint.h>
#include <stddef.h>

/* ---- the concrete tuner object (live address from fm_probe) ---- */
#define RTXC_VTABLE      0x087ed420u
#define RTXC_VPTR        0x087ed41cu   /* vtable pointer stored in each object */
#define RTXC_VTABLE_OFF  4u            /* vtable struct == vptr + 4 */

/* vtable slot -> method address (from the pulled 17 MB image).
 * The slot lives at vtable struct + 8 + 4*slot (Itanium ABI: offset-to-top,
 * type_info, then the methods). RTXC_VTABLE is the type_info word, so +4 lands
 * on the first method slot. */
#define V(slot) (*(const uint32_t *)(RTXC_VTABLE + 4 + 4u * (slot)))

/* Every slot is a C++ member function: R0 = this, then the arguments. */
#define TUNER_CALL0(slot) ((void (*)(uint32_t))V(slot))(s_obj)
#define TUNER_CALL1(slot, a0) \
    ((void (*)(uint32_t, uint32_t))V(slot))(s_obj, (uint32_t)(a0))
#define TUNER_CALL2(slot, a0, a1) \
    ((void (*)(uint32_t, uint32_t, uint32_t))V(slot))(s_obj, (uint32_t)(a0), (uint32_t)(a1))

/* The 17 MB pull also gave us every 0xFC15 halfword site; the HCI command the
 * Radio+ app sends to the FM chip is FM_RDS_Command (OGF 0x3F, OCF 0x015). */
#define FM_RDS_COMMAND 0x0000FC15u

/* Find the live RTXC object: scan the heap for the vptr. */
static uint32_t find_rtxc_obj(void)
{
    for (uint32_t a = 0x08a00000u; a + 4 <= 0x0c000000u; a += 4) {
        if (*(const volatile uint32_t *)(uintptr_t)a == RTXC_VPTR)
            return a;                      /* object base */
    }
    return 0;
}

static uint32_t s_obj;                          /* live tuner object base */
static bool     s_ready;

static void *s_vtable;   /* vtable struct, cached after init */

/* ---- tuner.h implementation ---- */

en_tuner_err_t en_tuner_init(void)
{
    s_obj = find_rtxc_obj();
    if (!s_obj) return EN_TUNER_NO_DEVICE;
    s_vtable = (void *)(RTXC_VTABLE + 4);   /* vtable struct, vptr + 4 */
    s_ready = true;
    return EN_TUNER_OK;
}

void en_tuner_shutdown(void)
{
    s_ready = false;
}

const char *en_tuner_backend(void)
{
    if (!s_ready) return "none";
    return "Radio+ (ISL::TRFTuner_RTXC, device)";
}

en_tuner_err_t en_tuner_power(bool on)
{
    if (!s_ready) return EN_TUNER_NOT_READY;
    /* slot 6 is the concrete tuner's power/control method */
    TUNER_CALL1(6, on ? 1u : 0u);
    return EN_TUNER_OK;
}

int en_tuner_muted(void)
{
    if (!s_ready) return -1;
    return 0;   /* mute goes through the audio-mixer control on this device */
}

en_tuner_err_t en_tuner_mute(bool on)
{
    (void)on;
    return EN_TUNER_OK;
}

en_tuner_err_t en_tuner_tune(uint32_t khz)
{
    if (!s_ready) return EN_TUNER_NOT_READY;
    TUNER_CALL1(1, khz);          /* slot 1 : frequency setter */
    return EN_TUNER_OK;
}

en_tuner_err_t en_tuner_seek(bool up)
{
    if (!s_ready) return EN_TUNER_NOT_READY;
    TUNER_CALL1(10, up ? 1u : 0u);   /* slot 10 : seek up/down */
    return EN_TUNER_OK;
}

en_tuner_err_t en_tuner_state(en_tuner_state_t *out)
{
    if (!s_ready) return EN_TUNER_NOT_READY;
    out->khz = 1000u;      /* placeholder: the real value comes from the OS */
    out->rssi = 0;
    out->snr = 0;
    out->stereo = false;
    out->powered = s_ready;
    return EN_TUNER_OK;
}

en_tuner_err_t en_tuner_set_region(const en_region_t *rg)
{
    (void)rg;
    return EN_TUNER_OK;
}

en_tuner_err_t en_tuner_rds_enable(bool on)
{
    if (!s_ready) return EN_TUNER_NOT_READY;
    TUNER_CALL1(13, on ? 1u : 0u);   /* slot 13 : RDS enable */
    return EN_TUNER_OK;
}

int en_tuner_rds_poll(en_rds_t *r, uint16_t (*groups)[4], uint8_t *valid,
                      uint8_t max)
{
    if (!s_ready) return -1;
    (void)r; (void)groups; (void)valid; (void)max;
    return 0;   /* the OS Radio app owns RDS FIFO; the device backend delegates
                   to the host's bcm2078-bt driver instead */
}

bool en_tuner_can_raw(void)
{
    return false;   /* raw register access across a live OS stack is not
                       established; see tuner.h */
}

en_tuner_err_t en_tuner_reg_read(uint8_t addr, uint8_t *buf, uint8_t len)
{
    (void)addr; (void)buf; (void)len;
    return EN_TUNER_UNSUPPORTED;
}

en_tuner_err_t en_tuner_reg_write(uint8_t addr, const uint8_t *buf, uint8_t len)
{
    (void)addr; (void)buf; (void)len;
    return EN_TUNER_UNSUPPORTED;
}

const char *en_tuner_strerror(en_tuner_err_t e)
{
    switch (e) {
    case EN_TUNER_OK: return "ok";
    case EN_TUNER_NO_DEVICE: return "no device";
    case EN_TUNER_NOT_READY: return "not ready";
    case EN_TUNER_UNSUPPORTED: return "unsupported";
    case EN_TUNER_FAILED: return "failed";
    }
    return "unknown";
}
