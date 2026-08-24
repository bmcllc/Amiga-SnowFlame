/* =====================================================================
 * mapu.h — MontêLauro Analytical Physics Unit
 *
 * Implementação de referência C99 do subsistema MAPU do ColdFire V4æ:
 * física analítica (forma fechada, sem integração numérica).
 *
 * Tudo opera sobre Q16.16 (MapuScalar) por padrão do dossiê, com
 * resultados bit-determinísticos entre builds. Ângulos usam "turns"
 * (1.0 == um círculo completo == 2π rad).
 * ===================================================================== */
#ifndef HI_MAPU_H
#define HI_MAPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Q16.16 ------------------------------------------------------- */
typedef int32_t MapuScalar;            /* valor fixo Q16.16 */
#define MAPU_ONE      ((MapuScalar)0x00010000)   /* 1.0  */
#define MAPU_HALF     ((MapuScalar)0x00008000)   /* 0.5  */
#define MAPU_PI_TURN  ((MapuScalar)0x00008000)   /* π rad = 0.5 turn */

static inline MapuScalar mapu_f2q(double f)
{
    return (MapuScalar)(f * (double)MAPU_ONE);
}
static inline double mapu_q2f(MapuScalar q)
{
    return (double)q / (double)MAPU_ONE;
}
static inline MapuScalar mapu_mul(MapuScalar a, MapuScalar b)
{
    int64_t t = (int64_t)a * (int64_t)b;
    return (MapuScalar)(t >> 16);
}
static inline MapuScalar mapu_div(MapuScalar a, MapuScalar b)
{
    if (b == 0) return (a >= 0) ? 0x7FFFFFFF : (MapuScalar)0x80000000;
    int64_t t = ((int64_t)a << 16) / (int64_t)b;
    if (t > 0x7FFFFFFF) return 0x7FFFFFFF;
    if (t < -0x80000000LL) return (MapuScalar)0x80000000;
    return (MapuScalar)t;
}

/* ---- 3-vetores Q16 ------------------------------------------------ */
typedef struct { MapuScalar x, y, z; } MapuVec3;

static inline MapuVec3 mapu_v3(MapuScalar x, MapuScalar y, MapuScalar z)
{
    MapuVec3 r = { x, y, z };
    return r;
}
static inline MapuVec3 mapu_vadd(MapuVec3 a, MapuVec3 b)
{
    MapuVec3 r = { a.x + b.x, a.y + b.y, a.z + b.z };
    return r;
}
static inline MapuVec3 mapu_vscale(MapuScalar s, MapuVec3 a)
{
    MapuVec3 r = { mapu_mul(s, a.x), mapu_mul(s, a.y), mapu_mul(s, a.z) };
    return r;
}

/* ---- geometria ---------------------------------------------------- */
typedef struct { MapuVec3 o, d; } MapuRay;
typedef struct { MapuVec3 p, n; } MapuPlane;
typedef struct { MapuVec3 c; MapuScalar r; } MapuSphere;
typedef struct { MapuVec3 bmin, bmax; } MapuBox;

/* ---- opcodes MAPU ------------------------------------------------- */

/* CORDIC CSINC.P : seno/cosseno de `ang` (turns Q16) → (sin,cos) Q16 */
void mapu_csinc_q16(MapuScalar ang_turns, MapuScalar *sin_q, MapuScalar *cos_q);

/* ATAN2.P : y,x(Q16) → ang em turns Q16, ∈[-½,½) */
MapuScalar mapu_atan2_q16(MapuScalar y, MapuScalar x);

/* EXP2.P / LOG2.P : base-2, Q16 → Q16 */
MapuScalar mapu_exp2_q16(MapuScalar s);
MapuScalar mapu_log2_q16(MapuScalar x);

/* ROOT2.P : ax²+bx+c=0 (Q16) → qr[2]; retorno 0..2 reais */
int mapu_root2_q16(MapuScalar a, MapuScalar b, MapuScalar c, MapuScalar qr[2]);

/* RAYSP.P : raio×esfera → dt(Q16>=0) ou -1; normal opcional */
MapuScalar mapu_ray_sphere_q16(MapuRay r, MapuSphere s, MapuVec3 *out_n);
/* RAYPL.P */
MapuScalar mapu_ray_plane_q16(MapuRay r, MapuPlane p);
/* RAYBB.P : AABB axis-aligned (slab) → dt entrada ou -1 */
MapuScalar mapu_ray_aabb_q16(MapuRay r, MapuBox box, MapuVec3 *out_n);

/* TRAJC.P : posição(t) = ro + v0*t + ½·g·t²  (t,g em Q16) */
MapuVec3 mapu_trajc_q16(MapuVec3 ro, MapuVec3 v0, MapuVec3 g, MapuScalar t);
/* TEVNT.P : impacto no chão y=0 → t (Q16) ou -1; veloc. y no impacto opcional */
MapuScalar mapu_tevnt_ground_q16(MapuVec3 ro, MapuVec3 v0, MapuScalar g,
                                 MapuScalar *out_vy);
/* SPRG.P : mola-amortecedor analítica (ζ≥1 crítica) */
MapuScalar mapu_sprg_q16(MapuScalar x0, MapuScalar v0, MapuScalar k,
                         MapuScalar m, MapuScalar zeta, MapuScalar t);

/* ---- util float (host/testing convenience) ----------------------- */
static inline MapuVec3 mapu_v3f(double x, double y, double z)
{
    MapuVec3 r = { mapu_f2q(x), mapu_f2q(y), mapu_f2q(z) };
    return r;
}

#ifdef __cplusplus
}
#endif
#endif /* HI_MAPU_H */
