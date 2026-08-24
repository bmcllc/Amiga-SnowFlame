/* =====================================================================
 * demo01 — TBR + HDE: esfera morfando sobre chão HIQTC, com CAA 2x
 * Gera build/demo01.png
 * ===================================================================== */
#include "common.h"

int main(void)
{
    const int W = 512, H = 384;
    hglCtx *ctx = hglCreateContext(W, H, 2);
    hglVertex ground[4];
    uint32_t gidx[6] = { 0, 1, 2, 0, 2, 3 };
    hglVertex *sph;
    uint32_t *six;
    int nv, ni;
    float *dent;
    uint32_t *chk;
    hglTex *floorTex, *sphTex;

    if (!ctx) return 1;

    /* --- chão: tabuleiro comprimido com HIQTC (4:1) --- */
    chk = dc_checker(256, 256, 8, 190, 210, 235, 235, 240, 248);
    floorTex = hglTexCreateHIQTCFromRGBA8(256, 256, chk);
    hglTexGenerateMipmaps(floorTex); /* minificação com trilinear */
    free(chk);
    if (!floorTex) { printf("falha ao criar textura HIQTC\n"); return 1; }

    memset(ground, 0, sizeof(ground));
    {
        float S = 7.0f, Y = -1.05f;
        float P[4][3] = {
            { -S, Y, -S }, { S, Y, -S }, { S, Y, S }, { -S, Y, S }
        };
        float UV[4][2] = { { 0, 0 }, { 6, 0 }, { 6, 6 }, { 0, 6 } };
        int i;
        for (i = 0; i < 4; i++) {
            ground[i].pos[0] = P[i][0]; ground[i].pos[1] = P[i][1];
            ground[i].pos[2] = P[i][2];
            ground[i].nrm[1] = 1.0f;    /* normal do chão para cima */
            ground[i].uv[0] = UV[i][0]; ground[i].uv[1] = UV[i][1];
            ground[i].col[0] = ground[i].col[1] = ground[i].col[2] = 1.0f;
            ground[i].col[3] = 1.0f;
        }
    }

    /* --- esfera base + alvo de morfing (HDE) --- */
    dc_gen_sphere(28, 44, &sph, &six, &nv, &ni);
    dent = dc_dent_targets(sph, nv, 0.42f);

    /* --- câmera e luz --- */
    hglViewport(ctx, 0, 0, W, H);
    hglClearColor4f(ctx, 0.06f, 0.07f, 0.12f, 1.0f);
    hglClearDepth(ctx, 1.0f);

    hglMatrixMode(ctx, HGL_PROJECTION);
    hglLoadIdentity(ctx);
    hglPerspective(ctx, 50.0f * (float)HI_PI / 180.0f,
                   (float)W / (float)H, 0.1f, 60.0f);

    /* VIEW fixa da cena; cada objeto compõe MODELVIEW = View·Model */
    {
        hiMat4 view;
        hiVec3 lw, lv;
        hi_mat_lookat(&view, hi_v3(2.7f, 2.3f, 3.9f), hi_v3(0.0f, 1.15f, 0.0f),
                      hi_v3(0.0f, 1.0f, 0.0f));
        hglLoadMatrixf(ctx, &view.m[0][0]);
        /* luz definida no mundo -> espaço da câmera (regra do HGL) */
        lw = hi_v3_norm(hi_v3(-0.45f, 0.80f, 0.35f));
        lv = hi_v3_norm(hi_mat_dir(&view, lw));
        hglLightDirf(ctx, lv.x, lv.y, lv.z);
    }
    hglLightColor4f(ctx, 1.0f, 0.96f, 0.88f, 1.0f);
    hglAmbient4f(ctx, 0.30f, 0.31f, 0.36f, 1.0f);

    hglEnable(ctx, HGL_LIGHTING);
    hglEnable(ctx, HGL_TEXTURE_2D);

    /* ================= frame ================= */
    hglFrameBegin(ctx);

    hglBindTexture(ctx, 0, floorTex);
    hglDrawTrianglesIndexed(ctx, 6, gidx, ground);   /* MV = View */

    /* textura própria da esfera: gradiente frio (não confundir com o chão) */
    {
        uint32_t *sp = malloc(sizeof(uint32_t) * 64 * 64);
        int sx, sy;
        for (sy = 0; sy < 64; sy++)
            for (sx = 0; sx < 64; sx++) {
                float u = sx / 63.0f, v = sy / 63.0f;
                sp[sy * 64 + sx] =
                    hi_pack_rgba8((int)(60 + 120 * u), (int)(90 + 110 * v),
                                  235, 255);
            }
        sphTex = hglTexCreateRGBA8(64, 64, sp);
        free(sp);
    }
    hglBindTexture(ctx, 0, sphTex);

    /* esfera morfando (t=0.62) girada: MV = View·Model */
    hglBindMorphTarget(ctx, 0, dent, nv);
    hglMorphWeight(ctx, 0, 0.62f);
    {
        hiMat4 rot, tr, sc, tmp, model;
        hi_mat_rotate_y(&rot, 0.6f);
        hi_mat_scale(&sc, 1.1f, 1.1f, 1.1f);
        hi_mat_mul(&tmp, &rot, &sc);          /* R·S */
        hi_mat_translate(&tr, 0.0f, 0.25f, 0.0f);
        hi_mat_mul(&model, &tr, &tmp);        /* T·R·S */
        hglMultMatrixf(ctx, &model.m[0][0]);
    }
    hglDrawTrianglesIndexed(ctx, ni, six, sph);
    hglBindTexture(ctx, 0, floorTex);   /* restaura p/ próximas passadas */
    hglFrameEnd(ctx);

    if (hglSavePNG(ctx, "demo01.png") == 0) {
        int tin = 0, tout = 0;
        hglGetStats(ctx, &tin, &tout);
        printf("demo01: %d tris entrada · %d pós-clip -> demo01.png\n", tin, tout);
    } else
        printf("demo01: falha ao gravar PNG\n");

    hglTexDestroy(floorTex);
    hglTexDestroy(sphTex);
    free(sph); free(six); free(dent);
    hglDestroyContext(ctx);
    return 0;
}
