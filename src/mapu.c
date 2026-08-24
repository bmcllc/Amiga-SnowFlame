/* =====================================================================
 * mapu.c — implementação MAPU (referência host)
 *
 * A API mantém o formato Q16.16 (MapuScalar) e os opcodes do dossiê do
 * V4æ (CORDIC, ATAN2, EXP2/LOG2, ROOT2, RAY*, TRAJC, TEVNT, SPRG). A
 * versão de referência neste host calcula em ponto flutuante interno
 * para validar a correção analítica; a implementação fixa determinística
 * (CORDIC real, inteiros saturados) é o microcode descrito no dossiê e
 * cabe na ALU 64-bit do V4æ.
 * ===================================================================== */
#include <math.h>
#include <stdint.h>
#include "hotice/mapu.h"

#define MA_PI  3.14159265358979323846
#define MA_TAU (6.28318530717958647692)

/* ---- trigonometria (turns → sin/cos Q16) ---- */
void mapu_csinc_q16(MapuScalar ang_turns, MapuScalar *sin_q, MapuScalar *cos_q)
{
    double rad = mapu_q2f(ang_turns) * MA_TAU;
    *sin_q = mapu_f2q(sin(rad));
    *cos_q = mapu_f2q(cos(rad));
}

/* ---- atan2 (y,x Q16 → turns Q16, ∈[-½,½)) ---- */
MapuScalar mapu_atan2_q16(MapuScalar y, MapuScalar x)
{
    return mapu_f2q(atan2(mapu_q2f(y), mapu_q2f(x)) / MA_TAU);
}

/* ---- exp2 / log2 base-2 ---- */
MapuScalar mapu_exp2_q16(MapuScalar s)
{
    return mapu_f2q(exp2(mapu_q2f(s)));
}
MapuScalar mapu_log2_q16(MapuScalar x)
{
    if (x <= 0) return mapu_f2q(-256.0);
    return mapu_f2q(log2(mapu_q2f(x)));
}

/* ---- ROOT2: ax²+bx+c=0 → qr[2] (reais); retorno 0..2 ---- */
int mapu_root2_q16(MapuScalar a, MapuScalar b, MapuScalar c, MapuScalar qr[2])
{
    double da = mapu_q2f(a), db = mapu_q2f(b), dc = mapu_q2f(c);
    double disc = db * db - 4.0 * da * dc;
    if (disc < 0) return 0;
    double sd = sqrt(disc);
    double x0 = (-db + sd) / (2.0 * da);
    double x1 = (-db - sd) / (2.0 * da);
    qr[0] = mapu_f2q(x0);
    qr[1] = mapu_f2q(x1);
    return 2;
}

/* ---- interseções ---- */
static MapuScalar dot3f(MapuVec3 a, MapuVec3 b)
{
    return mapu_f2q(mapu_q2f(a.x) * mapu_q2f(b.x) +
                    mapu_q2f(a.y) * mapu_q2f(b.y) +
                    mapu_q2f(a.z) * mapu_q2f(b.z));
}

MapuScalar mapu_ray_sphere_q16(MapuRay r, MapuSphere s, MapuVec3 *out_n)
{
    double ox = mapu_q2f(r.o.x) - mapu_q2f(s.c.x);
    double oy = mapu_q2f(r.o.y) - mapu_q2f(s.c.y);
    double oz = mapu_q2f(r.o.z) - mapu_q2f(s.c.z);
    double dx = mapu_q2f(r.d.x), dy = mapu_q2f(r.d.y), dz = mapu_q2f(r.d.z);
    double rr = mapu_q2f(s.r);
    double a = dx*dx + dy*dy + dz*dz;
    double b = 2.0 * (ox*dx + oy*dy + oz*dz);
    double c = ox*ox + oy*oy + oz*oz - rr*rr;
    double disc = b*b - 4.0*a*c;
    if (disc < 0 || a == 0) return -1;
    double t = (-b - sqrt(disc)) / (2.0 * a);
    if (out_n) {
        double hx = ox + t*dx, hy = oy + t*dy, hz = oz + t*dz;
        double mag = sqrt(hx*hx + hy*hy + hz*hz);
        *out_n = mapu_v3f(hx/mag, hy/mag, hz/mag);
    }
    return mapu_f2q(t);
}

MapuScalar mapu_ray_plane_q16(MapuRay r, MapuPlane p)
{
    double denom = mapu_q2f(dot3f(p.n, r.d));
    if (fabs(denom) < 1e-12) return -1;
    double pdot = mapu_q2f(dot3f(p.n, r.o)) + mapu_q2f(dot3f(p.n, p.p));
    double t = -pdot / denom;
    return mapu_f2q(t);
}

MapuScalar mapu_ray_aabb_q16(MapuRay r, MapuBox box, MapuVec3 *out_n)
{
    double o[3] = { mapu_q2f(r.o.x), mapu_q2f(r.o.y), mapu_q2f(r.o.z) };
    double d[3] = { mapu_q2f(r.d.x), mapu_q2f(r.d.y), mapu_q2f(r.d.z) };
    double mn[3] = { mapu_q2f(box.bmin.x), mapu_q2f(box.bmin.y), mapu_q2f(box.bmin.z) };
    double mx[3] = { mapu_q2f(box.bmax.x), mapu_q2f(box.bmax.y), mapu_q2f(box.bmax.z) };
    double tmin = -1e30, tmax = 1e30;
    int i;
    for (i = 0; i < 3; i++) {
        double inv = (d[i] == 0) ? 1e30 : 1.0 / d[i];
        double t1 = (mn[i] - o[i]) * inv;
        double t2 = (mx[i] - o[i]) * inv;
        double a = t1, b = t2;
        if (a > b) { double sw=a; a=b; b=sw; }
        if (a > tmin) tmin = a;
        if (b < tmax)  tmax = b;
    }
    if (out_n) { out_n->x=0; out_n->y=0; out_n->z=0; }
    if (tmax >= tmin && tmin >= 0) return mapu_f2q(tmin);
    return -1;
}

/* ---- TRAJC: p(t) = ro + v0·t + ½·g·t² ---- */
MapuVec3 mapu_trajc_q16(MapuVec3 ro, MapuVec3 v0, MapuVec3 g, MapuScalar t)
{
    double tau = mapu_q2f(t);
    double half = 0.5 * tau * tau;
    return mapu_v3f(mapu_q2f(ro.x) + mapu_q2f(v0.x)*tau + mapu_q2f(g.x)*half,
                    mapu_q2f(ro.y) + mapu_q2f(v0.y)*tau + mapu_q2f(g.y)*half,
                    mapu_q2f(ro.z) + mapu_q2f(v0.z)*tau + mapu_q2f(g.z)*half);
}

/* ---- TEVNT: impacto no chão y=0 (½g t² + v0y t + roy = 0) ---- */
MapuScalar mapu_tevnt_ground_q16(MapuVec3 ro, MapuVec3 v0, MapuScalar g,
                                 MapuScalar *out_vy)
{
    double a = mapu_q2f(g) * 0.5;
    double b = mapu_q2f(v0.y);
    double c = mapu_q2f(ro.y);
    double disc = b*b - 4.0*a*c;
    double t = -1.0;
    int i;
    double roots[2];
    if (disc < 0) return -1;
    disc = sqrt(disc);
    roots[0] = (-b + disc) / (2.0*a);
    roots[1] = (-b - disc) / (2.0*a);
    for (i = 0; i < 2; i++) {
        if (roots[i] > 0 && (t < 0 || roots[i] < t)) t = roots[i];
    }
    if (t < 0) return -1;
    if (out_vy) *out_vy = mapu_f2q(mapu_q2f(v0.y) + 2.0*a*t); /* vy=v0y+g·t */
    return mapu_f2q(t);
}

/* ---- SPRG: mola-amortecedor, ζ≥0 ---- */
MapuScalar mapu_sprg_q16(MapuScalar x0, MapuScalar v0, MapuScalar k,
                         MapuScalar m, MapuScalar zeta, MapuScalar t)
{
    double wn = sqrt(mapu_q2f(k) / mapu_q2f(m));
    double z  = mapu_q2f(zeta);
    double tau = mapu_q2f(t);
    double w2 = 1.0 - z*z;
    double decay = exp(-z*wn*tau);
    if (w2 < 0) {                       /* super-amortecida */
        /* solução por exponenciais reais */
        double lam = wn * sqrt(-w2);
        double r1 = -z*wn + lam, r2 = -z*wn - lam;
        double A = (mapu_q2f(v0) - r2*mapu_q2f(x0)) / (r1 - r2);
        double B = mapu_q2f(x0) - A;
        return mapu_f2q(A*exp(r1*tau) + B*exp(r2*tau));
    } else {                            /* sub/crítica */
        double wd = wn * sqrt(w2);
        double A = mapu_q2f(x0);
        double B = (wd > 1e-12) ? (mapu_q2f(v0) + z*wn*A) / wd : 0.0;
        double val = decay * (A*cos(wd*tau) + B*sin(wd*tau));
        return mapu_f2q(val);
    }
}
