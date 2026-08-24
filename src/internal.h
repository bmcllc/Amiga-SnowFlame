/* =====================================================================
 * internal.h — estruturas internas do renderizador Hot-ice
 * Não é parte da API pública; incluído apenas pelos .c e pelos testes.
 * ===================================================================== */
#ifndef HOTICE_INTERNAL_H
#define HOTICE_INTERNAL_H

#include <stdint.h>
#include "hotice/hgl.h"

#define HI_TILE 32 /* tile 32x32, como no dossiê */

enum { HI_FMT_RGBA8 = 0, HI_FMT_HIQTC = 1 };

struct hglTex {
    int w, h;
    int fmt;
    int filter;
    int wrap;
    uint32_t *pix;   /* HI_FMT_RGBA8 */
    uint8_t  *hiq;   /* HI_FMT_HIQTC: 8 bytes por bloco 4x4 */

    /* cadeia de mipmaps (RGBA8), gerada por hglTexGenerateMipmaps.
       lv[0] é cópia da base decodificada; sampler trilinear usa só lv[]. */
    int       nlevels;
    uint32_t **lv;
    int      *lw;
    int      *lh;
};

/* vértice pós-projeção (espaço de tela + varyings pré-divididos por w) */
typedef struct {
    float sx, sy;
    float iw;              /* 1/w clip                       */
    float z;               /* profundidade ndc mapeada [0,1] */
    float u, v;            /* uv * iw (perspectiva correta)  */
    float r, g, b, a;      /* cor lit * iw                   */
} hiVertScreen;

/* vértice em clip space + varyings planos (pré-divisão) */
typedef struct {
    float cx, cy, cz, cw;
    float u, v, r, g, b, a;
} hiGeomVert;

typedef struct {
    hiVertScreen v[3];
    const hglTex *tex;          /* textura capturada na submissão        */
    unsigned char stTest, stFunc, stRef, stOp; /* stencil idem — o estado
                                  é congelado por draw, não global no flush */
    int minx, miny, maxx, maxy; /* bbox em pixels (max exclusivo)        */
} hiTriScreen;

typedef struct {
    uint32_t *items;
    int n, cap;
} HiTileList;

struct hglCtx {
    /* target */
    int w, h, samples;
    int tx, ty;                 /* grade de tiles                    */
    uint32_t *color;            /* framebuffer final packed          */

    /* viewport */
    int vx, vy, vw, vh;

    /* clear state (TBR: aplicado por tile no início do processamento) */
    uint32_t clearColor;
    uint32_t clearZ24;          /* 0..0xFFFFFF (z apenas)      */
    int      clearStencil;      /* 0..255                      */

    /* estado do teste de stencil */
    int stenTest;               /* cap HGL_STENCIL_TEST        */
    int stenFunc;               /* hglStencilFunc              */
    int stenRef;                /* 0..255                      */
    int stenOp;                 /* hglStencilOp no sucesso     */

    /* leitura pós-frame para testes/ferramentas (amostra de maior valor) */
    uint8_t *stencilOut;

    /* bins + pool de triângulos do frame */
    HiTileList *tiles;
    hiTriScreen *triPool;
    int triCount, triCap;

    /* scratch por tile: cor+profundidade por amostra (CAA) */
    uint32_t *scolor;
    uint32_t *sdepth;
    float sampX[4], sampY[4];

    /* estado HDE */
    hiMat4 mv, proj;
    int matrixMode;
    int capLighting, capTexture, capSkinning;
    float lightDir[3], lightCol[3], ambient[4];
    hglTex *texBound;
    const float *morphTarget[4];
    int morphCount[4];
    float morphWeight[4];
    const float *skinPal;
    int skinBones;

    /* estatísticas */
    int statsTrisIn, statsTrisOut;
};

/* --- hiqtc.c --- */
uint8_t *hi_hiqtc_encode_rgba8(int w, int h, const uint32_t *pix);
uint32_t hi_hiqtc_decode_texel(const hglTex *t, int x, int y);
uint32_t *hi_hiqtc_decode_all(const hglTex *t); /* base decodificada */

/* --- texture.c --- */
uint32_t hi_sample_tex(const hglTex *t, float u, float v);
uint32_t hi_sample_texel(const hglTex *t, int x, int y);
uint32_t hi_sample_tex_mip(const hglTex *t, float u, float v, float rho);

/* --- hde.c (geometria) --- */
void hi_geom_submit(hglCtx *ctx, const hglVertex vin[3], const uint32_t vid[3]);
void hi_hde_vertex(hglCtx *ctx, const hglVertex *in, uint32_t vid,
                   hiGeomVert *out);

/* --- raster.c --- */
void hi_raster_flush(hglCtx *ctx);

/* --- hipng.c --- */
int hi_png_write_rgb8(const char *path, int w, int h, const uint8_t *rgb);

#endif /* HOTICE_INTERNAL_H */
