/*
 * hb_silver_res.c — runtime custom-resource serving + home-screen items.
 * nano 6G build: all addresses are for the nano 6G OS 1.2 image
 * (decrypted/n6_os_image.bin). The nano 7 originals are kept in comments.
 *
 * Differences vs the nano 7 build:
 *   - The 6G springboard model has NO SB_REGISTER twin. The registration
 *     path is MODEL virtual +0xc8 (fn 0x081dccec) called with a 6-word
 *     descriptor sitting at the bottom of the CALLER's stack frame:
 *
 *         desc = {d0, d1, d2, d3, d4, d5}
 *         MODEL vt+0xc8(model, id_str, zero, label_id);   (r2 = 0, r3 = label_id)
 *
 *     d0/d1 are stored into the new item's +0x38/+0x14 slots and passed to
 *     the attach helper 0x8308690 (plain next/prev chain link) only when
 *     non-zero. d2==0 (or -1) -> item +0x44 = 0 (attach skipped);
 *     d3==0 (or -1) -> item +0x20 = 0 (attach skipped);
 *     d4==-1 -> item +0x50 = 0x08b821d3 (default); d5==-1 -> item +0x2c =
 *     0x0dad02c0 (default). This port passes {0, 0, 0, image_id, -1, -1}:
 *     the item carries exactly one attachment — the image id at +0x20 —
 *     which is what the bitmap-cache alt-resolver hook serves. See
 *     Chat.txt 2026-11-13 and NANO6_PLAN.md "Done".
 *   - The 6G resource model is registry-of-registries (find-by-tag walks
 *     registries via virtual fns in each registry's sub-object), NOT the
 *     N7's raw [next][prev][tag][db] sentinel chain. The old raw-offset
 *     publish (RES_MODEL+RESDB_CHAIN_OFF tail-insert) does not exist;
 *     publish/unpublish/has_tag are therefore no-ops on 6G. Labels/screen
 *     DBs are not published into the OS chain; the OS resolves our custom
 *     image ids through the bitmap-cache alt-resolver hook instead, so the
 *     registration below works with no published resources.
 *   - SB_VIEW (N7 0x083f4dfc, "springboard view or NULL") and TV_RELOAD
 *     (N7 0x08400596) twins are not resolved yet; hb_silver_home_reload()
 *     is a no-op on 6G (fresh item inserts are visible at boot anyway;
 *     built-ins never call it).
 *   - ensure_capacity/trim_pages have no 6G page API mapped yet and are
 *     no-ops on 6G (registering into existing pages directly works for
 *     the built-in-style insert; revisit after on-device verification).
 *   - RESDB_GET twin unresolved; hb_silver_resdb_get returns NULL on 6G.
 *     RESDB_OPEN (0x081be5f0) IS mapped (same shape as N7 0x08006fde).
 */

#include "hb_silver_res.h"

#define FN_THUMB(addr) ((addr) | 1u)

#define ADDR_RESDB_OPEN  0x081be5f0u  /* 7G: 0x08006fde */
#define ADDR_RESDB_GET   0x0u         /* unresolved on 6G (7G: 0x08409e86) */
#define ADDR_RES_MODEL   0x08189b66u  /* 7G: 0x084157c4 */
#define RESDB_CHAIN_OFF  0x2cu        /* (unused on 6G — see header note) */
#define ADDR_SB_MODEL    0x081892a0u  /* 7G: 0x084084b4 */
#define ADDR_SB_REGISTER 0x0u         /* no 6G twin; use MODEL vt+0xc8     */
#define ADDR_SB_SHOW     0x08239dd8u  /* 7G: 0x0849a3dc (vtable +0x160)    */
#define ADDR_SB_HIDE     0x08239ba0u  /* 7G: 0x08499e68 (vtable +0x164)    */
#define ADDR_STR_CTOR    0x0817fd6cu  /* 7G: 0x0842d8bc                    */
#define ADDR_STR_RELEASE 0x081892c0u  /* string release (post-registration)*/
#define ADDR_SB_VT0XC8   0x081dccecu  /* MODEL virtual +0xc8 (item insert) */
#define ADDR_SB_VIEW     0x0u         /* unresolved on 6G (7G: 0x083f4dfc) */
#define SBVIEW_TABLE_OFF 0x388u       /* springboard view: table-view field offset */
#define ADDR_TV_RELOAD   0x0u         /* unresolved on 6G (7G: 0x08400596) */
#define ADDR_SB_ADDPAGE  0x0u         /* unresolved on 6G (7G: 0x08498cda) */
#define ADDR_SB_TRIMPAGE 0x0u         /* unresolved on 6G (7G: 0x0849968c) */
#define ADDR_SB_PAGES    0x0u         /* unresolved on 6G (7G: 0x084b2f32) */
#define ADDR_SB_PERPAGE  0x0u         /* unresolved on 6G (7G: 0x084b2fe8) */
#define ADDR_SB_GETMAXPAGES 0x0u      /* unresolved on 6G (7G: 0x084b2f38) */
#define ADDR_SB_SETMAXPAGES 0x0u      /* unresolved on 6G (7G: 0x08498cd6) */

typedef void *(*resdb_open_t)(void *self, const void *data, unsigned int size, int a4);
typedef const void *(*resdb_get_t)(void *self, unsigned int type, int id, unsigned int *out_size);
typedef void *(*get_ptr_t)(void);
typedef void *(*str_ctor_t)(void *, const char *, void *);
typedef void  (*str_release_t)(void *self);
typedef void  (*sb_show_t)(void *m, void *data);
typedef void  (*tv_reload_t)(void *table_view);
typedef int   (*int_meth_t)(void *self);          /* returns an int             */
typedef void  (*void_meth_t)(void *self);         /* void method(this)          */
typedef int   (*addpage_t)(void *self, int a2);    /* appends an empty home page      */

void *hb_silver_resdb_open(void *obj_storage, const void *blob, unsigned int size)
{
    if (!obj_storage || !blob)
        return 0;
    return ((resdb_open_t)FN_THUMB(ADDR_RESDB_OPEN))(obj_storage, blob, size, 0);
}

const void *hb_silver_resdb_get(void *resdb, unsigned int type4cc, int id,
                                unsigned int *out_size)
{
    if (!resdb || !ADDR_RESDB_GET) {
        if (out_size)
            *out_size = 0;
        return 0;                            /* unresolved on 6G */
    }
    {
        unsigned int sz = 0;
        const void *p = ((resdb_get_t)FN_THUMB(ADDR_RESDB_GET))(resdb, type4cc, id, &sz);
        if (out_size)
            *out_size = p ? sz : 0;
        return p;
    }
}

int hb_silver_resdb_publish(void *resdb, unsigned int tag4cc, void *node_storage)
{
    (void)resdb; (void)tag4cc; (void)node_storage;
    /* No raw chain-splice on 6G — see file header. */
    return 0;
}

int hb_silver_resdb_has_tag(unsigned int tag4cc)
{
    (void)tag4cc;
    return 0;                                /* chain not modelled on 6G */
}

void hb_silver_resdb_unpublish(void *node_storage)
{
    (void)node_storage;                      /* no-op on 6G */
}

/*
 * Register one item on the 6G springboard model. Naked by necessity: the
 * OS callee reads the 6-word descriptor from the bottom of the CALLER's
 * stack frame, which no C local can guarantee.
 *
 *   r0 = model, r1 = id_str (from ADDR_STR_CTOR), r2 = label_id, r3 = image_id
 *
 * Registers an item whose only attachment is the image id at item+0x20;
 * desc[0..2] = 0 (attach helpers are skipped for zero ids), desc[4]/desc[5]
 * = -1 (OS picks its built-in defaults). Verified against the built-in
 * item creators (e.g. 0x080185cc) and the vt+0xc8 callee (0x081dccec).
 *
 * WHERE THE GRID ICON COMES FROM (decoded from a live 6G RAM dump):
 *   every item stores seven 12-byte triples {0x80, image_id, flag}, in this
 *   order:  label, d1, d3, d5, d0, d2, d4  (d4/d5 default to the stock frame
 *   ids 0x0dad02bf / 0x0dad02c0). A built-in item (e.g. Photos, X=0x0dad02ef)
 *   passes {X-0x25, X-0x11, X+0x12, X, -1, -1}, so its triples are
 *   {label, X-0x11, X, 0x0dad02c0, X-0x25, X+0x12, 0x0dad02bf}. X is a
 *   105x105 bitmap and X-0x11 is another 105x105 bitmap; X-0x25 / X+0x12 are
 *   164x154 overlays. We pass d3 = X, i.e. the same slot the built-ins put
 *   their primary icon in.
 *
 * HB_6G_ITEM_ICON_SLOTS selects which descriptor slots carry our icon, so a
 * single rebuild can A/B the draw path without touching this code:
 *   3 (default) = d3 only  — stock primary slot
 *   1           = d1 only  — the other 105x105 slot
 *   2           = d2 only  — first 164x154 overlay slot
 *   4           = all four — guarantees the icon is in whichever slot the
 *                            renderer reads (identical bitmap, so the cell
 *                            looks the same if several are drawn)
 */
#ifndef HB_6G_ITEM_ICON_SLOTS
#define HB_6G_ITEM_ICON_SLOTS 3
#endif
__attribute__((naked, noinline))
static void hb6_home_register_item(void *model, void *id_str, long label_id, long image_id)
{
    __asm__ volatile(
        "push {r4, r5, r6, lr}\n\t"
        /* Desc is exactly 6 words (sp+0x00 .. sp+0x17). Use 0x18, not 0x1c:
         * AAPCS requires SP to be 8-byte aligned at the call, and 0x1c would
         * leave it 4(mod 8) for the whole stock insert (and every call it
         * makes). Same layout, aligned stack. */
        "sub sp, #0x18\n\t"
        "mov r4, r0\n\t"              /* r4 = model                       */
        "mov r5, r1\n\t"              /* r5 = id_str                      */
        "mov r6, r2\n\t"              /* r6 = label_id                    */
        "movs r0, #0\n\t"
#if HB_6G_ITEM_ICON_SLOTS == 1
        "str r0, [sp, #0x00]\n\t"     /* d1 = image_id                    */
        "str r3, [sp, #0x04]\n\t"
        "str r0, [sp, #0x08]\n\t"
        "str r0, [sp, #0x0c]\n\t"
#elif HB_6G_ITEM_ICON_SLOTS == 2
        "str r0, [sp, #0x00]\n\t"     /* d2 = image_id                    */
        "str r0, [sp, #0x04]\n\t"
        "str r3, [sp, #0x08]\n\t"
        "str r0, [sp, #0x0c]\n\t"
#elif HB_6G_ITEM_ICON_SLOTS == 4
        "str r3, [sp, #0x00]\n\t"     /* d0..d3 = image_id                */
        "str r3, [sp, #0x04]\n\t"
        "str r3, [sp, #0x08]\n\t"
        "str r3, [sp, #0x0c]\n\t"
#else
        "str r0, [sp, #0x00]\n\t"     /* d3 = image_id (stock primary)    */
        "str r0, [sp, #0x04]\n\t"
        "str r0, [sp, #0x08]\n\t"
        "str r3, [sp, #0x0c]\n\t"
#endif
        "mvn r0, r0\n\t"              /* r0 = -1                          */
        "str r0, [sp, #0x10]\n\t"     /* d4 = -1 (OS default)             */
        "str r0, [sp, #0x14]\n\t"     /* d5 = -1 (OS default)             */
        "movs r2, #0\n\t"
        "mov r3, r6\n\t"              /* r3 = label_id (item +0x08)       */
        "mov r0, r4\n\t"
        "mov r1, r5\n\t"
        "ldr r6, =0x081dcced\n\t"     /* MODEL vt+0xc8 (Thumb bit set)   */
        "blx r6\n\t"
        "add sp, #0x18\n\t"
        "pop {r4, r5, r6, pc}\n\t"
    );
}

void hb_silver_home_add_item(const char *sbid, int label_id, int image_id)
{
    void *model = ((get_ptr_t)FN_THUMB(ADDR_SB_MODEL))();
    uint8_t  id_obj[32] = {0};
    uint32_t extra = 0;
    void *id_str;
    uint8_t show[160] = {0};
    int i;

    if (!model || !sbid)
        return;

    id_str = ((str_ctor_t)FN_THUMB(ADDR_STR_CTOR))(id_obj, sbid, &extra);
    if (!id_str)
        return;
    hb6_home_register_item(model, id_str, (long)label_id, (long)image_id);
    ((str_release_t)FN_THUMB(ADDR_STR_RELEASE))(id_str);

    for (i = 0; sbid[i] && i < 120; i++)
        show[2 + i] = (uint8_t)sbid[i];
    ((sb_show_t)FN_THUMB(ADDR_SB_SHOW))(model, show);
}

void hb_silver_home_hide_item(const char *sbid)
{
    void *model = ((get_ptr_t)FN_THUMB(ADDR_SB_MODEL))();
    uint8_t data[160] = {0};
    int i;
    if (!model || !sbid) return;
    for (i = 0; sbid[i] && i < 120; i++) data[2 + i] = (uint8_t)sbid[i];
    ((sb_show_t)FN_THUMB(ADDR_SB_HIDE))(model, data);
}

void hb_silver_home_reload(void)
{
    /* SB_VIEW twin unresolved on 6G — no-op. Fresh inserts are realised at
     * boot with the built-in-style path; revisit after on-device testing. */
    if (!ADDR_SB_VIEW || !ADDR_TV_RELOAD)
        return;
    {
        void *view = ((get_ptr_t)FN_THUMB(ADDR_SB_VIEW))();
        void *table;

        if (!view)
            return;
        table = *(void **)((char *)view + SBVIEW_TABLE_OFF);
        if (table)
            ((tv_reload_t)FN_THUMB(ADDR_TV_RELOAD))(table);
    }
}

void hb_silver_home_ensure_capacity(int extra_items)
{
    /* No 6G page-API mapping yet — no-op on 6G. */
    (void)extra_items;
    return;
}

void hb_silver_home_trim_pages(void)
{
    /* No 6G page-API mapping yet — no-op on 6G. */
    return;
}
