/* =====================================================================
 * ccb.c — Continuity Coherence Buffer (CCB completo, dossiê §2.2)
 *
 * Os meshes chegam à HDE com conectividade intacta. O CCB mantém um mapa
 * topológico da malha e elimina REGIÕES INTEIRAS (patches) fora do frustum
 * ou de costas, propagando a decisão pela fronteira do patch em vez de
 * testar cada polígono — vértices de patches rejeitados nunca saem do
 * stream (meta do dossiê: 40–60% economizados).
 *
 * Pipeline por draw (hglDrawCcbMesh):
 *   1. Frustum por PATCH: os 8 cantos do bbox model-space do patch são
 *      transformados por MV·P; se todos cairem fora do MESMO plano do
 *      frustum (near/far/left/right/top/bottom), o patch inteiro é
 *      rejeitado — sem morph, sem skinning, sem luz, sem setup.
 *   2. Backface por PATCH (cone de normais): construído na criação da malha
 *      (eixo = média ponderada por área; meia-abertura θ = pior desvio).
 *      Em view space, se dot(eixo, dirParaPatch) < -sin θ, TODO o patch
 *      aponta para longe da câmera e é descartado de uma vez.
 *   3. Patches aceitos emitem seus triângulos na ordem original através do
 *      pipeline normal (hi_geom_submit) — saída bit-idêntica ao caminho
 *      direto.
 *
 * Limitação documentada: bbox/cone usam as posições BASE do modelo
 * (pré-morph/skin). Alvos de morfing ou paletas que desloquem vértices
 * para fora do bbox base podem causar falso-negativo de culling visível;
 * use hglDrawTrianglesIndexed nessas malhas.
 * ===================================================================== */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "internal.h"

static float fminf_(float a, float b) { return a < b ? a : b; }
static float fmaxf_(float a, float b) { return a > b ? a : b; }

/* union-find simples */
typedef struct {
    uint32_t *p;
} Uf;

static void uf_init(Uf *u, int n)
{
    int i;
    u->p = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(n > 0 ? n : 1));
    for (i = 0; i < n; i++) u->p[i] = (uint32_t)i;
}
static void uf_free(Uf *u) { free(u->p); }
static uint32_t uf_find(Uf *u, uint32_t x)
{
    while (u->p[x] != x) {
        u->p[x] = u->p[u->p[x]];   /* compressão de caminho */
        x = u->p[x];
    }
    return x;
}
static void uf_union(Uf *u, uint32_t a, uint32_t b)
{
    uint32_t ra = uf_find(u, a), rb = uf_find(u, b);
    if (ra != rb) u->p[rb] = ra;
}

/* tabela hash de arestas (open addressing): chave = aresta não-dirigida,
   valor = primeiro triângulo que a registrou; aresta repetida une os dois */
typedef struct {
    uint64_t key;      /* (min<<32)|max dos índices da aresta            */
    uint32_t tri;
    uint32_t used;
} EdgeSlot;

typedef struct {
    EdgeSlot *slots;
    uint32_t mask;
} EdgeMap;

static void em_init(EdgeMap *m, int nExpectedEdges)
{
    uint32_t cap = 16;
    while (cap < (uint32_t)nExpectedEdges * 2u) cap <<= 1;
    m->slots = (EdgeSlot *)calloc(cap, sizeof(EdgeSlot));
    m->mask = cap - 1;
}
static void em_free(EdgeMap *m) { free(m->slots); }

static void em_link(EdgeMap *m, Uf *uf, uint32_t a, uint32_t b, uint32_t tri)
{
    uint64_t key = a < b ? ((uint64_t)a << 32 | b) : ((uint64_t)b << 32 | a);
    uint32_t h = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 33) & m->mask;
    while (m->slots[h].used && m->slots[h].key != key)
        h = (h + 1) & m->mask;
    if (!m->slots[h].used) {
        m->slots[h].used = 1;
        m->slots[h].key = key;
        m->slots[h].tri = tri;
    } else {
        uf_union(uf, m->slots[h].tri, tri);
    }
}

/* ------------------------------------------------------------- estrutura
   (definição de struct hglCcbMesh vive em internal.h p/ os testes)      */

/* face normal orientada por área do triângulo (unitária); len<=0 → falha */
static int tri_face_normal(const hglVertex *verts, uint32_t ia,
                           uint32_t ib, uint32_t ic, float out[3], float *area)
{
    const float *pa = verts[ia].pos;
    const float *pb = verts[ib].pos;
    const float *pc = verts[ic].pos;
    float ux = pb[0]-pa[0], uy = pb[1]-pa[1], uz = pb[2]-pa[2];
    float vx = pc[0]-pa[0], vy = pc[1]-pa[1], vz = pc[2]-pa[2];
    float nx_ = uy*vz - uz*vy;
    float ny_ = uz*vx - ux*vz;
    float nz_ = ux*vy - uy*vx;
    float len = sqrtf(nx_*nx_ + ny_*ny_ + nz_*nz_);
    if (len <= 1e-20f) return 0;
    nx_/=len; ny_/=len; nz_/=len;
    out[0]=nx_; out[1]=ny_; out[2]=nz_;
    if (area) *area = 0.5f * len;
    return 1;
}

/* --------------------------------------------------------------- build */
hglCcbMesh *hglCcbBuild(const hglVertex *verts, int nVerts,
                        const uint32_t *indices, int nIndex,
                        int maxPatchTris)
{
    hglCcbMesh *m;
    Uf uf;
    EdgeMap em;
    int nTri = nIndex / 3;
    int t, pb;

    if (nTri <= 0 || !verts || !indices) return NULL;
    if (maxPatchTris < 1) maxPatchTris = 1;

    m = (hglCcbMesh *)calloc(1, sizeof(hglCcbMesh));
    if (!m) return NULL;

    /* --- 1. conectividade: aresta compartilhada une triângulos --- */
    uf_init(&uf, nTri);
    em_init(&em, nTri * 3);
    for (t = 0; t < nTri; t++) {
        em_link(&em, &uf, indices[t*3+0], indices[t*3+1], (uint32_t)t);
        em_link(&em, &uf, indices[t*3+1], indices[t*3+2], (uint32_t)t);
        em_link(&em, &uf, indices[t*3+2], indices[t*3+0], (uint32_t)t);
    }
    em_free(&em);

    /* alocações máximas (nPatches ≤ nTri) */
    m->patches      = (HiCcbPatch *)malloc(sizeof(HiCcbPatch) * (size_t)nTri);
    m->bbox         = (float (*)[6])malloc(sizeof(float[6]) * (size_t)nTri);
    m->centroid     = (float (*)[3])malloc(sizeof(float[3]) * (size_t)nTri);
    m->coneAxis     = (float (*)[3])malloc(sizeof(float[3]) * (size_t)nTri);
    m->coneSin      = (float *)malloc(sizeof(float) * (size_t)nTri);
    m->nUniqueVerts = (int *)malloc(sizeof(int) * (size_t)nTri);
    m->idx          = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)nIndex);
    m->capPatches   = nTri;
    m->nPatches     = 0;
    m->nIdx         = 0;

    if (!m->patches || !m->bbox || !m->centroid || !m->coneAxis ||
        !m->coneSin || !m->nUniqueVerts || !m->idx) {
        uf_free(&uf);
        hglCcbDestroy(m);
        return NULL;
    }

    {
        /* stamp array p/ contagem de vértices únicos (sem memset O(n)/patch) */
        uint32_t *stamp = (uint32_t *)calloc(
            (size_t)(nVerts > 0 ? nVerts : 1), sizeof(uint32_t));
        uint32_t gen = 0;

        for (t = 0; t < nTri; ) {
            uint32_t root = uf_find(&uf, (uint32_t)t);
            int first = t, cnt = 0, i;
            float bb[6] = { 1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f };
            float cx=0, cy=0, cz=0, wsum=0;
            float ax=0, ay=0, az=0;
            float worstDot = 1.0f;         /* cos θ pior caso              */

            /* coleta o componente atual em chunk limitado por maxPatchTris */
            while (t < nTri && cnt < maxPatchTris &&
                   uf_find(&uf, (uint32_t)t) == root) {
                t++; cnt++;
            }

            pb = m->nPatches++;
            m->patches[pb].first = (uint32_t)first;
            m->patches[pb].count = (uint32_t)cnt;
            gen++;
            m->nUniqueVerts[pb] = 0;

            for (i = 0; i < cnt; i++) {
                uint32_t tri = (uint32_t)(first + i);
                uint32_t ia = indices[tri*3+0];
                uint32_t ib = indices[tri*3+1];
                uint32_t ic = indices[tri*3+2];
                const float *pa = verts[ia].pos;
                const float *qb = verts[ib].pos;
                const float *pc = verts[ic].pos;
                float nrm[3], area;

                m->idx[m->nIdx++] = ia;
                m->idx[m->nIdx++] = ib;
                m->idx[m->nIdx++] = ic;

                if (stamp[ia] != gen) { stamp[ia]=gen; m->nUniqueVerts[pb]++; }
                if (stamp[ib] != gen) { stamp[ib]=gen; m->nUniqueVerts[pb]++; }
                if (stamp[ic] != gen) { stamp[ic]=gen; m->nUniqueVerts[pb]++; }

                bb[0]=fminf_(bb[0],fminf_(pa[0],fminf_(qb[0],pc[0])));
                bb[1]=fminf_(bb[1],fminf_(pa[1],fminf_(qb[1],pc[1])));
                bb[2]=fminf_(bb[2],fminf_(pa[2],fminf_(qb[2],pc[2])));
                bb[3]=fmaxf_(bb[3],fmaxf_(pa[0],fmaxf_(qb[0],pc[0])));
                bb[4]=fmaxf_(bb[4],fmaxf_(pa[1],fmaxf_(qb[1],pc[1])));
                bb[5]=fmaxf_(bb[5],fmaxf_(pa[2],fmaxf_(qb[2],pc[2])));

                if (tri_face_normal(verts, ia, ib, ic, nrm, &area)) {
                    ax += nrm[0]*area; ay += nrm[1]*area; az += nrm[2]*area;
                    wsum += area;
                    cx += (pa[0]+qb[0]+pc[0])/3.0f*area;
                    cy += (pa[1]+qb[1]+pc[1])/3.0f*area;
                    cz += (pa[2]+qb[2]+pc[2])/3.0f*area;
                }
            }

            memcpy(m->bbox[pb], bb, sizeof(bb));

            if (wsum > 1e-20f) {
                float alen = sqrtf(ax*ax + ay*ay + az*az);
                ax/=alen; ay/=alen; az/=alen;
                /* meia-abertura θ: maior desvio facial do eixo             */
                for (i = 0; i < cnt; i++) {
                    uint32_t tri = (uint32_t)(first + i);
                    float nrm[3], area, d;
                    if (!tri_face_normal(verts, indices[tri*3+0],
                                         indices[tri*3+1],
                                         indices[tri*3+2], nrm, &area)) continue;
                    d = nrm[0]*ax + nrm[1]*ay + nrm[2]*az;
                    if (d < worstDot) worstDot = d;
                }
                m->coneAxis[pb][0]=ax; m->coneAxis[pb][1]=ay;
                m->coneAxis[pb][2]=az;
                {
                    float c = worstDot > 0.0f ? worstDot : 0.0f;
                    m->coneSin[pb] = sqrtf(1.0f - c*c);
                }
            } else {
                /* sem área útil: cone inválido ⇒ jamais culle por backface */
                m->coneAxis[pb][0]=0; m->coneAxis[pb][1]=0;
                m->coneAxis[pb][2]=0;
                m->coneSin[pb] = 2.0f;
            }
            if (wsum > 1e-20f) { cx /= wsum; cy /= wsum; cz /= wsum; }
            m->centroid[pb][0]=cx; m->centroid[pb][1]=cy;
            m->centroid[pb][2]=cz;
        }
        free(stamp);
    }

    uf_free(&uf);
    return m;
}

void hglCcbDestroy(hglCcbMesh *m)
{
    if (!m) return;
    free(m->idx);
    free(m->patches);
    free(m->bbox);
    free(m->centroid);
    free(m->coneAxis);
    free(m->coneSin);
    free(m->nUniqueVerts);
    free(m);
}

int hglCcbPatchCount(const hglCcbMesh *m)
{
    return m ? m->nPatches : 0;
}

/* ---------------------------------------------------------------- draw */
static void ctx_ccb_reset(hglCtx *ctx)
{
    ctx->ccbPatches = ctx->ccbRejFrustum = ctx->ccbRejBackface = 0;
    ctx->ccbVertsSaved = ctx->ccbVertsDone = 0;
}

void hglDrawCcbMesh(hglCtx *ctx, hglCcbMesh *m, const hglVertex *verts)
{
    int p, i;

    if (!ctx || !m || !verts) return;
    ctx_ccb_reset(ctx);

    for (p = 0; p < m->nPatches; p++) {
        const float *bb = m->bbox[p];
        int rejected = 0;

        /* ---- 1. frustum por patch: 8 cantos→clip, tudo-fora-de-um-plano - */
        {
            float cx_[8], cy_[8], cz[8], cw[8];
            int k, allNear=1, allFar=1, allL=1, allR=1, allB=1, allT=1;

            for (k = 0; k < 8; k++) {
                hiVec4 c;
                c.x = (k & 1) ? bb[3] : bb[0];
                c.y = (k & 2) ? bb[4] : bb[1];
                c.z = (k & 4) ? bb[5] : bb[2];
                c.w = 1.0f;
                c = hi_mat_xform(&ctx->mv, c);       /* model → view      */
                c = hi_mat_xform(&ctx->proj, c);     /* view  → clip      */
                cx_[k]=c.x; cy_[k]=c.y; cz[k]=c.z; cw[k]=c.w;
            }
            for (k = 0; k < 8; k++) {
                if (cw[k] > HI_CCB_EPS)  allNear = 0;
                if (cz[k] < cw[k])       allFar  = 0;
                if (cx_[k] > -cw[k])     allL    = 0;
                if (cx_[k] <  cw[k])     allR    = 0;
                if (cy_[k] > -cw[k])     allB    = 0;
                if (cy_[k] <  cw[k])     allT    = 0;
            }
            if (allNear || allFar || allL || allR || allB || allT) {
                ctx->ccbRejFrustum++;
                rejected = 1;
            }
        }

        /* ---- 2. backface por patch (cone de normais em view space) ----- */
        if (!rejected && m->coneSin[p] < 1.0f) {
            hiVec4 cen;
            hiVec3 axv, dv;
            cen.x=m->centroid[p][0]; cen.y=m->centroid[p][1];
            cen.z=m->centroid[p][2]; cen.w=1.0f;
            cen = hi_mat_xform(&ctx->mv, cen);

            if (cen.w > HI_CCB_EPS) {   /* à frente do near: teste válido    */
                axv = hi_v3_norm(hi_mat_dir(&ctx->mv,
                     hi_v3(m->coneAxis[p][0], m->coneAxis[p][1],
                           m->coneAxis[p][2])));
                dv = hi_v3_norm(hi_v3(cen.x, cen.y, cen.z)); /* câmera→patch */
                /* φ = ângulo(eixo, câmera→patch). Patch 100% de costas ⇔
                   φ + θ ≤ 90° ⇔ cos φ ≥ sen θ (todo normal aponta para
                   longe da câmera — nenhum pode virar a face).           */
                if (hi_v3_dot(axv, dv) >= m->coneSin[p]) {
                    ctx->ccbRejBackface++;
                    rejected = 1;
                }
            }
        }

        /* ---- 3. destino do patch ---------------------------------------- */
        ctx->ccbPatches++;
        if (rejected) {
            ctx->ccbVertsSaved += m->nUniqueVerts[p];
            continue;                      /* vértices NUNCA saem do stream */
        }
        ctx->ccbVertsDone += m->nUniqueVerts[p];

        for (i = 0; i < (int)m->patches[p].count; i++) {
            uint32_t tri = m->patches[p].first + (uint32_t)i;
            uint32_t id[3];
            hglVertex tv[3];
            id[0] = m->idx[tri*3+0];
            id[1] = m->idx[tri*3+1];
            id[2] = m->idx[tri*3+2];
            tv[0] = verts[id[0]];
            tv[1] = verts[id[1]];
            tv[2] = verts[id[2]];
            hi_geom_submit(ctx, tv, id);
        }
    }
}

void hglCcbLastStats(const hglCtx *ctx, hglCcbStats *out)
{
    if (!out) return;
    if (!ctx) { memset(out, 0, sizeof(*out)); return; }
    out->patches       = ctx->ccbPatches;
    out->rejFrustum    = ctx->ccbRejFrustum;
    out->rejBackface   = ctx->ccbRejBackface;
    out->trisSubmitted = ctx->statsTrisIn;
    out->vertsSaved    = ctx->ccbVertsSaved;
    out->vertsDone     = ctx->ccbVertsDone;
}