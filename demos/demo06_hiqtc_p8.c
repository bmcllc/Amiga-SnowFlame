/* =====================================================================
 * demo06 — HIQTC: RGBA8 x HIQTC (4:1 blocos) x HIQTC-P8 (paleta 8:1)
 * Mesma cena de textura renderizada 3x em contextos paralelos;
 * PSNR de cada região contra o original impresso no console.
 * ===================================================================== */
#include "common.h"

static hglCtx *draw_textured(int W, int H, hglTex *tex)
{
    hglCtx *ctx = hglCreateContext(W, H, 2);
    hglVertex q[4];
    uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    memset(q, 0, sizeof(q));
    {
        float S = 5.0f;
        float P[4][3] = { {-S,-S,-5},{S,-S,-5},{S,S,-5},{-S,S,-5} };
        int i;
        for (i = 0; i < 4; i++) {
            q[i].pos[0] = P[i][0]; q[i].pos[1] = P[i][1]; q[i].pos[2] = P[i][2];
            q[i].uv[0] = (i==0||i==3)?0.0f:4.0f;
            q[i].uv[1] = (i<2)?0.0f:4.0f;
            q[i].col[0]=q[i].col[1]=q[i].col[2]=q[i].col[3]=1;
        }
    }
    hglClearColor4f(ctx, 0.05f, 0.05f, 0.07f, 1);
    hglMatrixMode(ctx, HGL_PROJECTION); hglLoadIdentity(ctx);
    hglPerspective(ctx, 50.f*(float)HI_PI/180.f, (float)W/H, 0.1f, 50.f);
    hglMatrixMode(ctx, HGL_MODELVIEW); hglLoadIdentity(ctx);
    hglEnable(ctx, HGL_TEXTURE_2D);
    hglBindTexture(ctx, 0, tex);
    hglFrameBegin(ctx);
    hglDrawTrianglesIndexed(ctx, 6, idx, q);
    hglFrameEnd(ctx);
    return ctx;
}

static double psnr_region(const uint32_t *orig, const uint32_t *cmp,
                          int w, int h)
{
    double mse = 0.0;
    int i, ch;
    for (i = 0; i < w * h; i++)
        for (ch = 0; ch < 3; ch++) {
            float a = (float)((orig[i] >> (ch * 8)) & 255);
            float b = (float)((cmp[i] >> (ch * 8)) & 255);
            mse += (a - b) * (a - b);
        }
    mse /= (double)(w * h * 3);
    if (mse <= 0.0) mse = 1e-9;
    return 10.0 * log10(255.0 * 255.0 / mse);
}

int main(void)
{
    const int W = 256, H = 256;
    hglTex *rgba8, *hiqtc4, *hiqtcp8;
    uint32_t *orig;
    hglCtx *cOrig, *cH4, *cP8;

    /* gera padrão suave + madeira */
    int x, y;
    orig = malloc(sizeof(uint32_t) * W * H);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            float u = (float)x / (W - 1), v = (float)y / (H - 1);
            float wood = fabsf(sinf((u * 12.0f + v * 5.0f) * 3.14159f));
            uint32_t a = (uint32_t)(20 + 180 * wood);
            uint32_t b = (uint32_t)(30 + 120 * u * v);
            uint32_t r = (uint32_t)(120 + 130 * u);
            uint32_t g = (uint32_t)(80 + 140 * (1.0f - v));
            orig[y * W + x] = hi_pack_rgba8((int)r, (int)g,
                                            (int)(a + b) > 255 ? 255
                                                                : (int)(a + b),
                                            255);
        }

    rgba8    = hglTexCreateRGBA8(W, H, orig);
    hiqtc4   = hglTexCreateHIQTCFromRGBA8(W, H, orig);
    hiqtcp8  = hglTexCreateHIQTCP8FromRGBA8(W, H, orig);
    hglTexGenerateMipmaps(rgba8);      /* evita aliasing em minificação    */
    hglTexGenerateMipmaps(hiqtc4);
    hglTexGenerateMipmaps(hiqtcp8);

    cOrig = draw_textured(W, H, rgba8);
    cH4   = draw_textured(W, H, hiqtc4);
    cP8   = draw_textured(W, H, hiqtcp8);

    /* composição horizontal 3x */
    {
        uint32_t out[(size_t)(3 * W) * H];
        int y;
        const uint32_t *p0 = hglColorBuffer(cOrig);
        const uint32_t *p1 = hglColorBuffer(cH4);
        const uint32_t *p2 = hglColorBuffer(cP8);
        for (y = 0; y < H; y++) {
            memcpy(out + (size_t)y * 3 * W + 0,        p0 + (size_t)y * W, W * 4);
            memcpy(out + (size_t)y * 3 * W + W,        p1 + (size_t)y * W, W * 4);
            memcpy(out + (size_t)y * 3 * W + 2 * W,    p2 + (size_t)y * W, W * 4);
        }
        hglSavePNGBuffer("demo06.png", 3 * W, H, out);

    }

    printf("demo06: PSNR da compressão vs baseline RGBA8 com mips\n");
    {
        const uint32_t *po = hglColorBuffer(cOrig);
        double a = psnr_region(po, po, W, H);
        double b = psnr_region(po, hglColorBuffer(cH4), W, H);
        double c = psnr_region(po, hglColorBuffer(cP8), W, H);
        printf("  RGBA8 baseline   : %.2f dB\n"
               "  HIQTC blocos     : %.2f dB  (4:1)\n"
               "  HIQTC paleta P8  : %.2f dB  (8:1)\n", a, b, c);
    }

    free(orig);
    hglDestroyContext(cOrig);
    hglDestroyContext(cH4);
    hglDestroyContext(cP8);
    hglTexDestroy(rgba8);
    hglTexDestroy(hiqtc4);
    hglTexDestroy(hiqtcp8);
    return 0;
}
