/*
 * engine.c — game state, map data, movement, doors, combat, monsters.
 * All world positions are 16.16 fixed point.  Rendering is in render.c.
 */
#include "doom.h"

/* ------------------------------------------------------------------ data */

uint32_t *g_cv;
uint16_t *g_depth;
const uint8_t *g_pak;
uint32_t g_pak_size;
uint32_t g_pal[256];
uint32_t g_shade[32][256];
int32_t  g_sin[1024];
int32_t  g_cos[1024];
int16_t  g_atan[257];

tex_t   g_tex[MAXTEX];    int g_ntex;
flat_t  g_flat[MAXFLAT];  int g_nflat;
pic_t   g_spr[MAXSPR];    int g_nspr;
pic_t   g_gfx[MAXGFX];    int g_ngfx;

vertex_t g_vert[MAXV];    int g_nvert;
line_t   g_line[MAXL];    int g_nline;
sector_t g_sec[MAXSEC];   int g_nsec;
sector_t g_sec0[MAXSEC];
uint16_t g_lidx[MAXIDX];  int g_nidx;
mthing_t g_thing[MAXTH];  int g_nthing;
tmeta_t  g_meta[MAXTH];
int16_t  g_line_other[MAXL];
int16_t  g_line_len[MAXL];
int16_t  g_line_door[MAXL];

mobj_t   g_mobj[MAXMOBJ]; int g_nmobj;
prj_t    g_prj[MAXPRJ];
door_t   g_door[MAXDOOR]; int g_ndoor;
player_t g_player;
int      g_tilt;
void   (*g_accel_fn)(int32_t out[3]);
static int32_t s_tilt_base;

int g_state = ST_TITLE;
int g_menu_sel = 0;
uint32_t g_now_ms;
uint32_t g_state_ms;
uint32_t g_game_ms;
int g_demo;
int g_demo_tips;
char g_message[64];
uint32_t g_message_ms;
int g_exit_line = -1;
int g_sky_flat = -1;
int g_frame;

/* sprite base table: index matches the pack's sorted base ids */
char    g_sprbase[64][5];
int     g_nbase;
int16_t g_sprmap[64][26][9];
int     g_pisg[2] = {-1,-1}, g_shtg[2] = {-1,-1};
int     g_pisf = -1, g_shtf = -1, g_puff = -1, g_puff_b = -1;
int     g_poss_base = -1, g_sarg_base = -1;
int     g_bal1[2] = {-1,-1}, g_bal7[2] = {-1,-1};
int     g_face[8];
int     g_keyspr[6];
int     g_num[10], g_ysnum[10];
int     g_stbar = -1, g_titlepic = -1, g_mdoom = -1;
int     g_font[64];

/* temp */
static uint32_t s_rng = 0x12345678u;

/* --------------------------------------------------------------- helpers */

static const uint8_t *s_pal_src;

static void memzero(void *p, int n)
{
    uint8_t *q = (uint8_t *)p;
    while (n-- > 0) *q++ = 0;
}

static void memcopy(void *d, const void *s, int n)
{
    uint8_t *a = (uint8_t *)d;
    const uint8_t *b = (const uint8_t *)s;
    while (n-- > 0) *a++ = *b++;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t  rdi16(const uint8_t *p) { return (int16_t)rd16(p); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int isqrt32(uint32_t v)
{
    uint32_t x, y;
    if (!v) return 0;
    x = v; y = (x + 1) >> 1;
    while (y < x) { x = y; y = (x + v / x) >> 1; }
    return (int)x;
}

int rnd(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (int)((s_rng >> 16) & 0x7fff);
}

int atan2_16(int y, int x)
{
    int ax, ay, a, t, sw = 0;
    if (!x && !y) return 0;
    ax = x < 0 ? -x : x;
    ay = y < 0 ? -y : y;
    if (ay > ax) { sw = 1; t = ax; ax = ay; ay = t; }
    a = g_atan[(ay * 256) / ax];
    if (sw) a = 16384 - a;
    if (x < 0) a = 32768 - a;
    if (y < 0) a = 65536 - a;
    return a & 65535;
}

static float fatan(float x)
{
    float x2 = x * x;
    return x * (0.9998660f + x2 * (-0.3302995f + x2 * (0.1801410f +
               x2 * (-0.0851330f + x2 * 0.0208351f))));
}

static void init_tables(void)
{
    int i;
    /* sin/cos by fixed-point rotation, one step = 2*pi/1024 */
    {
        int32_t x = 65536, y = 0;
        const int32_t C = 65535, S = 402;
        for (i = 0; i < 1024; i++) {
            g_cos[i] = x;
            g_sin[i] = y;
            {
                int32_t nx = (int32_t)(((int64_t)x * C - (int64_t)y * S) >> 16);
                int32_t ny = (int32_t)(((int64_t)x * S + (int64_t)y * C) >> 16);
                x = nx; y = ny;
            }
        }
    }
    for (i = 0; i <= 256; i++) {
        float r = (float)i / 256.0f;
        g_atan[i] = (int16_t)(fatan(r) * 10430.378f); /* 16384/(pi/2) */
    }
}

/* palette + 32 light levels */
static void build_shade(void)
{
    int i, s;
    const uint8_t *pal = s_pal_src;
    if (!pal) pal = g_pak;
    for (i = 0; i < 256; i++) {
        uint32_t r = pal[i * 3 + 0];
        uint32_t g = pal[i * 3 + 1];
        uint32_t b = pal[i * 3 + 2];
        g_pal[i] = (r << 16) | (g << 8) | b;
    }
    for (s = 0; s < 32; s++) {
        int lum = 255 - s * 7;
        if (s == 31) lum = 22;
        if (lum < 0) lum = 0;
        for (i = 0; i < 256; i++) {
            uint32_t c = g_pal[i];
            uint32_t r = ((c >> 16) & 255) * lum >> 8;
            uint32_t g = ((c >> 8) & 255) * lum >> 8;
            uint32_t b = (c & 255) * lum >> 8;
            g_shade[s][i] = (r << 16) | (g << 8) | b;
        }
    }
}

/* --------------------------------------------------------------- pack load */

static const uint8_t *chunk_ptr(const uint8_t *pak, uint32_t size,
                                const char *tag, uint32_t *out_len)
{
    uint32_t n, i;
    if (size < 12 || rd32(pak) != PACK_MAGIC) return 0;
    n = rd32(pak + 8);
    for (i = 0; i < n; i++) {
        const uint8_t *e = pak + 12 + i * 12;
        if (e[0] == tag[0] && e[1] == tag[1] && e[2] == tag[2] && e[3] == tag[3]) {
            uint32_t off = rd32(e + 4), len = rd32(e + 8);
            if (off + len > size) return 0;
            if (out_len) *out_len = len;
            return pak + off;
        }
    }
    return 0;
}

static int name_is(const char *a, const char *b)
{
    int i;
    for (i = 0; i < 8; i++) {
        char ca = a[i], cb = b[i];
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
    return 1;
}

static int name_eq4(const char *a, const char *b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static int find_gfx(const char *name)
{
    int i;
    for (i = 0; i < g_ngfx; i++)
        if (name_is(g_gfx[i].name, name)) return i;
    return -1;
}

static int find_spr(const char *name)
{
    int i;
    for (i = 0; i < g_nspr; i++)
        if (name_is(g_spr[i].name, name)) return i;
    return -1;
}

static void sort_base_names(void)
{
    int i;
    for (i = 1; i < g_nbase; i++) {
        char tmp[5];
        int k = i;
        memcopy(tmp, g_sprbase[i], 5);
        while (k > 0) {
            const char *a = g_sprbase[k - 1], *b = tmp;
            int c = 0;
            int q;
            for (q = 0; q < 4; q++) {
                if (a[q] != b[q]) { c = (uint8_t)a[q] - (uint8_t)b[q]; break; }
            }
            if (c <= 0) break;
            memcopy(g_sprbase[k], g_sprbase[k - 1], 5);
            k--;
        }
        memcopy(g_sprbase[k], tmp, 5);
    }
}

static void build_sprite_map(void)
{
    int i, j;
    g_nbase = 0;
    memzero(g_sprbase, sizeof(g_sprbase));
    memzero(g_sprmap, sizeof(g_sprmap));
    for (i = 0; i < g_nspr; i++) {
        int found = -1;
        for (j = 0; j < g_nbase; j++)
            if (name_eq4(g_sprbase[j], g_spr[i].name)) { found = j; break; }
        if (found < 0 && g_nbase < 64) {
            found = g_nbase++;
            memcopy(g_sprbase[found], g_spr[i].name, 4);
            g_sprbase[found][4] = 0;
        }
        (void)found;
    }
    sort_base_names();
    for (i = 0; i < g_nspr; i++) {
        int base = -1, letter, rot;
        for (j = 0; j < g_nbase; j++)
            if (name_eq4(g_sprbase[j], g_spr[i].name)) { base = j; break; }
        if (base < 0) continue;
        letter = (uint8_t)g_spr[i].name[4];
        rot = (uint8_t)g_spr[i].name[5] - '0';
        if (letter >= 'A' && letter <= 'Z' && rot >= 0 && rot <= 8)
            g_sprmap[base][letter - 'A'][rot] = (int16_t)i;
    }
}

int doom_load_pack(const uint8_t *pak, uint32_t size)
{
    const uint8_t *p;
    uint32_t len;
    int i, n;

    g_pak = pak; g_pak_size = size;
    p = chunk_ptr(pak, size, "PAL ", &len);
    if (!p || len < 768) return -1;
    s_pal_src = p;

    p = chunk_ptr(pak, size, "TEX ", &len);
    if (!p) return -2;
    n = rd16(p); if (n > MAXTEX) n = MAXTEX; g_ntex = n;
    {
        const uint8_t *tab = p + 2;
        const uint8_t *data = tab + (rd16(p) * 20);
        for (i = 0; i < n; i++) {
            const uint8_t *e = tab + i * 20;
            memcopy(g_tex[i].name, e, 8);
            g_tex[i].w = rd16(e + 8);
            g_tex[i].h = rd16(e + 10);
            g_tex[i].off = rd32(e + 12);
            g_tex[i].pix = data + g_tex[i].off;
        }
    }

    p = chunk_ptr(pak, size, "FLAT", &len);
    if (!p) return -3;
    n = rd16(p); if (n > MAXFLAT) n = MAXFLAT; g_nflat = n;
    {
        const uint8_t *tab = p + 2;
        const uint8_t *data = tab + (rd16(p) * 12);
        for (i = 0; i < n; i++) {
            const uint8_t *e = tab + i * 12;
            memcopy(g_flat[i].name, e, 8);
            g_flat[i].off = rd32(e + 8);
            g_flat[i].pix = data + g_flat[i].off;
            if (name_is(g_flat[i].name, "F_SKY1")) g_sky_flat = i;
        }
    }

    p = chunk_ptr(pak, size, "SPR ", &len);
    if (!p) return -4;
    n = rd16(p); if (n > MAXSPR) n = MAXSPR; g_nspr = n;
    {
        const uint8_t *tab = p + 2;
        const uint8_t *data = tab + (rd16(p) * 24);
        for (i = 0; i < n; i++) {
            const uint8_t *e = tab + i * 24;
            memcopy(g_spr[i].name, e, 8);
            g_spr[i].w = rd16(e + 8);
            g_spr[i].h = rd16(e + 10);
            g_spr[i].left = rdi16(e + 12);
            g_spr[i].top = rdi16(e + 14);
            g_spr[i].off = rd32(e + 16);
            g_spr[i].pix = data + g_spr[i].off;
        }
    }

    p = chunk_ptr(pak, size, "GFX ", &len);
    if (!p) return -5;
    n = rd16(p); if (n > MAXGFX) n = MAXGFX; g_ngfx = n;
    {
        const uint8_t *tab = p + 2;
        const uint8_t *data = tab + (rd16(p) * 24);
        for (i = 0; i < n; i++) {
            const uint8_t *e = tab + i * 24;
            memcopy(g_gfx[i].name, e, 8);
            g_gfx[i].w = rd16(e + 8);
            g_gfx[i].h = rd16(e + 10);
            g_gfx[i].left = rdi16(e + 12);
            g_gfx[i].top = rdi16(e + 14);
            g_gfx[i].off = rd32(e + 16);
            g_gfx[i].pix = data + g_gfx[i].off;
        }
    }

    /* map */
    p = chunk_ptr(pak, size, "MAP ", &len);
    if (!p) return -6;
    {
        const uint8_t *q = p;
        int nv = rd16(q), nl = rd16(q + 2);
        q += 4;
        if (nv > MAXV) nv = MAXV;
        if (nl > MAXL) nl = MAXL;
        g_nvert = nv; g_nline = nl;
        for (i = 0; i < nv; i++) {
            g_vert[i].x = rdi16(q); g_vert[i].y = rdi16(q + 2); q += 4;
        }
        for (i = 0; i < nl; i++) {
            line_t *L = &g_line[i];
            L->v1 = rd16(q); L->v2 = rd16(q + 2);
            L->flags = rd16(q + 4); L->special = rd16(q + 6);
            L->tag = rd16(q + 8);
            L->fsec = rd16(q + 10); L->bsec = rd16(q + 12);
            L->ftop = rd16(q + 14); L->fmid = rd16(q + 16); L->fbot = rd16(q + 18);
            L->btop = rd16(q + 20); L->bmid = rd16(q + 22); L->bbot = rd16(q + 24);
            L->fxoff = rdi16(q + 26); L->bxoff = rdi16(q + 28);
            q += 30;
        }
        {
            int ns = rd16(q); q += 2;
            if (ns > MAXSEC) ns = MAXSEC;
            g_nsec = ns;
            for (i = 0; i < ns; i++) {
                sector_t *s = &g_sec[i];
                s->floor_h = rdi16(q); s->ceil_h = rdi16(q + 2);
                s->floorpic = rd16(q + 4); s->ceilpic = rd16(q + 6);
                s->light = rd16(q + 8); s->special = rdi16(q + 10);
                s->tag = rdi16(q + 12);
                s->line_start = rd16(q + 14); s->line_count = rd16(q + 16);
                q += 18;
            }
        }
        {
            int total = 0;
            for (i = 0; i < g_nsec; i++) total += g_sec[i].line_count;
            if (total > MAXIDX) total = MAXIDX;
            g_nidx = total;
            for (i = 0; i < total; i++) { g_lidx[i] = rd16(q); q += 2; }
        }
        {
            int nt = rd16(q); q += 2;
            if (nt > MAXTH) nt = MAXTH;
            g_nthing = nt;
            for (i = 0; i < nt; i++) {
                mthing_t *t = &g_thing[i];
                t->x = rdi16(q); t->y = rdi16(q + 2);
                t->angle = rd16(q + 4); t->type = rd16(q + 6);
                t->flags = rdi16(q + 8);
                t->sector = rd16(q + 10); t->baseid = rd16(q + 12);
                q += 14;
            }
        }
    }

    p = chunk_ptr(pak, size, "THNG", &len);
    if (!p) return -7;
    {
        int nt = rd16(p); p += 2;
        if (nt > g_nthing) nt = g_nthing;
        for (i = 0; i < nt; i++) {
            g_meta[i].kind = rd16(p);
            g_meta[i].hp = rd16(p + 2);
            g_meta[i].letter = p[4];
            g_meta[i].radius = p[5];
            g_meta[i].height = p[6];
            g_meta[i].speed = p[7];
            p += 8;
        }
    }

    memcopy(g_sec0, g_sec, sizeof(sector_t) * g_nsec);

    /* line lengths + other sector */
    for (i = 0; i < g_nline; i++) {
        line_t *L = &g_line[i];
        int dx = g_vert[L->v2].x - g_vert[L->v1].x;
        int dy = g_vert[L->v2].y - g_vert[L->v1].y;
        int len = isqrt32((uint32_t)(dx * dx + dy * dy));
        g_line_len[i] = (int16_t)(len > 32000 ? 32000 : len);
        g_line_other[i] = -1;
        g_line_door[i] = -1;
    }

    /* find exit line + doors */
    for (i = 0; i < g_nline; i++) {
        line_t *L = &g_line[i];
        if (L->special == 11) g_exit_line = i;
    }
    g_ndoor = 0;
    for (i = 0; i < g_nline; i++) {
        line_t *L = &g_line[i];
        int ds = (L->special == 1 || L->special == 31 || L->special == 117 ||
                  L->special == 26 || L->special == 27 || L->special == 28);
        int d;
        if (!ds || L->bsec >= g_nsec) continue;
        for (d = 0; d < g_ndoor; d++)
            if (g_door[d].sector == L->bsec) break;
        if (d >= g_ndoor) {
            int open = 32767, j;
            door_t *D;
            if (g_ndoor >= MAXDOOR) continue;
            D = &g_door[g_ndoor];
            D->sector = L->bsec;
            D->key = (L->special == 26) ? 1 : (L->special == 27) ? 2 :
                     (L->special == 28) ? 4 : 0;
            for (j = 0; j < g_nline; j++) {
                line_t *M = &g_line[j];
                int other = -1;
                if (M->fsec == L->bsec) other = M->bsec;
                else if (M->bsec == L->bsec) other = M->fsec;
                if (other >= 0 && other < g_nsec && other != L->bsec)
                    if (g_sec[other].ceil_h < open) open = g_sec[other].ceil_h;
            }
            if (open == 32767) open = g_sec0[L->bsec].ceil_h + 128;
            open -= 4;
            D->open_h = (int16_t)open;
            D->state = (g_sec0[L->bsec].ceil_h > g_sec0[L->bsec].floor_h + 8) ? 2 : 0;
            d = g_ndoor++;
        }
        g_line_door[i] = (int16_t)d;
    }

    build_sprite_map();

    g_poss_base = -1; g_sarg_base = -1;
    for (i = 0; i < g_nbase; i++) {
        if (name_eq4(g_sprbase[i], "POSS")) g_poss_base = i;
        else if (name_eq4(g_sprbase[i], "SARG")) g_sarg_base = i;
    }

    /* named graphics */
    g_stbar = find_gfx("STBAR");
    g_titlepic = find_gfx("TITLEPIC");
    g_mdoom = find_gfx("M_DOOM");
    for (i = 0; i < 10; i++) {
        char nm[8];
        memzero(nm, 8);
        nm[0]='S'; nm[1]='T'; nm[2]='T'; nm[3]='N'; nm[4]='U'; nm[5]='M';
        nm[6] = (char)('0' + i);
        /* STTNUM0..9 are 7 chars; find_gfx compares 8 bytes incl NUL. */
        nm[6] = (char)('0' + i); nm[7] = 0;
        g_num[i] = find_gfx(nm);
        nm[3]='Y'; nm[4]='S';
        g_ysnum[i] = find_gfx(nm);
    }
    g_stbar = find_gfx("STBAR");
    {
        char nm[8] = "STFST00"; g_face[0] = find_gfx(nm);
        nm[5]='1'; g_face[1] = find_gfx(nm);
        nm[5]='2'; g_face[2] = find_gfx(nm);
        nm[5]='3'; g_face[3] = find_gfx(nm);
        nm[5]='4'; nm[6]='0'; g_face[4] = find_gfx(nm);
        nm[6]='2'; g_face[5] = find_gfx(nm);
        memcopy(nm, "STFDEAD0", 8); g_face[6] = find_gfx(nm);
        memcopy(nm, "STFGOD0", 8); g_face[7] = find_gfx(nm);
        for (i = 0; i < 6; i++) {
            memcopy(nm, "STKEYS0", 7); nm[6] = (char)('0' + i); nm[7] = 0;
            g_keyspr[i] = find_gfx(nm);
        }
    }
    for (i = 0; i < 64; i++) {
        char nm[8];
        memcopy(nm, "STCFN000", 8);
        {
            int v = i + 33;
            nm[5] = (char)('0' + (v / 100) % 10);
            nm[6] = (char)('0' + (v / 10) % 10);
            nm[7] = (char)('0' + v % 10);
        }
        g_font[i] = find_gfx(nm);
    }

    g_pisg[0] = find_spr("PISGA0"); g_pisg[1] = find_spr("PISGB0");
    g_shtg[0] = find_spr("SHTGA0"); g_shtg[1] = find_spr("SHTGB0");
    g_pisf = find_spr("PISFA0"); g_shtf = find_spr("SHTFA0");
    g_puff = find_spr("PUFFA0"); g_puff_b = find_spr("PUFFB0");
    g_bal1[0] = find_spr("BAL1A0"); g_bal1[1] = find_spr("BAL1B0");
    g_bal7[0] = find_spr("BAL7A0"); g_bal7[1] = find_spr("BAL7B0");

    build_shade();
    init_tables();
    return 0;
}

/* ---------------------------------------------------------------- utilities */

static void set_message(const char *s, uint32_t now)
{
    int i = 0;
    while (s[i] && i < 63) { g_message[i] = s[i]; i++; }
    g_message[i] = 0;
    g_message_ms = now;
}

static int point_seg_dist2(int px, int py, int ax, int ay, int bx, int by)
{
    int dx = bx - ax, dy = by - ay;
    int qx = px - ax, qy = py - ay;
    int dot = qx * dx + qy * dy;
    int len2 = dx * dx + dy * dy;
    if (len2 <= 0) return qx * qx + qy * qy;
    if (dot <= 0) return qx * qx + qy * qy;
    if (dot >= len2) {
        int ex = px - bx, ey = py - by;
        return ex * ex + ey * ey;
    }
    {
        int cross = qx * dy - qy * dx;
        return (cross * cross) / len2;
    }
}

/* --------------------------------------------------------------- game setup */

static void setup_mobj(int i)
{
    mobj_t *m = &g_mobj[i];
    mthing_t *t = &g_thing[i];
    tmeta_t *mt = &g_meta[i];
    memzero(m, sizeof(*m));
    m->x = (int32_t)t->x << 16;
    m->y = (int32_t)t->y << 16;
    m->angle = (int16_t)t->angle;
    m->type = (int16_t)t->type;
    m->kind = (uint8_t)mt->kind;
    m->baseid = (uint8_t)t->baseid;
    m->sector = (uint8_t)(t->sector < g_nsec ? t->sector : 0);
    m->radius = mt->radius;
    m->height = mt->height;
    m->speed = mt->speed;
    m->death_letter = mt->letter;
    m->hp = m->maxhp = (int16_t)mt->hp;
    m->active = 1;
    m->anim = (int16_t)(rnd() & 63);
    m->timer = (int16_t)(rnd() & 31);
    m->z = (int32_t)g_sec[m->sector].floor_h << 16;
    if (m->kind == 3) m->removed = 1;
}

void doom_start_game(int demo)
{
    int i;
    mthing_t *start = 0;
    mobj_t *st = 0;

    g_demo = demo;
    g_demo_tips = demo ? 900 : 0;
    g_game_ms = g_now_ms;
    g_state_ms = g_now_ms;
    g_state = ST_GAME;
    g_message[0] = 0;

    /* restore sectors and doors */
    memcopy(g_sec, g_sec0, sizeof(sector_t) * g_nsec);
    for (i = 0; i < g_ndoor; i++)
        g_door[i].state = (g_sec0[g_door[i].sector].ceil_h >
                           g_sec0[g_door[i].sector].floor_h + 8) ? 2 : 0;

    g_nmobj = g_nthing;
    for (i = 0; i < g_nthing; i++) setup_mobj(i);
    for (i = 0; i < MAXPRJ; i++) g_prj[i].active = 0;

    /* player start */
    for (i = 0; i < g_nthing; i++) {
        if (g_thing[i].type == 1) { start = &g_thing[i]; st = &g_mobj[i]; break; }
    }
    if (!start) {
        for (i = 0; i < g_nthing; i++)
            if (g_thing[i].type >= 2 && g_thing[i].type <= 4) {
                start = &g_thing[i]; st = &g_mobj[i]; break;
            }
    }
    memzero(&g_player, sizeof(g_player));
    if (start) {
        g_player.x = (int32_t)start->x << 16;
        g_player.y = (int32_t)start->y << 16;
        g_player.angle = (int16_t)start->angle;
        g_player.sector = (int16_t)(start->sector < g_nsec ? start->sector : 0);
        if (st) st->removed = 1;
    } else {
        g_player.x = 0; g_player.y = 0; g_player.angle = 0; g_player.sector = 0;
    }
    g_player.health = 100;
    g_player.armor = 0;
    g_player.bullets = 50;
    g_player.shells = 0;
    g_player.weapon = 0;
    g_player.floor_z = g_sec[g_player.sector].floor_h;
    g_player.fire_cd = 0;
    g_player.fire_anim = 0;
    g_player.bob = 0;
    g_player.damage_flash = 0;
    g_player.dead = 0;

    if (demo) {
        g_player.health = 200;
        g_player.armor = 100;
        g_player.bullets = 200;
        g_player.shells = 60;
        g_player.weapon = 1;
        g_player.keys = 1;   /* blue key */
        set_message("DEMO: BLUE KEY, SHOTGUN", g_now_ms);
    }
}

/* --------------------------------------------------------------- collision */

static int line_blocks(int li, int from_sector, int check_step)
{
    line_t *L = &g_line[li];
    int other;
    int top, bot;
    if (L->bsec == 0xFFFF || L->bsec >= g_nsec) return 1;
    other = (from_sector == L->fsec) ? L->bsec : L->fsec;
    if (from_sector != L->fsec && from_sector != L->bsec) return 0;
    top = g_sec[L->fsec].ceil_h < g_sec[L->bsec].ceil_h ?
          g_sec[L->fsec].ceil_h : g_sec[L->bsec].ceil_h;
    bot = g_sec[L->fsec].floor_h > g_sec[L->bsec].floor_h ?
          g_sec[L->fsec].floor_h : g_sec[L->bsec].floor_h;
    if (top - bot < 56) return 1;
    if (check_step && g_sec[other].floor_h - g_sec[from_sector].floor_h > 24) return 1;
    return 0;
}

static int try_pos(int32_t nx, int32_t ny)
{
    int i, sec = g_player.sector;
    int px = nx >> 16, py = ny >> 16;
    int r = 15;
    if (sec < 0 || sec >= g_nsec) return 1;
    for (i = 0; i < g_sec[sec].line_count; i++) {
        int li = g_lidx[g_sec[sec].line_start + i];
        line_t *L;
        if (li >= g_nline) continue;
        L = &g_line[li];
        if (!line_blocks(li, sec, 1)) continue;
        if (point_seg_dist2(px, py, g_vert[L->v1].x, g_vert[L->v1].y,
                            g_vert[L->v2].x, g_vert[L->v2].y) < r * r)
            return 0;
    }
    return 1;
}

/* proper segment/segment intersection, all in map units */
static int seg_intersect(int x1, int y1, int x2, int y2,
                         int x3, int y3, int x4, int y4)
{
    int64_t d = (int64_t)(x2 - x1) * (y4 - y3) - (int64_t)(y2 - y1) * (x4 - x3);
    int64_t t, u;
    if (!d) return 0;
    t = (int64_t)(x3 - x1) * (y4 - y3) - (int64_t)(y3 - y1) * (x4 - x3);
    u = (int64_t)(x3 - x1) * (y2 - y1) - (int64_t)(y3 - y1) * (x2 - x1);
    if (d < 0) { d = -d; t = -t; u = -u; }
    return t >= 0 && t <= d && u >= 0 && u <= d;
}

static int crosses_line(int32_t ox, int32_t oy, int32_t nx, int32_t ny, line_t *L)
{
    return seg_intersect(ox >> 16, oy >> 16, nx >> 16, ny >> 16,
                         g_vert[L->v1].x, g_vert[L->v1].y,
                         g_vert[L->v2].x, g_vert[L->v2].y);
}

static void update_sector(int32_t ox, int32_t oy, int32_t nx, int32_t ny)
{
    int i, sec = g_player.sector;
    if (sec < 0 || sec >= g_nsec) return;
    for (i = 0; i < g_sec[sec].line_count; i++) {
        int li = g_lidx[g_sec[sec].line_start + i];
        line_t *L;
        int other;
        if (li >= g_nline) continue;
        L = &g_line[li];
        if (L->bsec == 0xFFFF || L->bsec >= g_nsec) continue;
        if (!crosses_line(ox, oy, nx, ny, L)) continue;
        other = (sec == L->fsec) ? L->bsec : L->fsec;
        if (other < 0 || other >= g_nsec) continue;
        if (g_sec[other].ceil_h - g_sec[other].floor_h < 56) continue;
        if (g_sec[other].floor_h - g_player.floor_z > 24) continue;
        g_player.sector = (int16_t)other;
        g_player.floor_z = g_sec[other].floor_h;
        return;
    }
}

static void player_move(int32_t dx, int32_t dy)
{
    int32_t ox = g_player.x, oy = g_player.y;
    int32_t nx, ny;
    if (dx) {
        nx = g_player.x + dx;
        if (try_pos(nx, g_player.y)) g_player.x = nx;
        update_sector(ox, oy, g_player.x, g_player.y);
        ox = g_player.x; oy = g_player.y;
    }
    if (dy) {
        ny = g_player.y + dy;
        if (try_pos(g_player.x, ny)) g_player.y = ny;
        update_sector(ox, oy, g_player.x, g_player.y);
    }
}

/* ---------------------------------------------------------------- weapons */

static int wall_dist(int px, int py, int32_t dx, int32_t dy)
{
    int i, best = 32000;
    for (i = 0; i < g_nline; i++) {
        line_t *L = &g_line[i];
        int ax = g_vert[L->v1].x, ay = g_vert[L->v1].y;
        int ex = g_vert[L->v2].x - ax, ey = g_vert[L->v2].y - ay;
        int64_t den, num, t, un, ud;
        if (L->bsec != 0xFFFF && L->bsec < g_nsec) {
            int top = g_sec[L->fsec].ceil_h < g_sec[L->bsec].ceil_h ?
                      g_sec[L->fsec].ceil_h : g_sec[L->bsec].ceil_h;
            int bot = g_sec[L->fsec].floor_h > g_sec[L->bsec].floor_h ?
                      g_sec[L->fsec].floor_h : g_sec[L->bsec].floor_h;
            if (top - bot >= 56) continue;
        }
        den = (int64_t)dx * ey - (int64_t)dy * ex;
        if (!den) continue;
        num = (int64_t)(ax - px) * ey - (int64_t)(ay - py) * ex;
        un = (int64_t)(ax - px) * dy - (int64_t)(ay - py) * dx;
        ud = den;
        if (ud < 0) { ud = -ud; un = -un; }
        if (un < 0 || un > ud) continue;
        /* ray parameter in 16.16 map units */
        t = ((int64_t)num << 32) / den;
        if (t <= 0) continue;
        {
            int ti = (int)(t >> 16);
            if (ti < best) best = ti;
        }
    }
    return best;
}

static int damage_monster_in_line(int32_t dx, int32_t dy, int wdist, int dmg, int *hitdist)
{
    int i, best = -1, bestd = wdist;
    for (i = 0; i < g_nmobj; i++) {
        mobj_t *m = &g_mobj[i];
        int depth, side, r;
        int64_t ddx, ddy;
        if (!m->active || m->removed || m->kind != 1 || m->state == 4) continue;
        ddx = (int64_t)m->x - g_player.x;
        ddy = (int64_t)m->y - g_player.y;
        depth = (int)(((int64_t)ddx * dx + (int64_t)ddy * dy) >> 32);
        if (depth <= 4) continue;
        side = (int)(((int64_t)ddx * dy - (int64_t)ddy * dx) >> 32);
        r = m->radius + 8;
        if (side < -r || side > r) continue;
        if (depth < bestd) { bestd = depth; best = i; }
    }
    if (hitdist) *hitdist = bestd;
    if (best >= 0) {
        mobj_t *m = &g_mobj[best];
        m->hp = (int16_t)(m->hp - dmg);
        if (m->hp <= 0) {
            m->state = 4;
            m->timer = 0;
            doom_sound(m->baseid == g_poss_base ? SFX_POSDTH :
                       m->baseid == g_sarg_base ? SFX_SARGDTH : SFX_OOF);
        } else {
            m->state = 3;
            m->timer = 7;
            doom_sound(m->baseid == g_sarg_base ? SFX_SARGSIT : SFX_POSSIT);
        }
        return 1;
    }
    return 0;
}

static void add_puff(int32_t x, int32_t y, int32_t z, int base)
{
    int i;
    for (i = 0; i < g_nmobj; i++)
        if (g_mobj[i].removed || !g_mobj[i].active) break;
    if (i >= g_nmobj) {
        if (g_nmobj >= MAXMOBJ) return;
        i = g_nmobj++;
    }
    {
        mobj_t *m = &g_mobj[i];
        memzero(m, sizeof(*m));
        m->x = x; m->y = y; m->z = z;
        m->kind = 6;
        m->active = 1;
        m->baseid = (uint8_t)base;
        m->timer = 10;
        m->sector = g_player.sector;
        m->height = 20;
        m->radius = 8;
    }
}

static void fire_shot(int angle, int dmg, int pellets)
{
    int p;
    for (p = 0; p < pellets; p++) {
        int a = angle + (pellets > 1 ? (p - 1) * 1100 : 0);
        int32_t dx = dcos(a), dy = dsin(a);
        int wd = wall_dist(g_player.x >> 16, g_player.y >> 16, dx, dy);
        int hd = wd;
        if (damage_monster_in_line(dx, dy, wd, dmg, &hd)) {
            doom_sound(SFX_OOF);
        } else {
            int32_t hx = g_player.x + dx * hd;
            int32_t hy = g_player.y + dy * hd;
            add_puff(hx, hy, (int32_t)(g_player.floor_z + 32) << 16, g_puff);
            doom_sound(SFX_PISTOL);
        }
    }
}

static void do_fire(void)
{
    uint32_t cd;
    if (g_player.dead) return;
    if (g_player.fire_cd > 0) return;
    if (g_player.weapon == 1) {
        if (g_player.shells <= 0) { set_message("OUT OF SHELLS", g_now_ms); doom_sound(SFX_NOWAY); g_player.fire_cd = 400; return; }
        g_player.shells--;
        cd = 850;
        doom_sound(SFX_SHOTGUN);
        fire_shot(g_player.angle, 7 + (rnd() & 7), 3);
    } else {
        if (g_player.bullets <= 0) { set_message("OUT OF AMMO", g_now_ms); doom_sound(SFX_NOWAY); g_player.fire_cd = 400; return; }
        g_player.bullets--;
        cd = 360;
        doom_sound(SFX_PISTOL);
        fire_shot(g_player.angle + ((rnd() & 15) - 7), 4 + (rnd() & 7), 1);
    }
    g_player.fire_cd = (int16_t)cd;
    g_player.fire_anim = 10;
    g_player.bob = 12;
}

/* --------------------------------------------------------------- doors/use */

static void trigger_door(int di)
{
    door_t *D;
    if (di < 0 || di >= g_ndoor) return;
    D = &g_door[di];
    if (D->key && !(g_player.keys & D->key)) {
        set_message(D->key == 1 ? "YOU NEED A BLUE KEY" :
                    D->key == 2 ? "YOU NEED A YELLOW KEY" :
                                  "YOU NEED A RED KEY", g_now_ms);
        doom_sound(SFX_NOWAY);
        return;
    }
    if (D->state == 0 || D->state == 2) {
        /* already open: leave it */
        if (D->state == 0) D->state = 1;
    } else {
        D->state = 1;
    }
    doom_sound(SFX_DOOR);
}

static void do_use(void)
{
    int i, best = -1, bestd = 96 * 96;
    int px = g_player.x >> 16, py = g_player.y >> 16;
    for (i = 0; i < g_nline; i++) {
        line_t *L = &g_line[i];
        int d;
        if (!(L->special == 1 || L->special == 31 || L->special == 117 ||
              L->special == 26 || L->special == 27 || L->special == 28 ||
              L->special == 11)) continue;
        d = point_seg_dist2(px, py, g_vert[L->v1].x, g_vert[L->v1].y,
                            g_vert[L->v2].x, g_vert[L->v2].y);
        if (d < bestd) { bestd = d; best = i; }
    }
    if (best < 0) { doom_sound(SFX_SWITCH); return; }
    if (g_line[best].special == 11) {
        g_state = ST_WIN;
        g_state_ms = g_now_ms;
        doom_sound(SFX_SWITCH);
        return;
    }
    trigger_door(g_line_door[best]);
    doom_sound(SFX_SWITCH);
}

/* ---------------------------------------------------------------- monsters */

static int blocks_sight(line_t *L)
{
    if (L->bsec == 0xFFFF || L->bsec >= (uint16_t)g_nsec) return 1;
    {
        int top = g_sec[L->fsec].ceil_h < g_sec[L->bsec].ceil_h ?
                  g_sec[L->fsec].ceil_h : g_sec[L->bsec].ceil_h;
        int bot = g_sec[L->fsec].floor_h > g_sec[L->bsec].floor_h ?
                  g_sec[L->fsec].floor_h : g_sec[L->bsec].floor_h;
        if (top - bot >= 56) return 0;
        return 1;
    }
}

static int los_clear(int x1, int y1, int x2, int y2)
{
    int i;
    for (i = 0; i < g_nline; i++) {
        line_t *L = &g_line[i];
        if (!blocks_sight(L)) continue;
        if (seg_intersect(x1, y1, x2, y2,
                          g_vert[L->v1].x, g_vert[L->v1].y,
                          g_vert[L->v2].x, g_vert[L->v2].y)) return 0;
    }
    return 1;
}

static void spawn_projectile(mobj_t *m, int kind)
{
    int i;
    for (i = 0; i < MAXPRJ; i++) if (!g_prj[i].active) break;
    if (i >= MAXPRJ) return;
    {
        prj_t *p = &g_prj[i];
        int a = atan2_16((g_player.y - m->y) >> 16, (g_player.x - m->x) >> 16);
        int speed = kind ? 300 : 350;
        p->x = m->x;
        p->y = m->y;
        p->z = m->z + ((int32_t)28 << 16);
        p->vx = dcos(a) * speed;
        p->vy = dsin(a) * speed;
        p->life = 600;
        p->damage = 8 + (rnd() & 15);
        p->active = 1;
        p->kind = (uint8_t)kind;
        p->sector = m->sector;
    }
}

static void hurt_player(int dmg)
{
    int absorbed;
    if (g_player.dead) return;
    if (g_player.armor > 0) {
        absorbed = dmg / 3;
        if (absorbed > g_player.armor) absorbed = g_player.armor;
        g_player.armor -= (int16_t)absorbed;
        dmg -= absorbed;
    }
    g_player.health -= (int16_t)dmg;
    g_player.damage_flash = 180;
    if (g_player.health <= 0) {
        g_player.health = 0;
        g_player.dead = 1;
        g_state = ST_DEAD;
        g_state_ms = g_now_ms;
        doom_sound(SFX_DEATH);
    } else {
        doom_sound(SFX_PAIN);
    }
}

/* monster collision probe at world position (nx,ny), 16.16 */
static int mobj_probe(mobj_t *m, int32_t nx, int32_t ny)
{
    int px = nx >> 16, py = ny >> 16;
    int sec = m->sector;
    int i;
    if (sec < 0 || sec >= g_nsec) return 0;
    for (i = 0; i < g_sec[sec].line_count; i++) {
        int li = g_lidx[g_sec[sec].line_start + i];
        line_t *L;
        if (li >= g_nline) continue;
        L = &g_line[li];
        if (L->bsec == 0xFFFF) {
            if (point_seg_dist2(px, py, g_vert[L->v1].x, g_vert[L->v1].y,
                                g_vert[L->v2].x, g_vert[L->v2].y) < 18 * 18)
                return 0;
        } else if (line_blocks(li, sec, 1)) {
            if (point_seg_dist2(px, py, g_vert[L->v1].x, g_vert[L->v1].y,
                                g_vert[L->v2].x, g_vert[L->v2].y) < 18 * 18)
                return 0;
        }
    }
    return 1;
}

static void monster_update(mobj_t *m)
{
    int32_t dx, dy;
    int dist, a;
    if (!m->active || m->removed || m->kind != 1) return;
    if (m->state == 4) { m->anim++; return; }
    dx = g_player.x - m->x;
    dy = g_player.y - m->y;
    a = atan2_16(dy >> 16, dx >> 16);
    dist = isqrt32((uint32_t)(((int64_t)dx * dx + (int64_t)dy * dy) >> 32));
    if (m->timer > 0) m->timer--;
    if (m->state == 3 && m->timer == 0) m->state = 1;
    if (g_player.dead) { m->state = 0; return; }

    if ((m->anim & 15) == 0) {
        if (dist < 1000 && los_clear(m->x >> 16, m->y >> 16,
                                     g_player.x >> 16, g_player.y >> 16))
            m->state = (m->state == 0) ? 1 : m->state;
        else if (dist > 1000)
            m->state = 0;
    }
    if (m->state == 0) { m->anim++; return; }

    m->angle = (int16_t)a;
    m->anim++;

    if (dist > 58) {
        int mv = (int)m->speed * 35 * 16 / 1000;
        int32_t stepx, stepy;
        if (mv < 1) mv = 1;
        stepx = (int32_t)(((int64_t)dx * mv) / (dist ? dist : 1));
        stepy = (int32_t)(((int64_t)dy * mv) / (dist ? dist : 1));
        /* wall collision: probe, then axis slide */
        {
            int32_t nx = m->x + stepx, ny = m->y + stepy;
            if (mobj_probe(m, nx, ny)) { m->x = nx; m->y = ny; }
            else {
                if (mobj_probe(m, m->x + stepx, m->y)) m->x += stepx;
                if (mobj_probe(m, m->x, m->y + stepy)) m->y += stepy;
            }
        }
    } else if (m->timer == 0) {
        /* attack */
        if (m->type == 3001 || m->type == 58 || m->type == 3002) {
            hurt_player(3 + (rnd() & 9));
            doom_sound(SFX_CLAW);
            m->timer = 40 + (rnd() & 31);
            m->state = 2;
        } else if (dist < 700 && los_clear(m->x >> 16, m->y >> 16,
                                           g_player.x >> 16, g_player.y >> 16)) {
            if (m->type == 3004 || m->type == 9) {
                if (los_clear(m->x >> 16, m->y >> 16,
                              g_player.x >> 16, g_player.y >> 16))
                    hurt_player(2 + (rnd() & 7));
                doom_sound(SFX_POSACT);
                m->timer = 50 + (rnd() & 31);
            } else {
                spawn_projectile(m, m->type == 3001 ? 0 : 1);
                doom_sound(SFX_FIRSHT);
                m->timer = 60 + (rnd() & 31);
            }
            m->state = 2;
        }
    }
    if (dist < 700 && m->type != 3001 && m->type != 3002 && m->type != 58 &&
        dist > 70 && m->timer == 0) {
        if (los_clear(m->x >> 16, m->y >> 16, g_player.x >> 16, g_player.y >> 16)) {
            spawn_projectile(m, m->type == 3001 ? 0 : 1);
            doom_sound(SFX_FIRSHT);
            m->timer = 50 + (rnd() & 31);
            m->state = 2;
        }
    }
}

static void update_projectiles(int dt)
{
    int i;
    for (i = 0; i < MAXPRJ; i++) {
        prj_t *p = &g_prj[i];
        int32_t nx, ny;
        if (!p->active) continue;
        nx = p->x + (int32_t)(((int64_t)p->vx * dt) / 1000);
        ny = p->y + (int32_t)(((int64_t)p->vy * dt) / 1000);
        p->x = nx; p->y = ny;
        p->life -= (int16_t)(dt / 16);
        if (p->life <= 0) { p->active = 0; continue; }
        {
            int64_t ddx = (int64_t)nx - g_player.x;
            int64_t ddy = (int64_t)ny - g_player.y;
            int64_t hit_r = (int64_t)24 << 16;
            if ((ddx * ddx + ddy * ddy) < hit_r * hit_r) {
                hurt_player(p->damage);
                add_puff(nx, ny, p->z, g_puff);
                p->active = 0;
                doom_sound(SFX_FIRXPL);
                continue;
            }
        }
        /* wall check: any blocking line within 12 units */
        {
            int px = nx >> 16, py = ny >> 16;
            int j, hit = 0;
            for (j = 0; j < g_nline; j++) {
                line_t *L = &g_line[j];
                int top, bot;
                if (L->bsec != 0xFFFF && L->bsec < g_nsec) {
                    top = g_sec[L->fsec].ceil_h < g_sec[L->bsec].ceil_h ?
                          g_sec[L->fsec].ceil_h : g_sec[L->bsec].ceil_h;
                    bot = g_sec[L->fsec].floor_h > g_sec[L->bsec].floor_h ?
                          g_sec[L->fsec].floor_h : g_sec[L->bsec].floor_h;
                    if (top - bot >= 56) continue;
                }
                if (point_seg_dist2(px, py, g_vert[L->v1].x, g_vert[L->v1].y,
                                    g_vert[L->v2].x, g_vert[L->v2].y) < 10 * 10) {
                    hit = 1; break;
                }
            }
            if (hit) {
                add_puff(nx, ny, p->z, g_puff);
                p->active = 0;
                doom_sound(SFX_FIRXPL);
            }
        }
    }
}

/* ------------------------------------------------------------------ items */

static void pick_item(mobj_t *m)
{
    int t = m->type;
    int i;
    switch (t) {
    case 2011: g_player.health += 10; break;
    case 2012: g_player.health += 25; break;
    case 2014: g_player.health += 1; break;
    case 2015: g_player.health += 2; break;
    case 2013: g_player.health += 100; break;
    case 2018: g_player.armor = (int16_t)(g_player.armor + 100); break;
    case 2019: g_player.armor = (int16_t)(g_player.armor + 200); break;
    case 2007: g_player.bullets += 10; break;
    case 2008: g_player.shells += 4; break;
    case 2046: g_player.shells += 20; break;
    case 2047: g_player.bullets += 20; break;
    case 2048: g_player.bullets += 50; break;
    case 2001:
        g_player.weapon = 1; g_player.shells += 8;
        set_message("YOU GOT THE SHOTGUN", g_now_ms);
        doom_sound(SFX_WEAPON);
        break;
    case 2002:
        g_player.bullets += 20;
        set_message("YOU GOT A CHAINGUN", g_now_ms);
        doom_sound(SFX_WEAPON);
        break;
    case 2003: case 2004: case 2005:
        g_player.weapon = 1; g_player.shells += 10;
        set_message("NEW WEAPON!", g_now_ms);
        doom_sound(SFX_WEAPON);
        break;
    case 5: g_player.keys |= 1; set_message("BLUE KEYCARD", g_now_ms); doom_sound(SFX_ITEM); break;
    case 6: g_player.keys |= 2; set_message("YELLOW KEYCARD", g_now_ms); doom_sound(SFX_ITEM); break;
    case 13: g_player.keys |= 4; set_message("RED KEYCARD", g_now_ms); doom_sound(SFX_ITEM); break;
    default:
        if (t == 2028 || t == 2035) break;
        doom_sound(SFX_ITEM);
        break;
    }
    if (g_player.health > 200) g_player.health = 200;
    if (g_player.armor > 200) g_player.armor = 200;
    if (g_player.bullets > 400) g_player.bullets = 400;
    if (g_player.shells > 100) g_player.shells = 100;
    m->removed = 1;
    m->active = 0;
    (void)i;
}

static void check_items(void)
{
    int i;
    for (i = 0; i < g_nmobj; i++) {
        mobj_t *m = &g_mobj[i];
        int64_t dx, dy;
        if (!m->active || m->removed) continue;
        if (m->kind == 4) m->anim++;         /* keycard frame anim */
        if (m->kind != 2 && m->kind != 4) continue;
        dx = (int64_t)m->x - g_player.x;
        dy = (int64_t)m->y - g_player.y;
        {
            int64_t pick_r = (int64_t)28 << 16;
            if ((dx * dx + dy * dy) < pick_r * pick_r &&
                (m->sector == g_player.sector ||
                 (m->z - ((int32_t)g_player.floor_z << 16)) < ((int32_t)40 << 16)))
                pick_item(m);
        }
    }
    /* puffs */
    for (i = 0; i < g_nmobj; i++) {
        mobj_t *m = &g_mobj[i];
        if (m->kind == 6) {
            if (m->timer > 0) m->timer--;
            else { m->active = 0; m->removed = 1; }
        }
    }
}

/* ------------------------------------------------------------------ doors */

static void door_update(int dt)
{
    int i;
    for (i = 0; i < g_ndoor; i++) {
        door_t *D = &g_door[i];
        sector_t *s = &g_sec[D->sector];
        if (D->state == 1) {
            int step = (dt * 70) / 1000;
            if (step < 1) step = 1;
            if (s->ceil_h + step >= D->open_h) {
                s->ceil_h = D->open_h;
                D->state = 2;
            } else {
                s->ceil_h = (int16_t)(s->ceil_h + step);
            }
        }
    }
}

/* --------------------------------------------------------------- game tick */

static void handle_game_input(const input_t *in)
{
    int now = (int)g_now_ms;
    char msg[64];

    if (g_player.fire_cd > 0) {
        g_player.fire_cd = (int16_t)(g_player.fire_cd - 16);
        if (g_player.fire_cd < 0) g_player.fire_cd = 0;
    }
    if (g_player.damage_flash > 0) {
        g_player.damage_flash -= 16;
        if (g_player.damage_flash < 0) g_player.damage_flash = 0;
    }
    if (g_player.bob > 0) g_player.bob--;
    if (g_player.fire_anim > 0) g_player.fire_anim--;

    /* tilt-to-look (experimental): device roll turns, baseline follows slowly
     * so it works in any held pose; only axis 0 is used in landscape */
    if (g_tilt && g_accel_fn) {
        int32_t raw[3];
        int d, dead = 900;
        g_accel_fn(raw);
        if (!s_tilt_base) s_tilt_base = raw[0];
        d = raw[0] - s_tilt_base;
        if (d > dead || d < -dead) {
            g_player.angle = (int16_t)((g_player.angle + (d >> 6)) & 65535);
            g_player.bob = 8;
        } else {
            s_tilt_base += (raw[0] - s_tilt_base) >> 6;
        }
    }

    if (g_demo_tips > 0) g_demo_tips -= 16;

    /* pause button in the top-right corner */
    if (in->tap && in->tap_x >= GW - 44 && in->tap_x < GW - 4 &&
        in->tap_y >= 4 && in->tap_y < 32) {
        g_state = ST_PAUSE;
        g_state_ms = g_now_ms;
        return;
    }

    if (in->btn_fire) do_fire();
    if (in->btn_use) do_use();

    /* drag controls: left half = strafe, right half = turn, vertical = walk */
    if (in->down && (in->dx || in->dy)) {
        int32_t fx = dcos(g_player.angle), fy = dsin(g_player.angle);
        int32_t rx = dsin(g_player.angle), ry = -dcos(g_player.angle);
        if (in->press_x >= CX) {
            /* right-half drag: swipe right = look right (angle -= dx) */
            g_player.angle = (int16_t)((g_player.angle - in->dx * 64) & 65535);
        } else {
            int st = in->dx * 2;
            player_move((int32_t)((int64_t)rx * st),
                        (int32_t)((int64_t)ry * st));
        }
        {
            int fwd = -in->dy * 2;
            player_move((int32_t)((int64_t)fx * fwd),
                        (int32_t)((int64_t)fy * fwd));
        }
        g_player.bob = 8;
    }

    if (in->tap && g_player.fire_cd == 0) {
        /* a tap that is not the pause button fires */
        if (!(in->tap_x >= GW - 44 && in->tap_y < 32))
            do_fire();
    }

    check_items();
    {
        int i;
        for (i = 0; i < g_nmobj; i++) monster_update(&g_mobj[i]);
    }
    update_projectiles(16);
    door_update(16);

    /* message expiry */
    if (g_message[0] && g_now_ms - g_message_ms > 2600) g_message[0] = 0;
    (void)msg; (void)now;
}

/* ------------------------------------------------------------------ frame */

static void handle_menu_input(const input_t *in)
{
    int i;
    if (!in->tap) return;
    if (g_state == ST_MENU) {
        for (i = 0; i < 4; i++) {
            int y = 118 + i * 28;
            if (in->tap_x >= 96 && in->tap_x < GW - 96 &&
                in->tap_y >= y - 4 && in->tap_y < y + 22) {
                g_menu_sel = i;
                g_state_ms = g_now_ms;
                if (i == 0) doom_start_game(0);
                else if (i == 1) doom_start_game(1);
                else if (i == 2) g_state = ST_CONTROLS;
                else g_state = ST_ABOUT;
                return;
            }
        }
    } else if (g_state == ST_TITLE) {
        g_state = ST_MENU;
        g_state_ms = g_now_ms;
    } else if (g_state == ST_CONTROLS || g_state == ST_ABOUT || g_state == ST_WIN) {
        g_state = ST_MENU;
        g_state_ms = g_now_ms;
    }
}

static void handle_pause_input(const input_t *in)
{
    if (!in->tap) return;
    if (in->tap_y >= 100 && in->tap_y < 126) {
        g_state = ST_GAME;
        g_state_ms = g_now_ms;
    } else if (in->tap_y >= 132 && in->tap_y < 158) {
        doom_start_game(g_demo);
    } else if (in->tap_y >= 164 && in->tap_y < 190) {
        g_state = ST_MENU;
        g_state_ms = g_now_ms;
    } else if (in->tap_y >= 196 && in->tap_y < 222) {
        g_tilt ^= 1;               /* tilt-to-look toggle */
        if (!g_tilt) s_tilt_base = 0;
    }
}

static void handle_dead_input(const input_t *in)
{
    if (!in->tap) return;
    if (in->tap_y >= 140 && in->tap_y < 172) doom_start_game(g_demo);
    else if (in->tap_y >= 178 && in->tap_y < 210) {
        g_state = ST_MENU; g_state_ms = g_now_ms;
    }
}

void doom_frame(const input_t *in)
{
    g_frame++;
    switch (g_state) {
    case ST_TITLE:
    case ST_MENU:
    case ST_CONTROLS:
    case ST_ABOUT:
        handle_menu_input(in);
        r_clear();
        if (g_state == ST_TITLE) ui_draw_title();
        else if (g_state == ST_MENU) ui_draw_menu();
        else if (g_state == ST_CONTROLS) ui_draw_controls();
        else ui_draw_about();
        break;
    case ST_GAME:
        handle_game_input(in);
        if (g_state == ST_GAME) {
            r_draw_world();
            r_draw_sprites();
            r_draw_weapon();
            ui_draw_hud();
            if (g_demo && g_demo_tips > 0) ui_draw_demo_tips();
            if (g_message[0]) {
                ui_text(CX - ui_text_width(g_message, 1) / 2, VIEW_H - 22,
                        g_message, 1, 0xffffcc);
            }
        }
        break;
    case ST_PAUSE:
        handle_pause_input(in);
        r_draw_world();
        r_draw_sprites();
        ui_draw_hud();
        ui_draw_pause();
        break;
    case ST_DEAD:
        handle_dead_input(in);
        r_draw_world();
        r_draw_sprites();
        ui_draw_hud();
        ui_draw_dead();
        break;
    case ST_WIN:
        handle_menu_input(in);
        if (g_state == ST_WIN) {
            r_draw_world();
            r_draw_sprites();
            ui_draw_win();
        } else {
            r_clear();
            ui_draw_menu();
        }
        break;
    }
}

void doom_quit_to_menu(void)
{
    g_state = ST_MENU;
    g_state_ms = g_now_ms;
}

/* --------------------------------------------------------------- present */

void doom_present(uint32_t *fb)
{
    int cx, cy;
    /* portrait panel: canvas (cx,cy) -> (239-cy, cx) */
    for (cx = 0; cx < GW; cx++) {
        uint32_t *dst = fb + (long)cx * 240 + 239;
        const uint32_t *src = g_cv + cx;
        for (cy = 0; cy < GH; cy++) {
            *dst-- = src[0] | 0xff000000u;
            src += GW;
        }
    }
}
