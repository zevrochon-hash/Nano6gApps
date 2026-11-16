/*
 * reg_pack_test.c — host dry-run of the 6G pack registrar.
 *
 * Build and run from NanoApps/:
 *   gcc -std=c99 -I sdk -o /tmp/reg_pack_test tools/reg_pack_test.c sdk/hb_app_bundle.c
 *   /tmp/reg_pack_test_v2 tools/fixtures/hbpk_6g_3apps_v2.pack
 *
 * Compiles the REAL registrar with stubbed OS calls and checks the pack parse,
 * icon id assignment, home-item arguments and the same-boot re-deploy path.
 * (The re-deploy check caught a bug where every icon id became 0.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "hb_app_bundle.h"
#include "hb_app_catalog.h"
#include "hb_silver_icon.h"
#include "hb_silver_res.h"

static int fails;
#define CHECK(cond, msg) do { \
    if (cond) printf("  ok   %s\n", msg); \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

/* ---- captured OS calls ---- */
static int n_publish, n_add, n_cap, n_trim, n_reload, n_setname;
static uint32_t pub_lens[64], pub_sizes[64];
static char add_sbid[64][32];
static int add_label[64], add_icon[64];
static char set_name[64][64];
static uint32_t set_hash[64];
static long first_icon_seen;

/* ---- stubs ---- */
int  hb_silver_icon_live(void) { return 1; }
long hb_silver_icon_refresh(void) { return 0x0dad0761L; }
long hb_silver_icon_publish(const uint32_t *bmh, const uint32_t *lens,
                            int count, void *(*alloc)(uint32_t))
{
    (void)alloc;
    n_publish = count;
    for (int i = 0; i < count; i++) {
        pub_lens[i] = lens[i];
        /* the registrar must hand us pointers inside the pack */
        pub_sizes[i] = bmh[i] ? 1 : 0;
    }
    first_icon_seen = 0x0dad0761L;
    return first_icon_seen;
}
void hb_silver_icon_set_name(int k, const char *name)
{
    if (k >= 0 && k < 64) { snprintf(set_name[k], sizeof set_name[k], "%s", name ? name : ""); }
    n_setname++;
}
void hb_silver_icon_set_hash(int k, uint32_t hash, int bundled)
{
    (void)bundled;
    if (k >= 0 && k < 64) set_hash[k] = hash;
}
int hb_silver_icon_grow_cache(uint32_t b, void *(*a)(uint32_t)) { (void)b; (void)a; return 1; }
long hb_silver_icon_register_lazy(int c, void *(*a)(uint32_t)) { (void)c; (void)a; return 0; }

void hb_silver_home_ensure_capacity(int extra) { n_cap = extra; }
void hb_silver_home_add_item(const char *sbid, int label_id, int image_id)
{
    int i = n_add++;
    if (i < 64) {
        snprintf(add_sbid[i], sizeof add_sbid[i], "%s", sbid ? sbid : "");
        add_label[i] = label_id;
        add_icon[i] = image_id;
    }
}
void hb_silver_home_trim_pages(void) { n_trim++; }
void hb_silver_home_reload(void) { n_reload++; }

void *hb_silver_resdb_open(void *o, const void *b, unsigned int s) { (void)o;(void)b;(void)s; return 0; }
const void *hb_silver_resdb_get(void *d, unsigned int t, int i, uint32_t *s) { (void)d;(void)t;(void)i;(void)s; return 0; }
int hb_silver_resdb_publish(void *d, unsigned int t, void *n) { (void)d;(void)t;(void)n; return 0; }
int hb_silver_resdb_has_tag(unsigned int t) { (void)t; return 0; }
void hb_silver_resdb_unpublish(void *n) { (void)n; }

static void *halloc(uint32_t n) { return malloc(n); }

static int trace_n;
static void trace(const char *tag, uint32_t a, uint32_t b)
{
    printf("  trace %-8s %08x %08x\n", tag, a, b);
    trace_n++;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "hbpk_6g_3apps_v2.pack";
    FILE *f = fopen(path, "rb");
    static uint8_t buf[1 << 20];
    size_t n;
    if (!f) { perror(path); return 2; }
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    printf("pack %s: %zu bytes\n", path, n);

    hb_app_launch_t out[8];
    memset(out, 0, sizeof out);
    int count = hb_app_register_pack(buf, halloc, trace, out, 8,
                                     NULL, NULL, /* screen obj/node */
                                     1,          /* register_to_os */
                                     NULL);      /* no fs */
    printf("registered %d apps\n", count);
    printf("icons published=%d first=%#lx  capacity=%d trim=%d reload=%d setname=%d\n",
           n_publish, first_icon_seen, n_cap, n_trim, n_reload, n_setname);

    CHECK(count == 3, "three bundles registered");
    CHECK(n_publish == 3, "three icons published in one batch");
    CHECK(n_add == 3, "three home items added");
    CHECK(n_cap == 3, "ensure_capacity(3) before adds");
    CHECK(n_trim == 1 && n_reload == 1, "trim + reload after adds");
    for (int i = 0; i < count; i++) {
        printf("  app%d id=%08x kind=%d sbid=%s name=%s icon=%#lx screen=%#lx layout=%#lx code=%08x+%u\n",
               i, out[i].app_id, out[i].app_kind, out[i].sbid, out[i].name,
               out[i].icon_id, out[i].screen_id, out[i].layout_id,
               out[i].code_addr, out[i].code_len);
    }
    CHECK(count == 3 && strcmp(add_sbid[0], "hb.test0") == 0 &&
          strcmp(add_sbid[1], "hb.test1") == 0 && strcmp(add_sbid[2], "hb.test2") == 0,
          "home items carry the right sbids in order");
    CHECK(add_label[0] == 0 && add_label[1] == 0 && add_label[2] == 0,
          "label ids are 0 (v2 fixture; 0x0dad07xx ids are unsafe on 6G)");
    CHECK(add_icon[0] == (int)0x0dad0761 && add_icon[1] == (int)0x0dad0762 &&
          add_icon[2] == (int)0x0dad0763, "icon ids are first_icon + index");
    CHECK(pub_lens[0] == 44132u && pub_lens[1] == 44132u && pub_lens[2] == 44132u,
          "icon section lengths are 32-byte header + 105x105 RGBA");
    CHECK(strcmp(set_name[0], "test0") == 0 && strcmp(set_name[2], "test2") == 0,
          "icon names come from the bundle name field");
    CHECK(out[0].code_len == 64 && out[0].code_addr != 0,
          "code section recorded");
    CHECK(out[0].sbid[0] && strcmp(out[0].sbid, "hb.test0") == 0, "launch sbid copied");

    /* Re-deploy: register_to_os == 2 must reuse the icon ids, not re-copy. */
    memset(out, 0, sizeof out);
    n_add = n_publish = n_cap = n_trim = n_reload = 0;
    int count2 = hb_app_register_pack(buf, halloc, trace, out, 8, NULL, NULL, 2, NULL);
    printf("redeploy: registered=%d icons=%d items=%d cap=%d\n", count2, n_publish, n_add, n_cap);
    CHECK(count2 == 3, "redeploy registers three apps");
    CHECK(n_publish == 0, "redeploy does not republish icons");
    CHECK(n_add == 3, "redeploy still ensures home items");
    CHECK(out[0].icon_id == 0x0dad0761L, "redeploy keeps the same icon ids");

    printf(fails ? "\nFAILURES: %d\n" : "\nall checks passed\n", fails);
    return fails ? 1 : 0;
}
