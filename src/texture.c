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

hglTex *hglTexCreateHIQTCP8FromRGBA8(int w, int h, const uint32_t *pixels_packed)
{
    hglTex *t = hglTexCreateRGBA8(w, h, pixels_packed);
    uint8_t *idx;
    int ncol;
    if (!t) return NULL;
    idx = NULL;
    ncol = hi_hiqtc_p8_encode(w, h, pixels_packed, &idx, t->pal);
    if (ncol <= 0 || !idx) { hglTexDestroy(t); return NULL; }
    free(t->pix);
    t->pix = NULL;
    t->p8 = idx;
    t->fmt = HI_FMT_HIQTC_P8;
    return t;
}

void hglTexDestroy(hglTex *tex)
{
    int i;
    if (!tex) return;
    free(tex->pix);
    free(tex->hiq);
    free(tex->p8);
    if (tex->lv) {
        for (i = 0; i < tex->nlevels; i++) free(tex->lv[i]);
        free(tex->lv);
        free(tex->lw);
        free(tex->lh);
    }
    free(tex);
}

/* ------------------------------------------------------- mipmaps */
static uint32_t *box_downsample(int w, int h, const uint32_t *src,
                                int *ow, int *oh)
{
    int dw = w >> 1, dh = h >> 1, x, y, ch;
    uint32_t *dst;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    dst = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)dw * dh);
    if (!dst) return NULL;

    for (y = 0; y < dh; y++)
        for (x = 0; x < dw; x++) {
            int x0 = x * 2, y0 = y * 2, x1 = x0 + 1 < w ? x0 + 1 : w - 1;
            int y1 = y0 + 1 < h ? y0 + 1 : h - 1;
            int acc[4] = { 0, 0, 0, 0 };
            const int xs[2] = { x0, x1 }, ys[2] = { y0, y1 };
            int i;
            for (i = 0; i < 4; i++) {
                uint32_t c = src[(size_t)ys[i >> 1] * w + xs[i & 1]];
                acc[0] += (int)(c & 255);         acc[1] += (int)((c >> 8) & 255);
                acc[2] += (int)((c >> 16) & 255); acc[3] += (int)((c >> 24) & 255);
            }
            for (ch = 0; ch < 4; ch++) acc[ch] = (acc[ch] + 2) / 4;
            dst[(size_t)y * dw + x] = hi_pack_rgba8(acc[0], acc[1], acc[2], acc[3]);
        }
    *ow = dw; *oh = dh;
    return dst;
}

int hglTexGenerateMipmaps(hglTex *tex)
{
    int levels = 0, L, w, h;
    uint32_t *base = NULL;

    if (!tex || tex->lv) return -1;

    if (tex->fmt == HI_FMT_HIQTC) {
        base = hi_hiqtc_decode_all(tex);
        if (!base) return -1;
    } else if (tex->fmt == HI_FMT_HIQTC_P8) {
        base = hi_hiqtc_p8_decode_all(tex);
        if (!base) return -1;
    } else {
        base = (uint32_t *)malloc(sizeof(uint32_t) *
                                  (size_t)tex->w * tex->h);
        if (!base) return -1;
        memcpy(base, tex->pix, sizeof(uint32_t) * (size_t)tex->w * tex->h);
    }

    {
        int mw = tex->w, mh = tex->h;
        levels = 1;
        while (mw > 1 || mh > 1) {
            mw >>= 1; mh >>= 1;
            if (mw < 1) mw = 1;
            if (mh < 1) mh = 1;
            levels++;
        }
    }

    tex->nlevels = levels;
    tex->lv = (uint32_t **)calloc((size_t)levels, sizeof(uint32_t *));
    tex->lw = (int *)calloc((size_t)levels, sizeof(int));
    tex->lh = (int *)calloc((size_t)levels, sizeof(int));
    if (!tex->lv || !tex->lw || !tex->lh) return -1;

    tex->lv[0] = base; tex->lw[0] = tex->w; tex->lh[0] = tex->h;

    for (L = 1; L < levels; L++) {
        uint32_t *next = box_downsample(tex->lw[L-1], tex->lh[L-1],
                                        tex->lv[L-1], &w, &h);
        if (!next) return -1;
        tex->lv[L] = next; tex->lw[L] = w; tex->lh[L] = h;
    }
    return levels;
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
    if (t->fmt == HI_FMT_HIQTC_P8)
        return hi_hiqtc_p8_decode_texel(t, x, y);
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

/* ------------------------------------------------ trilinear single-pass */

/* bilinear restrito a um nível da cadeia (sempre RGBA8 em lv[]) */
static uint32_t bilinear_level(const hglTex *t, int L, float u, float v)
{
    int tw = t->lw[L], th = t->lh[L];
    float fu = u * tw - 0.5f, fv = v * th - 0.5f;
    int x0 = (int)floorf(fu), y0 = (int)floorf(fv);
    int x1 = x0 + 1, y1 = y0 + 1;
    float tx = fracf(fu), ty = fracf(fv);
    const uint32_t *img = t->lv[L];
    uint32_t c00, c10, c01, c11;
    int ch, out[4] = { 0, 0, 0, 0 };

    #define WRAPV(vv, lim) (t->wrap == HGL_WRAP_REPEAT ?            \
        ((vv % lim) < 0 ? (vv % lim) + lim : (vv % lim)) :          \
        (vv < 0 ? 0 : (vv >= lim ? lim - 1 : vv)))
    c00 = img[(size_t)WRAPV(y0,th) * tw + WRAPV(x0,tw)];
    c10 = img[(size_t)WRAPV(y0,th) * tw + WRAPV(x1,tw)];
    c01 = img[(size_t)WRAPV(y1,th) * tw + WRAPV(x0,tw)];
    c11 = img[(size_t)WRAPV(y1,th) * tw + WRAPV(x1,tw)];
    #undef WRAPV

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

uint32_t hi_sample_tex_mip(const hglTex *t, float u, float v, float rho)
{
    float lambda, fr;
    int L;

    if (!t) return 0xFFFFFFFFu;
    if (t->nlevels <= 1 || t->filter != HGL_FILTER_LINEAR)
        return hi_sample_tex(t, u, v); /* sem cadeia: caminho legado */

    if (rho < 1.0f) rho = 1.0f;
    lambda = log2f(rho);
    if (lambda > (float)(t->nlevels - 1)) lambda = (float)(t->nlevels - 1);
    L = (int)floorf(lambda);
    fr = lambda - (float)L;

    {
        uint32_t ca = bilinear_level(t, L, u, v);
        if (L + 1 < t->nlevels && fr > 1e-6f) {
            uint32_t cb = bilinear_level(t, L + 1, u, v);
            int ch, out[4] = { 0, 0, 0, 0 };
            for (ch = 0; ch < 4; ch++) {
                int shift = ch * 8;
                float va = (float)((ca >> shift) & 255);
                float vb = (float)((cb >> shift) & 255);
                out[ch] = (int)(va + (vb - va) * fr + 0.5f);
            }
            return hi_pack_rgba8(out[0], out[1], out[2], out[3]);
        }
        return ca;
    }
}
