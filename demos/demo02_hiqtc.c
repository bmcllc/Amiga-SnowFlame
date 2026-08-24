/* =====================================================================
 * demo02 — HIQTC: RGBA8 puro vs comprimido lado a lado (+ PSNR)
 * Gera build/demo02.png
 * ===================================================================== */
#include "common.h"

int main(void)
{
    const int W = 256, H = 256;
    uint32_t *pat;
    hglTex *texRaw, *texCmp;
    hglCtx *ctxA, *ctxB;
    hglVertex quad[4];
    uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    uint32_t *side;
    double q;
    int i, y;

    pat = dc_pattern(W, H);
    texRaw = hglTexCreateRGBA8(W, H, pat);
    texCmp = hglTexCreateHIQTCFromRGBA8(W, H, pat);

    ctxA = hglCreateContext(W, H, 1);
    ctxB = hglCreateContext(W, H, 1);

    /* quad fullscreen em clip space direto (projeção identidade) */
    memset(quad, 0, sizeof(quad));
    {
        float P[4][3] = { { -1, -1, 0 }, { 1, -1, 0 }, { 1, 1, 0 }, { -1, 1, 0 } };
        float UV[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
        for (i = 0; i < 4; i++) {
            quad[i].pos[0] = P[i][0]; quad[i].pos[1] = P[i][1];
            quad[i].pos[2] = P[i][2]; quad[i].pos[2] -= 0.5f; /* z ndc -0.5 */
            quad[i].uv[0] = UV[i][0]; quad[i].uv[1] = UV[i][1];
            quad[i].col[0] = quad[i].col[1] = quad[i].col[2] = 1.0f;
            quad[i].col[3] = 1.0f;
        }
    }

    hglEnable(ctxA, HGL_TEXTURE_2D);
    hglEnable(ctxB, HGL_TEXTURE_2D);

    hglFrameBegin(ctxA);
    hglBindTexture(ctxA, 0, texRaw);
    hglDrawTrianglesIndexed(ctxA, 6, idx, quad);
    hglFrameEnd(ctxA);

    hglFrameBegin(ctxB);
    hglBindTexture(ctxB, 0, texCmp);
    hglDrawTrianglesIndexed(ctxB, 6, idx, quad);
    hglFrameEnd(ctxB);

    q = dc_psnr(hglColorBuffer(ctxA), hglColorBuffer(ctxB), (size_t)W * H);
    printf("demo02: PSNR cena completa RGBA8 vs HIQTC = %.2f dB\n", q);

    /* composição lado a lado: original | comprimido */
    side = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(2 * W) * H);
    for (y = 0; y < H; y++) {
        memcpy(side + (size_t)y * 2 * W,
               hglColorBuffer(ctxA) + (size_t)y * W, W * 4);
        memcpy(side + (size_t)y * 2 * W + W,
               hglColorBuffer(ctxB) + (size_t)y * W, W * 4);
    }
    if (hglSavePNGBuffer("demo02.png", 2 * W, H, side) == 0)
        printf("demo02: gravado demo02.png (esq=original · dir=HIQTC)\n");

    free(side);
    hglTexDestroy(texRaw); hglTexDestroy(texCmp);
    hglDestroyContext(ctxA); hglDestroyContext(ctxB);
    free(pat);
    return 0;
}
