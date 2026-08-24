/* =====================================================================
 * texture.c — upload e amostragem (nearest/bilinear), RGBA8 e HIQTC
 * ===================================================================== */
#include <stdlib.h>
#include <string.h>
#include "internal.h"

hglTex *hglTexCreateRGBA8(int w, int h, const uint32_t *pixels_packed)
{
    hglTex *t;
    if (w <= 0 || h <= 0 || !pixels_packed) return NULL;
    t = (hglTex *)calloc(1, sizeof(hglTex));
    if (!t) return NULL;
    t->w = w; t->h = h;
    t->fmt = HI_FMT_RGBA8;
    t->filter = HGL_FILTER_LINEAR;
    t->wrap = HGL_WRAP_REPEAT;
    t->pix = (uint32_t *)malloc((size_t)w * h * sizeof(uint32_t));
    if (!t->pix) { free(t); return NULL; }
    memcpy(t->pix, pixels_packed, (size_t)w * h * sizeof(uint32_t));
    return t;
}

hglTex *hglTexCreateHIQTCFromRGBA8(int w, int h, const uint32_t *pixels_packed)
{
    hglTex *t = hglTexCreateRGBA8(w, h, pixels_packed);
    uint8_t *enc;
    if (!t) return NULL;
    enc = hi_hiqtc_encode_rgba8(w, h, pixels_packed);
    if (!enc) { hglTexDestroy(t); return NULL; }
    free(t->pix);
    t->pix = NULL;
    t->hiq = enc;
    t->fmt = HI_FMT_HIQTC;
    return t;
}

void hglTexDestroy(hglTex *tex)
{
    if (!tex) return;
    free(tex->pix);
    free(tex->hiq);
    free(tex);
}

void hglTexFilter(hglTex *tex, hglFilter f) { if (tex) tex->filter = (int)f; }
void hglTexWrap(hglTex *tex, hglWrap w)     { if (tex) tex->wrap = (int)w; }
void hglBindTexture(hglCtx *ctx, int unit, hglTex *tex)
{
    (void)unit; /* unidade única na referência */
    if (ctx) ctx->texBound = tex;
}

/* texel com wrap/clamp — coordenada inteira "crua" */
static uint32_t fetch_wrapped(const hglTex *t, int x, int y)
{
    if (t->wrap == HGL_WRAP_REPEAT) {
        x %= t->w; if (x < 0) x += t->w;
        y %= t->h; if (y < 0) y += t->h;
    } else {
        if (x < 0) x = 0;
        if (x >= t->w) x = t->w - 1;
        if (y < 0) y = 0;
        if (y >= t->h) y = t->h - 1;
    }
    if (t->fmt == HI_FMT_HIQTC)
        return hi_hiqtc_decode_texel(t, x, y);
    return t->pix[(size_t)y * t->w + x];
}

uint32_t hi_sample_texel(const hglTex *t, int x, int y)
{
    return fetch_wrapped(t, x, y);
}

static inline float fracf(float f) { return f - floorf(f); }

uint32_t hi_sample_tex(const hglTex *t, float u, float v)
{
    if (!t) return 0xFFFFFFFFu;

    if (t->filter == HGL_FILTER_NEAREST) {
        float fu = u * t->w - 0.5f;
        float fv = v * t->h - 0.5f;
        int x = (int)floorf(fu + 0.5f);
        int y = (int)floorf(fv + 0.5f);
        return fetch_wrapped(t, x, y);
    } else {
        float fu = u * t->w - 0.5f, fv = v * t->h - 0.5f;
        int x0 = (int)floorf(fu), y0 = (int)floorf(fv);
        int x1 = x0 + 1, y1 = y0 + 1;
        float tx = fracf(fu), ty = fracf(fv);
        uint32_t c00 = fetch_wrapped(t, x0, y0), c10 = fetch_wrapped(t, x1, y0);
        uint32_t c01 = fetch_wrapped(t, x0, y1), c11 = fetch_wrapped(t, x1, y1);
        int ch, out[4] = { 0, 0, 0, 0 };
        for (ch = 0; ch < 4; ch++) {
            int shift = ch * 8;
            float a = (float)((c00 >> shift) & 255);
            float b = (float)((c10 >> shift) & 255);
            float c = (float)((c01 >> shift) & 255);
            float d = (float)((c11 >> shift) & 255);
            float top = a + (b - a) * tx;
            float bot = c + (d - c) * tx;
            out[ch] = (int)(top + (bot - top) * ty + 0.5f);
        }
        return hi_pack_rgba8(out[0], out[1], out[2], out[3]);
    }
}
