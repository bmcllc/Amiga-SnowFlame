/* =====================================================================
 * demo04 — stencil: reflexão planar no espelho-chão
 * 1) objeto normal  2) espelho grava stencil=1  3) objeto refletido
 *    desenhado com EQUAL 1 (só aparece "dentro" do espelho)
 * Gera build/demo04.png
 * ===================================================================== */
#include "common.h"

int main(void)
{
    const int W = 480, H = 360;
    const float PLANE_Y = -1.0f;
    hglCtx *ctx = hglCreateContext(W, H, 2);
    hglVertex mirror[4];
    uint32_t qidx[6] = { 0, 1, 2, 0, 2, 3 };
    hglVertex *cube;
    uint32_t *cidx;
    int nv, ni;
    hiMat4 rotY, tr, scl, model, refl, tmpA, tmpB, view;
    float ang = 0.55f;

    if (!ctx) return 1;

    dc_gen_cube(&cube, &cidx, &nv, &ni, 1.0f, 0.62f, 0.15f);

    /* --- espelho: quad horizontal --- */
    memset(mirror, 0, sizeof(mirror));
    {
        float S = 3.4f;
        float P[4][3] = {
            { -S, PLANE_Y, -S }, { S, PLANE_Y, -S },
            {  S, PLANE_Y,  S }, {-S, PLANE_Y,  S }
        };
        int i;
        for (i = 0; i < 4; i++) {
            mirror[i].pos[0] = P[i][0]; mirror[i].pos[1] = P[i][1];
            mirror[i].pos[2] = P[i][2];
            mirror[i].col[0] = 0.07f; mirror[i].col[1] = 0.20f;
            mirror[i].col[2] = 0.28f; mirror[i].col[3] = 1.0f;
        }
    }

    /* --- câmera/luz --- */
    hglViewport(ctx, 0, 0, W, H);
    hglClearColor4f(ctx, 0.02f, 0.04f, 0.08f, 1);
    hglMatrixMode(ctx, HGL_PROJECTION);
    hglLoadIdentity(ctx);
    hglPerspective(ctx, 50.0f * (float)HI_PI / 180.0f,
                   (float)W / (float)H, 0.1f, 50.0f);
    hglMatrixMode(ctx, HGL_MODELVIEW);
    {
        hiVec3 lw, lv;   /* guarda a VIEW para compor View·Model por objeto */
        hi_mat_lookat(&view, hi_v3(0.9f, 1.5f, 4.4f), hi_v3(0.0f, 0.15f, 0.0f),
                      hi_v3(0.0f, 1.0f, 0.0f));
        lw = hi_v3_norm(hi_v3(-0.4f, 0.85f, 0.30f));
        lv = hi_v3_norm(hi_mat_dir(&view, lw));
        hglLightDirf(ctx, lv.x, lv.y, lv.z);
    }
    hglLightColor4f(ctx, 1.0f, 0.97f, 0.9f, 1);
    hglAmbient4f(ctx, 0.26f, 0.27f, 0.31f, 1);
    hglEnable(ctx, HGL_LIGHTING);

    /* modelo do cubo flutuando acima do plano */
    hi_mat_rotate_y(&rotY, ang);
    hi_mat_scale(&scl, 0.95f, 0.95f, 0.95f);
    hi_mat_mul(&tmpA, &rotY, &scl);
    hi_mat_translate(&tr, 0.0f, 0.30f, 0.0f);
    hi_mat_mul(&model, &tr, &tmpA);

    /* reflexão sobre o plano y=PLANE_Y: y' = 2k − y */
    hi_mat_identity(&refl);
    refl.m[1][1] = -1.0f;
    refl.m[1][3] = 2.0f * PLANE_Y;

    hglFrameBegin(ctx);

    /* 1) cubo normal: MODELVIEW = View·Model */
    hglLoadMatrixf(ctx, &view.m[0][0]);
    hglMultMatrixf(ctx, &model.m[0][0]);
    hglDrawTrianglesIndexed(ctx, ni, cidx, cube);

    /* 2) espelho escreve stencil=1 (fragmentos atrás do cubo falham depth) */
    hglDisable(ctx, HGL_LIGHTING);
    hglLoadMatrixf(ctx, &view.m[0][0]);   /* espelho já está em coords de mundo */
    hglEnable(ctx, HGL_STENCIL_TEST);
    hglStencilFunc(ctx, HGL_ST_ALWAYS, 1);
    hglStencilOp(ctx, HGL_SO_REPLACE);
    hglDrawTrianglesIndexed(ctx, 6, qidx, mirror);

    /* 3) cubo refletido, mascarado pelo espelho: View·(Refl·Model) */
    hglEnable(ctx, HGL_LIGHTING);
    hi_mat_mul(&tmpB, &refl, &model);
    hglLoadMatrixf(ctx, &view.m[0][0]);
    hglMultMatrixf(ctx, &tmpB.m[0][0]);
    hglStencilFunc(ctx, HGL_ST_EQUAL, 1);
    hglStencilOp(ctx, HGL_SO_KEEP);
    hglDrawTrianglesIndexed(ctx, ni, cidx, cube);

    hglDisable(ctx, HGL_STENCIL_TEST);
    hglFrameEnd(ctx);

    if (hglSavePNG(ctx, "demo04.png") == 0) {
        int tin = 0, tout = 0;
        hglGetStats(ctx, &tin, &tout);
        printf("demo04: reflexao planar gravada (%d tris in / %d out)\n",
               tin, tout);
    }

    free(cube); free(cidx);
    hglDestroyContext(ctx);
    return 0;
}
