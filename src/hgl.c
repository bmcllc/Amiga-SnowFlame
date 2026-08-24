/* =====================================================================
 * hgl.c — front-end HGL: estado convencional que alimenta a Homotopia
 * ===================================================================== */
#include <stdlib.h>
#include <string.h>
#include "internal.h"

/* ----------------------------------------------------------- matrizes */
void hglMatrixMode(hglCtx *ctx, hglMatrixModeSel mode)
{
    if (ctx) ctx->matrixMode = (int)mode;
}

static hiMat4 *current_matrix(hglCtx *ctx)
{
    return (ctx->matrixMode == HGL_PROJECTION) ? &ctx->proj : &ctx->mv;
}

void hglLoadIdentity(hglCtx *ctx)
{
    if (!ctx) return;
    hi_mat_identity(current_matrix(ctx));
}

void hglLoadMatrixf(hglCtx *ctx, const float *m16)
{
    hiMat4 tmp;
    int i, j;
    if (!ctx) return;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            tmp.m[i][j] = m16[i * 4 + j];
    *current_matrix(ctx) = tmp;
}

void hglMultMatrixf(hglCtx *ctx, const float *m16)
{
    hiMat4 tmp, res;
    int i, j;
    if (!ctx) return;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            tmp.m[i][j] = m16[i * 4 + j];
    hi_mat_mul(&res, current_matrix(ctx), &tmp);
    *current_matrix(ctx) = res;
}

void hglPerspective(hglCtx *ctx, float fovyRad, float aspect,
                    float znear, float zfar)
{
    hiMat4 p;
    hi_mat_perspective(&p, fovyRad, aspect, znear, zfar);
    hglMultMatrixf(ctx, &p.m[0][0]);
}

void hglLookAt(hglCtx *ctx, float ex, float ey, float ez,
               float cx, float cy, float cz,
               float ux, float uy, float uz)
{
    hiMat4 v;
    hi_mat_lookat(&v, hi_v3(ex, ey, ez), hi_v3(cx, cy, cz), hi_v3(ux, uy, uz));
    hglMultMatrixf(ctx, &v.m[0][0]);
}

/* -------------------------------------------------------------- estado */
void hglEnable(hglCtx *ctx, hglCap cap)
{
    if (!ctx) return;
    if (cap == HGL_LIGHTING)   ctx->capLighting = 1;
    if (cap == HGL_TEXTURE_2D) ctx->capTexture = 1;
    if (cap == HGL_SKINNING)   ctx->capSkinning = 1;
    if (cap == HGL_STENCIL_TEST) ctx->stenTest = 1;
}
void hglDisable(hglCtx *ctx, hglCap cap)
{
    if (!ctx) return;
    if (cap == HGL_LIGHTING)   ctx->capLighting = 0;
    if (cap == HGL_TEXTURE_2D) ctx->capTexture = 0;
    if (cap == HGL_SKINNING)   ctx->capSkinning = 0;
    if (cap == HGL_STENCIL_TEST) ctx->stenTest = 0;
}

void hglGetStats(hglCtx *ctx, int *trisIn, int *trisOut)
{
    if (!ctx) return;
    if (trisIn)  *trisIn = ctx->statsTrisIn;
    if (trisOut) *trisOut = ctx->statsTrisOut;
}

void hglLightDirf(hglCtx *ctx, float x, float y, float z)
{
    if (!ctx) return;
    ctx->lightDir[0] = x; ctx->lightDir[1] = y; ctx->lightDir[2] = z;
}
void hglLightColor4f(hglCtx *ctx, float r, float g, float b, float a_)
{
    (void)a_;
    if (!ctx) return;
    ctx->lightCol[0] = r; ctx->lightCol[1] = g; ctx->lightCol[2] = b;
}
void hglAmbient4f(hglCtx *ctx, float r, float g, float b, float a_)
{
    if (!ctx) return;
    ctx->ambient[0] = r; ctx->ambient[1] = g;
    ctx->ambient[2] = b; ctx->ambient[3] = a_;
}

/* ----------------------------------------------------------------- HDE */
void hglBindMorphTarget(hglCtx *ctx, int channel,
                        const float *xyz_per_vertex, int count)
{
    if (!ctx || channel < 0 || channel > 3) return;
    ctx->morphTarget[channel] = xyz_per_vertex;
    ctx->morphCount[channel] = count;
}
void hglMorphWeight(hglCtx *ctx, int channel, float t)
{
    if (!ctx || channel < 0 || channel > 3) return;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    ctx->morphWeight[channel] = t;
}
void hglSkinPalette(hglCtx *ctx, const float *mat16_bones, int boneCount)
{
    if (!ctx) return;
    ctx->skinPal = mat16_bones;
    ctx->skinBones = boneCount;
}

/* ---------------------------------------------------------------- draw */
void hglDrawTrianglesIndexed(hglCtx *ctx, int nIndices,
                             const uint32_t *indices,
                             const hglVertex *vertices)
{
    int i;
    if (!ctx || !indices || !vertices) return;
    for (i = 0; i + 2 < nIndices; i += 3) {
        hglVertex tri[3];
        tri[0] = vertices[indices[i]];
        tri[1] = vertices[indices[i + 1]];
        tri[2] = vertices[indices[i + 2]];
        hi_geom_submit(ctx, tri, &indices[i]);
    }
}

/* ------------------------------------------------------------- readback */
static int save_packed(const char *path, int w, int h, const uint32_t *pix)
{
    uint8_t *rgb;
    size_t i, n = (size_t)w * h;
    int ok;
    if (!path || !pix || w <= 0 || h <= 0) return -1;
    rgb = (uint8_t *)malloc(n * 3);
    if (!rgb) return -1;
    for (i = 0; i < n; i++) {
        int r, g, b, a;
        hi_unpack_rgba8(pix[i], &r, &g, &b, &a);
        (void)a;
        rgb[i * 3 + 0] = (uint8_t)r;
        rgb[i * 3 + 1] = (uint8_t)g;
        rgb[i * 3 + 2] = (uint8_t)b;
    }
    ok = hi_png_write_rgb8(path, w, h, rgb);
    free(rgb);
    return ok;
}

int hglSavePNG(hglCtx *ctx, const char *path)
{
    if (!ctx) return -1;
    return save_packed(path, ctx->w, ctx->h, ctx->color);
}

int hglSavePNGBuffer(const char *path, int w, int h, const uint32_t *pix)
{
    return save_packed(path, w, h, pix);
}
