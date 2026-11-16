/*
 * hb_silver_uitask.c — patch an OS class-method slot so resident code runs in
 * that object's context. See hb_silver_uitask.h.
 *
 * Mechanism (verified on iPod nano 7 OS 1.1.2): the controller's method table is
 * a plain array of code pointers in the OS image; overwriting one slot
 * redirects every virtual call through it. Patching the home controller's
 * action-dispatch slot lands our code on the UI task (we picked that slot by which
 * patch made our handler run, not from a labelled table). We flush the I-cache
 * over the patched word so the new pointer is seen, and return the prior value
 * for forwarding.
 */

#include "hb_silver_uitask.h"

#define FN_THUMB(addr) ((uintptr_t)((addr) | 1u))

uint32_t hb_silver_uitask_hook(uint32_t class_vtable, uint32_t slot_off, void *handler)
{
    uint32_t *vt = (uint32_t *)(uintptr_t)class_vtable;
    uint32_t  slot = slot_off / 4u;
    uint32_t  prev = vt[slot];

    vt[slot] = ((uint32_t)(uintptr_t)handler) | 1u;   /* thumb */
    hb_icache_invalidate(class_vtable, slot_off + 8u);
    return prev;
}

/* Emit an absolute jump to `target` (thumb bit set) at dst.
 * ARMv6K/Thumb-1 has no movw/movt, so use the literal-pool form:
 *     ldr r3, [pc, #imm] ; bx r3 ; .word target|1
 * 8 bytes total. Thumb-1 computes the literal address from (dst+4)&~3,
 * so: dst 4-aligned -> pc-base is dst+4, literal sits at dst+4 (imm 0);
 *     dst 2 mod 4   -> pc-base is dst,   literal sits at dst+4 (imm 1).
 * Works at the 2-byte-aligned OS function entries we patch. */
static void hb__emit_jump(uint8_t *dst, uint32_t target)
{
    uint32_t t = target | 1u;
    uint16_t hw = (uint16_t)(0x4B00u | ((((uintptr_t)dst & 2u) ? 1u : 0u)));
    dst[0]=(uint8_t)hw; dst[1]=(uint8_t)(hw>>8);
    dst[2]=0x18; dst[3]=0x47;                       /* bx r3 */
    dst[4]=(uint8_t)t; dst[5]=(uint8_t)(t>>8); dst[6]=(uint8_t)(t>>16); dst[7]=(uint8_t)(t>>24);
}

/* ARMv6K/Thumb-1 absolute jump, 8 bytes (see hb__emit_jump). Callers may rely
 * on only 8 bytes being overwritten at the patch site; the trampoline argument
 * n must cover those 8 bytes. */
static void hb__emit_bw(uint8_t *dst, uint32_t target)
{
    hb__emit_jump(dst, target);
}

/* Like hb_silver_patch_function_n but the entry jump is a 4-byte B.W (so n can be
 * as small as 4 — one Thumb-2 op). Valid only when |hook-func| and |func-tramp|
 * are within B.W's ±16 MB and the relocated `n` prologue bytes are not PC-relative
 * (the rest of the original, from func+n, stays in place and may be PC-relative). */
void *hb_silver_patch_function_bw_stock(uint32_t func, void *hook, void *tramp,
                                        const void *stock, int n)
{
    uint8_t *f = (uint8_t *)(uintptr_t)func;
    uint8_t *t = (uint8_t *)tramp;
    const uint8_t *s = (const uint8_t *)stock;
    int i;

    for (i = 0; i < n; i++)
        t[i] = s[i];
    hb__emit_bw(t + n, func + (uint32_t)n);          /* trampoline -> func+n (rest)  */
    hb__emit_bw(f, (uint32_t)(uintptr_t)hook);       /* func entry -> our hook       */
    hb_icache_invalidate((uint32_t)(uintptr_t)tramp, (uint32_t)n + 6u);
    hb_icache_invalidate(func, 8u);
    return (void *)((uintptr_t)tramp | 1u);          /* callable "original"          */
}

void *hb_silver_patch_function_bw(uint32_t func, void *hook, void *tramp, int n)
{
    return hb_silver_patch_function_bw_stock(func, hook, tramp,
                                              (const void *)(uintptr_t)func, n);
}

/* Same as hb_silver_patch_function but copies `n` prologue bytes (the entry jump
 * is 10 bytes, so the prologue must be >= 10 and `n` must land on an instruction
 * boundary — a 4-byte Thumb-2 op spanning byte 10 needs n=12, etc.). */
void *hb_silver_patch_function_stock(uint32_t func, void *hook, void *tramp,
                                     const void *stock, int n)
{
    uint8_t *f = (uint8_t *)(uintptr_t)func;
    uint8_t *t = (uint8_t *)tramp;
    const uint8_t *s = (const uint8_t *)stock;
    int i;

    for (i = 0; i < n; i++)
        t[i] = s[i];
    hb__emit_jump(t + n, (func + (uint32_t)n) | 1u); /* trampoline -> func+n (rest of original) */
    hb__emit_jump(f, ((uint32_t)(uintptr_t)hook) | 1u);  /* func entry -> our hook */
    hb_icache_invalidate((uint32_t)(uintptr_t)tramp, (uint32_t)n + 14u);
    hb_icache_invalidate(func, 16u);
    return (void *)((uintptr_t)tramp | 1u);          /* callable "original" */
}

void *hb_silver_patch_function_n(uint32_t func, void *hook, void *tramp, int n)
{
    return hb_silver_patch_function_stock(func, hook, tramp,
                                           (const void *)(uintptr_t)func, n);
}

void *hb_silver_patch_function(uint32_t func, void *hook, void *tramp)
{
    return hb_silver_patch_function_n(func, hook, tramp, 10);
}

void hb_silver_repoint_function(uint32_t func, void *hook)
{
    /* Re-aim an already-patched function's entry at a new hook WITHOUT touching
     * its trampoline (its saved prologue + jump-back stay valid). Use on a
     * re-deploy where the hook code moved but the function is still patched. */
    hb__emit_jump((uint8_t *)(uintptr_t)func, ((uint32_t)(uintptr_t)hook) | 1u);
    hb_icache_invalidate(func, 16u);
}

void hb_silver_repoint_function_bw(uint32_t func, void *hook)
{
    /* Re-aim a B.W-patched function (only the 4-byte entry branch is rewritten;
     * its trampoline stays). MUST be used instead of hb_silver_repoint_function
     * for a function patched with hb_silver_patch_function_bw — a 10-byte repoint
     * would clobber the bytes the trampoline jumps back to. */
    hb__emit_bw((uint8_t *)(uintptr_t)func, (uint32_t)(uintptr_t)hook);
    hb_icache_invalidate(func, 8u);
}

static void hb__emit_abs(uint32_t address, void *target)
{
    /* Same ARMv6K literal-pool jump as hb__emit_jump (8 bytes). */
    hb__emit_jump((uint8_t *)(uintptr_t)address, (uint32_t)(uintptr_t)target | 1u);
}

void hb_silver_repoint_function_abs(uint32_t func, void *hook)
{
    if (func & 3u) return;
    hb__emit_abs(func, hook);
    hb_icache_invalidate(func, 8u);
}

void *hb_silver_patch_function_abs_stock(uint32_t func, void *hook, void *tramp,
                                        const void *stock, int n)
{
    if (n < 8 || ((func | (uint32_t)(uintptr_t)tramp | (uint32_t)n) & 3u))
        return 0;
    const uint8_t *s = (const uint8_t *)stock;
    uint8_t *t = (uint8_t *)tramp;
    for (int i = 0; i < n; i++) t[i] = s[i];
    hb__emit_abs((uint32_t)(uintptr_t)(t + n), (void *)(uintptr_t)(func + n));
    hb_icache_invalidate((uint32_t)(uintptr_t)t, (uint32_t)n + 8u);
    hb_silver_repoint_function_abs(func, hook);
    return (void *)((uintptr_t)tramp | 1u);
}

/*
 * The params message holds its payload object pointer in a fixed field, and
 * the payload stores the data pointer in a fixed field; for a named UI event
 * that data is the event-name C-string. (iPod nano 7 OS 1.1.2, read straight out
 * of the named-event handler: payload object at msg+0x08; data pointer at
 * payload+0x0c, after the payload's vtable / type / size words.) These are
 * plain field reads — no method call — so it is safe to call on every event.
 */
#define MSG_PAYLOAD_OBJ_OFF 0x08u
#define PAYLOAD_DATA_OFF    0x0cu

const char *hb_silver_event_name(void *params_msg)
{
    void *payload;
    const char *name;

    if (!params_msg)
        return 0;
    payload = *(void **)((char *)params_msg + MSG_PAYLOAD_OBJ_OFF);
    if ((uintptr_t)payload <= 0x1000u)
        return 0;
    name = *(const char **)((char *)payload + PAYLOAD_DATA_OFF);
    return ((uintptr_t)name > 0x1000u) ? name : 0;
}

/*
 * The controller turns a raw input message into an event-name string via a
 * method that takes `this`, the raw message, and two output string slots. Each
 * string slot is a single word: a pointer to the character data, with an
 * "empty" slot pointing at a fixed shared buffer. So we init two scratch words
 * to that shared-empty pointer, call the method, copy out the resulting
 * characters, then release each slot via the string release fn — no
 * constructor needed.
 *
 * 6G addresses (derived from stock rawmsg 0x08171ac1):
 *   translator  0x0825a618 (7G: 0x084aa17a)
 *   release     0x081892c0 (7G: 0x0842d8f4)
 *   empty rep   0x08b821d4 (7G: 0x08d164e4)
 * The 6G translator uses msg->vt[0x18]/vt[0x1c]; the 7G one uses
 * vt[0x14]/vt[0x18] (one extra earlier slot on the 6G message class).
 */
#define A_TRANSLATE   HB6_A_TRANSLATE
#define A_STR_UNLINK  HB6_A_STR_UNLINK   /* string release(this)            */
#define STR_EMPTY_REP HB6_STR_EMPTY_REP  /* shared empty-string data pointer */

typedef void (*translate_t)(void *self, void *msg, void *out, void *out_alt);
typedef void (*str_unlink_t)(void *str_obj);

const char *hb_silver_translate_msg(void *self, void *raw_msg, char *out_buf, int buf_len)
{
#if !HB_SILVER_HAVE_MSG_TRANSLATE
    /* 6G: no translator twin yet — never call the 7G address. */
    (void)self;
    (void)raw_msg;
    if (buf_len > 0)
        out_buf[0] = 0;
    return out_buf;
#else
    void *s1 = (void *)STR_EMPTY_REP;       /* empty string slot */
    void *s2 = (void *)STR_EMPTY_REP;
    const char *p;
    int i = 0;

    if (buf_len > 0)
        out_buf[0] = 0;
    if (!self || !raw_msg || buf_len < 1)
        return out_buf;

    ((translate_t)FN_THUMB(A_TRANSLATE))(self, raw_msg, &s1, &s2);

    p = (const char *)s1;                   /* s1 word = translated data ptr */
    if ((uintptr_t)p > 0x1000u) {
        /* 6G string object: length at data-4 (stock rawmsg reads the same
         * word to decide whether anything was translated). Bound the copy by
         * both that length and the caller's buffer. */
        uint32_t len = *(const uint32_t *)(p - 4);
        if (len > (uint32_t)(buf_len - 1))
            len = (uint32_t)(buf_len - 1);
        for (; i < (int)len && p[i]; i++)
            out_buf[i] = p[i];
    }
    out_buf[i] = 0;

    ((str_unlink_t)FN_THUMB(A_STR_UNLINK))(&s1);
    ((str_unlink_t)FN_THUMB(A_STR_UNLINK))(&s2);
    return out_buf;
#endif
}

#if HB_SILVER_HAVE_MSG_TRANSLATE
/* Event dispatcher the stock 6G raw-input handler (0x08171ac1) calls with
 * (self, &s1, &s2, 0, raw_msg) after translating. 7G used 0x081c42e4. */
#define A_DISPATCH_SE 0x082b9d78u
#define VT_GET_VIEW   0x158u    /* self->vtable[0x158] = current-view getter   */
#define VT_HANDLEMSG  0x44u     /* view->vtable[0x44]  = message handler       */

typedef int   (*dispatch_se_t)(void *self, void *ev, void *alt, int from_param, void *msg);
typedef void *(*get_view_t)(void *self);
typedef int   (*handle_msg_t)(void *self, void *msg);

/* Forward an unconsumed message to the current view, exactly as the stock
 * raw-input handler does: call the getter once and bail if it returns 0, then
 * call it again and invoke the view's message handler. Returns its result. */
static int hb__forward_to_view(void *self, void *raw_msg)
{
    void **self_vt = *(void ***)self;
    void *view;

    if (!((get_view_t)self_vt[VT_GET_VIEW / 4u])(self))
        return 0;
    view = ((get_view_t)self_vt[VT_GET_VIEW / 4u])(self);
    if (!view)
        return 0;
    return ((handle_msg_t)(*(void ***)view)[VT_HANDLEMSG / 4u])(view, raw_msg);
}
#endif /* HB_SILVER_HAVE_MSG_TRANSLATE */

static int hb__prefix(const char *s, const char *p)
{ while (*p) { if (*s != *p) return 0; s++; p++; } return 1; }

static int hb__suffix(const char *s, const char *suf)
{
    int ls = 0, lf = 0;
    while (s[ls]) ls++;
    while (suf[lf]) lf++;
    if (lf > ls) return 0;
    for (int i = 0; i < lf; i++)
        if (s[ls - lf + i] != suf[i]) return 0;
    return 1;
}

/* Breadcrumbs are compiled only into the resident (HB_RAWMSG_TRACE). The
 * untether bootstrap links this file too, but it lives inside the firmware
 * image and its BSS is not cleared across a warm reboot, so it must not gain
 * a function-pointer global. */
#ifdef HB_RAWMSG_TRACE
void (*hb_silver_rawmsg_trace)(const char *tag, uint32_t v1, uint32_t v2);
#endif

int hb_silver_rawmsg(void *self, void *raw_msg, const char *claim_prefix,
                     const char *claim_suffix, char *out_buf, int buf_len)
{
#if !HB_SILVER_HAVE_MSG_TRANSLATE
    /* 6G: translator + string-release + SE-dispatch twins are not derived.
     * Claim nothing and let the caller forward the message to the stock
     * handler; touching A_TRANSLATE here would jump into 7G code. */
    (void)self;
    (void)raw_msg;
    (void)claim_prefix;
    (void)claim_suffix;
    if (buf_len > 0)
        out_buf[0] = 0;
    return 0;
#else
    void *s1 = (void *)STR_EMPTY_REP;
    void *s2 = (void *)STR_EMPTY_REP;
    const char *p;
    int i = 0, claimed = 0, r = 0;

    if (buf_len > 0)
        out_buf[0] = 0;
    if (!self || !raw_msg)
        return 0;

    /* The stock handler swallows the message without translating when the
     * list at self+0xdc is non-empty (it computes (end-start)>>2 and returns 1
     * if non-zero). Mirror that so a message the OS would consume is not
     * re-routed here. */
    {
        uint32_t lo = *(volatile uint32_t *)((char *)self + HB6_RAWMSG_BUSY_OFF);
        uint32_t hi = *(volatile uint32_t *)((char *)self + HB6_RAWMSG_BUSY_OFF + 4u);
        if (((int32_t)(hi - lo) >> 2) != 0)
            return 1;
    }

    ((translate_t)FN_THUMB(A_TRANSLATE))(self, raw_msg, &s1, &s2);

    p = (const char *)s1;
    if ((uintptr_t)p > 0x1000u && buf_len > 0) {
        uint32_t len = *(const uint32_t *)(p - 4);
        if (len > (uint32_t)(buf_len - 1))
            len = (uint32_t)(buf_len - 1);
        for (; i < (int)len && p[i]; i++)
            out_buf[i] = p[i];
        out_buf[i] = 0;
    }

    /* claim only events that match BOTH the prefix and the suffix, so e.g.
     * a tap on one of our items is ours; everything else (other suffixes, other
     * items) passes through to the OS unchanged. */
    if (out_buf[0] && claim_prefix && hb__prefix(out_buf, claim_prefix)
                   && (!claim_suffix || hb__suffix(out_buf, claim_suffix)))
        claimed = 1;

    /* Record sbitem event names in 8-char chunks (tag = text, v1 = offset). */
#ifdef HB_RAWMSG_TRACE
    if (hb_silver_rawmsg_trace && hb__prefix(out_buf, "sbitem.")) {
        for (int k = 0; k < 32 && out_buf[k]; k += 8)   /* icon taps only */
            hb_silver_rawmsg_trace(out_buf + k, (uint32_t)k, (uint32_t)claimed);
    }
#endif

    if (!claimed) {
        if (out_buf[0])                     /* decoded but not ours: dispatch once */
            r = ((dispatch_se_t)FN_THUMB(A_DISPATCH_SE))(self, &s1, &s2, 0, raw_msg);
        /* untranslatable input, or an event this controller didn't consume:
         * hand it to the current view (stock behaviour — this is what carries
         * scroll/swipe). Strings are released after, as in the original. */
        if (r == 0)
            r = hb__forward_to_view(self, raw_msg);
    }

    ((str_unlink_t)FN_THUMB(A_STR_UNLINK))(&s1);
    ((str_unlink_t)FN_THUMB(A_STR_UNLINK))(&s2);
    return claimed ? HB_SILVER_RAWMSG_CLAIMED : r;
#endif
}
