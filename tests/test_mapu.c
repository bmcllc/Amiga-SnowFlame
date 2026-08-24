/* =====================================================================
 * test_mapu.c — testes da unidade analítica MAPU
 * Reusa g_run/g_fail da suíte principal (agora não-estáticas).
 * ===================================================================== */
#include <stdio.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "hotice/mapu.h"

extern int g_run, g_fail;
#define MPCHECK(cond, msg) do {                          \
        g_run++;                                         \
        if (cond) { printf("        ok: %s\n", msg); }  \
        else       { g_fail++; printf("        FALHOU: %s\n", msg); } \
    } while (0)

/* epsilon em Q16 para comparações */
#define QEPS ((MapuScalar)128)   /* ~0.002 */

static int qclose(MapuScalar a, MapuScalar b)
{
    MapuScalar d = a - b;
    if (d < 0) d = -d;
    return d < QEPS;
}

void test_mapu(void)
{
    /* CSINC: 0, 45°, 90° */
    {
        MapuScalar s, c;
        mapu_csinc_q16(0, &s, &c);
        MPCHECK(qclose(s, 0) && qclose(c, MAPU_ONE),
                "csinc: 0° sin=0 cos=1");
        mapu_csinc_q16(MAPU_ONE >> 3, &s, &c);            /* 1/8 turn 45° */
        MPCHECK(fabs(mapu_q2f(s) - sin(M_PI/4)) < 0.003 &&
                fabs(mapu_q2f(c) - cos(M_PI/4)) < 0.003,
                "csinc: 45° ≈ 0.7071");
        mapu_csinc_q16(MAPU_ONE >> 2, &s, &c);            /* 90° = 0.25 turn */
        MPCHECK(qclose(s, MAPU_ONE) && qclose(c, 0),
                "csinc: 90° sin=1 cos=0");
    }

    /* ATAN2 */
    {
        MPCHECK(fabs(mapu_q2f(mapu_atan2_q16(mapu_f2q(1), mapu_f2q(0))) * 360 - 90) < 1.0,
                "atan2: +y → 90°");
        MPCHECK(fabs(mapu_q2f(mapu_atan2_q16(mapu_f2q(0), mapu_f2q(1))) * 360) < 1.0,
                "atan2: +x → 0°");
        MPCHECK(fabs(mapu_q2f(mapu_atan2_q16(mapu_f2q(1), mapu_f2q(1))) * 360 - 45) < 1.0,
                "atan2: 1,1 → 45°");
    }

    /* EXP2/LOG2 recíprocos */
    {
        MPCHECK(qclose(mapu_log2_q16(MAPU_ONE), 0), "log2(1)=0");
        MPCHECK(fabs(mapu_q2f(mapu_exp2_q16(MAPU_ONE)) - 2.0) < 0.01, "exp2(1)=2");
        MapuScalar x = mapu_f2q(3.5);
        double back = mapu_q2f(mapu_log2_q16(mapu_exp2_q16(x)));
        MPCHECK(fabs(back - 3.5) < 0.05, "exp2/log2 inversos (3.5)");
        (void)back;
    }

    /* ROOT2: x²-5x+6 = 0 → 2,3 */
    {
        MapuScalar r[2];
        int n = mapu_root2_q16(MAPU_ONE, mapu_f2q(-5.0), mapu_f2q(6.0), r);
        MPCHECK(n == 2, "root2: x²-5x+6 tem 2 raízes");
        if (n == 2) {
            double d0 = fabs(mapu_q2f(r[0]) - 2.0) + fabs(mapu_q2f(r[1]) - 3.0);
            double d1 = fabs(mapu_q2f(r[0]) - 3.0) + fabs(mapu_q2f(r[1]) - 2.0);
            MPCHECK(d0 < 0.05 || d1 < 0.05, "root2: raízes ≈ {2,3}");
        }
    }

    /* TRAJC: queda livre p=ro+v0 t+½gt² ; t=1 → y=10+0-4.9=5.1 */
    {
        MapuVec3 ro = mapu_v3f(0, 10, 0), v0 = mapu_v3f(0,0,0),
                 g  = mapu_v3f(0, -9.8, 0);
        MapuVec3 p = mapu_trajc_q16(ro, v0, g, MAPU_ONE);
        MPCHECK(fabs(mapu_q2f(p.y) - 5.1) < 0.02, "trajc: y(1s)=10-4.9=5.1");
        MPCHECK(fabs(mapu_q2f(p.x)) < 2e-2, "trajc: x(1s)=0 (sem vento)");
    }

    /* VELOCIDADE: vy(t)=v0y+g t (verifica via TRAJC diferencial) */
    {
        MapuVec3 ro = mapu_v3f(0,0,0), v0 = mapu_v3f(0, 10, 0), g = mapu_v3f(0,-9.8,0);
        /* vy aos 1s: 10 - 9.8 = 0.2 */
        MapuVec3 p = mapu_trajc_q16(ro, v0, g, MAPU_ONE);
        /* derivada: p(1)-p(0) na y = 0.2 → vy medio */
        MapuScalar vy0;
        mapu_tevnt_ground_q16(ro, v0, g.y, &vy0); /* vy no impacto */
        /* impacto no chão: t=2*v0y/g = 2*10/9.8≈2.0408, vy=-vy0 */
        double t_impact = mapu_q2f(mapu_tevnt_ground_q16(ro, v0, g.y, NULL));
        MPCHECK(fabs(t_impact - (2.0*10.0/9.8)) < 0.02,
                "tevnt: impacto chão t=2v0y/g ≈ 2.041s");
        (void)p; (void)vy0;
    }

    /* RAYSP: raio eixo-x vs esfera r=1 em origem → entry at x=-1, dt=4 */
    {
        MapuRay r = { mapu_v3f(-5,0,0), mapu_v3f(1,0,0) };
        MapuSphere sp = { mapu_v3f(0,0,0), MAPU_ONE };   /* r=1.0 Q16 */
        MapuVec3 n;
        MapuScalar dt = mapu_ray_sphere_q16(r, sp, &n);
        MPCHECK(dt > 0 && fabs(mapu_q2f(dt) - 4.0) < 0.05,
                "raysp: hit dist dt ≈ 4.0");
        /* normal na face de entrada deve ser -x */
        MPCHECK(fabs(mapu_q2f(n.x) + 1.0) < 0.02 && fabs(mapu_q2f(n.y)) < 0.02,
                "raysp: normal de entrada ≈ -x");
    }

    /* RAYSP: raio paralelo sem interseção → -1 */
    {
        MapuRay r = { mapu_v3f(0,2,0), mapu_v3f(1,0,0) };
        MapuSphere sp = { mapu_v3f(0,0,0), MAPU_ONE };
        MapuScalar dt = mapu_ray_sphere_q16(r, sp, NULL);
        MPCHECK(dt < 0, "raysp: raio paralelo à distancia → sem hit (−1)");
    }

    /* SPRG: t=0 → x0 ; decaimento: x em t grande ~ 0 */
    {
        MapuScalar z0 = mapu_sprg_q16(MAPU_ONE, 0, MAPU_ONE, MAPU_ONE,
                                      mapu_f2q(0.1), 0);
        MPCHECK(qclose(z0, MAPU_ONE), "sprg: t=0 → x0");
        MapuScalar zbig = mapu_sprg_q16(MAPU_ONE, 0, MAPU_ONE, MAPU_ONE,
                                       mapu_f2q(2.0), mapu_f2q(10.0));
        MPCHECK(fabs(mapu_q2f(zbig)) < 0.1, "sprg: decai a ~0 em t grande");
    }
}
