/*
 * fm_probe — locate Apple's Radio objects in the live iPod nano 7 OS.
 *
 * Runs once from the SCSI exec channel (`./start run fm_probe`) with the OS
 * up and the filesystem mounted. It scans DRAM read-only and writes:
 *
 *   /Data/fm_probe/fm_probe.txt   a structured report:
 *     - every known ISL/Radio RTTI class name found, with the type_info and
 *       vtable reached from it (Itanium ABI walk), every vtable slot with the
 *       first bytes of its code, and live objects pointing at those vtables;
 *     - every "N3ISL*" / "TRFTuner*" / Radio-related string discovered;
 *     - the HCI/FM opcode patterns: MOVW #0xFC15 (FM_RDS_Command), the
 *       0xFC15 halfword in tables, and the HCI bring-up opcode sequence
 *       (0x0C03, 0x1001, 0x1009).
 *   /Data/fm_probe/os_image.bin    the raw OS image as it sits in DRAM
 *                                 (0x08000000..0x08C00000), for static
 *                                 disassembly on the host.
 *
 * Nothing here writes to the OS or calls into it. The scans are byte/word
 * walks over RAM. The homebrew window (0x09200000..0x09300000 — our own .bss
 * and .text) is skipped so our own pointers are never mistaken for OS objects.
 *
 * Why a device-side probe at all: the only published tuner-object addresses
 * (ISL::TRFTuner vtable 0x087f0050, RTXC implementation 0x087ed41c) came from
 * a 1.1.2/39A10023 image, and this device may be on another build. Scanning
 * for the RTTI names finds the objects wherever this build put them, and the
 * image dump lets the host disassemble the slots and settle what each one does.
 */

#include "hb_sdk.h"
#include <string.h>

#define IMG_LO       0x08000000u   /* OS image + rodata + data */
#define IMG_HI       0x09100000u   /* homebrew window starts here */
#define HEAP_LO      0x08a00000u   /* OS heap region for object scans */
#define HEAP_HI      0x0c000000u
#define SKIP_LO      0x09100000u   /* homebrew window: SDK allocations + our .bss/.text */
#define SKIP_HI      0x09300000u

#define REPORT_PATH  "/Data/fm_probe/fm_probe.txt"
#define IMAGE_PATH   "/Data/fm_probe/os_image.bin"
#define DUMP_END     0x0c000000u   /* full heap 0x08a00000..0x0c000000 */

#define MAX_HITS     96
#define MAX_VTS      24
#define MAX_PREF     96
#define MAX_OBJ      4
#define MAX_OBJ_VT   6

/* ------------------------------------------------------------------ */
/* buffered text output                                                */

static char s_out[4096];
static int  s_out_n;
static bool g_out;

static void out_flush(void)
{
    if (s_out_n) {
        /* flush more often: the OS FAT layer has a fragile buffer path, and
         * a few thousand bytes at a time avoids the large-chunk write that
         * truncated the report in earlier runs. */
        if (g_out && s_out_n >= 1024) {
            hb_fs_stream_write(s_out, (uint32_t)s_out_n);
            s_out_n = 0;
        }
    }
}

static void out_ch(char c)
{
    s_out[s_out_n++] = c;
    if (s_out_n == (int)sizeof s_out) out_flush();
}

static void out_str(const char *s) { while (*s) out_ch(*s++); }
static void out_nl(void) { out_ch('\n'); }

static const char HEX[] = "0123456789abcdef";

static void out_hex(uint32_t v, int digits)
{
    for (int i = digits - 1; i >= 0; i--)
        out_ch(HEX[(v >> (i * 4)) & 0xF]);
}

static void out_u32(uint32_t v) { out_hex(v, 8); }
static void out_u8(uint8_t v)   { out_hex(v, 2); }

static void out_dec(uint32_t v)
{
    char t[12];
    int n = 0;
    if (!v) { out_ch('0'); return; }
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) out_ch(t[--n]);
}

static void out_addr(uint32_t a) { out_str("0x"); out_u32(a); }

static void out_bytes(const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++) {
        if (i) out_ch(' ');
        out_u8(p[i]);
    }
}

/* ------------------------------------------------------------------ */
/* needles                                                             */

static const char *const k_names[] = {
    "N3ISL8TRFTunerE",
    "N3ISL11IPodRFTunerE",
    "N3ISL12TRFTuner_IAPE",
    "N3ISL13TRFTuner_RTXCE",
    "N3ISL21IPodRFTunerPresetListE",
    "18TRFTunerPresetList",
    "16CIapLingoRFTuner",
    "24TSilverRadioTunerBarView",
    "23TCSettings_RadioRegions",
    "27TSilverCntlrTransitionAddonI23TCSettings_RadioRegionsE",
};
#define NNAMES ((int)(sizeof k_names / sizeof k_names[0]))

/* prefixes whose whole containing string is worth reporting */
static const char *const k_prefix[] = {
    "N3ISL", "TRFTuner", "RadioBuffer", "LivePause",
    "iPod_Control/Device/Radio", "TSilver", "Radio",
};
#define NPREFIX ((int)(sizeof k_prefix / sizeof k_prefix[0]))
#define PREF_CAP 24

static uint32_t s_first[256];

static void build_first(void)
{
    memset(s_first, 0, sizeof s_first);
    for (int i = 0; i < NNAMES; i++)
        s_first[(uint8_t)k_names[i][0]] |= 1u << i;
}

static bool smatch(const uint8_t *p, const char *s)
{
    int i = 0;
    while (s[i] && p[i] == (uint8_t)s[i]) i++;
    return s[i] == 0;
}

/* ------------------------------------------------------------------ */
/* results                                                             */

static struct { uint32_t addr; int idx; } s_hits[MAX_HITS];
static int s_nhits;

static uint32_t s_vts[MAX_VTS];
static int s_vt_idx[MAX_VTS];       /* which needle this vtable came from */
static int s_nvts;

static struct { uint32_t addr; char text[48]; } s_pref[MAX_PREF];
static int s_npref;

/* ------------------------------------------------------------------ */
/* scans                                                               */

static void scan_names(void)
{
    for (uint32_t a = IMG_LO; a < IMG_HI; a++) {
        uint8_t b = *(volatile const uint8_t *)(uintptr_t)a;
        uint32_t m = s_first[b];
        while (m) {
            int i = __builtin_ctz(m);
            m &= m - 1;
            if (smatch((const uint8_t *)(uintptr_t)a, k_names[i]) &&
                s_nhits < MAX_HITS) {
                s_hits[s_nhits].addr = a;
                s_hits[s_nhits].idx = i;
                s_nhits++;
            }
        }
    }
}

static void scan_prefixes(void)
{
    int cap[NPREFIX];
    for (int i = 0; i < NPREFIX; i++) cap[i] = 0;

    for (uint32_t a = IMG_LO; a < IMG_HI; a++) {
        for (int i = 0; i < NPREFIX; i++) {
            if (cap[i] >= PREF_CAP) continue;
            if (!smatch((const uint8_t *)(uintptr_t)a, k_prefix[i])) continue;
            cap[i]++;
            if (s_npref >= MAX_PREF) return;
            s_pref[s_npref].addr = a;
            int n = 0;
            const uint8_t *p = (const uint8_t *)(uintptr_t)a;
            while (p[n] && n < (int)sizeof s_pref[0].text - 1) {
                s_pref[s_npref].text[n] = (char)p[n];
                n++;
            }
            s_pref[s_npref].text[n] = 0;
            s_npref++;
        }
    }
}

/* word scan over [lo,hi); returns hits found (capped at max stored) */
static int find_word(uint32_t val, uint32_t lo, uint32_t hi,
                     uint32_t *hits, int max)
{
    int n = 0;
    for (uint32_t a = lo; a + 4 <= hi; a += 4) {
        if (a >= SKIP_LO && a < SKIP_HI) continue;
        if (*(volatile const uint32_t *)(uintptr_t)a == val) {
            if (n < max) hits[n] = a;
            n++;
        }
    }
    return n;
}

static bool in_img(uint32_t a) { return a >= IMG_LO && a < IMG_HI; }

/* ------------------------------------------------------------------ */
/* RTTI walk + report                                                  */

static void report_vtable(uint32_t slots, int nslots)
{
    for (int i = 0; i < nslots; i++) {
        uint32_t w = *(volatile const uint32_t *)(uintptr_t)(slots + 4u * i);
        out_ch('[');
        if (i < 10) out_ch('0');
        out_dec(i);
        out_str("] ");
        out_addr(w);
        if (in_img(w & ~1u)) {
            out_ch(' ');
            out_bytes((const uint8_t *)(uintptr_t)(w & ~1u), 8);
        }
        out_nl();
    }
}

static void walk_rtti(void)
{
    for (int h = 0; h < s_nhits; h++) {
        uint32_t s = s_hits[h].addr;
        int idx = s_hits[h].idx;

        out_str("-- ");
        out_str(k_names[idx]);
        out_str(" str=");
        out_addr(s);
        out_nl();

        /* type_info: its __name field (at +4) holds the string address */
        uint32_t ti_refs[8];
        int nti = find_word(s, IMG_LO, IMG_HI, ti_refs, 8);
        for (int i = 0; i < nti; i++) {
            uint32_t ti = ti_refs[i] - 4;
            if (!in_img(ti)) continue;
            uint32_t vptr = *(volatile const uint32_t *)(uintptr_t)ti;
            if (!in_img(vptr)) continue;          /* not a type_info */

            /* vtable: its type_info* field (at +4) holds the type_info */
            uint32_t vt_refs[8];
            int nvt = find_word(ti, IMG_LO, IMG_HI, vt_refs, 8);
            for (int j = 0; j < nvt; j++) {
                uint32_t slots = vt_refs[j] + 4;
                uint32_t s0 = *(volatile const uint32_t *)(uintptr_t)slots;
                if (s0 != 0 && !in_img(s0 & ~1u)) continue;   /* not a vtable */
                uint32_t vt = slots - 8;

                out_str("   ti=");
                out_addr(ti);
                out_str(" vt=");
                out_addr(vt);
                out_nl();
                report_vtable(slots, 40);

                if (s_nvts < MAX_VTS) {
                    s_vts[s_nvts] = vt;
                    s_vt_idx[s_nvts] = idx;
                    s_nvts++;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* live objects pointing at a vtable                                   */

static void scan_objects(void)
{
    for (int v = 0; v < s_nvts && v < MAX_OBJ_VT; v++) {
        uint32_t vt = s_vts[v];

        out_str("== object for vt=");
        out_addr(vt);
        out_str(" (");
        out_str(k_names[s_vt_idx[v]]);
        out_str(")");
        out_nl();

        int found = 0;
        for (uint32_t a = HEAP_LO; a + 260u <= HEAP_HI && found < MAX_OBJ; a += 4) {
            if (a >= SKIP_LO && a < SKIP_HI) continue;
            uint32_t w = *(volatile const uint32_t *)(uintptr_t)a;
            if (w != vt && w != (vt | 1u)) continue;

            found++;
            out_str("   obj=");
            out_addr(a);
            out_nl();
            for (int k = 0; k < 64; k++) {
                if (k % 8 == 0) {
                    out_str("      ");
                    if (k < 0x10) out_ch('0');
                    out_hex((uint32_t)k, 2);
                    out_str(": ");
                }
                out_hex(*(volatile const uint32_t *)(uintptr_t)(a + 4u * k), 8);
                out_ch((k % 8 == 7) ? '\n' : ' ');
            }
        }
        if (!found) out_str("   none in heap range\n");
    }
}

/* ------------------------------------------------------------------ */
/* HCI / FM opcode patterns                                            */

static void scan_hci(void)
{
    /* MOVW rd, #0xFC15 — hw1 = 0xF64F, hw2 = (rd<<8)|0x15 */
    int n = 0;
    for (uint32_t a = IMG_LO; a + 4 <= IMG_HI; a += 2) {
        uint16_t h1 = *(volatile const uint16_t *)(uintptr_t)a;
        uint16_t h2 = *(volatile const uint16_t *)(uintptr_t)(a + 2);
        if (h1 == 0xF64F && (h2 & 0xFF) == 0x15 && (h2 & 0xF000) == 0) {
            if (n < 24) {
                out_str("   movw_fc15 ");
                out_addr(a);
                out_str(" rd=");
                out_dec((h2 >> 8) & 0xF);
                out_nl();
            }
            n++;
        }
    }
    out_str("   movw_fc15 total ");
    out_dec(n);
    out_nl();

    /* 0xFC15 as a bare halfword (a dispatch table entry) */
    n = 0;
    for (uint32_t a = IMG_LO; a + 2 <= IMG_HI; a += 2) {
        if (*(volatile const uint16_t *)(uintptr_t)a == 0xFC15) {
            if (n < 24) {
                out_str("   fc15_half ");
                out_addr(a);
                out_nl();
            }
            n++;
        }
    }
    out_str("   fc15_half total ");
    out_dec(n);
    out_nl();

    /* the bring-up table: opcodes 0x0C03, 0x1001, 0x1009 in order */
    static const uint8_t pat[6] = { 0x03, 0x0C, 0x01, 0x10, 0x09, 0x10 };
    n = 0;
    for (uint32_t a = IMG_LO; a + 6 <= IMG_HI; a++) {
        if (*(volatile const uint8_t *)(uintptr_t)a != pat[0]) continue;
        if (memcmp((const void *)(uintptr_t)a, pat, sizeof pat) != 0) continue;
        if (n < 24) {
            out_str("   bringup_table ");
            out_addr(a);
            out_nl();
        }
        n++;
    }
    out_str("   bringup_table total ");
    out_dec(n);
    out_nl();
}

/* ------------------------------------------------------------------ */
/* boot report (the C6 reporter the resident publishes)                */

static void report_boot(void)
{
    static const uint32_t base = 0x086c9b84u;
    uint32_t magic = *(volatile const uint32_t *)(base + 0x2cu);

    out_str("== boot ==\n");
    out_str("   magic 0x");
    out_hex(magic, 8);
    out_nl();
    if (magic != 0x32505452u) {
        out_str("   no boot report (older bootstrap)\n");
        return;
    }
    out_str("   resident_version 0x");
    out_hex(*(volatile const uint32_t *)(base + 0x15cu), 8);
    out_nl();
    out_str("   abi 0x");
    out_hex(*(volatile const uint32_t *)(base + 0x160u), 8);
    out_str(" flag 0x");
    out_hex(*(volatile const uint32_t *)(base + 0x164u), 8);
    out_str(" live 0x");
    out_hex(*(volatile const uint32_t *)(base + 0x168u), 8);
    out_nl();
}

/* ------------------------------------------------------------------ */
/* image dump                                                          */

static void dump_image(void)
{
    static uint8_t buf[16384];

    if (!hb_fs_stream_open(IMAGE_PATH)) {
        hb_trace_log("FMPDUMP0", 0, 0);
        return;
    }
    for (uint32_t a = IMG_LO; a < DUMP_END; a += sizeof buf) {
        uint32_t n = DUMP_END - a;
        if (n > sizeof buf) n = sizeof buf;
        memcpy(buf, (const void *)(uintptr_t)a, n);
        if (!hb_fs_stream_write(buf, n)) break;
    }
    hb_fs_stream_close();
    hb_trace_log("FMPDUMP1", DUMP_END - IMG_LO, 0);
}

/* ------------------------------------------------------------------ */
/* entry                                                               */

HB_APP_ENTRY(payload_entry)
{
    hb_fs_mkdir("/Data");
    hb_fs_mkdir("/Data/fm_probe");

    g_out = hb_fs_stream_open(REPORT_PATH);

    out_str("FM_PROBE 1\n");
    out_str("img ");
    out_addr(IMG_LO);
    out_ch('-');
    out_addr(IMG_HI);
    out_str(" heap ");
    out_addr(HEAP_LO);
    out_ch('-');
    out_addr(HEAP_HI);
    out_str(" skip ");
    out_addr(SKIP_LO);
    out_ch('-');
    out_addr(SKIP_HI);
    out_nl();

    hb_trace_log("FMPROBE0", 0, 0);

    build_first();
    scan_names();
    hb_trace_log("FMPROBE1", (uint32_t)s_nhits, 0);

    out_str("== names ==\n");
    walk_rtti();

    report_boot();

    scan_prefixes();
    hb_trace_log("FMPROBE2", (uint32_t)s_npref, 0);

    out_str("== prefix ==\n");
    for (int i = 0; i < s_npref; i++) {
        out_addr(s_pref[i].addr);
        out_ch(' ');
        out_ch('"');
        out_str(s_pref[i].text);
        out_ch('"');
        out_nl();
    }

    out_str("== objects ==\n");
    scan_objects();

    out_str("== hci ==\n");
    scan_hci();

    out_flush();
    if (g_out) hb_fs_stream_close();

    hb_trace_log("FMPROBE3", (uint32_t)s_nvts, 0);

    dump_image();

    /* brief on-device confirmation; the report file is the real output */
    hb_fill_screen(HB_BLACK);
    hb_draw_str(8, 8, "FM PROBE", 2, HB_GREEN, HB_BLACK);
    hb_draw_str(8, 40, s_nhits ? "objects found" : "no objects found",
                1, HB_WHITE, HB_BLACK);
    hb_draw_str(8, 56, "see /Data/fm_probe/", 1, HB_RGB(0x80, 0x80, 0x80),
                HB_BLACK);

    hb_trace_log("FMPROBE4", 0, 0);
}
