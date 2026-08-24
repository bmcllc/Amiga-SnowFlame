/* =====================================================================
 * HGL — Hot-ice Graphics Library
 *
 * A face CONVENCIONAL da arquitetura Homotopia: pipeline mental idêntico
 * ao Glide/DirectX da época — matrizes, luzes, materiais, texturas.
 * As features Homotopia (morph/skin via HDE, HIQTC, CAA) são opt-in e
 * expostas como chamadas normais. Nada de paradigma exótico obrigatório.
 * ===================================================================== */
#ifndef HOTICE_HGL_H
#define HOTICE_HGL_H

#include <stdint.h>
#include "hotice/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hglCtx hglCtx;
typedef struct hglTex hglTex;

/* ------------------------------------------------------------ enums */
typedef enum {
    HGL_LIGHTING   = 1,   /* iluminação difusa por vértice (HDE)      */
    HGL_TEXTURE_2D = 2,   /* mapeamento de textura                    */
    HGL_SKINNING   = 4    /* palette esquelética (HDE, até 8 ossos/vtx)*/
} hglCap;

typedef enum { HGL_MODELVIEW = 0, HGL_PROJECTION = 1 } hglMatrixModeSel;

typedef enum { HGL_FILTER_NEAREST = 0, HGL_FILTER_LINEAR = 1 } hglFilter;
typedef enum { HGL_WRAP_REPEAT = 0, HGL_WRAP_CLAMP = 1 }       hglWrap;

/* ----------------------------------------------------------- vértice */
typedef struct hglVertex {
    float pos[3];    /* posição no espaço do modelo                    */
    float nrm[3];    /* normal                                          */
    float uv[2];     /* coordenada de textura                           */
    float col[4];    /* cor difusa rgba 0..1                            */
    float bone[4];   /* índices de osso (só com HGL_SKINNING + palette) */
    float bw[4];     /* pesos de pele (soma <= 1)                       */
} hglVertex;

/* ---------------------------------------------------------- contexto */
/* samples: 1 (SSAA off), 2 ou 4 — CAA "grade rotacionada" por tile.   */
hglCtx  *hglCreateContext(int width, int height, int samples);
void     hglDestroyContext(hglCtx *ctx);

void     hglViewport(hglCtx *ctx, int x, int y, int w, int h);
void     hglClearColor4f(hglCtx *ctx, float r, float g, float b, float a);
void     hglClearDepth(hglCtx *ctx, float d);          /* 0..1, padrão 1 */

void     hglFrameBegin(hglCtx *ctx);   /* abre frame: limpa bins/tris    */
void     hglFrameEnd(hglCtx *ctx);     /* fecha: processa tiles (TBR)    */

uint32_t *hglColorBuffer(hglCtx *ctx); /* leitura pós-FrameEnd (packed)  */
int       hglSavePNG(hglCtx *ctx, const char *path);
int       hglSavePNGBuffer(const char *path, int w, int h, const uint32_t *pix);

/* ----------------------------------------------------------- matrizes */
void     hglMatrixMode(hglCtx *ctx, hglMatrixModeSel mode);
void     hglLoadIdentity(hglCtx *ctx);
void     hglLoadMatrixf(hglCtx *ctx, const float *m16_rowmajor);
void     hglMultMatrixf(hglCtx *ctx, const float *m16_rowmajor);
void     hglPerspective(hglCtx *ctx, float fovyRad, float aspect,
                        float znear, float zfar);
void     hglLookAt(hglCtx *ctx, float ex, float ey, float ez,
                   float cx, float cy, float cz,
                   float ux, float uy, float uz);

/* -------------------------------------------------------------- estado */
void     hglEnable(hglCtx *ctx, hglCap cap);
void     hglDisable(hglCtx *ctx, hglCap cap);
void     hglGetStats(hglCtx *ctx, int *trisIn, int *trisOut);

void     hglLightDirf(hglCtx *ctx, float x, float y, float z); /* p/ a luz */
void     hglLightColor4f(hglCtx *ctx, float r, float g, float b, float a_);
void     hglAmbient4f(hglCtx *ctx, float r, float g, float b, float a_);

/* ----------------------------------------------------------- texturas */
hglTex  *hglTexCreateRGBA8(int w, int h, const uint32_t *pixels_packed);
hglTex  *hglTexCreateHIQTCFromRGBA8(int w, int h, const uint32_t *pixels_packed);
void     hglTexDestroy(hglTex *tex);
void     hglTexFilter(hglTex *tex, hglFilter f);
void     hglTexWrap(hglTex *tex, hglWrap w);
void     hglBindTexture(hglCtx *ctx, int unit, hglTex *tex);

/* --------------------------------------- HDE: morphing, skinning, T&L */
void     hglBindMorphTarget(hglCtx *ctx, int channel,
                            const float *xyz_per_vertex, int count);
void     hglMorphWeight(hglCtx *ctx, int channel, float t); /* 0..1 */
void     hglSkinPalette(hglCtx *ctx, const float *mat16_bones, int boneCount);

/* --------------------------------------------------------------- draw */
void     hglDrawTrianglesIndexed(hglCtx *ctx, int nIndices,
                                 const uint32_t *indices,
                                 const hglVertex *vertices);

#ifdef __cplusplus
}
#endif
#endif /* HOTICE_HGL_H */
