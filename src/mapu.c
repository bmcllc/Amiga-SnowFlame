/* =====================================================================
 * mapu.c — MontêLauro Analytical Physics Unit (microcódigo Q16.16)
 *
 * Implementação fixed-point bit-exata do MAPU do ColdFire V4æ.
 * Operações:
 *   - Aritmética Q16.16 saturada (MAC/EMAC estilo V4e)
 *   - CORDIC 16 iterações (CSINC.P, ATAN2.P)
 *   - LOG2/EXP2 base-2 via polinômio minimax grau 3
 *   - Raiz quadrada inteira (Newton-Raphson 5 iterações)
 *   - Raízes quadráticas (ROOT2.P)
 *   - Interseções ray (RAYSP/RAYPL/RAYBB)
 *   - Trajetória/projétil (TRAJC.P, TEVNT.P)
 *   - Mola-amortecedor (SPRG.P)
 *
 * Tudo determinístico, sem ponto flutuante.
 * ===================================================================== */
#include <stdint.h>
#include "hotice/mapu.h"

/* =====================================================================
 * Constantes Q16.16
 * ===================================================================== */
#define MAPU_LN2_Q16     ((MapuScalar)0x0000B172)   /* ln(2) ≈ 0.693147 */
#define MAPU_INV_LN2_Q16 ((MapuScalar)0x00017154)   /* 1/ln(2) ≈ 1.442695 */
#define MAPU_PI_Q16      ((MapuScalar)0x0001921F)   /* π ≈ 3.141593 */
#define MAPU_TAU_Q16     ((MapuScalar)0x0003243F)   /* 2π ≈ 6.283185 */
#define MAPU_SQRT2_Q16   ((MapuScalar)0x00016A0A)   /* √2 ≈ 1.414214 */
#define MAPU_INV_SQRT2_Q16 ((MapuScalar)0x0000B504) /* 1/√2 ≈ 0.707107 */

/* Ganho CORDIC K = ∏(1+2^-2i)^-½ ≈ 0.607252935 */
#define MAPU_CORDIC_GAIN_Q16 ((MapuScalar)0x00009B74)

/* =====================================================================
 * Tabela CORDIC: atan(2^-i) em turns Q16 (1.0 = 2π rad)
 * ===================================================================== */
static const MapuScalar MAPU_ATAN_LUT[16] = {
    0x00002000,  /* atan(1)       = 1/8 turn  = 0.125 */
    0x000012E4,  /* atan(1/2)     ≈ 0.07379   */
    0x000009FB,  /* atan(1/4)     ≈ 0.03899   */
    0x00000511,  /* atan(1/8)     ≈ 0.01979   */
    0x0000028B,  /* atan(1/16)    ≈ 0.00993   */
    0x00000146,  /* atan(1/32)    ≈ 0.00497   */
    0x000000A3,  /* atan(1/64)    ≈ 0.00249   */
    0x00000051,  /* atan(1/128)   ≈ 0.00124   */
    0x00000029,  /* atan(1/256)   ≈ 0.00062   */
    0x00000014,  /* atan(1/512)   ≈ 0.00031   */
    0x0000000A,  /* atan(1/1024)  ≈ 0.000155  */
    0x00000005,  /* atan(1/2048)  ≈ 0.0000777 */
    0x00000003,  /* atan(1/4096)  ≈ 0.0000389 */
    0x00000001,  /* atan(1/8192)  ≈ 0.0000194 */
    0x00000001,  /* atan(1/16384) ≈ 0.0000097 */
    0x00000000,  /* atan(1/32768) ≈ 0.0000049 */
};

/* =====================================================================
 * Tabela LOG2: log2(x) para x em [0.5, 1) em Q16.16
 * Índice i corresponde a x = 0.5 + i/256 + 0.5/256 (centro do intervalo)
 * Interpolação linear entre entradas adjacentes.
 * ===================================================================== */
static const MapuScalar MAPU_LOG2_LUT[256] = {
    0xFFFF0171, 0xFFFF044E, 0xFFFF0725, 0xFFFF09F7, 0xFFFF0CC3, 0xFFFF0F8A, 0xFFFF124B, 0xFFFF1508,
    0xFFFF17BF, 0xFFFF1A71, 0xFFFF1D1E, 0xFFFF1FC6, 0xFFFF226A, 0xFFFF2508, 0xFFFF27A2, 0xFFFF2A37,
    0xFFFF2CC8, 0xFFFF2F54, 0xFFFF31DC, 0xFFFF345F, 0xFFFF36DE, 0xFFFF3958, 0xFFFF3BCE, 0xFFFF3E41,
    0xFFFF40AF, 0xFFFF4319, 0xFFFF457F, 0xFFFF47E1, 0xFFFF4A3F, 0xFFFF4C99, 0xFFFF4EEF, 0xFFFF5142,
    0xFFFF5391, 0xFFFF55DC, 0xFFFF5824, 0xFFFF5A68, 0xFFFF5CA8, 0xFFFF5EE5, 0xFFFF611F, 0xFFFF6355,
    0xFFFF6588, 0xFFFF67B7, 0xFFFF69E4, 0xFFFF6C0C, 0xFFFF6E32, 0xFFFF7055, 0xFFFF7274, 0xFFFF7490,
    0xFFFF76AA, 0xFFFF78C0, 0xFFFF7AD3, 0xFFFF7CE3, 0xFFFF7EF0, 0xFFFF80FB, 0xFFFF8302, 0xFFFF8507,
    0xFFFF8709, 0xFFFF8908, 0xFFFF8B04, 0xFFFF8CFE, 0xFFFF8EF5, 0xFFFF90E9, 0xFFFF92DB, 0xFFFF94CA,
    0xFFFF96B6, 0xFFFF98A0, 0xFFFF9A87, 0xFFFF9C6C, 0xFFFF9E4F, 0xFFFFA02E, 0xFFFFA20C, 0xFFFFA3E7,
    0xFFFFA5C0, 0xFFFFA796, 0xFFFFA96A, 0xFFFFAB3C, 0xFFFFAD0C, 0xFFFFAED9, 0xFFFFB0A4, 0xFFFFB26C,
    0xFFFFB433, 0xFFFFB5F7, 0xFFFFB7BA, 0xFFFFB97A, 0xFFFFBB38, 0xFFFFBCF4, 0xFFFFBEAD, 0xFFFFC065,
    0xFFFFC21B, 0xFFFFC3CF, 0xFFFFC580, 0xFFFFC730, 0xFFFFC8DE, 0xFFFFCA8A, 0xFFFFCC34, 0xFFFFCDDC,
    0xFFFFCF82, 0xFFFFD126, 0xFFFFD2C8, 0xFFFFD469, 0xFFFFD607, 0xFFFFD7A4, 0xFFFFD93F, 0xFFFFDAD9,
    0xFFFFDC70, 0xFFFFDE06, 0xFFFFDF9A, 0xFFFFE12C, 0xFFFFE2BC, 0xFFFFE44C, 0xFFFFE5D9, 0xFFFFE765,
    0xFFFFE8EF, 0xFFFFEA77, 0xFFFFEBFE, 0xFFFFED83, 0xFFFFEF06, 0xFFFFF088, 0xFFFFF209, 0xFFFFF387,
    0xFFFFF505, 0xFFFFF680, 0xFFFFF7FB, 0xFFFFF973, 0xFFFFFAEA, 0xFFFFFC60, 0xFFFFFDD4, 0xFFFFFF47,
    0x000000B8, 0x00000228, 0x00000397, 0x00000504, 0x00000670, 0x000007DA, 0x00000943, 0x00000AAA,
    0x00000C10, 0x00000D75, 0x00000ED9, 0x0000103B, 0x0000119B, 0x000012FB, 0x00001459, 0x000015B6,
    0x00001712, 0x0000186C, 0x000019C5, 0x00001B1D, 0x00001C73, 0x00001DC9, 0x00001F1D, 0x00002070,
    0x000021C1, 0x00002312, 0x00002461, 0x000025AF, 0x000026FC, 0x00002848, 0x00002992, 0x00002ADC,
    0x00002C24, 0x00002D6B, 0x00002EB1, 0x00002FF6, 0x0000313A, 0x0000327D, 0x000033BE, 0x000034FF,
    0x0000363E, 0x0000377D, 0x000038BA, 0x000039F6, 0x00003B31, 0x00003C6B, 0x00003DA5, 0x00003EDD,
    0x00004014, 0x0000414A, 0x0000427F, 0x000043B3, 0x000044E5, 0x00004617, 0x00004748, 0x00004878,
    0x000049A8, 0x00004AD6, 0x00004C03, 0x00004D2F, 0x00004E5A, 0x00004F84, 0x000050AE, 0x000051D6,
    0x000052FD, 0x00005424, 0x0000554A, 0x0000566E, 0x00005792, 0x000058B5, 0x000059D7, 0x00005AF8,
    0x00005C19, 0x00005D38, 0x00005E56, 0x00005F74, 0x00006091, 0x000061AD, 0x000062C8, 0x000063E2,
    0x000064FC, 0x00006614, 0x0000672C, 0x00006843, 0x00006959, 0x00006A6E, 0x00006B83, 0x00006C96,
    0x00006DA9, 0x00006EBB, 0x00006FCC, 0x000070DD, 0x000071EC, 0x000072FB, 0x0000740A, 0x00007517,
    0x00007624, 0x0000772F, 0x0000783A, 0x00007945, 0x00007A4E, 0x00007B57, 0x00007C5F, 0x00007D67,
    0x00007E6D, 0x00007F73, 0x00008078, 0x0000817D, 0x00008281, 0x00008384, 0x00008486, 0x00008588,
    0x00008689, 0x00008789, 0x00008888, 0x00008987, 0x00008A85, 0x00008B83, 0x00008C80, 0x00008D7C,
    0x00008E77, 0x00008F72, 0x0000906C, 0x00009166, 0x0000925E, 0x00009357, 0x0000944E, 0x00009545,
};

/* =====================================================================
 * Saturação Q16.16
 * ===================================================================== */
static inline MapuScalar mapu_sat_q16(int64_t v)
{
    if (v > 0x7FFFFFFF) return 0x7FFFFFFF;
    if (v < -0x80000000LL) return (MapuScalar)0x80000000;
    return (MapuScalar)v;
}

/* =====================================================================
 * Raiz quadrada Q16.16 — Newton-Raphson 5 iterações
 * Entrada: x ≥ 0 em Q16.16
 * Saída: √x em Q16.16
 * ===================================================================== */
static MapuScalar mapu_sqrt_q16(MapuScalar x)
{
    if (x <= 0) return 0;
    MapuScalar y = x;
    int i;
    for (i = 0; i < 8; i++) {
        MapuScalar q = mapu_div(x, y);
        int64_t ny = ((int64_t)y + q) >> 1;
        if (ny == y || ny == y + 1 || ny == y - 1) { y = (MapuScalar)ny; break; }
        y = (MapuScalar)ny;
    }
    return y;
}

/* =====================================================================
 * CORDIC modo rotação (CSINC.P)
 * ang_turns: ângulo em turns Q16 (1.0 = 2π)
 * *sin_q, *cos_q: resultados em Q16.16
 * ===================================================================== */
void mapu_csinc_q16(MapuScalar ang_turns, MapuScalar *sin_q, MapuScalar *cos_q)
{
    int64_t z = ang_turns;
    int64_t x = MAPU_CORDIC_GAIN_Q16;
    int64_t y = 0;
    int i;

    while (z >  0x00008000) z -= 0x00010000;
    while (z < -0x00008000) z += 0x00010000;

    for (i = 0; i < 16; i++) {
        int64_t d = (z >= 0) ? 1 : -1;
        int64_t xn = x - (d * (y >> i));
        int64_t yn = y + (d * (x >> i));
        z -= d * MAPU_ATAN_LUT[i];
        x = xn; y = yn;
    }
    *cos_q = mapu_sat_q16(x);
    *sin_q = mapu_sat_q16(y);
}

/* =====================================================================
 * CORDIC modo vetor (ATAN2.P)
 * Retorna ângulo em turns Q16 ∈ [-0.5, 0.5)
 * ===================================================================== */
MapuScalar mapu_atan2_q16(MapuScalar y, MapuScalar x)
{
    int64_t ax = (x >= 0) ? x : -x;
    int64_t ay = (y >= 0) ? y : -y;
    int sx = (x >= 0) ? 1 : -1;
    int sy = (y >= 0) ? 1 : -1;
    int64_t z = 0;
    int64_t xv = ax, yv = ay;
    int i;

    for (i = 0; i < 16; i++) {
        int64_t d = (yv >= 0) ? -1 : 1;
        int64_t xn = xv - (d * (yv >> i));
        int64_t yn = yv + (d * (xv >> i));
        xv = xn; yv = yn;
        z -= d * MAPU_ATAN_LUT[i];
    }

    if (sx < 0) z = 0x00010000 - z;
    if (sy < 0) z = -z;

    while (z >  0x00008000) z -= 0x00010000;
    while (z < -0x00008000) z += 0x00010000;
    return mapu_sat_q16(z);
}

/* =====================================================================
 * LOG2.P base-2 via LUT 256 entradas + interpolação linear
 * ===================================================================== */
MapuScalar mapu_log2_q16(MapuScalar x)
{
    if (x <= 0) return (MapuScalar)0xFC000000;

    /* Caso especial: potências exatas de 2 (x = 2^e) */
    if ((x & 0xFFFF) == 0 && (x & (x - 1)) == 0) {
        /* x é potência de 2 em Q16: log2(x) = e */
        int e = 0;
        int64_t v = x;
        while (v > 0x10000) { v >>= 1; e++; }
        while (v < 0x10000) { v <<= 1; e--; }
        return (MapuScalar)(e << 16);
    }

    int e = 0;
    int64_t v = x;
    while (v >= 0x10000) { v >>= 1; e++; }
    while (v <  0x8000) { v <<= 1; e--; }

    /* v in [0x8000, 0x10000) -> index in [0, 255] */
    int idx = (int)((v - 0x8000) >> 8);  /* /256 */
    int64_t frac = (v - 0x8000) & 0xFF;  /* fractional part 0..255 */

    int64_t v0 = MAPU_LOG2_LUT[idx];
    int64_t v1 = MAPU_LOG2_LUT[idx + 1];
    int64_t interp = v0 + ((v1 - v0) * frac >> 8);

    return mapu_sat_q16(interp + ((int64_t)e << 16));
}

/* =====================================================================
 * EXP2.P base-2 via Taylor grau 3
 * ===================================================================== */
MapuScalar mapu_exp2_q16(MapuScalar x)
{
    int e = (int)(x >> 16);
    MapuScalar f = x & 0xFFFF;

    int64_t a = ((int64_t)f * MAPU_LN2_Q16) >> 16;
    int64_t a2 = (a * a) >> 16;
    int64_t a3 = (a2 * a) >> 16;
    int64_t r = (1 << 16) + a + (a2 >> 1) + (a3 / 6);

    if (e >= 0 && e < 16) r <<= e;
    else if (e < 0 && e >= -16) r >>= (-e);
    else if (e >= 16) r = 0x7FFFFFFF;
    else r = 0;

    return mapu_sat_q16(r);
}

/* =====================================================================
 * ROOT2.P: ax² + bx + c = 0
 * ===================================================================== */
int mapu_root2_q16(MapuScalar a, MapuScalar b, MapuScalar c, MapuScalar qr[2])
{
    if (a == 0) {
        if (b == 0) return 0;
        qr[0] = mapu_div(-c, b);
        return 1;
    }

    int64_t a64 = a, b64 = b, c64 = c;
    int64_t disc = (b64 * b64) - (4 * a64 * c64);
    disc >>= 16;

    if (disc < 0) return 0;

    MapuScalar sd = mapu_sqrt_q16((MapuScalar)disc);
    MapuScalar two_a = (MapuScalar)(2 * a64);
    if (two_a == 0) return 0;

    qr[0] = mapu_div((MapuScalar)(-b64 + (int64_t)sd), two_a);
    qr[1] = mapu_div((MapuScalar)(-b64 - (int64_t)sd), two_a);
    return 2;
}

/* =====================================================================
 * Produto interno 3D Q16.16
 * ===================================================================== */
static inline MapuScalar mapu_dot3(MapuVec3 a, MapuVec3 b)
{
    int64_t r = ((int64_t)a.x * b.x + (int64_t)a.y * b.y + (int64_t)a.z * b.z) >> 16;
    return mapu_sat_q16(r);
}

/* =====================================================================
 * RAYSP.P
 * ===================================================================== */
MapuScalar mapu_ray_sphere_q16(MapuRay r, MapuSphere s, MapuVec3 *out_n)
{
    MapuVec3 m = { r.o.x - s.c.x, r.o.y - s.c.y, r.o.z - s.c.z };
    MapuScalar a = mapu_dot3(r.d, r.d);
    MapuScalar b = (MapuScalar)((((int64_t)m.x * r.d.x + (int64_t)m.y * r.d.y + (int64_t)m.z * r.d.z) * 2) >> 16);
    MapuScalar c = mapu_dot3(m, m) - mapu_mul(s.r, s.r);

    int64_t disc = ((int64_t)b * b) >> 16;
    disc -= ((int64_t)4 * a * c) >> 16;

    if (disc < 0 || a == 0) return -1;

    MapuScalar sd = mapu_sqrt_q16((MapuScalar)disc);
    int64_t t = (((-b - (int64_t)sd) << 16) / (2 * a));
    if (t < 0) return -1;

    if (out_n) {
        MapuScalar tq = (MapuScalar)t;
        MapuVec3 hit = { r.o.x + mapu_mul(tq, r.d.x),
                         r.o.y + mapu_mul(tq, r.d.y),
                         r.o.z + mapu_mul(tq, r.d.z) };
        MapuVec3 nn = { hit.x - s.c.x, hit.y - s.c.y, hit.z - s.c.z };
        MapuScalar inv = mapu_div(MAPU_ONE, s.r);
        *out_n = mapu_vscale(inv, nn);
    }
    return (MapuScalar)t;
}

/* =====================================================================
 * RAYPL.P
 * ===================================================================== */
MapuScalar mapu_ray_plane_q16(MapuRay r, MapuPlane p)
{
    MapuScalar denom = mapu_dot3(p.n, r.d);
    if (denom == 0) return -1;
    MapuScalar pdot = mapu_dot3(p.n, r.o) + mapu_dot3(p.n, p.p);
    int64_t t = -(((int64_t)pdot << 16) / denom);
    return (MapuScalar)t;
}

/* =====================================================================
 * RAYBB.P
 * ===================================================================== */
MapuScalar mapu_ray_aabb_q16(MapuRay r, MapuBox box, MapuVec3 *out_n)
{
    MapuScalar o[3]  = { r.o.x, r.o.y, r.o.z };
    MapuScalar d[3]  = { r.d.x, r.d.y, r.d.z };
    MapuScalar mn[3] = { box.bmin.x, box.bmin.y, box.bmin.z };
    MapuScalar mx[3] = { box.bmax.x, box.bmax.y, box.bmax.z };
    MapuScalar inv[3];
    MapuScalar tmin = 0, tmax = 0x7FFFFFFF;
    int i;

    for (i = 0; i < 3; i++) {
        inv[i] = (d[i] == 0) ? 0x7FFFFFFF : mapu_div(MAPU_ONE, d[i]);
    }
    for (i = 0; i < 3; i++) {
        int64_t t1 = ((int64_t)(mn[i] - o[i]) << 16) * inv[i];
        int64_t t2 = ((int64_t)(mx[i] - o[i]) << 16) * inv[i];
        MapuScalar a = (MapuScalar)(t1 >> 16);
        MapuScalar b = (MapuScalar)(t2 >> 16);
        if (inv[i] < 0) { MapuScalar sw = a; a = b; b = sw; }
        if (a > tmin) tmin = a;
        if (b < tmax) tmax = b;
    }
    if (out_n) { out_n->x = 0; out_n->y = 0; out_n->z = 0; }
    if (tmax >= tmin && tmin >= 0) return tmin;
    return -1;
}

/* =====================================================================
 * TRAJC.P
 * ===================================================================== */
MapuVec3 mapu_trajc_q16(MapuVec3 ro, MapuVec3 v0, MapuVec3 g, MapuScalar t)
{
    MapuScalar t2 = mapu_mul(t, t);
    MapuScalar half_t2 = t2 >> 1;  /* divide by 2 */
    MapuVec3 p;
    p.x = ro.x + mapu_mul(v0.x, t) + mapu_mul(g.x, half_t2);
    p.y = ro.y + mapu_mul(v0.y, t) + mapu_mul(g.y, half_t2);
    p.z = ro.z + mapu_mul(v0.z, t) + mapu_mul(g.z, half_t2);
    return p;
}

/* =====================================================================
 * TEVNT.P
 * ===================================================================== */
MapuScalar mapu_tevnt_ground_q16(MapuVec3 ro, MapuVec3 v0, MapuScalar g, MapuScalar *out_vy)
{
    MapuScalar a = mapu_mul(g, MAPU_HALF);
    MapuScalar b = v0.y;
    MapuScalar c = ro.y;
    MapuScalar qr[2];
    int n = mapu_root2_q16(a, b, c, qr);
    int i, best = -1;
    MapuScalar mn = 0;
    for (i = 0; i < n; i++) {
        if (qr[i] > 0 && (best < 0 || qr[i] < mn)) { best = i; mn = qr[i]; }
    }
    if (best < 0) return -1;
    if (out_vy) *out_vy = b + mapu_mul(g, qr[best]);
    return qr[best];
}

/* =====================================================================
 * SPRG.P
 * ===================================================================== */
MapuScalar mapu_sprg_q16(MapuScalar x0, MapuScalar v0, MapuScalar k,
                         MapuScalar m, MapuScalar zeta, MapuScalar t)
{
    MapuScalar km = mapu_div(k, m);
    MapuScalar wn = mapu_sqrt_q16(km);
    MapuScalar zwn = mapu_mul(zeta, wn);

    MapuScalar z2 = mapu_mul(zeta, zeta);
    MapuScalar w2 = MAPU_ONE - z2;
    if (w2 < 0) w2 = 0;
    MapuScalar wd = mapu_mul(wn, mapu_sqrt_q16(w2));

    MapuScalar arg = -mapu_div(mapu_mul(zwn, t), MAPU_LN2_Q16);
    MapuScalar decay = mapu_exp2_q16(arg);

    MapuScalar ang = mapu_mul(wd, t);
    MapuScalar s, c;
    mapu_csinc_q16(ang, &s, &c);

    MapuScalar term = mapu_mul(x0, c) +
                      mapu_mul(mapu_div(v0 + mapu_mul(zwn, x0),
                                       (wd > 0 ? wd : MAPU_ONE)), s);
    return mapu_mul(decay, term);
}