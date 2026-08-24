/* =====================================================================
 * demo03 — mipmaps/trilinear: campo distante SEM vs COM cadeia de mips
 * Gera build/demo03.png (topo = sem mip · baixo = trilinear)
 * Métrica no console: cores únicas e energia de Laplaciano no longe.
 * ===================================================================== */
#include "common.h"

static hglCtx *render_scene(int use_mips)
{
    const int W = 384, H = 160;
    hglCtx *ctx = hglCreateContext(W, H, 1);
    uint32_t *chk;
    hglTex *ground;
    hglVertex q[4];
    uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    int i;

    chk = dc_checker(128, 128, 16, 235, 240, 250, 60, 90, 140);
    ground = hglTexCreateRGBA8(128, 128, chk);
    free(chk);
    if (use_mips)
        hglTexGenerateMipmaps(ground); /* trilinear single-pass */

    memset(q, 0, sizeof(q));
    {
        float S = 30.0f, Y = -0.6f;
        float P[4][3] = { { -S, Y, -S }, { S, Y, -S }, { S, Y, S }, { -S, Y, S } };
        float UV[4][2] = { { 0, 0 }, { 24, 0 }, { 24, 24 }, { 0, 24 } };
        for (i = 0; i < 4; i++) {
            q[i].pos[0] = P[i][0]; q[i].pos[1] = P[i][1]; q[i].pos[2] = P[i][2];
            q[i].uv[0] = UV[i][0]; q[i].uv[1] = UV[i][1];
            q[i].col[0] = q[i].col[1] = q[i].col[2] = 1.0f;
            q[i].col[3] = 1.0f;
        }
    }

    hglClearColor4f(ctx, 0.05f, 0.06f, 0.10f, 1);
    hglMatrixMode(ctx, HGL_PROJECTION);
    hglLoadIdentity(ctx);
    hglPerspective(ctx, 45.0f * (float)HI_PI / 180.0f,
                   (float)W / (float)H, 0.05f, 120.0f);
    hglMatrixMode(ctx, HGL_MODELVIEW);
    hglLoadIdentity(ctx);
    /* câmera rasante: horizonte alto, chão comprimido no topo da tela */
    hglLookAt(ctx, 0.0f, 0.35f, 3.0f, 0.0f, -0.55f, -8.0f, 0.0f, 1.0f, 0.0f);

    hglEnable(ctx, HGL_TEXTURE_2D);
    hglBindTexture(ctx, 0, ground);

    hglFrameBegin(ctx);
    hglDrawTrianglesIndexed(ctx, 6, idx, q);
    hglFrameEnd(ctx);

    hglTexDestroy(ground);
    return ctx;
}

/* energia média do Laplaciano absoluto em tons de cinza (região inferior) */
static double laplacian_energy(hglCtx *c, int y_from)
{
    const uint32_t *px = hglColorBuffer(c);
    int w = 0, h = 0, x, y;
    hglGetSize(c, &w, &h);
    double acc = 0.0;
    long n = 0;
    for (y = y_from + 1; y < h - 1; y++)
        for (x = 1; x < w - 1; x++) {
            #define L_G(i) ((double)(((px[(i)] >> 8) & 255)))
            double c0  = L_G((size_t)y * w + x);
            double up  = L_G((size_t)(y - 1) * w + x);
            double dn  = L_G((size_t)(y + 1) * w + x);
            double lf  = L_G((size_t)y * w + x - 1);
            double rt  = L_G((size_t)y * w + x + 1);
            #undef L_G
            acc += fabs(up + dn + lf + rt - 4.0 * c0);
            n++;
        }
    return n ? acc / n : 0.0;
}

int main(void)
{
    hglCtx *a = render_scene(0);   /* sem mips   */
    hglCtx *b = render_scene(1);   /* trilinear  */
    int W = 0, H = 0;
    hglGetSize(a, &W, &H);
    uint32_t *out = malloc(sizeof(uint32_t) * (size_t)W * (2 * H));
    int y;

    for (y = 0; y < H; y++) {
        memcpy(out + (size_t)y * W, hglColorBuffer(a) + (size_t)y * W, W * 4);
        memcpy(out + (size_t)(H + y) * W, hglColorBuffer(b) + (size_t)y * W, W * 4);
    }
    hglSavePNGBuffer("demo03.png", W, 2 * H, out);

    printf("demo03: longe (metade inferior)\n");
    printf("  sem mip : laplaciano medio = %.3f\n", laplacian_energy(a, H / 2));
    printf("  trilinear: laplaciano medio = %.3f\n", laplacian_energy(b, H / 2));

    free(out);
    hglDestroyContext(a);
    hglDestroyContext(b);
    return 0;
}
