/* =====================================================================
 * context.c — ciclo de vida do contexto Hot-ice e disciplina de frame
 * ===================================================================== */
#include <stdlib.h>
#include <string.h>
#include "internal.h"

static void set_samples(hglCtx *ctx, int samples)
{
    switch (samples) {
    case 2: /* grade "rotacionada" 2x */
        ctx->sampX[0] = -0.25f; ctx->sampY[0] =  0.25f;
        ctx->sampX[1] =  0.25f; ctx->sampY[1] = -0.25f;
        break;
    case 4: /* grade rotacionada simplificada 4x */
        ctx->sampX[0] = -0.25f; ctx->sampY[0] = -0.25f;
        ctx->sampX[1] =  0.25f; ctx->sampY[1] = -0.25f;
        ctx->sampX[2] = -0.25f; ctx->sampY[2] =  0.25f;
        ctx->sampX[3] =  0.25f; ctx->sampY[3] =  0.25f;
        break;
    default:
        samples = 1;
        ctx->sampX[0] = 0.0f; ctx->sampY[0] = 0.0f;
        break;
    }
    ctx->samples = samples;
}

hglCtx *hglCreateContext(int width, int height, int samples)
{
    hglCtx *ctx;
    size_t ntiles;

    if (width <= 0 || height <= 0) return NULL;
    if ((samples != 1) && (samples != 2) && (samples != 4)) samples = 1;

    ctx = (hglCtx *)calloc(1, sizeof(hglCtx));
    if (!ctx) return NULL;

    ctx->w = width; ctx->h = height;
    ctx->color = (uint32_t *)calloc((size_t)width * height, sizeof(uint32_t));
    ctx->tx = (width + HI_TILE - 1) / HI_TILE;
    ctx->ty = (height + HI_TILE - 1) / HI_TILE;
    ntiles = (size_t)ctx->tx * (size_t)ctx->ty;
    ctx->tiles = (HiTileList *)calloc(ntiles, sizeof(HiTileList));
    set_samples(ctx, samples);
    {
        size_t scratch = (size_t)HI_TILE * HI_TILE * (size_t)ctx->samples;
        ctx->scolor = (uint32_t *)malloc(scratch * sizeof(uint32_t));
        ctx->sdepth = (uint32_t *)malloc(scratch * sizeof(uint32_t));
    }

    if (!ctx->color || !ctx->tiles || !ctx->scolor || !ctx->sdepth) {
        hglDestroyContext(ctx);
        return NULL;
    }

    hglViewport(ctx, 0, 0, width, height);
    ctx->clearColor = hi_pack_rgba8(0, 0, 0, 255);
    ctx->clearZ24 = 0xFFFFFFu;

    hi_mat_identity(&ctx->mv);
    hi_mat_identity(&ctx->proj);
    ctx->matrixMode = HGL_MODELVIEW;
    ctx->ambient[0] = ctx->ambient[1] = ctx->ambient[2] = ctx->ambient[3] = 1.0f;
    ctx->lightCol[0] = ctx->lightCol[1] = ctx->lightCol[2] = 1.0f;
    ctx->lightDir[1] = -1.0f;

    return ctx;
}

void hglDestroyContext(hglCtx *ctx)
{
    size_t i, n;
    if (!ctx) return;
    if (ctx->tiles) {
        n = (size_t)ctx->tx * (size_t)ctx->ty;
        for (i = 0; i < n; i++) free(ctx->tiles[i].items);
    }
    free(ctx->tiles);
    free(ctx->triPool);
    free(ctx->scolor);
    free(ctx->sdepth);
    free(ctx->color);
    free(ctx);
}

void hglViewport(hglCtx *ctx, int x, int y, int w, int h)
{
    if (!ctx) return;
    ctx->vx = x; ctx->vy = y; ctx->vw = w; ctx->vh = h;
}

void hglClearColor4f(hglCtx *ctx, float r, float g, float b, float a)
{
    if (!ctx) return;
    #define CL8(x) ((int)((x) <= 0.0f ? 0 : ((x) >= 1.0f ? 255 : (x) * 255.0f + 0.5f)))
    ctx->clearColor = hi_pack_rgba8(CL8(r), CL8(g), CL8(b), CL8(a));
    #undef CL8
}

void hglClearDepth(hglCtx *ctx, float d)
{
    if (!ctx) return;
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    ctx->clearZ24 = (uint32_t)(d * 16777215.0f);
}

void hglFrameBegin(hglCtx *ctx)
{
    size_t i, n;
    if (!ctx) return;
    ctx->triCount = 0;
    ctx->statsTrisIn = ctx->statsTrisOut = 0;
    n = (size_t)ctx->tx * (size_t)ctx->ty;
    for (i = 0; i < n; i++) ctx->tiles[i].n = 0;
    /* fora do viewport permanece estável entre frames */
    memset(ctx->color, 0, (size_t)ctx->w * (size_t)ctx->h * sizeof(uint32_t));
}

void hglFrameEnd(hglCtx *ctx)
{
    if (!ctx) return;
    hi_raster_flush(ctx);
}

uint32_t *hglColorBuffer(hglCtx *ctx)
{
    return ctx ? ctx->color : NULL;
}
