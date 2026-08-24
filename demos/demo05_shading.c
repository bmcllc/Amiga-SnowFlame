/* =====================================================================
 * demo05 — DOT3 bump mapping + environment (sphere) mapping
 *
 * Composição horizontal:
 *   [quadro esquerdo]  parede tijolo SEM bump · esfera cromada OFF
 *   [quadro direito ]  mesma parede COM bump   · esfera cromada ON
 *
 * Métrica: quantos tons distintos a parede produz (luz por pixel gera
 * variação que a Gouraud plana não consegue representar).
 * ===================================================================== */
#include "common.h"

static uint32_t brick_albedo[64 * 64];
static uint32_t brick_nmap[64 * 64];

static void build_assets(void)
{
    int x, y;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            int mortar = (((x / 11) + (y / 15)) & 1) && (x % 11 < 2) && (y % 15 < 3);
            brick_albedo[y * 64 + x] =
                mortar ? hi_pack_rgba8(200, 200, 205, 255)
                       : hi_pack_rgba8(198, 95, 75, 255);
            brick_nmap[y * 64 + x] =
                mortar
                    ? hi_pack_rgba8(128, 128, 255, 255)
                    : hi_pack_rgba8(128, 128, 210, 255);
        }
}

static uint32_t env_pixel(int u, int v)
{
    float fu = (float)u / 64.0f, fv = (float)v / 64.0f;
    float r = 90 + 150 * fu;
    float g = 95 + 150 * fv;
    float b = 110 + 145 * (1.0f - fu * fv);
    return hi_pack_rgba8((int)r, (int)g, (int)b, 255);
}

static hglCtx *render(int W, int H, int with_bump, int with_env)
{
    hglCtx *ctx = hglCreateContext(W, H, 2);
    hglTex *albtex, *nmTex, *envTex;
    uint32_t envpix[64 * 64];
    hglVertex wall[4];
    uint32_t widx[6] = { 0, 1, 2, 0, 2, 3 };
    hglVertex *cube, *sp;
    uint32_t *cidx;
    int cnv, cni, i;
    hiMat4 view, model, tr, rot, scl, tmp, mv;

    albtex = hglTexCreateRGBA8(64, 64, brick_albedo);
    nmTex  = hglTexCreateRGBA8(64, 64, brick_nmap);
    for (i = 0; i < 64 * 64; i++)
        envpix[i] = env_pixel(i % 64, i / 64);
    envTex = hglTexCreateRGBA8(64, 64, envpix);

    memset(wall, 0, sizeof(wall));
    {
        float S = 4.2f;
        float P[4][3] = { { -S, -0.9f, -0.5f }, { S, -0.9f, -0.5f },
                          { S,  0.9f, -5.0f }, { -S, 0.9f, -5.0f } };
        float UV[4][2] = { { 0, 0 }, { 5, 0 }, { 5, 3 }, { 0, 3 } };
        for (i = 0; i < 4; i++) {
            wall[i].pos[0] = P[i][0]; wall[i].pos[1] = P[i][1];
            wall[i].pos[2] = P[i][2];
            wall[i].nrm[1] = 1.0f;
            wall[i].uv[0] = UV[i][0]; wall[i].uv[1] = UV[i][1];
            wall[i].col[0] = 1; wall[i].col[1] = 1;
            wall[i].col[2] = 1; wall[i].col[3] = 1;
        }
    }

    hglViewport(ctx, 0, 0, W, H);
    hglClearColor4f(ctx, 0.07f, 0.08f, 0.12f, 1);
    hglClearDepth(ctx, 1.0f);
    hglMatrixMode(ctx, HGL_PROJECTION);
    hglLoadIdentity(ctx);
    hglPerspective(ctx, 46.0f * (float)HI_PI / 180.0f, (float)W / (float)H,
                   0.1f, 60.0f);
    hglMatrixMode(ctx, HGL_MODELVIEW);
    hi_mat_lookat(&view, hi_v3(0.0f, 0.3f, 3.2f), hi_v3(0, 0, -3),
                  hi_v3(0, 1, 0));
    hglLightDirf(ctx, 0.6f, 0.9f, 0.3f);
    hglLightColor4f(ctx, 1, 1, 1, 1);
    hglAmbient4f(ctx, 0.18f, 0.19f, 0.24f, 1);

    dc_gen_cube(&cube, &cidx, &cnv, &cni, 0.55f, 0.55f, 0.55f);
    sp = (hglVertex *)malloc(sizeof(hglVertex) * (size_t)cnv);
    for (i = 0; i < cnv; i++) {
        sp[i] = cube[i];
        sp[i].col[0] = 0.18f; sp[i].col[1] = 0.20f; sp[i].col[2] = 0.24f;
    }
    hi_mat_rotate_y(&rot, 0.6f);
    hi_mat_scale(&scl, 1.0f, 1.0f, 1.0f);
    hi_mat_mul(&tmp, &rot, &scl);
    hi_mat_translate(&tr, 0.0f, 0.2f, -3.0f);
    hi_mat_mul(&model, &tr, &tmp);
    hi_mat_mul(&mv, &view, &model);

    hglFrameBegin(ctx);

    /* parede */
    hglEnable(ctx, HGL_LIGHTING);
    hglBindTexture(ctx, 0, albtex);
    if (with_bump) {
        hglEnable(ctx, HGL_BUMP_DOT3);
        hglBindBumpTexture(ctx, nmTex);
    }
    hglLoadMatrixf(ctx, &view.m[0][0]);
    hglDrawTrianglesIndexed(ctx, 6, widx, wall);
    if (with_bump) hglDisable(ctx, HGL_BUMP_DOT3);

    /* esfera cromada sobre a parede */
    hglLoadMatrixf(ctx, &mv.m[0][0]);
    hglDisable(ctx, HGL_LIGHTING);
    if (with_env) {
        hglEnable(ctx, HGL_ENVMAP);
        hglBindEnvTexture(ctx, envTex);
    }
    hglDrawTrianglesIndexed(ctx, cni, cidx, sp);
    if (with_env) hglDisable(ctx, HGL_ENVMAP);

    hglFrameEnd(ctx);

    free(cube); free(cidx); free(sp);
    hglTexDestroy(albtex);
    hglTexDestroy(nmTex);
    hglTexDestroy(envTex);
    return ctx;
}

int main(void)
{
    const int W = 384, H = 256;
    hglCtx *off, *on;
    const uint32_t *po, *pn;
    uint32_t out[(size_t)768 * 256];
    int x, y;
    static uint8_t seen[1u << 24];
    int tones[2] = { 0, 0 }, pass;

    build_assets();
    off = render(W, H, 0, 0);
    on  = render(W, H, 1, 1);
    po = hglColorBuffer(off);
    pn = hglColorBuffer(on);

    for (y = 0; y < H; y++) {
        memcpy(out + (size_t)y * 768,     po + (size_t)y * W, W * 4);
        memcpy(out + (size_t)y * 768 + W, pn + (size_t)y * W, W * 4);
    }
    hglSavePNGBuffer("demo05.png", 768, H, out);

    for (pass = 0; pass < 2; pass++) {
        const uint32_t *px = pass == 0 ? po : pn;
        int y2;
        memset(seen, 0, 1u << 24);
        for (y2 = 50; y2 < H - 50; y2++)
            for (x = 0; x < W; x++) {
                uint32_t c = px[(size_t)y2 * W + x];
                int k = ((c & 255) << 16)
                        | (((c >> 8) & 255) << 8)
                        | ((c >> 16) & 255);
                if (!seen[k]) { seen[k] = 1; tones[pass]++; }
            }
    }
    printf("demo05: tons distintos na parede  bump OFF=%d  bump ON=%d  "
           "(luz por pixel gera mais variação de cor)\n",
           tones[0], tones[1]);

    hglDestroyContext(off);
    hglDestroyContext(on);
    (void)x;
    return 0;
}
