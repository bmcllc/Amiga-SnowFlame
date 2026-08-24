/* =====================================================================
 * hde.c — Homotopy Deformation Engine (referência em software)
 *
 * Estágio de geometria: morfing contínuo → skinning → modelview →
 * iluminação por vértice → projeção → clip do near plane → viewport.
 * É o "modelo de ouro" que o silício real implementaria em hardware.
 * ===================================================================== */
#include <stdlib.h>
#include <string.h>
#include "internal.h"

#define HI_NEAR_EPS 1e-3f

void hi_hde_vertex(hglCtx *ctx, const hglVertex *in, uint32_t vid,
                   hiGeomVert *out)
{
    hiVec3 p = hi_v3(in->pos[0], in->pos[1], in->pos[2]);
    hiVec3 n = hi_v3_norm(hi_v3(in->nrm[0], in->nrm[1], in->nrm[2]));
    float colR = in->col[0], colG = in->col[1], colB = in->col[2], colA = in->col[3];
    hiVec4 world, clip;
    int ch, k;

    /* --- homotopia de morfing: p(t) = p + Σ t_ch·(alvo_ch − p) --- */
    for (ch = 0; ch < 4; ch++) {
        float t = ctx->morphWeight[ch];
        if (t != 0.0f && ctx->morphTarget[ch] && vid < (uint32_t)ctx->morphCount[ch]) {
            const float *tp = ctx->morphTarget[ch] + (size_t)vid * 3;
            p.x += t * (tp[0] - p.x);
            p.y += t * (tp[1] - p.y);
            p.z += t * (tp[2] - p.z);
        }
    }

    /* --- skinning: mistura ponderada de até 4 ossos da palette --- */
    if ((ctx->capSkinning) && ctx->skinPal && ctx->skinBones > 0) {
        hiVec3 pp = hi_v3(0, 0, 0), nn = hi_v3(0, 0, 0);
        for (k = 0; k < 4; k++) {
            int b = (int)(in->bone[k] + 0.5f);
            float wgt = in->bw[k];
            hiMat4 bone;
            hiVec3 bp, bn;
            if (wgt <= 0.0f || b < 0 || b >= ctx->skinBones) continue;
            memcpy(&bone, ctx->skinPal + (size_t)b * 16, sizeof(bone));
            bp = hi_mat_dir(&bone, p); /* parte 3x3 + translação manual */
            bp.x += bone.m[0][3]; bp.y += bone.m[1][3]; bp.z += bone.m[2][3];
            bn = hi_v3_norm(hi_mat_dir(&bone, n));
            pp = hi_v3_add(pp, hi_v3_scale(bp, wgt));
            nn = hi_v3_add(nn, hi_v3_scale(bn, wgt));
        }
        p = pp;
        n = hi_v3_norm(nn);
    }

    /* --- modelview --- */
    world = hi_mat_xform(&ctx->mv, hi_v4(p.x, p.y, p.z, 1.0f));

    /* --- iluminação difusa por vértice (Gouraud, como o HDE faria).
       SEMÂNTICA: lightDir APONTA PARA A LUZ e é dado em ESPAÇO DA CÂMERA
       (as demos transformam a direção do mundo pela rotação da view). --- */
    if (ctx->capLighting) {
        hiVec3 nw = hi_v3_norm(hi_mat_dir(&ctx->mv, n));
        hiVec3 L = hi_v3_norm(hi_v3(ctx->lightDir[0], ctx->lightDir[1],
                                    ctx->lightDir[2]));
        float ndl = hi_v3_dot(nw, L);
        if (ndl < 0.0f) ndl = 0.0f;
        colR = in->col[0] * (ctx->ambient[0] + ctx->lightCol[0] * ndl);
        colG = in->col[1] * (ctx->ambient[1] + ctx->lightCol[1] * ndl);
        colB = in->col[2] * (ctx->ambient[2] + ctx->lightCol[2] * ndl);
        colA = in->col[3] * ctx->ambient[3];
        if (colR > 1) colR = 1;
        if (colG > 1) colG = 1;
        if (colB > 1) colB = 1;
    }

    /* --- projeção --- */
    clip = hi_mat_xform(&ctx->proj, world);

    out->cx = clip.x; out->cy = clip.y; out->cz = clip.z; out->cw = clip.w;
    out->u = in->uv[0]; out->v = in->uv[1];
    out->r = colR; out->g = colG; out->b = colB; out->a = colA;
}

static void lerp_gv(hiGeomVert *o, const hiGeomVert *a, const hiGeomVert *b,
                    float t)
{
    o->cx = a->cx + (b->cx - a->cx) * t;
    o->cy = a->cy + (b->cy - a->cy) * t;
    o->cz = a->cz + (b->cz - a->cz) * t;
    o->cw = a->cw + (b->cw - a->cw) * t;
    o->u = a->u + (b->u - a->u) * t;
    o->v = a->v + (b->v - a->v) * t;
    o->r = a->r + (b->r - a->r) * t;
    o->g = a->g + (b->g - a->g) * t;
    o->b = a->b + (b->b - a->b) * t;
    o->a = a->a + (b->a - a->a) * t;
}

/* Sutherland–Hodgman contra o plano w = eps (near). Retorna nova contagem */
static int clip_near(hiGeomVert *poly, int n)
{
    hiGeomVert tmp[16];
    int m = 0, i;
    for (i = 0; i < n; i++) {
        const hiGeomVert *a = &poly[i];
        const hiGeomVert *b = &poly[(i + 1) % n];
        int ain = a->cw > HI_NEAR_EPS;
        int bin = b->cw > HI_NEAR_EPS;
        if (ain) tmp[m++] = *a;
        if (ain != bin) {
            float t = (HI_NEAR_EPS - a->cw) / (b->cw - a->cw);
            lerp_gv(&tmp[m++], a, b, t);
        }
    }
    memcpy(poly, tmp, sizeof(hiGeomVert) * (size_t)m);
    return m;
}

static void project_to_screen(hglCtx *ctx, const hiGeomVert *gv,
                              hiVertScreen *sv)
{
    float iw = 1.0f / gv->cw;
    float ndcx = gv->cx * iw;
    float ndcy = gv->cy * iw;
    sv->sx = (float)ctx->vx + (float)ctx->vw * (ndcx * 0.5f + 0.5f);
    sv->sy = (float)ctx->vy + (float)ctx->vh * ((1.0f - ndcy) * 0.5f);
    sv->iw = iw;
    sv->z = gv->cz * iw * 0.5f + 0.5f; /* ndc [-1,1] -> [0,1] */
    sv->u = gv->u * iw;
    sv->v = gv->v * iw;
    sv->r = gv->r * iw; sv->g = gv->g * iw;
    sv->b = gv->b * iw; sv->a = gv->a * iw;
}

static void tile_list_push(HiTileList *tl, uint32_t v)
{
    if (tl->n == tl->cap) {
        tl->cap = tl->cap ? tl->cap * 2 : 8;
        tl->items = (uint32_t *)realloc(tl->items,
                                        sizeof(uint32_t) * (size_t)tl->cap);
    }
    tl->items[tl->n++] = v;
}

void hi_geom_submit(hglCtx *ctx, const hglVertex vin[3], const uint32_t vid[3])
{
    hiGeomVert poly[16];
    int n = 3, i;
    hiTriScreen tri;
    int tx0, tx1, ty0, ty1, tx, ty;

    ctx->statsTrisIn++;

    for (i = 0; i < 3; i++)
        hi_hde_vertex(ctx, &vin[i], vid[i], &poly[i]);

    n = clip_near(poly, n);
    if (n < 3) return;

    /* fan: (0,i,i+1) — saída do near-clip é convexa */
    for (i = 1; i + 1 < n; i++) {
        project_to_screen(ctx, &poly[0],     &tri.v[0]);
        project_to_screen(ctx, &poly[i],     &tri.v[1]);
        project_to_screen(ctx, &poly[i + 1], &tri.v[2]);
        tri.tex = ctx->capTexture ? ctx->texBound : NULL;
        tri.stTest = (unsigned char)ctx->stenTest;
        tri.stFunc = (unsigned char)ctx->stenFunc;
        tri.stRef  = (unsigned char)ctx->stenRef;
        tri.stOp   = (unsigned char)ctx->stenOp;

        /* bbox clampada ao viewport */
        {
            float minx = tri.v[0].sx, maxx = tri.v[0].sx;
            float miny = tri.v[0].sy, maxy = tri.v[0].sy;
            int k;
            for (k = 1; k < 3; k++) {
                if (tri.v[k].sx < minx) minx = tri.v[k].sx;
                if (tri.v[k].sx > maxx) maxx = tri.v[k].sx;
                if (tri.v[k].sy < miny) miny = tri.v[k].sy;
                if (tri.v[k].sy > maxy) maxy = tri.v[k].sy;
            }
            tri.minx = (int)floorf(minx); tri.maxx = (int)ceilf(maxx) + 1;
            tri.miny = (int)floorf(miny); tri.maxy = (int)ceilf(maxy) + 1;
            if (tri.minx < ctx->vx) tri.minx = ctx->vx;
            if (tri.miny < ctx->vy) tri.miny = ctx->vy;
            if (tri.maxx > ctx->vx + ctx->vw) tri.maxx = ctx->vx + ctx->vw;
            if (tri.maxy > ctx->vy + ctx->vh) tri.maxy = ctx->vy + ctx->vh;
            if (tri.minx >= tri.maxx || tri.miny >= tri.maxy) continue;
        }

        /* append ao pool */
        if (ctx->triCount == ctx->triCap) {
            ctx->triCap = ctx->triCap ? ctx->triCap * 2 : 256;
            ctx->triPool = (hiTriScreen *)realloc(
                ctx->triPool, sizeof(hiTriScreen) * (size_t)ctx->triCap);
        }
        {
            int id = ctx->triCount++;
            ctx->triPool[id] = tri;
            ctx->statsTrisOut++;

            /* binning nos tiles cobertos */
            tx0 = tri.minx / HI_TILE; tx1 = (tri.maxx - 1) / HI_TILE;
            ty0 = tri.miny / HI_TILE; ty1 = (tri.maxy - 1) / HI_TILE;
            for (ty = ty0; ty <= ty1; ty++)
                for (tx = tx0; tx <= tx1; tx++) {
                    HiTileList *tl = &ctx->tiles[(size_t)ty * ctx->tx + tx];
                    tile_list_push(tl, (uint32_t)id);
                }
        }
    }
}
