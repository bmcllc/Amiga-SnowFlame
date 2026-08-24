/* =====================================================================
 * raster.c — pipeline tile-based diferido (TBR) com CAA
 *
 * Cada frame é binado em tiles de 32×32. Um tile só é processado quando
 * todos os seus triângulos já chegaram; cor e profundidade vivem em SRAM
 * interna (scratch) durante o processamento e só o resultado resolvido
 * toca o framebuffer — overdraw quase gratuito, como no dossiê.
 *
 * CAA: cada amostra da grade rotacionada é testada/rasterizada dentro do
 * tile e resolvida (média) antes de escrever na memória externa.
 *
 * Regra de aresta: inclusão estrita (E > 0). Arestas compartilhadas por
 * dois triângulos produzem valores idênticos nos dois lados; o depth
 * test (LESS) elimina o re-pintado. Referência simples e sem cracks
 * para geometria opaca com depth test sempre ativo.
 * ===================================================================== */
#include <string.h>
#include "internal.h"

static inline float hi_edge(float ax, float ay, float bx, float by,
                            float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static uint32_t hi_shade(const hiTriScreen *tri,
                         float u, float v, float r, float g, float b)
{
    uint32_t base;
    int br, bg, bb, ba;

    if (tri->tex)
        base = hi_sample_tex(tri->tex, u, v);
    else {
        int ir = (int)(r * 255.0f + 0.5f);
        int ig = (int)(g * 255.0f + 0.5f);
        int ib = (int)(b * 255.0f + 0.5f);
        if (ir < 0) ir = 0;
        if (ir > 255) ir = 255;
        if (ig < 0) ig = 0;
        if (ig > 255) ig = 255;
        if (ib < 0) ib = 0;
        if (ib > 255) ib = 255;
        return hi_pack_rgba8(ir, ig, ib, 255);
    }

    hi_unpack_rgba8(base, &br, &bg, &bb, &ba);
    br = (int)(br * r); bg = (int)(bg * g); bb = (int)(bb * b);
    if (br < 0) br = 0;
    if (br > 255) br = 255;
    if (bg < 0) bg = 0;
    if (bg > 255) bg = 255;
    if (bb < 0) bb = 0;
    if (bb > 255) bb = 255;
    return hi_pack_rgba8(br, bg, bb, 255);
}

typedef struct {
    int x0, y0; /* origem do tile */
} HiTileRect;

static void hi_raster_tri(hglCtx *ctx, HiTileRect rect, const hiTriScreen *tri)
{
    hiVertScreen a = tri->v[0], b = tri->v[1], c = tri->v[2];
    float area = hi_edge(a.sx, a.sy, b.sx, b.sy, c.sx, c.sy);
    int x, y, s, ns = ctx->samples;

    if (fabsf(area) < 1e-9f) return;
    if (area < 0.0f) {
        hiVertScreen t = b; b = c; c = t;
        area = -area;
    }
    {
        float inv_area = 1.0f / area;
        int x0 = tri->minx > rect.x0 ? tri->minx : rect.x0;
        int y0 = tri->miny > rect.y0 ? tri->miny : rect.y0;
        int x1 = tri->maxx < rect.x0 + HI_TILE ? tri->maxx : rect.x0 + HI_TILE;
        int y1 = tri->maxy < rect.y0 + HI_TILE ? tri->maxy : rect.y0 + HI_TILE;

        for (y = y0; y < y1; y++) {
            float cy = (float)y + 0.5f;
            for (x = x0; x < x1; x++) {
                float cx = (float)x + 0.5f;
                int pix = (y - rect.y0) * HI_TILE + (x - rect.x0);

                for (s = 0; s < ns; s++) {
                    float spx = cx + ctx->sampX[s];
                    float spy = cy + ctx->sampY[s];
                    float w0 = hi_edge(b.sx, b.sy, c.sx, c.sy, spx, spy);
                    float w1 = hi_edge(c.sx, c.sy, a.sx, a.sy, spx, spy);
                    float w2 = hi_edge(a.sx, a.sy, b.sx, b.sy, spx, spy);
                    float diw, u, v, rr, gg, bb_, z;
                    uint32_t z24, col;
                    size_t si;

                    if (w0 <= 0.0f || w1 <= 0.0f || w2 <= 0.0f) continue;

                    w0 *= inv_area; w1 *= inv_area; w2 *= inv_area;
                    diw   = w0 * a.iw + w1 * b.iw + w2 * c.iw;
                    if (diw <= 1e-12f) continue;
                    z     = w0 * a.z + w1 * b.z + w2 * c.z;
                    u     = (w0 * a.u + w1 * b.u + w2 * c.u) / diw;
                    v     = (w0 * a.v + w1 * b.v + w2 * c.v) / diw;
                    rr    = (w0 * a.r + w1 * b.r + w2 * c.r) / diw;
                    gg    = (w0 * a.g + w1 * b.g + w2 * c.g) / diw;
                    bb_   = (w0 * a.b + w1 * b.b + w2 * c.b) / diw;

                    if (z < 0.0f) z = 0.0f;
                    if (z > 1.0f) z = 1.0f;
                    z24 = (uint32_t)(z * 16777215.0f);

                    si = ((size_t)pix * (size_t)ns) + (size_t)s;
                    if (z24 >= ctx->sdepth[si]) continue; /* depth LESS */

                    col = hi_shade(tri, u, v, rr, gg, bb_);
                    ctx->sdepth[si] = z24;
                    ctx->scolor[si] = col;
                }
            }
        }
    }
}

void hi_raster_flush(hglCtx *ctx)
{
    int tx, ty, i, ns = ctx->samples;
    size_t npix = (size_t)HI_TILE * HI_TILE * (size_t)ns;

    for (ty = 0; ty < ctx->ty; ty++) {
        for (tx = 0; tx < ctx->tx; tx++) {
            HiTileList *tl = &ctx->tiles[(size_t)ty * ctx->tx + tx];
            HiTileRect rect;
            rect.x0 = tx * HI_TILE;
            rect.y0 = ty * HI_TILE;

            /* início de tile: SRAM interna parte do estado de clear */
            for (i = 0; i < (int)npix; i++) {
                ctx->scolor[i] = ctx->clearColor;
                ctx->sdepth[i] = ctx->clearZ24;
            }

            for (i = 0; i < tl->n; i++)
                hi_raster_tri(ctx, rect, &ctx->triPool[tl->items[i]]);

            /* resolve: média das amostras -> framebuffer externo */
            {
                int px, py, k;
                for (py = rect.y0; py < rect.y0 + HI_TILE && py < ctx->h; py++)
                    for (px = rect.x0; px < rect.x0 + HI_TILE && px < ctx->w; px++) {
                        int sr = 0, sg = 0, sb = 0, sa = 0;
                        size_t pix = (size_t)(py - rect.y0) * HI_TILE
                                   + (size_t)(px - rect.x0);
                        for (k = 0; k < ns; k++) {
                            int r, g, b, a;
                            hi_unpack_rgba8(ctx->scolor[pix * (size_t)ns + k],
                                            &r, &g, &b, &a);
                            sr += r; sg += g; sb += b; sa += a;
                        }
                        ctx->color[(size_t)py * ctx->w + px] =
                            hi_pack_rgba8(sr / ns, sg / ns, sb / ns, sa / ns);
                    }
            }
        }
    }
}
