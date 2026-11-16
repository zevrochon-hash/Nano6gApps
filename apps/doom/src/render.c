/*
 * render.c — textured sector-portal column renderer.
 *
 * One ray per screen column.  The ray walks from the player's sector through
 * two-sided openings, drawing floors, ceilings and wall pieces in near-to-far
 * order.  Everything is fixed point; there is no floating point in here.
 */
#include "doom.h"

#define MAXD16       (8000 << 16)
#define MIDMAX       12

typedef struct {
    int16_t  y0, y1;
    int16_t  tex;
    int16_t  topz;
    int16_t  light;
    int32_t  t16;
    int32_t  u16;
} midpic_t;

typedef struct {
    int16_t h;
    int32_t d[100];
} pctab_t;

static int32_t  s_rayx[GW], s_rayy[GW];
static pctab_t  s_pc[PLANE_CACHE];
static int      s_npc;
static midpic_t s_mid[MIDMAX];
static int      s_nmid;

static uint32_t s_sky[VIEW_H];

/* --------------------------------------------------------------- utilities */

static uint32_t shade_rgb(int r, int g, int b, int lev)
{
    int lum = 255 - lev * 7;
    if (lev == 31) lum = 22;
    if (lum < 0) lum = 0;
    return (((uint32_t)(r * lum >> 8)) << 16) |
           (((uint32_t)(g * lum >> 8)) << 8) |
            ((uint32_t)(b * lum >> 8));
}

static int shade_level(int light, int dist)
{
    int l = ((255 - light) * 20) / 255;
    l += dist / 192;
    if (l < 0) l = 0;
    if (l > 31) l = 31;
    return l;
}

static void init_sky(void)
{
    int y;
    for (y = 0; y < VIEW_H; y++) {
        int r = 12 + (y * 26) / VIEW_H;
        int g = 20 + (y * 44) / VIEW_H;
        int b = 52 + (y * 84) / VIEW_H;
        s_sky[y] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
}

void r_clear(void)
{
    int i, n = GW * GH;
    for (i = 0; i < n; i++) g_cv[i] = 0;
}

/* ------------------------------------------------------------ plane cache */

static int32_t *get_plane(int z)
{
    int i, dy;
    int eye = g_player.floor_z + 41;
    int dz = z - eye;
    pctab_t *p;
    if (dz < 0) dz = -dz;
    for (i = 0; i < s_npc; i++)
        if (s_pc[i].h == z) return s_pc[i].d;
    if (s_npc >= PLANE_CACHE) return s_pc[0].d;
    p = &s_pc[s_npc++];
    p->h = (int16_t)z;
    if (dz == 0) dz = 1;
    for (dy = 0; dy < 100; dy++) {
        int64_t v;
        if (dy == 0) { p->d[0] = MAXD16; continue; }
        v = ((int64_t)FOCAL * dz * 65536) / dy;
        if (v > MAXD16) v = MAXD16;
        p->d[dy] = (int32_t)v;
    }
    return p->d;
}

/* --------------------------------------------------------------- planes */

static void draw_plane(int x, int y0, int y1, int height, int is_ceil,
                       int light, int flatid)
{
    const uint8_t *fp = 0;
    int32_t *tab;
    int32_t px = g_player.x, py = g_player.y;
    int32_t rx = s_rayx[x], ry = s_rayy[x];
    int y;

    if (y0 < 0) y0 = 0;
    if (y1 > VIEW_H) y1 = VIEW_H;
    if (y0 >= y1) return;

    if (is_ceil && flatid == g_sky_flat) {
        for (y = y0; y < y1; y++) g_cv[y * GW + x] = s_sky[y];
        return;
    }
    if (flatid < 0 || flatid >= g_nflat) {
        for (y = y0; y < y1; y++) g_cv[y * GW + x] = shade_rgb(58, 44, 34, 18);
        return;
    }
    fp = g_flat[flatid].pix;
    tab = get_plane(height);
    for (y = y0; y < y1; y++) {
        int dy = is_ceil ? (HORIZON - y) : (y - HORIZON);
        int32_t d, wx, wy;
        int lev;
        if (dy <= 0) continue;
        if (dy > 99) dy = 99;
        d = tab[dy];
        wx = px + (int32_t)(((int64_t)d * rx) >> 16);
        wy = py + (int32_t)(((int64_t)d * ry) >> 16);
        lev = shade_level(light, d >> 16);
        g_cv[y * GW + x] = g_shade[lev][fp[(((wy >> 16) & 63) << 6) |
                                            ((wx >> 16) & 63)]];
    }
}

/* ---------------------------------------------------------------- walls */

static void draw_wall_col(int x, int y0, int y1, int texid, int32_t t16,
                          int topz, int light, int32_t u16, int allow_alpha)
{
    const tex_t *tex;
    int64_t step, v16, h16;
    int uu, depth, y, lev;
    (void)topz;
    int rows = y1 - y0;              /* full wall span in screen rows     */
    int cy0, cy1;

    if (y0 >= y1) return;

    lev = shade_level(light, t16);
    depth = t16;
    if (depth > 65534) depth = 65534;
    if (depth < 0) depth = 0;

    cy0 = y0 < 0 ? 0 : y0;
    cy1 = y1 > VIEW_H ? VIEW_H : y1;

    if (texid < 0 || texid >= g_ntex || !g_tex[texid].pix ||
        g_tex[texid].w == 0 || g_tex[texid].h == 0) {
        uint32_t c = shade_rgb(78, 58, 42, lev);
        for (y = cy0; y < cy1; y++) {
            g_cv[y * GW + x] = c;
            g_depth[y * GW + x] = (uint16_t)depth;
        }
        return;
    }
    tex = &g_tex[texid];
    uu = (u16 >> 16) % tex->w;
    if (uu < 0) uu += tex->w;
    if (uu >= tex->w) uu = tex->w - 1;
    step = ((int64_t)tex->h << 16) / rows;
    v16 = (int64_t)(cy0 - y0) * step;
    h16 = (int64_t)tex->h << 16;
    for (y = cy0; y < cy1; y++) {
        int vr = (int)(v16 >> 16);
        uint8_t pix;
        if (vr >= tex->h) vr = tex->h - 1;
        else if (vr < 0) vr = 0;
        pix = tex->pix[vr * tex->w + uu];
        if (pix != 255 || !allow_alpha) {
            if (pix == 255) g_cv[y * GW + x] = shade_rgb(20, 16, 12, lev);
            else g_cv[y * GW + x] = g_shade[lev][pix];
            g_depth[y * GW + x] = (uint16_t)depth;
        }
        v16 += step;
    }
    (void)h16;
}

static void flush_mids(int x)
{
    int i;
    for (i = s_nmid - 1; i >= 0; i--) {
        midpic_t *m = &s_mid[i];
        draw_wall_col(x, m->y0, m->y1, m->tex, m->t16, m->topz,
                      m->light, m->u16, 1);
    }
    s_nmid = 0;
}

/* ------------------------------------------------------------- ray walking */

static void render_column(int x)
{
    int sector = g_player.sector;
    int32_t rx = s_rayx[x], ry = s_rayy[x];
    int64_t tmin = 2;                 /* near clip, map units            */
    int band_top = 0, band_bot = VIEW_H;
    int iter;
    int eye = g_player.floor_z + 41;

    s_nmid = 0;
    if (sector < 0 || sector >= g_nsec) sector = 0;

    for (iter = 0; iter < 20; iter++) {
        sector_t *sc = &g_sec[sector];
        int best = -1;
        int64_t bestt = 0, bun = 0, bden = 0;
        int i;

        for (i = 0; i < sc->line_count; i++) {
            int li = g_lidx[sc->line_start + i];
            line_t *L;
            int ax, ay, ex, ey;
            int64_t den, apx, apy, un, num, t16;
            if (li >= g_nline) continue;
            L = &g_line[li];
            ax = g_vert[L->v1].x; ay = g_vert[L->v1].y;
            ex = g_vert[L->v2].x - ax; ey = g_vert[L->v2].y - ay;
            den = (int64_t)rx * ey - (int64_t)ry * ex;
            if (!den) continue;
            apx = ax - (g_player.x >> 16);
            apy = ay - (g_player.y >> 16);
            un = apx * ry - apy * rx;
            {
                int64_t ud = den, uu = un;
                if (ud < 0) { ud = -ud; uu = -uu; }
                if (uu < 0 || uu > ud) continue;
            }
            num = apx * ey - apy * ex;
            t16 = (num << 16) / den;
            if (t16 <= tmin) continue;
            if (best < 0 || t16 < bestt) {
                best = li;
                bestt = t16;
                bun = un;
                bden = den;
            }
        }

        if (best < 0) {
            int32_t tp = 6000;
            int32_t inv16 = (int32_t)(((int64_t)1 << 16) / tp);
            int cy = HORIZON + (int32_t)(((int64_t)(eye - sc->ceil_h) * FOCAL * inv16) >> 16);
            int fy = HORIZON + (int32_t)(((int64_t)(eye - sc->floor_h) * FOCAL * inv16) >> 16);
            if (band_bot > cy) draw_plane(x, band_top, band_bot < cy ? band_bot : cy,
                                          sc->ceil_h, 1, sc->light, sc->ceilpic);
            if (band_bot > fy) draw_plane(x, fy > band_top ? fy : band_top, band_bot,
                                          sc->floor_h, 0, sc->light, sc->floorpic);
            break;
        }

        {
            line_t *L = &g_line[best];
            int32_t tp = (int32_t)(bestt < 8 ? 8 : bestt);
            int32_t inv16 = (int32_t)(((int64_t)1 << 16) / tp);
            int front = (sector == L->fsec);
            int other = front ? L->bsec : L->fsec;
            int cc = sc->ceil_h, cf = sc->floor_h;
            int nc, nf;
            int open = 1;
            int32_t u16, ufrac;

            if (sector != L->fsec && sector != L->bsec) break;
            if (other < 0 || other >= g_nsec) other = -1;
            nc = (other >= 0) ? g_sec[other].ceil_h : cc;
            nf = (other >= 0) ? g_sec[other].floor_h : cf;

            /* floor/ceiling planes up to the wall */
            {
                int cy = HORIZON + (int32_t)(((int64_t)(eye - cc) * FOCAL * inv16) >> 16);
                int fy = HORIZON + (int32_t)(((int64_t)(eye - cf) * FOCAL * inv16) >> 16);
                int cy1 = band_bot < cy ? band_bot : cy;
                int fy0 = band_top > fy ? band_top : fy;
                if (cy1 > band_top)
                    draw_plane(x, band_top, cy1, cc, 1, sc->light, sc->ceilpic);
                if (band_bot > fy0)
                    draw_plane(x, fy0, band_bot, cf, 0, sc->light, sc->floorpic);
            }

            /* texture-u along the line, 16.16 */
            ufrac = (int32_t)(((int64_t)bun << 16) / bden);
            if (!front) ufrac = 65536 - ufrac;
            u16 = (int32_t)(((int64_t)ufrac * g_line_len[best]) +
                            ((int64_t)(front ? L->fxoff : L->bxoff) << 16));

            if (other < 0) {
                /* one-sided: middle texture fills the gap */
                int cy = HORIZON + (int32_t)(((int64_t)(eye - cc) * FOCAL * inv16) >> 16);
                int fy = HORIZON + (int32_t)(((int64_t)(eye - cf) * FOCAL * inv16) >> 16);
                draw_wall_col(x, cy < band_top ? band_top : cy,
                              fy > band_bot ? band_bot : fy,
                              front ? L->fmid : L->bmid, bestt, cc,
                              sc->light, u16, 0);
                break;
            }

            /* upper (neighbour ceiling lower) */
            if (nc < cc) {
                int y0 = HORIZON + (int32_t)(((int64_t)(eye - cc) * FOCAL * inv16) >> 16);
                int y1 = HORIZON + (int32_t)(((int64_t)(eye - nc) * FOCAL * inv16) >> 16);
                draw_wall_col(x, y0 < band_top ? band_top : y0,
                              y1 > band_bot ? band_bot : y1,
                              front ? L->ftop : L->btop, bestt, cc,
                              sc->light, u16, 0);
            }
            /* lower (neighbour floor higher) */
            if (nf > cf) {
                int y0 = HORIZON + (int32_t)(((int64_t)(eye - nf) * FOCAL * inv16) >> 16);
                int y1 = HORIZON + (int32_t)(((int64_t)(eye - cf) * FOCAL * inv16) >> 16);
                draw_wall_col(x, y0 < band_top ? band_top : y0,
                              y1 > band_bot ? band_bot : y1,
                              front ? L->fbot : L->bbot, bestt, nf,
                              sc->light, u16, 0);
            }

            /* middle texture on a portal: defer so the far room draws first */
            {
                int mt = front ? L->fmid : L->bmid;
                if (mt >= 0 && mt < g_ntex) {
                    int top = cc < nc ? cc : nc;
                    int bot = cf > nf ? cf : nf;
                    int y0 = HORIZON + (int32_t)(((int64_t)(eye - top) * FOCAL * inv16) >> 16);
                    int y1 = HORIZON + (int32_t)(((int64_t)(eye - bot) * FOCAL * inv16) >> 16);
                    if (y0 < band_top) y0 = band_top;
                    if (y1 > band_bot) y1 = band_bot;
                    if (y0 < y1 && s_nmid < MIDMAX) {
                        midpic_t *m = &s_mid[s_nmid++];
                        m->y0 = (int16_t)y0; m->y1 = (int16_t)y1;
                        m->tex = (int16_t)mt; m->topz = (int16_t)top;
                        m->light = (int16_t)sc->light;
                        m->t16 = (int32_t)bestt;
                        m->u16 = u16;
                    }
                }
            }

            /* can we see through? */
            open = (nc > cf && nf < cc);
            if (!open) break;

            {
                int top = cc < nc ? cc : nc;
                int bot = cf > nf ? cf : nf;
                int yt = HORIZON + (int32_t)(((int64_t)(eye - top) * FOCAL * inv16) >> 16);
                int yb = HORIZON + (int32_t)(((int64_t)(eye - bot) * FOCAL * inv16) >> 16);
                if (yt > band_top) band_top = yt;
                if (yb < band_bot) band_bot = yb;
            }
            if (band_top >= band_bot) break;
            sector = other;
            tmin = bestt;
        }
    }

    flush_mids(x);
}

void r_draw_world(void)
{
    int x, i;
    int32_t fx, fy, rx, ry;

    if (g_state == ST_GAME || g_state == ST_PAUSE || g_state == ST_DEAD ||
        g_state == ST_WIN) {
        for (i = 0; i < GW * VIEW_H; i++) g_depth[i] = 0xFFFF;
    }
    /* clear only the 3D view */
    for (i = 0; i < GW * VIEW_H; i++) g_cv[i] = 0;
    if (g_state != ST_GAME && g_state != ST_PAUSE && g_state != ST_DEAD &&
        g_state != ST_WIN) return;

    fx = dcos(g_player.angle);
    fy = dsin(g_player.angle);
    rx = dsin(g_player.angle);
    ry = -dcos(g_player.angle);
    for (x = 0; x < GW; x++) {
        int32_t s = ((x - CX) * 65536) / FOCAL;
        s_rayx[x] = fx + (int32_t)(((int64_t)rx * s) >> 16);
        s_rayy[x] = fy + (int32_t)(((int64_t)ry * s) >> 16);
    }
    s_npc = 0;
    init_sky();
    for (x = 0; x < GW; x++) render_column(x);
}

/* --------------------------------------------------------------- sprites */

typedef struct {
    int32_t x, y, z;
    int32_t depth;
    int16_t pic;
    int16_t light;
    int16_t top;
} sdraw_t;

static int sprite_lookup(int base, int letter, int rot)
{
    int i;
    if (base < 0 || base >= g_nbase) return -1;
    if (letter < 'A' || letter > 'Z') letter = 'A';
    if (rot < 0) rot = 0;
    if (rot > 8) rot = 8;
    i = g_sprmap[base][letter - 'A'][rot];
    if (i >= 0) return i;
    if (rot >= 2 && rot <= 8) {
        i = g_sprmap[base][letter - 'A'][10 - rot];
        if (i >= 0) return i;
    }
    i = g_sprmap[base][letter - 'A'][0];
    if (i >= 0) return i;
    for (i = 0; i < 9; i++)
        if (g_sprmap[base][letter - 'A'][i] >= 0)
            return g_sprmap[base][letter - 'A'][i];
    for (i = 'A'; i <= 'Z'; i++) {
        int j;
        for (j = 0; j < 9; j++)
            if (g_sprmap[base][i - 'A'][j] >= 0)
                return g_sprmap[base][i - 'A'][j];
    }
    return -1;
}

static int thing_pic(mobj_t *m)
{
    int base = m->baseid;
    int rot = 0;
    int letter = 'A';
    if (m->kind == 1) {
        if (m->state == 4) {
            letter = m->death_letter ? m->death_letter : 'A';
            rot = 1;
        } else {
            int ay = g_player.y - m->y;
            int ax = g_player.x - m->x;
            int rel = (atan2_16(ay >> 16, ax >> 16) - m->angle) & 65535;
            rot = ((rel + 4096) >> 13) + 1;
            if (rot > 8) rot = 1;
            if (m->state == 3) letter = 'G';
            else if (m->state == 2) letter = 'E';
            else if (m->state == 1) letter = "ABCD"[ (m->anim >> 4) & 3 ];
            else letter = 'A';
        }
    } else if (m->kind == 4) {
        letter = (m->anim & 32) ? 'B' : 'A';
        rot = 0;
    } else if (m->kind == 0) {
        letter = m->death_letter ? m->death_letter : 'A';
        rot = 1;
    } else if (m->kind == 6) {
        /* bullet puff */
        return (m->timer > 5 || g_puff_b < 0) ? g_puff : g_puff_b;
    }
    return sprite_lookup(base, letter, rot);
}

void r_draw_sprites(void)
{
    static sdraw_t list[MAXMOBJ + MAXPRJ];
    int n = 0, i, j;
    int32_t fx = dcos(g_player.angle), fy = dsin(g_player.angle);
    int32_t rxv = dsin(g_player.angle), ryv = -dcos(g_player.angle);

    for (i = 0; i < g_nmobj; i++) {
        mobj_t *m = &g_mobj[i];
        int64_t dx, dy;
        int32_t depth, side;
        int pic;
        if (!m->active || m->removed) continue;
        if (m->kind == 1 && m->state == 4 && m->timer > 100) continue;
        dx = (int64_t)m->x - g_player.x;
        dy = (int64_t)m->y - g_player.y;
        depth = (int32_t)((dx * fx + dy * fy) >> 32);
        side = (int32_t)((dx * rxv + dy * ryv) >> 32);
        if (depth < 6 || depth > 12000) continue;
        if (side > depth * 3 || side < -depth * 3) continue;
        pic = thing_pic(m);
        if (pic < 0) continue;
        list[n].x = m->x; list[n].y = m->y; list[n].z = m->z;
        list[n].depth = depth;
        list[n].pic = (int16_t)pic;
        list[n].light = (int16_t)g_sec[m->sector].light;
        list[n].top = g_spr[pic].top;
        n++;
    }
    for (i = 0; i < MAXPRJ; i++) {
        prj_t *p = &g_prj[i];
        int64_t dx, dy;
        int32_t depth, side;
        int pic;
        if (!p->active) continue;
        dx = (int64_t)p->x - g_player.x;
        dy = (int64_t)p->y - g_player.y;
        depth = (int32_t)((dx * fx + dy * fy) >> 32);
        side = (int32_t)((dx * rxv + dy * ryv) >> 32);
        if (depth < 6 || depth > 12000) continue;
        if (side > depth * 3 || side < -depth * 3) continue;
        pic = p->kind ? g_bal7[(p->life >> 3) & 1] : g_bal1[(p->life >> 3) & 1];
        if (pic < 0) continue;
        list[n].x = p->x; list[n].y = p->y; list[n].z = p->z;
        list[n].depth = depth;
        list[n].pic = (int16_t)pic;
        list[n].light = (int16_t)g_sec[p->sector].light;
        list[n].top = g_spr[pic].top;
        n++;
    }
    /* far to near */
    for (i = 1; i < n; i++) {
        sdraw_t tmp = list[i];
        j = i;
        while (j > 0 && list[j - 1].depth < tmp.depth) {
            list[j] = list[j - 1];
            j--;
        }
        list[j] = tmp;
    }

    for (i = 0; i < n; i++) {
        sdraw_t *s = &list[i];
        pic_t *p = &g_spr[s->pic];
        int32_t depth = s->depth;
        int32_t scale16, sx, x0, y0, w, h;
        int64_t dx = (int64_t)s->x - g_player.x;
        int64_t dy = (int64_t)s->y - g_player.y;
        int32_t side = (int32_t)((dx * rxv + dy * ryv) >> 32);
        int32_t topz;
        int lev;
        int xx, yy;

        if (p->w == 0 || p->h == 0) continue;
        scale16 = (int32_t)(((int64_t)FOCAL << 16) / depth);
        sx = CX + (int32_t)(((int64_t)side * FOCAL) / depth);
        topz = (s->z >> 16) + p->top;
        y0 = HORIZON + (int32_t)(((int64_t)(g_player.floor_z + 41 - topz) * scale16) >> 16);
        x0 = sx - (int32_t)(((int64_t)p->left * scale16) >> 16);
        w = ((int32_t)p->w * scale16) >> 16;
        h = ((int32_t)p->h * scale16) >> 16;
        if (w < 1 || h < 1) continue;
        if (x0 >= GW || x0 + w <= 0) continue;
        if (x0 < -4000 || w > 8000) continue;
        lev = shade_level(s->light, depth);
        {
            int ux = 0, uy = 0;
            int ustep = (int)(((int64_t)p->w << 16) / w);
            int vstep = (int)(((int64_t)p->h << 16) / h);
            int xs = x0 < 0 ? 0 : x0;
            int xe = x0 + w > GW ? GW : x0 + w;
            int ys = y0 < 0 ? 0 : y0;
            int ye = y0 + h > VIEW_H ? VIEW_H : y0 + h;
            ux = (xs - x0) * ustep;
            for (xx = xs; xx < xe; xx++, ux += ustep) {
                int tu = ux >> 16;
                if (tu < 0 || tu >= p->w) continue;
                uy = (ys - y0) * vstep;
                for (yy = ys; yy < ye; yy++, uy += vstep) {
                    int tv = uy >> 16;
                    uint8_t pix;
                    uint16_t *dep;
                    if (tv < 0 || tv >= p->h) continue;
                    pix = p->pix[tv * p->w + tu];
                    if (pix == 255) continue;
                    dep = &g_depth[yy * GW + xx];
                    if ((int32_t)(*dep) < depth) continue;
                    g_cv[yy * GW + xx] = g_shade[lev][pix];
                    *dep = (uint16_t)(depth > 65534 ? 65534 : depth);
                }
            }
        }
    }
}

/* --------------------------------------------------------------- weapon */

static void draw_weapon_pic(int idx, int x, int y, int scale16)
{
    pic_t *p;
    int xx, yy, w, h;
    int ustep, vstep;
    if (idx < 0 || idx >= g_nspr) return;
    p = &g_spr[idx];
    w = ((int32_t)p->w * scale16) >> 16;
    h = ((int32_t)p->h * scale16) >> 16;
    if (w <= 0 || h <= 0) return;
    ustep = (int)(((int64_t)p->w << 16) / w);
    vstep = (int)(((int64_t)p->h << 16) / h);
    for (yy = 0; yy < h; yy++) {
        int py = y + yy;
        int ty = yy * vstep >> 16;
        if (py < 0 || py >= VIEW_H) continue;
        for (xx = 0; xx < w; xx++) {
            int px = x + xx;
            int tx = xx * ustep >> 16;
            uint8_t pix;
            if (px < 0 || px >= GW) continue;
            pix = p->pix[ty * p->w + tx];
            if (pix != 255)
                g_cv[py * GW + px] = g_pal[pix];
        }
    }
}

void r_draw_weapon(void)
{
    int scale16 = 88474; /* 1.35x */
    int idx, flash;
    pic_t *p;
    int w, h, x, y;
    if (g_player.dead) return;
    if (g_player.weapon == 1) {
        idx = (g_player.fire_anim > 4) ? g_shtg[1] : g_shtg[0];
        flash = g_shtf;
    } else {
        idx = (g_player.fire_anim > 4) ? g_pisg[1] : g_pisg[0];
        flash = g_pisf;
    }
    if (idx < 0 || idx >= g_nspr) return;
    p = &g_spr[idx];
    w = ((int32_t)p->w * scale16) >> 16;
    h = ((int32_t)p->h * scale16) >> 16;
    x = CX - w / 2;
    y = VIEW_H - h + 12 + (g_player.bob > 0 ? (g_player.bob >> 2) : 0);
    draw_weapon_pic(idx, x, y, scale16);
    if (g_player.fire_anim > 4 && flash >= 0) {
        pic_t *f = &g_spr[flash];
        int fw = ((int32_t)f->w * scale16) >> 16;
        int fh = ((int32_t)f->h * scale16) >> 16;
        draw_weapon_pic(flash, CX - fw / 2, y + h / 2 - fh / 2, scale16);
    }
}
