/* =====================================================================
 * Hot-ice — núcleo matemático do renderizador de referência
 * Arquitetura Homotopia · Console SnowFlame (projeto fictício, 1999)
 *
 * Convenções:
 *  - Matrizes row-major m[linha][coluna], transformação v' = M·v (coluna).
 *  - Clip space estilo OpenGL: -w <= x,y,z <= w.
 *  - Cor empacotada em uint32 como 0xAABBGGRR (RGBA na memória LE).
 *  - Ponto fixo 16.16 aceito nas fronteiras HDE (formato ColdFire).
 * ===================================================================== */
#ifndef HOTICE_TYPES_H
#define HOTICE_TYPES_H

#include <stdint.h>
#include <math.h>

#ifndef HI_PI
#define HI_PI 3.14159265358979323846f
#endif

/* ---------------------------------------------------------------- vec */
typedef struct { float x, y, z;    } hiVec3;
typedef struct { float x, y, z, w; } hiVec4;

static inline hiVec3 hi_v3(float x, float y, float z) { hiVec3 v = { x, y, z }; return v; }
static inline hiVec4 hi_v4(float x, float y, float z, float w)
{
    hiVec4 v = { x, y, z, w };
    return v;
}
static inline hiVec3 hi_v3_add(hiVec3 a, hiVec3 b) { return hi_v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline hiVec3 hi_v3_sub(hiVec3 a, hiVec3 b) { return hi_v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline hiVec3 hi_v3_scale(hiVec3 a, float s) { return hi_v3(a.x * s, a.y * s, a.z * s); }
static inline float  hi_v3_dot(hiVec3 a, hiVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline hiVec3 hi_v3_cross(hiVec3 a, hiVec3 b)
{
    return hi_v3(a.y * b.z - a.z * b.y,
                 a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x);
}
static inline float hi_v3_len(hiVec3 a) { return sqrtf(hi_v3_dot(a, a)); }
static inline hiVec3 hi_v3_norm(hiVec3 a)
{
    float l = hi_v3_len(a);
    return (l > 1e-12f) ? hi_v3_scale(a, 1.0f / l) : hi_v3(0.0f, 0.0f, 0.0f);
}
static inline hiVec3 hi_v3_lerp(hiVec3 a, hiVec3 b, float t)
{
    return hi_v3(a.x + (b.x - a.x) * t,
                 a.y + (b.y - a.y) * t,
                 a.z + (b.z - a.z) * t);
}

/* ---------------------------------------------------------------- mat */
typedef struct { float m[4][4]; } hiMat4;

static inline void hi_mat_identity(hiMat4 *o)
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            o->m[i][j] = (i == j) ? 1.0f : 0.0f;
}

/* o = a·b (aplica b primeiro, depois a) */
static inline void hi_mat_mul(hiMat4 *o, const hiMat4 *a, const hiMat4 *b)
{
    hiMat4 r;
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) s += a->m[i][k] * b->m[k][j];
            r.m[i][j] = s;
        }
    *o = r;
}

static inline hiVec4 hi_mat_xform(const hiMat4 *m, hiVec4 v)
{
    hiVec4 r;
    r.x = m->m[0][0] * v.x + m->m[0][1] * v.y + m->m[0][2] * v.z + m->m[0][3] * v.w;
    r.y = m->m[1][0] * v.x + m->m[1][1] * v.y + m->m[1][2] * v.z + m->m[1][3] * v.w;
    r.z = m->m[2][0] * v.x + m->m[2][1] * v.y + m->m[2][2] * v.z + m->m[2][3] * v.w;
    r.w = m->m[3][0] * v.x + m->m[3][1] * v.y + m->m[3][2] * v.z + m->m[3][3] * v.w;
    return r;
}

/* Transforma direção pela parte 3x3 (normais; assumir escala uniforme). */
static inline hiVec3 hi_mat_dir(const hiMat4 *m, hiVec3 v)
{
    hiVec3 r;
    r.x = m->m[0][0] * v.x + m->m[0][1] * v.y + m->m[0][2] * v.z;
    r.y = m->m[1][0] * v.x + m->m[1][1] * v.y + m->m[1][2] * v.z;
    r.z = m->m[2][0] * v.x + m->m[2][1] * v.y + m->m[2][2] * v.z;
    return r;
}

static inline void hi_mat_translate(hiMat4 *o, float x, float y, float z)
{
    hi_mat_identity(o);
    o->m[0][3] = x; o->m[1][3] = y; o->m[2][3] = z;
}
static inline void hi_mat_scale(hiMat4 *o, float x, float y, float z)
{
    hi_mat_identity(o);
    o->m[0][0] = x; o->m[1][1] = y; o->m[2][2] = z;
}
static inline void hi_mat_rotate_y(hiMat4 *o, float rad)
{
    float c = cosf(rad), s = sinf(rad);
    hi_mat_identity(o);
    o->m[0][0] = c;  o->m[0][2] = s;
    o->m[2][0] = -s; o->m[2][2] = c;
}

/* Projeção perspectiva (fovy em radianos), convenção OpenGL. */
static inline void hi_mat_perspective(hiMat4 *o, float fovy, float aspect,
                                      float zn, float zf)
{
    float f = 1.0f / tanf(fovy * 0.5f);
    hi_mat_identity(o);
    o->m[0][0] = f / aspect;
    o->m[1][1] = f;
    o->m[2][2] = (zf + zn) / (zn - zf);
    o->m[2][3] = (2.0f * zf * zn) / (zn - zf);
    o->m[3][2] = -1.0f;
    o->m[3][3] = 0.0f;
}

static inline void hi_mat_lookat(hiMat4 *o, hiVec3 eye, hiVec3 center, hiVec3 up)
{
    hiVec3 f = hi_v3_norm(hi_v3_sub(center, eye));
    hiVec3 s = hi_v3_norm(hi_v3_cross(f, up));
    hiVec3 u = hi_v3_cross(s, f);
    hi_mat_identity(o);
    o->m[0][0] = s.x;  o->m[0][1] = s.y;  o->m[0][2] = s.z;  o->m[0][3] = -hi_v3_dot(s, eye);
    o->m[1][0] = u.x;  o->m[1][1] = u.y;  o->m[1][2] = u.z;  o->m[1][3] = -hi_v3_dot(u, eye);
    o->m[2][0] = -f.x; o->m[2][1] = -f.y; o->m[2][2] = -f.z; o->m[2][3] = hi_v3_dot(f, eye);
}

/* ------------------------------------------------- ponto fixo 16.16 */
typedef int32_t hiFix1616;

static inline hiFix1616 hi_fix_from_float(float f) { return (hiFix1616)lrintf(f * 65536.0f); }
static inline float     hi_fix_to_float(hiFix1616 q) { return (float)q / 65536.0f; }

/* ------------------------------------------------------------- cores */
static inline uint32_t hi_pack_rgba8(int r, int g, int b, int a)
{
    return ((uint32_t)(a & 255) << 24) | ((uint32_t)(b & 255) << 16) |
           ((uint32_t)(g & 255) << 8)  |  (uint32_t)(r & 255);
}
static inline void hi_unpack_rgba8(uint32_t px, int *r, int *g, int *b, int *a)
{
    if (r) *r = (int)(px & 255);
    if (g) *g = (int)((px >> 8) & 255);
    if (b) *b = (int)((px >> 16) & 255);
    if (a) *a = (int)((px >> 24) & 255);
}
static inline uint16_t hi_pack_rgb565(int r, int g, int b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static inline void hi_unpack_rgb565(uint16_t c, int *r, int *g, int *b)
{
    if (r) *r = (int)(((c >> 11) & 31) << 3);
    if (g) *g = (int)(((c >> 5) & 63) << 2);
    if (b) *b = (int)((c & 31) << 3);
}

#endif /* HOTICE_TYPES_H */
