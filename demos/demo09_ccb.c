/* =====================================================================
 * demo09_ccb.c — CCB: culling por continuidade (dossiê §2.2)
 *
 * Sala com 4 pilares: 2 dentro do frustum, 2 totalmente fora. A cena é
 * renderizada duas vezes — caminho direto e caminho CCB (patches
 * topológicos descartados inteiros por frustum + cone de normais) — e os
 * buffers são comparados bit a bit. Métricas impressas:
 *   - patches avaliados / rejeitados por frustum / por backface
 *   - % de vértices que NUNCA saem do stream (meta do dossiê: 40–60%)
 *   - igualdade bit-idêntica das imagens
 * ===================================================================== */
#include <stdio.h>
#include <string.h>
#include "../include/hotice/hgl.h"
#include "../include/hotice/types.h"

static void make_cube(hglVertex *v, uint32_t *idx,
                      float cx, float cy, float cz, float s,
                      float r, float g, float b, uint32_t base)
{
    static const float N[6][3] = {
        {0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}
    };
    static const float F[6][4][3] = {
        {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},
        {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}},
        {{ 1,-1, 1},{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1}},
        {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}},
        {{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1},{-1, 1,-1}},
        {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}},
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
        idx[f*6+0]=base+(uint32_t)f*4+0; idx[f*6+1]=base+(uint32_t)f*4+1;
        idx[f*6+2]=base+(uint32_t)f*4+2; idx[f*6+3]=base+(uint32_t)f*4+0;
        idx[f*6+4]=base+(uint32_t)f*4+2; idx[f*6+5]=base+(uint32_t)f*4+3;
    }
}

enum { W = 320, H = 240 };

int main(void)
{
    hglCtx *ctx = hglCreateContext(W, H, 4);
    hglVertex pv[4][24], fv[4];
    uint32_t pidx[4][36], fidx[6] = { 0,1,2, 0,2,3 };
    hglCcbMesh *pillar[4], *floorMesh;
    hglCcbStats st, acc;
    const uint32_t *direct, *via;
    int i, identical;
    float savedPct;

    /* --- câmera na origem olhando -z, fov 60° -------------------------- */
    hglMatrixMode(ctx, HGL_PROJECTION);
    hglLoadIdentity(ctx);
    hglPerspective(ctx, 60.0f * (float)HI_PI / 180.0f,
                   (float)W / (float)H, 0.1f, 100.0f);
    hglMatrixMode(ctx, HGL_MODELVIEW);
    hglLoadIdentity(ctx);
    hglLookAt(ctx, 0, 1.5f, 0,  0, 1, -8,  0, 1, 0);

    /* --- cena: chão + 4 pilares (os externos fora do frustum) ---------- */
    make_cube(pv[0], pidx[0], -2.2f, 0, -8, 1, 0.9f, 0.4f, 0.2f, 0);
    make_cube(pv[1], pidx[1],  2.2f, 0, -8, 1, 0.2f, 0.5f, 0.9f, 0);
    make_cube(pv[2], pidx[2], -9.0f, 0, -8, 1, 0.7f, 0.7f, 0.2f, 0);
    make_cube(pv[3], pidx[3],  9.0f, 0, -8, 1, 0.2f, 0.7f, 0.4f, 0);

    memset(fv, 0, sizeof(fv));
    fv[0].pos[0]=-14; fv[0].pos[1]=-1; fv[0].pos[2]=  0;
    fv[1].pos[0]= 14; fv[1].pos[1]=-1; fv[1].pos[2]=  0;
    fv[2].pos[0]= 14; fv[2].pos[1]=-1; fv[2].pos[2]=-16;
    fv[3].pos[0]=-14; fv[3].pos[1]=-1; fv[3].pos[2]=-16;
    for (i = 0; i < 4; i++) {
        fv[i].nrm[1] = 1;
        fv[i].col[0] = 0.25f; fv[i].col[1] = 0.28f;
        fv[i].col[2] = 0.33f; fv[i].col[3] = 1;
    }

    pillar[0] = hglCcbBuild(pv[0], 24, pidx[0], 36, 2); /* 1 patch = face */
    pillar[1] = hglCcbBuild(pv[1], 24, pidx[1], 36, 2);
    pillar[2] = hglCcbBuild(pv[2], 24, pidx[2], 36, 2);
    pillar[3] = hglCcbBuild(pv[3], 24, pidx[3], 36, 2);
    floorMesh = hglCcbBuild(fv, 4, fidx, 6, 64);

    /* --- passe 1: caminho direto (referência) --------------------------- */
    hglClearColor4f(ctx, 0.06f, 0.07f, 0.10f, 1);
    hglFrameBegin(ctx);
    hglDrawTrianglesIndexed(ctx, 6, fidx, fv);
    for (i = 0; i < 4; i++)
        hglDrawTrianglesIndexed(ctx, 36, pidx[i], pv[i]);
    hglFrameEnd(ctx);
    direct = hglColorBuffer(ctx);

    /* copia p/ comparação */
    {
        static uint32_t ref[W * H];
        memcpy(ref, direct, sizeof(ref));

        /* --- passe 2: caminho CCB --------------------------------------- */
        memset(&acc, 0, sizeof(acc));
        hglFrameBegin(ctx);
        hglDrawCcbMesh(ctx, floorMesh, fv);
        hglCcbLastStats(ctx, &st);
        acc.patches += st.patches; acc.rejFrustum += st.rejFrustum;
        acc.rejBackface += st.rejBackface;
        acc.vertsSaved += st.vertsSaved; acc.vertsDone += st.vertsDone;
        for (i = 0; i < 4; i++) {
            hglDrawCcbMesh(ctx, pillar[i], pv[i]);
            hglCcbLastStats(ctx, &st);
            acc.patches += st.patches; acc.rejFrustum += st.rejFrustum;
            acc.rejBackface += st.rejBackface;
            acc.vertsSaved += st.vertsSaved; acc.vertsDone += st.vertsDone;
        }
        hglFrameEnd(ctx);
        via = hglColorBuffer(ctx);

        identical = (memcmp(ref, via, sizeof(ref)) == 0);
        savedPct = 100.0f * (float)acc.vertsSaved /
                   (float)(acc.vertsSaved + acc.vertsDone);

        printf("demo09 CCB (dossiê §2.2):\n");
        printf("  patches avaliados ......... %d\n", acc.patches);
        printf("  rejeitados pelo frustum ... %d\n", acc.rejFrustum);
        printf("  rejeitados por backface ... %d\n", acc.rejBackface);
        printf("  vértices poupados ......... %d de %d (%.1f%%)"
               " — meta do dossiê: 40–60%%\n",
               acc.vertsSaved, acc.vertsSaved + acc.vertsDone, savedPct);
        printf("  saída bit-idêntica ........ %s\n",
               identical ? "SIM" : "NÃO");
    }

    hglSavePNG(ctx, "build/demo09.png");

    for (i = 0; i < 4; i++) hglCcbDestroy(pillar[i]);
    hglCcbDestroy(floorMesh);
    hglDestroyContext(ctx);
    return identical && savedPct >= 40.0f ? 0 : 1;
}