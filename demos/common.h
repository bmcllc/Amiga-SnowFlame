/* =====================================================================
 * common.h — helpers compartilhados das demos Hot-ice
 * ===================================================================== */
#ifndef DEMOS_COMMON_H
#define DEMOS_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "hotice/hgl.h"

#ifndef HI_PI
#define HI_PI 3.14159265358979f
#endif

static inline uint32_t dc_pack(int r, int g, int b)
{
    return ((uint32_t)(b & 255) << 16) | ((uint32_t)(g & 255) << 8)
         | (uint32_t)(r & 255);
}

/* tabuleiro de xadrez RGBA8 */
static inline uint32_t *dc_checker(int w, int h, int cells,
                            int ar, int ag, int ab, int br, int bg, int bb)
{
    uint32_t *p = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)w * h);
    int x, y;
    if (!p) return NULL;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int k = ((x * cells) / w + (y * cells) / h) & 1;
            p[(size_t)y * w + x] = k ? dc_pack(br, bg, bb)
                                     : dc_pack(ar, ag, ab);
        }
    return p;
}

/* esfera lat-long com UV/normais; retorna verts e índices */
static inline void dc_gen_sphere(int stacks, int slices,
                          hglVertex **outVerts, uint32_t **outIdx,
                          int *outNv, int *outNi)
{
    int nv = (stacks + 1) * slices;
    hglVertex *v = (hglVertex *)calloc((size_t)nv, sizeof(hglVertex));
    uint32_t *ix;
    int ni = 0, i, j;

    for (j = 0; j <= stacks; j++) {
        float phi = (float)j / stacks * (float)HI_PI;
        for (i = 0; i < slices; i++) {
            float th = (float)i / slices * 2.0f * (float)HI_PI;
            float sp = sinf(phi), cp = cosf(phi);
            hglVertex *vv = &v[(size_t)j * slices + i];
            vv->pos[0] = sp * cosf(th);
            vv->pos[1] = cp;
            vv->pos[2] = sp * sinf(th);
            vv->nrm[0] = vv->pos[0]; vv->nrm[1] = vv->pos[1];
            vv->nrm[2] = vv->pos[2];
            vv->uv[0] = (float)i / slices;
            vv->uv[1] = (float)j / stacks;
            vv->col[0] = 0.85f; vv->col[1] = 0.9f; vv->col[2] = 1.0f;
            vv->col[3] = 1.0f;
        }
    }

    ix = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)stacks * slices * 6);
    for (j = 0; j < stacks; j++)
        for (i = 0; i < slices; i++) {
            uint32_t a = (uint32_t)(j * slices + i);
            uint32_t b = (uint32_t)(j * slices + (i + 1) % slices);
            uint32_t c = (uint32_t)((j + 1) * slices + i);
            uint32_t d = (uint32_t)((j + 1) * slices + (i + 1) % slices);
            ix[ni++] = a; ix[ni++] = c; ix[ni++] = b;
            ix[ni++] = b; ix[ni++] = c; ix[ni++] = d;
        }

    *outVerts = v; *outIdx = ix; *outNv = nv; *outNi = ni;
}

/* alvo de morfing "dentes": raio modulado por harmônicos (mesma topologia) */
static inline float *dc_dent_targets(const hglVertex *base, int nv,
                              float amount)
{
    float *t = (float *)malloc(sizeof(float) * (size_t)nv * 3);
    int i;
    if (!t) return NULL;
    for (i = 0; i < nv; i++) {
        float px = base[i].pos[0], py = base[i].pos[1], pz = base[i].pos[2];
        float th = atan2f(pz, px);
        float ph = acosf(py);
        float d = 1.0f + amount * sinf(3.0f * th) * cosf(4.0f * ph);
        t[(size_t)i * 3 + 0] = px * d;
        t[(size_t)i * 3 + 1] = py * d;
        t[(size_t)i * 3 + 2] = pz * d;
    }
    return t;
}

/* cubo unitário centrado na origem (24 vértices, normais por face) */
static inline void dc_gen_cube(hglVertex **outVerts, uint32_t **outIdx,
                               int *outNv, int *outNi,
                               float r, float g, float b)
{
    static const float N[6][3] = {
        { 0, 0, 1}, { 0, 0,-1}, { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}
    };
    static const float V[6][4][3] = {
        {{-.5,-.5, .5},{ .5,-.5, .5},{ .5, .5, .5},{-.5, .5, .5}},
        {{ .5,-.5,-.5},{-.5,-.5,-.5},{-.5, .5,-.5},{ .5, .5,-.5}},
        {{ .5,-.5, .5},{ .5,-.5,-.5},{ .5, .5,-.5},{ .5, .5, .5}},
        {{-.5,-.5,-.5},{-.5,-.5, .5},{-.5, .5, .5},{-.5, .5,-.5}},
        {{-.5, .5, .5},{ .5, .5, .5},{ .5, .5,-.5},{-.5, .5,-.5}},
        {{-.5,-.5,-.5},{ .5,-.5,-.5},{ .5,-.5, .5},{-.5,-.5, .5}}
    };
    hglVertex *v = (hglVertex *)calloc(24, sizeof(hglVertex));
    uint32_t *ix = (uint32_t *)malloc(sizeof(uint32_t) * 36);
    int f, i, ni = 0;
    for (f = 0; f < 6; f++)
        for (i = 0; i < 4; i++) {
            hglVertex *vv = &v[f * 4 + i];
            vv->pos[0] = V[f][i][0]; vv->pos[1] = V[f][i][1]; vv->pos[2] = V[f][i][2];
            vv->nrm[0] = N[f][0]; vv->nrm[1] = N[f][1]; vv->nrm[2] = N[f][2];
            vv->uv[0] = (i == 1 || i == 2) ? 1.0f : 0.0f;
            vv->uv[1] = (i >= 2) ? 1.0f : 0.0f;
            vv->col[0] = r; vv->col[1] = g; vv->col[2] = b; vv->col[3] = 1;
        }
    for (f = 0; f < 6; f++) {
        uint32_t o = (uint32_t)(f * 4);
        ix[ni++]=o;   ix[ni++]=o+1; ix[ni++]=o+2;
        ix[ni++]=o;   ix[ni++]=o+2; ix[ni++]=o+3;
    }
    *outVerts = v; *outIdx = ix; *outNv = 24; *outNi = 36;
}

/* textura procedural colorida (gradiente + círculo + faixas) */
static inline uint32_t *dc_pattern(int w, int h)
{
    uint32_t *p = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)w * h);
    int x, y;
    if (!p) return NULL;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            float u = (float)x / w, v = (float)y / h;
            int r = (int)(u * 255);
            int g = (int)((sinf(u * 12.0f) * 0.5f + 0.5f) * 200);
            int b = (int)(v * 255);
            float dx = u - 0.62f, dy = v - 0.38f;
            if (dx * dx + dy * dy < 0.03f) { r = 255; g = 240; b = 60; }
            if (((x + y) & 15) < 3) { r = 20; g = 30; b = 40; }
            p[(size_t)y * w + x] = dc_pack(r, g, b);
        }
    return p;
}

/* PSNR entre dois buffers RGBA8 empacotados (compara R,G,B) */
static inline double dc_psnr(const uint32_t *a, const uint32_t *b, size_t n)
{
    double mse = 0.0;
    size_t i;
    int ch;
    for (i = 0; i < n; i++)
        for (ch = 0; ch < 3; ch++) {
            int va = (int)((a[i] >> (ch * 8)) & 255);
            int vb = (int)((b[i] >> (ch * 8)) & 255);
            double d = (double)(va - vb);
            mse += d * d;
        }
    mse /= (double)n * 3.0;
    if (mse <= 1e-9) return 999.0;
    return 10.0 * log10(255.0 * 255.0 / mse);
}

#endif /* DEMOS_COMMON_H */
