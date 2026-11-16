/* soft_gfx.c — small software implementation of gfx.h for offline previews.
 * Mirrors the device renderer's semantics: premultiplied alpha, GL_MODULATE
 * texture colouring, ONE / ONE_MINUS_SRC_ALPHA blending, logical-pixel quads. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"

typedef struct { int w, h; unsigned char *px; } Tex; /* RGBA, premultiplied */
static Tex sTex[65];
static unsigned char *sFB;
static int sW, sH;
static int sBlend = GFX_BLEND_ALPHA;
static int sClipOn;
static float sClip[4];
static float sOffX, sOffY;
static uint32_t sBudget = 3u << 20;

int gfx_stat_draws, gfx_stat_quads, gfx_stat_textures;
uint32_t gfx_stat_tex_bytes;
uint32_t gfx_stat_upload_us;
uint32_t gfx_stat_buffer_us;
int gfx_stat_merges, gfx_stat_state_calls;

static unsigned char *sample_tex(const Tex *t, float u, float v)
{
    static unsigned char out[4];
    float x = u * t->w - 0.5f, y = v * t->h - 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - x0, fy = y - y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= t->w) x1 = t->w - 1;
    if (y1 >= t->h) y1 = t->h - 1;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x0 >= t->w) x0 = t->w - 1;
    if (y0 >= t->h) y0 = t->h - 1;
    const unsigned char *a = t->px + ((size_t)y0 * t->w + x0) * 4;
    const unsigned char *b = t->px + ((size_t)y0 * t->w + x1) * 4;
    const unsigned char *c = t->px + ((size_t)y1 * t->w + x0) * 4;
    const unsigned char *d = t->px + ((size_t)y1 * t->w + x1) * 4;
    for (int k = 0; k < 4; k++) {
        float top = a[k] + (b[k] - a[k]) * fx;
        float bot = c[k] + (d[k] - c[k]) * fx;
        float val = top + (bot - top) * fy;
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        out[k] = (unsigned char)(val + 0.5f);
    }
    return out;
}

static void blend_pixel(int x, int y, const unsigned char *src)
{
    unsigned char *d = sFB + ((size_t)y * sW + x) * 4;
    int a = src[3];
    if (sBlend == GFX_BLEND_NONE) {
        d[0] = src[0]; d[1] = src[1]; d[2] = src[2]; d[3] = a;
    } else if (sBlend == GFX_BLEND_ADD) {
        for (int k = 0; k < 3; k++) {
            int v = d[k] + src[k];
            d[k] = (unsigned char)(v > 255 ? 255 : v);
        }
        d[3] = 255;
    } else {
        int inv = 255 - a;
        for (int k = 0; k < 3; k++)
            d[k] = (unsigned char)(src[k] + (d[k] * inv + 127) / 255);
        d[3] = (unsigned char)(a + (d[3] * inv + 127) / 255);
    }
}

void gfx_set_texture_budget(uint32_t bytes) { sBudget = bytes; }

int gfx_tex_create(int w, int h, int fmt, const void *pixels, int linear, int repeat)
{
    (void)linear; (void)repeat;
    int id;
    uint32_t bytes = (uint32_t)w * (uint32_t)h * 4u;
    if (w < 1 || h < 1 || bytes > sBudget || gfx_stat_tex_bytes > sBudget - bytes)
        return 0;
    for (id = 1; id <= 64 && sTex[id].px; id++) {}
    if (id > 64) return 0;
    sTex[id].w = w;
    sTex[id].h = h;
    sTex[id].px = malloc(bytes);
    if (!sTex[id].px) return 0;
    if (fmt == GFX_FMT_RGB565) {
        const unsigned char *p = pixels;
        for (int i = 0; i < w * h; i++) {
            unsigned v = (unsigned)p[0] | (unsigned)p[1] << 8;
            p += 2;
            unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
            sTex[id].px[i * 4 + 0] = (unsigned char)(r * 255 / 31);
            sTex[id].px[i * 4 + 1] = (unsigned char)(g * 255 / 63);
            sTex[id].px[i * 4 + 2] = (unsigned char)(b * 255 / 31);
            sTex[id].px[i * 4 + 3] = 255;
        }
    } else if (fmt == GFX_FMT_RGBA4444) {
        const unsigned char *p = pixels;
        for (int i = 0; i < w * h; i++) {
            unsigned v = (unsigned)p[0] | (unsigned)p[1] << 8;
            p += 2;
            unsigned r = (v >> 12) & 15, g = (v >> 8) & 15, b = (v >> 4) & 15, a = v & 15;
            r = r * a / 15; g = g * a / 15; b = b * a / 15;
            sTex[id].px[i * 4 + 0] = (unsigned char)(r * 255 / 15);
            sTex[id].px[i * 4 + 1] = (unsigned char)(g * 255 / 15);
            sTex[id].px[i * 4 + 2] = (unsigned char)(b * 255 / 15);
            sTex[id].px[i * 4 + 3] = (unsigned char)(a * 255 / 15);
        }
    } else {
        memcpy(sTex[id].px, pixels, bytes);
    }
    gfx_stat_textures++;
    gfx_stat_tex_bytes += bytes;
    return id;
}

void gfx_tex_destroy(int tex)
{
    if (tex <= 0 || tex > 64 || !sTex[tex].px) return;
    gfx_stat_tex_bytes -= (uint32_t)sTex[tex].w * (uint32_t)sTex[tex].h * 4u;
    gfx_stat_textures--;
    free(sTex[tex].px);
    sTex[tex].px = NULL;
}

void gfx_forget_all(void)
{
    for (int i = 1; i <= 64; i++) {
        free(sTex[i].px);
        sTex[i].px = NULL;
    }
    gfx_stat_textures = 0;
    gfx_stat_tex_bytes = 0;
}

void gfx_begin_frame(int panel_w, int panel_h, float logical_w, float logical_h, int rot90,
                     float r, float g, float b)
{
    (void)rot90;
    sW = panel_w;
    sH = panel_h;
    sOffX = sOffY = 0;
    sClipOn = 0;
    sBlend = GFX_BLEND_ALPHA;
    extern float g_preview_logical_w, g_preview_logical_h;
    g_preview_logical_w = logical_w;
    g_preview_logical_h = logical_h;
    sFB = realloc(sFB, (size_t)sW * sH * 4);
    for (int i = 0; i < sW * sH; i++) {
        sFB[i * 4 + 0] = (unsigned char)(r * 255.0f + 0.5f);
        sFB[i * 4 + 1] = (unsigned char)(g * 255.0f + 0.5f);
        sFB[i * 4 + 2] = (unsigned char)(b * 255.0f + 0.5f);
        sFB[i * 4 + 3] = 255;
    }
    gfx_stat_draws = gfx_stat_quads = 0;
}

static void raster_tri(const float *p, const float *uv, const unsigned char *col,
                       const Tex *tex, int textured)
{
    float ax = p[0] + sOffX, ay = p[1] + sOffY;
    float bx = p[2] + sOffX, by = p[3] + sOffY;
    float cx = p[4] + sOffX, cy = p[5] + sOffY;
    float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
    if (fabsf(area) < 1e-6f) return;
    int minx = (int)floorf(fminf(ax, fminf(bx, cx)));
    int maxx = (int)ceilf(fmaxf(ax, fmaxf(bx, cx)));
    int miny = (int)floorf(fminf(ay, fminf(by, cy)));
    int maxy = (int)ceilf(fmaxf(ay, fmaxf(by, cy)));
    if (sClipOn) {
        int cx0 = (int)floorf(sClip[0]), cy0 = (int)floorf(sClip[1]);
        int cx1 = (int)ceilf(sClip[0] + sClip[2]), cy1 = (int)ceilf(sClip[1] + sClip[3]);
        if (minx < cx0) minx = cx0;
        if (miny < cy0) miny = cy0;
        if (maxx > cx1) maxx = cx1;
        if (maxy > cy1) maxy = cy1;
    }
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > sW) maxx = sW;
    if (maxy > sH) maxy = sH;
    float inv = 1.0f / area;
    for (int y = miny; y < maxy; y++) {
        float py = y + 0.5f;
        for (int x = minx; x < maxx; x++) {
            float px = x + 0.5f;
            float w0 = ((bx - px) * (cy - py) - (cx - px) * (by - py)) * inv;
            float w1 = ((cx - px) * (ay - py) - (ax - px) * (cy - py)) * inv;
            float w2 = 1.0f - w0 - w1;
            if (w0 < -0.0005f || w1 < -0.0005f || w2 < -0.0005f) continue;
            unsigned char src[4];
            if (textured && tex) {
                float u = uv[0] * w0 + uv[2] * w1 + uv[4] * w2;
                float v = uv[1] * w0 + uv[3] * w1 + uv[5] * w2;
                const unsigned char *t = sample_tex(tex, u, v);
                for (int k = 0; k < 4; k++)
                    src[k] = (unsigned char)((t[k] * col[k] + 127) / 255);
            } else {
                src[0] = col[0]; src[1] = col[1]; src[2] = col[2]; src[3] = col[3];
            }
            blend_pixel(x, y, src);
        }
    }
}

void gfx_quad(int tex, int tex_w, int tex_h, const float xy[8], const float uv[8], uint32_t argb)
{
    static const int order[6] = { 0, 1, 2, 0, 2, 3 };
    unsigned char col[4] = {
        (unsigned char)((argb >> 16) & 255), (unsigned char)((argb >> 8) & 255),
        (unsigned char)(argb & 255), (unsigned char)((argb >> 24) & 255)
    };
    const Tex *t = (tex > 0 && tex <= 64) ? &sTex[tex] : NULL;
    if (tex > 0 && !t->px) return;
    for (int tri = 0; tri < 2; tri++) {
        float p[6], q[6];
        for (int k = 0; k < 3; k++) {
            int i = order[tri * 3 + k];
            p[k * 2] = xy[i * 2];
            p[k * 2 + 1] = xy[i * 2 + 1];
            q[k * 2] = t ? uv[i * 2] / (float)tex_w : 0;
            q[k * 2 + 1] = t ? uv[i * 2 + 1] / (float)tex_h : 0;
        }
        raster_tri(p, q, col, t, t != NULL);
    }
    gfx_stat_quads++;
}

void gfx_tri(const float xy[6], uint32_t argb)
{
    unsigned char col[4] = {
        (unsigned char)((argb >> 16) & 255), (unsigned char)((argb >> 8) & 255),
        (unsigned char)(argb & 255), (unsigned char)((argb >> 24) & 255)
    };
    float q[6] = {0, 0, 0, 0, 0, 0};
    raster_tri(xy, q, col, NULL, 0);
}

void gfx_flush(void) {}
void gfx_set_frame_offset(float dx, float dy) { sOffX = dx; sOffY = dy; }
void gfx_set_clip(float x, float y, float w, float h)
{
    sClipOn = w > 0 && h > 0;
    sClip[0] = x; sClip[1] = y; sClip[2] = w; sClip[3] = h;
}
void gfx_set_blend(int mode) { sBlend = mode; }

void gfx_end_frame(void)
{
    gfx_stat_draws = gfx_stat_quads;
}

void gfx_write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", sW, sH);
    for (int i = 0; i < sW * sH; i++)
        fwrite(sFB + i * 4, 1, 3, f);
    fclose(f);
}
