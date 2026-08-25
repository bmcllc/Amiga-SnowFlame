/* =====================================================================
 * test_main.c — testes automatizados do renderizador Hot-ice
 * Retorna não-zero em caso de falha.
 * ===================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/internal.h"
extern int run_sys_tests(void);

int g_fail = 0;
int g_run  = 0;

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


/* ---------------------------------------------------------- mipmaps */
static void test_mipmap_chain(void)
{
    uint32_t px[64];
    hglTex *t;
    int lvls, x, y;
    uint32_t c3;

    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            px[y * 8 + x] = (y < 4) ? hi_pack_rgba8(255, 0, 0, 255)
                                    : hi_pack_rgba8(0, 0, 255, 255);
    t = hglTexCreateRGBA8(8, 8, px);
    lvls = hglTexGenerateMipmaps(t);

    CHECK(lvls == 4 && t->nlevels == 4, "mips: cadeia 8->4->2->1");
    CHECK(t->lw[1] == 4 && t->lh[1] == 4 && t->lw[3] == 1,
          "mips: dimensões por nível");

    /* nível 1: metade vermelha, metade azul (fronteira alinhada) */
    {
        int ok = 1, i;
        for (i = 0; i < 8; i++) {
            uint32_t c = t->lv[1][i];
            int r, g, b, a;
            hi_unpack_rgba8(c, &r, &g, &b, &a);
            if (i < 8 && g != 0) ok = 0; /* canal G sempre 0 */
        }
        CHECK(ok, "mips: nível 1 preserva tons puros");
    }

    /* nível final 1x1: média exata de 2 vermelhos + 2 azuis = (128,0,128) */
    c3 = t->lv[3][0];
    {
        int r, g, b, a;
        hi_unpack_rgba8(c3, &r, &g, &b, &a);
        CHECK(r == 128 && b == 128 && g == 0,
              "mips: média box-filter exata no nível 1x1");
    }
    hglTexDestroy(t);
}

static void test_trilinear_levels(void)
{
    uint32_t px[64];
    hglTex *t;
    uint32_t mag, far_;
    int x, y;

    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            px[y * 8 + x] = (y < 4) ? hi_pack_rgba8(255, 0, 0, 255)
                                    : hi_pack_rgba8(0, 0, 255, 255);
    t = hglTexCreateRGBA8(8, 8, px);
    hglTexGenerateMipmaps(t);

    /* ampliação (rho<1): deve ser idêntico ao caminho bilinear nível 0 */
    mag = hi_sample_tex_mip(t, 0.25f, 0.25f, 0.5f);
    CHECK(mag == hi_sample_tex(t, 0.25f, 0.25f),
          "trilinear: ampliação usa nível base");

    /* minificação extrema (lambda clampado no último nível):
       qualquer uv retorna a cor média da cadeia */
    far_ = hi_sample_tex_mip(t, 0.7f, 0.9f, 4096.0f);
    CHECK(far_ == hi_pack_rgba8(128, 0, 128, 255),
          "trilinear: minificação extrema cai no nível médio");
    hglTexDestroy(t);
}

/* --------------------------------------------------------- stencil */
static void fill_rect_px(hglCtx *ctx, hglVertex v[4],
                         int x0, int y0, int x1, int y1,
                         float r, float g, float bl)
{
    int W = ctx->w, H = ctx->h, i;
    float nx[4], ny[4];
    float sx[4] = { (float)x0, (float)x1, (float)x1, (float)x0 };
    float sy[4] = { (float)y0, (float)y0, (float)y1, (float)y1 };
    memset(v, 0, sizeof(hglVertex) * 4);
    for (i = 0; i < 4; i++) {
        nx[i] = 2.0f * sx[i] / W - 1.0f;
        ny[i] = 1.0f - 2.0f * sy[i] / H;
        v[i].pos[0] = nx[i]; v[i].pos[1] = ny[i]; v[i].pos[2] = -0.5f;
        v[i].col[0] = r; v[i].col[1] = g; v[i].col[2] = bl; v[i].col[3] = 1;
    }
}

static void test_stencil(void)
{
    const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    hglVertex q[4];
    hglCtx *ctx = hglCreateContext(32, 32, 1);
    const uint8_t *st;

    hglClearColor4f(ctx, 0, 0, 0, 1);
    hglEnable(ctx, HGL_STENCIL_TEST);

    /* 1) máscara isolada: REPLACE grava ref na região */
    hglStencilFunc(ctx, HGL_ST_ALWAYS, 1);
    hglStencilOp(ctx, HGL_SO_REPLACE);
    fill_rect_px(ctx, q, 4, 4, 16, 16, 1, 1, 1);
    hglFrameBegin(ctx);
    hglDrawTrianglesIndexed(ctx, 6, idx, q);
    hglFrameEnd(ctx);
    st = ctx->stencilOut;
    CHECK(st[8 * 32 + 8] == 1 && st[24 * 32 + 24] == 0,
          "stencil: REPLACE grava ref na região");

    /* 2) uso da máscara NO MESMO frame (multipasse intra-frame por design
       do TBR): estado trocado ENTRE os draws — cada draw congela o seu */
    fill_rect_px(ctx, q, 4, 4, 16, 16, 1, 1, 1);
    {
        hglVertex big[3];
        uint32_t ti[3] = { 0, 1, 2 };
        memset(big, 0, sizeof(big));
        big[0].pos[0] = -2; big[0].pos[1] = -2; big[0].pos[2] = -0.6f;
        big[1].pos[0] =  4; big[1].pos[1] = -2; big[1].pos[2] = -0.6f;
        big[2].pos[0] = -2; big[2].pos[1] =  4; big[2].pos[2] = -0.6f;
        big[0].col[0] = big[1].col[0] = big[2].col[0] = 1;
        big[0].col[3] = big[1].col[3] = big[2].col[3] = 1;

        hglStencilFunc(ctx, HGL_ST_ALWAYS, 1);   /* passe máscara */
        hglStencilOp(ctx, HGL_SO_REPLACE);
        hglFrameBegin(ctx);
        hglDrawTrianglesIndexed(ctx, 6, idx, q);

        hglStencilFunc(ctx, HGL_ST_EQUAL, 1);    /* passe uso */
        hglStencilOp(ctx, HGL_SO_KEEP);
        hglDrawTrianglesIndexed(ctx, 3, ti, big);
        hglFrameEnd(ctx);
    }
    {
        const uint32_t *cb = hglColorBuffer(ctx);
        int r, g, b, a;
        hi_unpack_rgba8(cb[8 * 32 + 8], &r, &g, &b, &a);
        CHECK(r == 255, "stencil: EQUAL pinta dentro da máscara");
        hi_unpack_rgba8(cb[24 * 32 + 24], &r, &g, &b, &a);
        CHECK(r == 0, "stencil: EQUAL bloqueia fora da máscara");
    }

    /* INCR com saturação + ZERO */
    hglClearStencil(ctx, 254);
    hglStencilFunc(ctx, HGL_ST_ALWAYS, 0);
    hglStencilOp(ctx, HGL_SO_INCR);
    fill_rect_px(ctx, q, 20, 20, 28, 28, 0, 1, 0);
    hglFrameBegin(ctx);   /* clear stencil=254 */
    hglDrawTrianglesIndexed(ctx, 6, idx, q);
    hglDrawTrianglesIndexed(ctx, 6, idx, q); /* segunda passada satura em 255 */
    hglFrameEnd(ctx);
    st = ctx->stencilOut;
    CHECK(st[24 * 32 + 24] == 255, "stencil: INCR satura em 255");

    hglClearStencil(ctx, 5);
    hglStencilOp(ctx, HGL_SO_ZERO);
    hglFrameBegin(ctx);   /* clear stencil=5 */
    hglDrawTrianglesIndexed(ctx, 6, idx, q);
    hglFrameEnd(ctx);
    st = ctx->stencilOut;
    CHECK(st[24 * 32 + 24] == 0 && st[2 * 32 + 2] == 5,
          "stencil: ZERO zera fragmentos e clear preserva o resto");

    hglDisable(ctx, HGL_STENCIL_TEST);
    hglDestroyContext(ctx);
}


/* =====================================================================
 * Versores (quat) — espelho das instruções V*.Q da MLVU do V4æ
 * ===================================================================== */
static void test_quat(void)
{
    {
        hiQuat q = hi_quat_axis(hi_v3(0, 1, 0), (float)HI_PI * 0.5f);
        hiVec3 r = hi_quat_rotate(q, hi_v3(1, 0, 0));
        CHECK(fabsf(r.x) < 1e-4f && fabsf(r.y) < 1e-4f && r.z < -0.999f,
              "quat: rotação 90°Y leva +X para -Z");
    }
    {
        float ang = 0.7f;
        hiQuat q = hi_quat_axis(hi_v3(0, 1, 0), ang);
        hiMat4 A, B;
        int i, j, same = 1;
        hi_quat_to_mat4(&A, q);
        hi_mat_rotate_y(&B, ang);
        for (i = 0; i < 3 && same; i++)
            for (j = 0; j < 3; j++)
                if (fabsf(A.m[i][j] - B.m[i][j]) > 1e-5f) same = 0;
        CHECK(same, "quat: mat4 do versor = rotate_y");
    }
    {
        hiQuat qa = hi_quat_axis(hi_v3(1, 0, 0), (float)HI_PI * 0.5f);
        hiQuat qb = hi_quat_axis(hi_v3(1, 0, 0), -(float)HI_PI * 0.5f);
        hiQuat m = hi_quat_slerp(qa, qb, 0.5f);
        hiVec3 r = hi_quat_rotate(m, hi_v3(0, 1, 0));
        CHECK(fabsf(r.x) < 1e-3f && fabsf(r.y - 1.0f) < 1e-3f
              && fabsf(r.z) < 1e-3f,
              "quat: slerp médio de ±90°X é identidade");
    }
}

/* =====================================================================
 * DOT3 bump — luz por pixel a partir da normal codificada no texel
 * ===================================================================== */
static uint32_t g_white[16], g_nm[16];

/* caso 0 = sem bump · 1 = normal +Z · 2 = normal -X */
static void dot3_scene(int caso)
{
    hglCtx *ctx = hglCreateContext(32, 32, 1);
    hglTex *base, *bump;
    hglVertex q[4];
    uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    const uint32_t *cb;

    base = hglTexCreateRGBA8(4, 4, g_white);
    bump = hglTexCreateRGBA8(4, 4, g_nm);

    fill_rect_px(ctx, q, 2, 2, 30, 30, 1, 1, 1);

    if (caso > 0) {
        hglEnable(ctx, HGL_BUMP_DOT3);
        hglBindBumpTexture(ctx, bump);
        hglLightDirf(ctx, 0.6f, 0.0f, 0.8f);
        hglLightColor4f(ctx, 1, 1, 1, 1);
        hglAmbient4f(ctx, 0.2f, 0.2f, 0.2f, 1);
    }
    hglEnable(ctx, HGL_TEXTURE_2D);
    hglBindTexture(ctx, 0, base);

    hglFrameBegin(ctx);
    hglDrawTrianglesIndexed(ctx, 6, idx, q);
    hglFrameEnd(ctx);

    cb = hglColorBuffer(ctx);
    if (caso == 0) {
        CHECK((int)(cb[16 * 32 + 16] & 255) == 255,
              "dot3: sem bump, albedo puro passa direto");
    } else if (caso == 1) {
        float bx = (128.0f / 255.0f) * 2.0f - 1.0f;
        float Lx = 0.6f, Ly = 0.0f, Lz = 0.8f, ln;
        float ndl, k;
        int er, expect;
        ln = sqrtf(Lx * Lx + Ly * Ly + Lz * Lz);
        ndl = bx * (Lx / ln) + bx * (Ly / ln) + 1.0f * (Lz / ln);
        k = 0.2f + 1.0f * ndl;
        er = (int)(255.0f * k);
        expect = er > 255 ? 255 : er;
        CHECK(abs((int)(cb[16 * 32 + 16] & 255) - expect) <= 1,
              "dot3: normal +Z ilumina conforme fórmula");
    } else {
        CHECK(abs((int)(cb[16 * 32 + 16] & 255) - 51) <= 1,
              "dot3: normal -X cai no ambiente");
    }

    hglTexDestroy(base);
    hglTexDestroy(bump);
    hglDisable(ctx, HGL_BUMP_DOT3);
    hglDisable(ctx, HGL_TEXTURE_2D);
    hglDestroyContext(ctx);
}

static void test_dot3_bump(void)
{
    int i;
    for (i = 0; i < 16; i++) g_white[i] = 0xFFFFFFFFu;

    dot3_scene(0);                                    /* albedo puro     */

    for (i = 0; i < 16; i++)                          /* normal +Z       */
        g_nm[i] = hi_pack_rgba8(128, 128, 255, 255);
    dot3_scene(1);

    for (i = 0; i < 16; i++)                          /* normal -X       */
        g_nm[i] = hi_pack_rgba8(0, 128, 128, 255);
    dot3_scene(2);
}

/* =====================================================================
 * Env map esférico — quad de frente para a câmera → uv=(0.5,0.5)
 * ===================================================================== */
static void test_envmap(void)
{
    hglCtx *ctx = hglCreateContext(32, 32, 1);
    hglTex *env;
    hglVertex q[4];
    uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    uint32_t envpix[64];
    const uint32_t *cb;
    int i;

    for (i = 0; i < 64; i++) envpix[i] = hi_pack_rgba8(0, 200, 0, 255);
    env = hglTexCreateRGBA8(8, 8, envpix);

    memset(q, 0, sizeof(q));
    {
        float sx[4] = { 2, 30, 30, 2 }, sy[4] = { 2, 2, 30, 30 };
        for (i = 0; i < 4; i++) {
            q[i].pos[0] = 2.f * sx[i] / 32.f - 1.f;
            q[i].pos[1] = 1.f - 2.f * sy[i] / 32.f;
            q[i].pos[2] = -0.5f;
            q[i].nrm[2] = -1.0f;          /* normal apontando p/ câmera */
            q[i].col[0] = 10.f / 255.f;
            q[i].col[1] = 20.f / 255.f;
            q[i].col[2] = 30.f / 255.f;
            q[i].col[3] = 1.0f;
        }
    }
    hglEnable(ctx, HGL_ENVMAP);
    hglBindEnvTexture(ctx, env);

    hglFrameBegin(ctx);
    hglDrawTrianglesIndexed(ctx, 6, idx, q);
    hglFrameEnd(ctx);

    cb = hglColorBuffer(ctx);
    {
        int r = (int)(cb[16 * 32 + 16] & 255);
        int g = (int)((cb[16 * 32 + 16] >> 8) & 255);
        int b = (int)((cb[16 * 32 + 16] >> 16) & 255);
        CHECK(r == 10 && abs(g - 220) <= 2 && b == 30,
              "envmap: normal frontal amostra o centro (aditivo)");
    }
    hglDestroyContext(ctx);
}

/* =====================================================================
 * HIQTC modo PALETA (8bpp)
 * ===================================================================== */
static void test_hiqtc_p8(void)
{
    /* sólido decodifica exato (pós-roundtrip 565) */
    {
        uint32_t px[256], ref;
        hglTex *t;
        uint32_t *dec;
        int i, ok = 1;
        for (i = 0; i < 256; i++) px[i] = hi_pack_rgba8(200, 120, 40, 255);
        t = hglTexCreateHIQTCP8FromRGBA8(16, 16, px);
        CHECK(t != NULL && t->fmt == HI_FMT_HIQTC_P8,
              "hiqtc-p8: criação em modo paleta");
        dec = hi_hiqtc_p8_decode_all(t);
        {
            int r, g, b;
            hi_unpack_rgba8(px[0], &r, &g, &b, NULL);
            ref = hi_pack_rgba8((r >> 3) << 3, (g >> 2) << 2, (b >> 3) << 3,
                                255);
        }
        for (i = 0; i < 256; i++)
            if (dec[i] != ref) ok = 0;
        CHECK(ok, "hiqtc-p8: bloco sólido é exato pós-quantização");
        free(dec);
        hglTexDestroy(t);
    }
    /* gradiente suave: PSNR >= 28 dB */
    {
        static uint32_t px[64 * 64];
        hglTex *t;
        uint32_t *dec;
        double mse = 0.0;
        int i, x, y;
        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++)
                px[y * 64 + x] = hi_pack_rgba8(x * 4, y * 4,
                                               (x + y) * 2, 255);
        t = hglTexCreateHIQTCP8FromRGBA8(64, 64, px);
        dec = hi_hiqtc_p8_decode_all(t);
        for (i = 0; i < 64 * 64; i++) {
            int ch;
            for (ch = 0; ch < 3; ch++) {
                float a = (float)((px[i] >> (ch * 8)) & 255);
                float b = (float)((dec[i] >> (ch * 8)) & 255);
                mse += (a - b) * (a - b);
            }
        }
        mse /= (double)(64 * 64 * 3);
        if (mse <= 0.0) mse = 1e-9;
        printf("     PSNR gradiente HIQTC-P8 = %.2f dB\n",
               10.0 * log10(255.0 * 255.0 / mse));
        CHECK(10.0 * log10(255.0 * 255.0 / mse) >= 28.0,
              "hiqtc-p8: PSNR gradiente >= 28 dB");
        free(dec);
        hglTexDestroy(t);
    }
}

/* =====================================================================
 * CCB — Continuity Coherence Buffer (dossiê §2.2)
 * ===================================================================== */

/* cubo axis-aligned: 24 vértices (4 por face, normais planas), 36 índices */
static void make_cube(hglVertex *v, uint32_t *idx,
                      float cx, float cy, float cz, float s,
                      float r, float g, float b, uint32_t base)
{
    static const float N[6][3] = {
        {0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}
    };
    static const float F[6][4][3] = {
        {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},   /* +z */
        {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}},   /* -z */
        {{ 1,-1, 1},{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1}},   /* +x */
        {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}},   /* -x */
        {{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1},{-1, 1,-1}},   /* +y */
        {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}},   /* -y */
    };
    int f, i;
    for (f = 0; f < 6; f++) {
        for (i = 0; i < 4; i++) {
            hglVertex *vv = &v[f * 4 + i];
            memset(vv, 0, sizeof(*vv));
            vv->pos[0] = cx + F[f][i][0] * s;
            vv->pos[1] = cy + F[f][i][1] * s;
            vv->pos[2] = cz + F[f][i][2] * s;
            vv->nrm[0] = N[f][0]; vv->nrm[1] = N[f][1]; vv->nrm[2] = N[f][2];
            vv->col[0] = r; vv->col[1] = g; vv->col[2] = b; vv->col[3] = 1;
        }
        idx[f*6+0] = base+(uint32_t)f*4+0;
        idx[f*6+1] = base+(uint32_t)f*4+1;
        idx[f*6+2] = base+(uint32_t)f*4+2;
        idx[f*6+3] = base+(uint32_t)f*4+0;
        idx[f*6+4] = base+(uint32_t)f*4+2;
        idx[f*6+5] = base+(uint32_t)f*4+3;
    }
}

static void test_ccb(void)
{
    /* --- conectividade: quad compartilhado = 1 patch ------------------- */
    {
        hglVertex v[4];
        uint32_t idx[6] = { 0,1,2, 0,2,3 };
        hglCcbMesh *m;
        memset(v, 0, sizeof(v));
        v[0].pos[0]=0; v[0].pos[1]=0; v[1].pos[0]=2; v[1].pos[1]=0;
        v[2].pos[0]=2; v[2].pos[1]=2; v[3].pos[0]=0; v[3].pos[1]=2;
        m = hglCcbBuild(v, 4, idx, 6, 64);
        CHECK(m && hglCcbPatchCount(m) == 1,
              "ccb: quad de 2 tris conectados = 1 patch");
        CHECK(m && fabsf(m->centroid[0][0] - 1.0f) < 1e-5f &&
                   fabsf(m->centroid[0][1] - 1.0f) < 1e-5f,
              "ccb: centróide do patch ponderado por área é o centro real");
        hglCcbDestroy(m);
    }
    /* --- blobs disjuntos = patches distintos --------------------------- */
    {
        hglVertex v[8];
        uint32_t idx[12] = { 0,1,2, 0,2,3,   4,5,6, 4,6,7 };
        hglCcbMesh *m;
        memset(v, 0, sizeof(v));
        m = hglCcbBuild(v, 8, idx, 12, 64);
        CHECK(m && hglCcbPatchCount(m) == 2,
              "ccb: blobs disjuntos geram patches distintos");
        hglCcbDestroy(m);
    }
    /* --- chunk respeita maxPatchTris ----------------------------------- */
    {
        enum { COLS = 4 };
        hglVertex v[COLS * 2];
        uint32_t idx[COLS * 6];
        hglCcbMesh *m;
        int i, ok = 1, total = 0;
        memset(v, 0, sizeof(v));
        /* fita de COLS quads: coluna k usa verts {k,k+1,COLS+k,COLS+k+1} */
        for (i = 0; i < COLS; i++) {
            idx[i*6+0]=(uint32_t)i;          idx[i*6+1]=(uint32_t)(i+1);
            idx[i*6+2]=(uint32_t)(COLS+i+1); idx[i*6+3]=(uint32_t)i;
            idx[i*6+4]=(uint32_t)(COLS+i+1); idx[i*6+5]=(uint32_t)(COLS+i);
        }
        m = hglCcbBuild(v, COLS*2, idx, COLS*6, 3);
        CHECK(m && hglCcbPatchCount(m) > 1,
              "ccb: chunk divide componente grande");
        for (i = 0; i < hglCcbPatchCount(m); i++)
            if ((int)m->patches[i].count > 3) ok = 0;
        for (i = 0; i < hglCcbPatchCount(m); i++)
            total += (int)m->patches[i].count;
        CHECK(ok, "ccb: nenhum patch excede maxPatchTris");
        CHECK(total == COLS * 2, "ccb: todos os triângulos atribuídos");
        hglCcbDestroy(m);
    }
    /* --- frustum por patch: objeto fora inteiro não manda vértice ------ */
    {
        hglVertex va[24], vb[24];
        uint32_t ia[36], ib[36], all[72];
        hglVertex allv[48];
        hglCcbMesh *m;
        hglCtx *ctx = hglCreateContext(64, 64, 1);
        hglCcbStats st;

        make_cube(va, ia,  0, 0, -6, 1, 0, 1, 0, 0);   /* visível           */
        make_cube(vb, ib, 30, 0, -6, 1, 1, 0, 0, 24);  /* totalmente à dir. */
        memcpy(allv, va, sizeof(va));
        memcpy(allv + 24, vb, sizeof(vb));
        memcpy(all, ia, sizeof(ia));
        memcpy(all + 36, ib, sizeof(ib));

        hglMatrixMode(ctx, HGL_PROJECTION);
        hglLoadIdentity(ctx);
        hglPerspective(ctx, 60.0f * (float)HI_PI / 180.0f, 1.0f, 0.1f, 100.0f);
        hglMatrixMode(ctx, HGL_MODELVIEW);
        hglLoadIdentity(ctx);

        m = hglCcbBuild(allv, 48, all, 72, 2);
        hglClearColor4f(ctx, 0, 0, 0, 1);
        hglFrameBegin(ctx);
        hglDrawCcbMesh(ctx, m, allv);
        hglFrameEnd(ctx);
        hglCcbLastStats(ctx, &st);

        printf("     ccb frustum: patches=%d rejF=%d salvos=%d feitos=%d\n",
               st.patches, st.rejFrustum, st.vertsSaved, st.vertsDone);
        CHECK(st.rejFrustum == 6,
              "ccb: cubo externo é rejeitado patch a patch pelo frustum");
        /* externo: 6 faces × 4 verts = 24 salvos; interno: só a face frontal
           fica (as outras 5 caem pelo cone) → +20 salvos, 4 processados    */
        CHECK(st.vertsSaved == 44 && st.vertsDone == 4,
              "ccb: vértices dos patches rejeitados nunca saem do stream");
        CHECK(st.trisSubmitted == 2,
              "ccb: só os triângulos visíveis entram no pipeline");
        hglCcbDestroy(m);

        /* --- saída bit-idêntica ao caminho direto (mesma cena) ---------- */
        {
            const uint32_t *direct, *via;
            hglFrameBegin(ctx);
            hglDrawTrianglesIndexed(ctx, 72, all, allv);
            hglFrameEnd(ctx);
            direct = hglColorBuffer(ctx);

            m = hglCcbBuild(allv, 48, all, 72, 2);
            hglFrameBegin(ctx);
            hglDrawCcbMesh(ctx, m, allv);
            hglFrameEnd(ctx);
            via = hglColorBuffer(ctx);

            CHECK(memcmp(direct, via, 64 * 64 * sizeof(uint32_t)) == 0,
                  "ccb: saída bit-idêntica ao caminho direto");
            hglCcbDestroy(m);
        }
        hglDestroyContext(ctx);
    }
    /* --- cone de normais: faces traseiras caem em lote ------------------ */
    {
        hglVertex v[24];
        uint32_t idx[36];
        hglCcbMesh *m;
        hglCtx *ctx = hglCreateContext(32, 32, 1);
        hglCcbStats st;

        make_cube(v, idx, 0, 0, -5, 1, 1, 1, 1, 0);
        hglMatrixMode(ctx, HGL_PROJECTION);
        hglLoadIdentity(ctx);
        hglPerspective(ctx, 60.0f * (float)HI_PI / 180.0f, 1.0f, 0.1f, 100.0f);
        hglMatrixMode(ctx, HGL_MODELVIEW);
        hglLoadIdentity(ctx);

        m = hglCcbBuild(v, 24, idx, 36, 2);   /* 1 patch = 1 face plana      */
        hglClearColor4f(ctx, 0, 0, 0, 1);
        hglFrameBegin(ctx);
        hglDrawCcbMesh(ctx, m, v);
        hglFrameEnd(ctx);
        hglCcbLastStats(ctx, &st);
        printf("     ccb backface: patches=%d rejB=%d\n",
               st.patches, st.rejBackface);
        CHECK(st.rejBackface == 5,
              "ccb: só a face frontal sobrevive ao cone de normais");
        hglCcbDestroy(m);
        hglDestroyContext(ctx);
    }
}

extern void test_mapu(void);
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
    test_mipmap_chain();
    test_trilinear_levels();
    test_stencil();
    test_mapu();
    test_quat();
    test_dot3_bump();
    test_envmap();
    test_hiqtc_p8();
    test_ccb();
    if (run_sys_tests()) g_fail++;

    printf("\n%d verificações, %d falhas\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}