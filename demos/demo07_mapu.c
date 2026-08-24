/* =====================================================================
 * demo07 — simulação MAPU: projétil TRAJC vs esfera (RAYSP) + chão (TEVNT)
 *
 * O motor resolve ANALÍTICAMENTE (sem integração numérica):
 *   - TEVNT  → instante em que o projétil cruza y=0 (chão);
 *   - RAYSP  → interseção do raio canoa→alvo com a esfera inimiga;
 *   - TRAJC  → amostras da parábola para desenhar a curva.
 *
 * A PNG (`demo07.png`) mostra: céu, chão, curva da bola e a esfera alvo.
 * A saída textual compara previsão analítica com a posição visual do acerto.
 * ===================================================================== */
#include "common.h"
#include "hotice/mapu.h"

#define W 512
#define H 384

/* --- geometria 2D auxiliar (NDC) --------------------------------- */
static void line2(hglVertex *v, float x0, float y0, float x1, float y1,
                  float r, float g, float b, float thick)
{
    float dx = x1 - x0, dy = y1 - y0, L = sqrtf(dx*dx + dy*dy);
    if (L < 1e-6f) L = 1e-6f;
    float nx = -dy / L * thick, ny = dx / L * thick;
    v[0].pos[0] = x0 - nx; v[0].pos[1] = y0 - ny; v[0].pos[2] = -0.5f;
    v[1].pos[0] = x0 + nx; v[1].pos[1] = y0 + ny; v[1].pos[2] = -0.5f;
    v[2].pos[0] = x1 + nx; v[2].pos[1] = y1 + ny; v[2].pos[2] = -0.5f;
    v[3].pos[0] = x1 - nx; v[3].pos[1] = y1 - ny; v[3].pos[2] = -0.5f;
    int k;
    for (k = 0; k < 4; k++) {
        v[k].col[0] = r; v[k].col[1] = g; v[k].col[2] = b; v[k].col[3] = 1.0f;
        v[k].uv[0] = 0; v[k].uv[1] = 1;
    }
}

int main(void)
{
    hglCtx *ctx = hglCreateContext(W, H, 2);
    uint32_t sky[64], *skyrow;
    hglTex *skybox;
    int i;
    MapuVec3 ro, v0, g, ogro, dir;
    MapuSphere alvo;
    MapuRay ray;
    MapuVec3 nhit;
    MapuScalar t_chao, dt;
    float SCALE = 0.32f;         /* metros → NDC */

    /* céu degradê (textura 8x8) */
    skyrow = sky;
    for (i = 0; i < 8; i++)
        skyrow[i] = hi_pack_rgba8(30, 60 + 6*i, 120 + 4*i, 255);
    for (i = 8; i < 64; i++) sky[i] = sky[i % 8];
    skybox = hglTexCreateRGBA8(8, 8, sky);

    /* cena em Q16.16 (metros) — visão lateral (x horizontal, y vertical) */
    ro   = mapu_v3f(-3.0f, 1.5f, 0.0f);
    v0   = mapu_v3f(4.0f, 7.0f, 0.0f);
    g    = mapu_v3f(0.0f, -9.8f, 0.0f);
    ogro = mapu_v3f(1.2f, 0.6f, 0.0f);
    alvo = (MapuSphere){ ogro, mapu_f2q(0.4f) };

    /* ---- previsão analítica MAPU ------------------------------- */
    t_chao = mapu_tevnt_ground_q16(ro, v0, g.y, NULL);
    dir = mapu_vadd(ogro, mapu_vscale((MapuScalar)-MAPU_ONE, ro));
    /* normaliza direção: dir / |dir| em Q16 */
    {
        double lx = mapu_q2f(dir.x), ly = mapu_q2f(dir.y), lz = mapu_q2f(dir.z);
        double ln = sqrt(lx*lx + ly*ly + lz*lz);
        ray.o = ro;
        ray.d = mapu_v3f((float)(lx/ln), (float)(ly/ln), (float)(lz/ln));
    }
    dt = mapu_ray_sphere_q16(ray, alvo, &nhit);

    /* ---- renderização (proj=ident, pos=NDC direto) --------------- */
    hglViewport(ctx, 0, 0, W, H);
    hglClearColor4f(ctx, 0.05f, 0.07f, 0.10f, 1);
    hglMatrixMode(ctx, HGL_PROJECTION); hglLoadIdentity(ctx);
    hglMatrixMode(ctx, HGL_MODELVIEW); hglLoadIdentity(ctx);
    hglEnable(ctx, HGL_TEXTURE_2D);
    hglBindTexture(ctx, 0, skybox);

    /* céu (fullscreen quad) */
    {
        hglVertex q[4];
        uint32_t qi[6] = { 0,1,2, 0,2,3 };
        for (i = 0; i < 4; i++) {
            int sx = (i==1||i==2)? 1:-1, sy=(i>=2)? 1:-1;
            q[i].pos[0]=sx; q[i].pos[1]=sy; q[i].pos[2]=-0.9f;
            q[i].col[0]=q[i].col[1]=q[i].col[2]=1; q[i].col[3]=1;
            q[i].uv[0]=(i==0||i==3)?0:1; q[i].uv[1]=(i<2)?0:1;
        }
        hglFrameBegin(ctx);
        hglDrawTrianglesIndexed(ctx, 6, qi, q);
        hglDisable(ctx, HGL_TEXTURE_2D);
    }

    /* chão (linha cinza em y=0) */
    {
        hglVertex v[4]; uint32_t qi[6]={0,1,2,0,2,3};
        line2(v, -3.f*SCALE, 0.f, 3.f*SCALE, 0.f, 0.25f,0.27f,0.30f, 0.004f);
        hglDrawTrianglesIndexed(ctx, 6, qi, v);
    }

    /* curva TRAJC (amostras), cor amarela */
    {
        int N = 48;
        hglVertex *va = malloc(sizeof(hglVertex)*N*4);
        uint32_t qi[6]={0,1,2,0,2,3};
        MapuScalar span = t_chao;
        int s;
        MapuVec3 prev = {0,0,0}; int has_prev = 0;
        for (s = 0; s < N; s++) {
            MapuScalar t = mapu_mul(span, mapu_div((MapuScalar)(s * MAPU_ONE),
                                                  (MapuScalar)((N-1)*MAPU_ONE)));
            MapuVec3 p = mapu_trajc_q16(ro, v0, g, t);
            float x = (float)mapu_q2f(p.x) * SCALE;
            float y = (float)mapu_q2f(p.y) * SCALE;
            if (has_prev) {
                line2(va, (float)mapu_q2f(prev.x)*SCALE,
                           (float)mapu_q2f(prev.y)*SCALE, x, y,
                           1.0f, 0.8f, 0.2f, 0.003f);
                hglDrawTrianglesIndexed(ctx, 6, qi, va);
            }
            prev = p; has_prev = 1;
        }
        free(va);
        (void)dt;
    }

    /* esfera alvo (sprite branco) */
    {
        hglVertex v[4]; uint32_t qi[6]={0,1,2,0,2,3};
        float cx = (float)mapu_q2f(ogro.x)*SCALE, cy = (float)mapu_q2f(ogro.y)*SCALE;
        float r = (float)mapu_q2f(alvo.r)*SCALE;
        for (i = 0; i < 4; i++) {
            int sx=(i==1||i==2?1:-1), sy=(i>=2?1:-1);
            v[i].pos[0]=cx+sx*r; v[i].pos[1]=cy+sy*r; v[i].pos[2]=-0.2f;
            v[i].col[0]=0.25f; v[i].col[1]=0.9f; v[i].col[2]=0.3f; v[i].col[3]=1;
            v[i].uv[0]=0; v[i].uv[1]=1;
        }
        hglDrawTrianglesIndexed(ctx, 6, qi, v);
    }

    /* ponto de acerto (RAISG) se houver */
    if (dt > 0) {
        MapuVec3 h = mapu_vadd(ray.o, mapu_vscale(dt, ray.d));
        hglVertex v[4]; uint32_t qi[6]={0,1,2,0,2,3};
        float x=(float)mapu_q2f(h.x)*SCALE, y=(float)mapu_q2f(h.y)*SCALE;
        for (i = 0; i < 4; i++) {
            int sx=(i==1||i==2?1:-1), sy=(i>=2?1:-1);
            v[i].pos[0]=x+sx*0.04f; v[i].pos[1]=y+sy*0.04f; v[i].pos[2]=-0.1f;
            v[i].col[0]=1; v[i].col[1]=0.2f; v[i].col[2]=0.2f; v[i].col[3]=1;
            v[i].uv[0]=0; v[i].uv[1]=1;
        }
        hglDrawTrianglesIndexed(ctx, 6, qi, v);
    }

    hglSavePNG(ctx, "demo07.png");

    {
        MapuVec3 hit = mapu_trajc_q16(ro, v0, g, t_chao);
        printf("demo07 MAPU: chão em t=%.3f s (x=%.3f) | esfera dt=%.3f%s\n",
               mapu_q2f(t_chao), mapu_q2f(hit.x),
               dt > 0 ? mapu_q2f(dt) : -1.0,
               dt > 0 ? " → acertou alvo" : " → erra (passou ao lado)");
    }
    hglTexDestroy(skybox);
    hglDestroyContext(ctx);
    return 0;
}
