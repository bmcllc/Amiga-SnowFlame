/* =====================================================================
 * test_main.c — testes automatizados do renderizador Hot-ice
 * Retorna não-zero em caso de falha.
 * ===================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/internal.h"

static int g_fail = 0;
static int g_run  = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        g_run++;                                                            \
        if (!(cond)) {                                                      \
            g_fail++;                                                       \
            printf("FALHOU: %s (linha %d)\n", msg, __LINE__);               \
        } else {                                                            \
            printf("ok: %s\n", msg);                                        \
        }                                                                   \
    } while (0)

static float dist3(float ax, float ay, float az, float bx, float by, float bz)
{
    float dx = ax - bx, dy = ay - by, dz = az - bz;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* PSNR entre dois buffers packed RGBA8 (compara R,G,B) */
static double psnr_packed(const uint32_t *a, const uint32_t *b, size_t n)
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

/* ------------------------------------------------------------- matemática */
static void test_math(void)
{
    hiMat4 id, m, r;
    hiVec4 p;

    hi_mat_identity(&id);
    p = hi_mat_xform(&id, hi_v4(1, 2, 3, 1));
    CHECK(p.x == 1 && p.y == 2 && p.z == 3 && p.w == 1,
          "mat: identidade preserva ponto");

    hi_mat_translate(&m, 5, 0, 0);
    hi_mat_mul(&r, &id, &m);
    p = hi_mat_xform(&r, hi_v4(1, 1, 1, 1));
    CHECK(fabsf(p.x - 6.0f) < 1e-5f, "mat: translação aplicada");

    {
        hiMat4 proj;
        hi_mat_perspective(&proj, 90.0f * (float)HI_PI / 180.0f, 1.0f,
                           1.0f, 100.0f);
        p = hi_mat_xform(&proj, hi_v4(0, 0, -10, 1)); /* à frente da câmera */
        CHECK(p.w > 0.0f && fabsf(p.x) < 1e-5f && fabsf(p.y) < 1e-5f,
              "mat: perspectiva centraliza eixo");
    }

    /* ponto fixo ida-e-volta */
    {
        hiFix1616 q = hi_fix_from_float(1.5f);
        CHECK(fabsf(hi_fix_to_float(q) - 1.5f) < 1e-4f, "fix16: ida-e-volta");
    }
}

/* ------------------------------------------------------------------ HIQTC */
static void test_hiqtc_solid(void)
{
    uint32_t px[16], ref, exp, got;
    uint8_t *enc;
    hglTex t;

    ref = hi_pack_rgba8(200, 30, 40, 255);
    { int i; for (i = 0; i < 16; i++) px[i] = ref; }

    enc = hi_hiqtc_encode_rgba8(4, 4, px);
    CHECK(enc != NULL, "hiqtc: encode 4x4");
    if (!enc) return;

    /* referência = cor passada pela quantização RGB565 das âncoras */
    {
        uint16_t c565 = hi_pack_rgb565(200, 30, 40);
        int er, eg, eb;
        hi_unpack_rgb565(c565, &er, &eg, &eb);
        exp = hi_pack_rgba8(er, eg, eb, 255);
    }

    memset(&t, 0, sizeof(t));
    t.w = 4; t.h = 4; t.fmt = HI_FMT_HIQTC; t.hiq = enc;
    got = hi_hiqtc_decode_texel(&t, 2, 2);
    CHECK(got == exp, "hiqtc: bloco sólido é exato pós-quantização 565");
    free(enc);
}

static void test_hiqtc_gradient(void)
{
    enum { W = 64, H = 64 };
    static uint32_t px[W * H];
    static uint32_t dec[W * H];
    uint8_t *enc;
    hglTex t;
    int x, y;
    double q;

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int r = x * 255 / W;
            int g = y * 255 / H;
            int b = 128 + ((x + y) & 31);
            px[(size_t)y * W + x] = hi_pack_rgba8(r, g, b > 255 ? 255 : b, 255);
        }

    enc = hi_hiqtc_encode_rgba8(W, H, px);
    CHECK(enc != NULL, "hiqtc: encode 64x64 gradiente");
    if (!enc) return;

    memset(&t, 0, sizeof(t));
    t.w = W; t.h = H; t.fmt = HI_FMT_HIQTC; t.hiq = enc;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            dec[(size_t)y * W + x] = hi_hiqtc_decode_texel(&t, x, y);

    q = psnr_packed(px, dec, W * H);
    printf("     PSNR gradiente HIQTC = %.2f dB\n", q);
    CHECK(q >= 24.0, "hiqtc: PSNR gradiente >= 24 dB");
    free(enc);
}

/* -------------------------------------------------------------------- HDE */
static void test_hde_morph_skin(void)
{
    hglCtx *ctx = hglCreateContext(8, 8, 1);
    hglVertex v;
    hiGeomVert out;
    float target[3] = { 2.0f, 0.0f, 0.0f };
    float palette[16];

    memset(&v, 0, sizeof(v));
    v.pos[0] = 0; v.pos[1] = 0; v.pos[2] = 0;
    v.col[0] = v.col[1] = v.col[2] = v.col[3] = 1;

    /* morph: p(t)=p+t*(alvo-p), t=0.25 => x=0.5 */
    hglBindMorphTarget(ctx, 0, target, 1);
    hglMorphWeight(ctx, 0, 0.25f);
    hi_hde_vertex(ctx, &v, 0, &out);
    CHECK(fabsf(out.cx - 0.5f) < 1e-5f && fabsf(out.cw - 1.0f) < 1e-6f,
          "hde: morfing contínuo interpola posição");

    hglMorphWeight(ctx, 0, 0.0f);

    /* skin: um osso que translada (1,2,3), peso total */
    memset(palette, 0, sizeof(palette));
    palette[0] = 1; palette[5] = 1; palette[10] = 1; palette[15] = 1;
    palette[3] = 1; palette[7] = 2; palette[11] = 3;
    hglSkinPalette(ctx, palette, 1);
    hglEnable(ctx, HGL_SKINNING);
    v.bone[0] = 0; v.bw[0] = 1.0f;
    hi_hde_vertex(ctx, &v, 0, &out);
    CHECK(dist3(out.cx, out.cy, out.cz, 1, 2, 3) < 1e-5f,
          "hde: skinning de osso único translada vértice");
    hglDisable(ctx, HGL_SKINNING);
    hglSkinPalette(ctx, NULL, 0);

    hglDestroyContext(ctx);
}

/* ------------------------------------------- rasterização / TBR / depth */
static void fill_quad(hglVertex v[4], float x0, float y0, float z,
                      float x1, float y1, float r, float g, float bl)
{
    float pos[4][3] = {
        { x0, y0, z }, { x1, y0, z }, { x1, y1, z }, { x0, y1, z }
    };
    int i;
    memset(v, 0, sizeof(hglVertex) * 4);
    for (i = 0; i < 4; i++) {
        v[i].pos[0] = pos[i][0]; v[i].pos[1] = pos[i][1]; v[i].pos[2] = pos[i][2];
        v[i].col[0] = r; v[i].col[1] = g; v[i].col[2] = bl; v[i].col[3] = 1;
    }
}

static void test_coverage_shared_edge(void)
{
    enum { W = 128, H = 128 };
    hglCtx *ctx = hglCreateContext(W, H, 1);
    hglVertex tA[3], tB[3];
    uint32_t idx[3] = { 0, 1, 2 };
    const uint32_t *cbuf;
    int i, green = 0, blue = 0, other = 0;
    /* retângulo-alvo em pixels: [8.25..104.25]x[8.25..56.25] = 96x48 */
    float px0 = 8.25f, py0 = 8.25f, px1 = 104.25f, py1 = 56.25f;

    memset(tA, 0, sizeof(tA));
    memset(tB, 0, sizeof(tB));
    {
        /* inverso do viewport: ndcx = 2*sx/W - 1 ; ndcy = 1 - 2*sy/H */
        float ax = 2.0f * px0 / W - 1.0f, ay = 1.0f - 2.0f * py0 / H;
        float bx = 2.0f * px1 / W - 1.0f, by = 1.0f - 2.0f * py1 / H;

        tA[0].pos[0] = ax; tA[0].pos[1] = ay;   /* sup-esq */
        tA[1].pos[0] = bx; tA[1].pos[1] = ay;   /* sup-dir */
        tA[2].pos[0] = bx; tA[2].pos[1] = by;   /* inf-dir */
        tB[0].pos[0] = ax; tB[0].pos[1] = ay;   /* sup-esq */
        tB[1].pos[0] = bx; tB[1].pos[1] = by;   /* inf-dir */
        tB[2].pos[0] = ax; tB[2].pos[1] = by;   /* inf-esq */

        for (i = 0; i < 3; i++) {
            tA[i].col[1] = 1.0f; tA[i].col[3] = 1.0f;              /* verde */
            tB[i].col[2] = 1.0f; tB[i].col[3] = 1.0f;              /* azul  */
        }
    }

    hglClearColor4f(ctx, 0, 0, 0, 1);
    hglFrameBegin(ctx);
    hglDrawTrianglesIndexed(ctx, 3, idx, tA);
    hglDrawTrianglesIndexed(ctx, 3, idx, tB);
    hglFrameEnd(ctx);

    cbuf = hglColorBuffer(ctx);
    for (i = 0; i < W * H; i++) {
        int r, g, b, a;
        hi_unpack_rgba8(cbuf[i], &r, &g, &b, &a);
        if (g == 255 && r == 0 && b == 0) green++;
        else if (b == 255 && r == 0 && g == 0) blue++;
        else other++;
    }
    printf("     cobertura: %d verdes + %d azuis (%d fora)\n",
           green, blue, other);
    CHECK(green + blue == 96 * 48, "tbr: quad dividido cobre área exata");
    CHECK(other == W * H - 96 * 48, "tbr: nada pintado fora do quad");
    hglDestroyContext(ctx);
}

static void test_depth_less(void)
{
    hglCtx *ctx = hglCreateContext(32, 32, 1);
    hglVertex far_[4], nearv[4];
    uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    int r, g, b, a;

    hglClearColor4f(ctx, 0, 0, 0, 1);
    fill_quad(far_,  -8, -8, -0.2f, 8, 8, 0, 0, 1); /* azul: ndcz -0.2 -> longe */
    fill_quad(nearv, -8, -8, -0.8f, 8, 8, 1, 0, 0); /* vermelho: -0.8 -> perto */

    /* ordem 1: longe primeiro */
    hglFrameBegin(ctx);
    hglDrawTrianglesIndexed(ctx, 6, idx, far_);
    hglDrawTrianglesIndexed(ctx, 6, idx, nearv);
    hglFrameEnd(ctx);
    hi_unpack_rgba8(hglColorBuffer(ctx)[16 * 32 + 16], &r, &g, &b, &a);
    CHECK(r == 255 && g == 0 && b == 0, "depth: perto vence (longe primeiro)");

    /* ordem 2: perto primeiro — LESS rejeita o longe */
    hglFrameBegin(ctx);
    hglDrawTrianglesIndexed(ctx, 6, idx, nearv);
    hglDrawTrianglesIndexed(ctx, 6, idx, far_);
    hglFrameEnd(ctx);
    hi_unpack_rgba8(hglColorBuffer(ctx)[16 * 32 + 16], &r, &g, &b, &a);
    CHECK(r == 255 && g == 0 && b == 0, "depth: perto vence (ordem inversa)");

    hglDestroyContext(ctx);
}

static void test_near_clip(void)
{
    hglCtx *ctx = hglCreateContext(64, 64, 1);
    /* triângulo com um vértice ATRÁS do olho (w<=0) — precisa clipar */
    hglVertex tri[3];
    uint32_t idx[3] = { 0, 1, 2 };
    const uint32_t *buf;
    int i, red = 0;

    hglClearColor4f(ctx, 0, 0, 0, 1);
    memset(tri, 0, sizeof(tri));
    /* projeção identidade: w=1 sempre... usar perspectiva real */
    hglMatrixMode(ctx, HGL_PROJECTION);
    hglLoadIdentity(ctx);
    hglPerspective(ctx, 60.0f * (float)HI_PI / 180.0f, 1.0f, 0.1f, 50.0f);
    hglMatrixMode(ctx, HGL_MODELVIEW);
    hglLoadIdentity(ctx);

    /* câmera olha para -z: vértices com z>0 estão ATRÁS do olho */
    tri[0].pos[0] =  0.0f; tri[0].pos[1] = -1.0f; tri[0].pos[2] = -2.0f;
    tri[1].pos[0] = -1.0f; tri[1].pos[1] =  1.0f; tri[1].pos[2] = -2.0f;
    tri[2].pos[0] =  1.0f; tri[2].pos[1] =  1.0f; tri[2].pos[2] =  2.0f;
    tri[0].col[0] = tri[1].col[0] = tri[2].col[0] = 1;
    tri[0].col[3] = tri[1].col[3] = tri[2].col[3] = 1;

    hglFrameBegin(ctx);
    hglDrawTrianglesIndexed(ctx, 3, idx, tri);
    hglFrameEnd(ctx);

    buf = hglColorBuffer(ctx);
    for (i = 0; i < 64 * 64; i++) {
        int r, g, b, a;
        hi_unpack_rgba8(buf[i], &r, &g, &b, &a);
        if (r > 200) red++;
    }
    CHECK(red > 0 && red < 64 * 64, "clip: near plane recorta sem travar");
    hglDestroyContext(ctx);
}

static void test_caa_levels(void)
{
    enum { W = 64, H = 64 };
    hglCtx *c1 = hglCreateContext(W, H, 1);
    hglCtx *c4 = hglCreateContext(W, H, 4);
    hglVertex tri[3];
    uint32_t idx[3] = { 0, 1, 2 };
    int shades1 = 0, shades4 = 0, i;

    memset(tri, 0, sizeof(tri));
    /* meia-tela diagonal: preto/branco */
    tri[0].pos[0] = -1.0f; tri[0].pos[1] = -1.0f;
    tri[1].pos[0] =  1.9f; tri[1].pos[1] = -1.0f;
    tri[2].pos[0] = -1.0f; tri[2].pos[1] =  1.9f;
    tri[0].col[0] = tri[1].col[0] = tri[2].col[0] = 1;
    tri[0].col[1] = tri[1].col[1] = tri[2].col[1] = 1;
    tri[0].col[2] = tri[1].col[2] = tri[2].col[2] = 1;
    tri[0].col[3] = tri[1].col[3] = tri[2].col[3] = 1;

    hglClearColor4f(c1, 0, 0, 0, 1);
    hglFrameBegin(c1);
    hglDrawTrianglesIndexed(c1, 3, idx, tri);
    hglFrameEnd(c1);

    hglClearColor4f(c4, 0, 0, 0, 1);
    hglFrameBegin(c4);
    hglDrawTrianglesIndexed(c4, 3, idx, tri);
    hglFrameEnd(c4);

    /* níveis distintos de cinza ao longo da diagonal */
    {
        static uint8_t seen1[256], seen4[256];
        const uint32_t *b1 = hglColorBuffer(c1);
        const uint32_t *b4 = hglColorBuffer(c4);
        int x, y;
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                int r, g, b, a;
                i = y * W + x;
                hi_unpack_rgba8(b1[i], &r, &g, &b, &a);
                seen1[g] = 1;
                hi_unpack_rgba8(b4[i], &r, &g, &b, &a);
                seen4[g] = 1;
            }
        for (i = 0; i < 256; i++) {
            shades1 += seen1[i];
            shades4 += seen4[i];
        }
    }
    printf("     níveis na diagonal: 1x=%d · CAA4x=%d\n", shades1, shades4);
    CHECK(shades4 > shades1, "caa: grade rotacionada cria tons intermediários");

    hglDestroyContext(c1);
    hglDestroyContext(c4);
}

static void test_sampler_wrap(void)
{
    hglTex *t;
    uint32_t pix[4];
    uint32_t a, b;

    pix[0] = hi_pack_rgba8(255, 0, 0, 255);
    pix[1] = hi_pack_rgba8(0, 255, 0, 255);
    pix[2] = hi_pack_rgba8(0, 0, 255, 255);
    pix[3] = hi_pack_rgba8(255, 255, 0, 255);
    t = hglTexCreateRGBA8(2, 2, pix);
    hglTexFilter(t, HGL_FILTER_NEAREST);

    a = hi_sample_tex(t, 0.25f, 0.75f);
    b = hi_sample_tex(t, 1.25f, 0.75f); /* wrap repeat: mesma coluna */
    CHECK(a == b, "sampler: wrap repeat em u=1.25");
    hglTexDestroy(t);
}

int main(void)
{
    printf("== Hot-ice: suíte de testes ==\n");
    test_math();
    test_hiqtc_solid();
    test_hiqtc_gradient();
    test_hde_morph_skin();
    test_coverage_shared_edge();
    test_depth_less();
    test_near_clip();
    test_caa_levels();
    test_sampler_wrap();

    printf("\n%d verificações, %d falhas\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
