/* =====================================================================
 * demo08_skinning_physics.c — MLVU Skinning + MAPU Física
 *
 * Demonstra integração MLVU (skinning esquelético) + MAPU (física analítica):
 * - Personagem simples (cubo com 2 ossos) animado via skinning MLVU
 * - Projétil lançado (TRAJC) colidindo com esfera (RAYSP) + chão (TEVNT)
 * - Mola-amortecedor (SPRG) anexada ao personagem
 * ===================================================================== */
#include <stdio.h>
#include <math.h>
#include "../include/hotice/hgl.h"
#include "../include/hotice/mapu.h"
#include "../include/hotice/sys.h"
#include "../include/hotice/types.h"

/* ---- geometria simples: cubo unitário centrado ---- */
static hglVertex g_cube_verts[8];

static void init_cube_verts(void)
{
    const float p[8][3] = {
        {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
    };
    const float c[8][4] = {
        {1,0,0,1}, {0,1,0,1}, {0,0,1,1}, {1,1,0,1},
        {1,0,1,1}, {0,1,1,1}, {1,1,1,1}, {0.5f,0.5f,0.5f,1},
    };
    for (int i = 0; i < 8; i++) {
        g_cube_verts[i].pos[0] = p[i][0];
        g_cube_verts[i].pos[1] = p[i][1];
        g_cube_verts[i].pos[2] = p[i][2];
        g_cube_verts[i].nrm[0] = g_cube_verts[i].nrm[1] = g_cube_verts[i].nrm[2] = 0;
        g_cube_verts[i].uv[0] = g_cube_verts[i].uv[1] = 0;
        g_cube_verts[i].col[0] = c[i][0];
        g_cube_verts[i].col[1] = c[i][1];
        g_cube_verts[i].col[2] = c[i][2];
        g_cube_verts[i].col[3] = c[i][3];
        g_cube_verts[i].bone[0] = g_cube_verts[i].bone[1] = 
        g_cube_verts[i].bone[2] = g_cube_verts[i].bone[3] = 0;
        g_cube_verts[i].bw[0] = g_cube_verts[i].bw[1] = 
        g_cube_verts[i].bw[2] = g_cube_verts[i].bw[3] = 0;
    }
}

static const uint32_t g_cube_idx[36] = {
    0,1,2, 0,2,3,  /* back */
    4,6,5, 4,7,6,  /* front */
    0,3,7, 0,7,4,  /* left */
    1,5,6, 1,6,2,  /* right */
    3,2,6, 3,6,7,  /* top */
    0,4,5, 0,5,1,  /* bottom */
};

/* ---- paleta de skinning: 2 ossos ---- */
static float g_skin_palette[32];  /* 2 ossos * 16 floats */

static void mat4_identity(float m[16])
{
    for (int i=0;i<16;i++) m[i] = 0;
    m[0]=m[5]=m[10]=m[15]=1;
}

static void mat4_mul(float dst[16], const float a[16], const float b[16])
{
    float tmp[16];
    for (int i=0;i<4;i++)
        for (int j=0;j<4;j++) {
            tmp[i*4+j] = a[i*4+0]*b[0*4+j] + a[i*4+1]*b[1*4+j] +
                         a[i*4+2]*b[2*4+j] + a[i*4+3]*b[3*4+j];
        }
    for (int i=0;i<16;i++) dst[i] = tmp[i];
}

static void mat4_translate(float m[16], float x, float y, float z)
{
    mat4_identity(m);
    m[12] = x; m[13] = y; m[14] = z;
}

static void mat4_rotate_y(float m[16], float angle)
{
    float c = cosf(angle), s = sinf(angle);
    mat4_identity(m);
    m[0] = c;  m[2] = s;
    m[8] = -s; m[10] = c;
}

static void mat4_rotate_x(float m[16], float angle)
{
    float c = cosf(angle), s = sinf(angle);
    mat4_identity(m);
    m[5] = c; m[6] = -s;
    m[9] = s; m[10] = c;
}

static void build_skin_palette(float angle_y, float bend)
{
    float m0[16], m1[16], t[16], r[16];
    
    /* Osso 0: raiz (rotação Y + translação) */
    mat4_identity(m0);
    mat4_rotate_y(r, angle_y);
    mat4_translate(t, 0, 1.0f, 0);
    mat4_mul(m0, t, r);
    
    /* Osso 1: filho dobrado */
    mat4_identity(m1);
    mat4_translate(t, 0, 2.0f, 0);
    mat4_rotate_x(r, bend);
    mat4_mul(m1, t, r);
    mat4_mul(m1, m1, m0);  /* relativo ao pai */
    
    for (int i = 0; i < 16; i++) {
        g_skin_palette[i] = m0[i];
        g_skin_palette[16+i] = m1[i];
    }
}

/* ---- simulação MAPU ---- */
typedef struct {
    MapuVec3 pos, vel;
    MapuScalar life;
    int active;
} Projectile;

static Projectile g_proj;
static MapuScalar g_spring_x, g_spring_v;
static MapuScalar g_time_accum = 0;

static void spawn_projectile(void)
{
    g_proj.pos = mapu_v3f(0, 2, -5);
    g_proj.vel = mapu_v3f(0, 0, mapu_f2q(10.0));
    g_proj.life = mapu_f2q(5.0);
    g_proj.active = 1;
}

static void update_physics(MapuScalar dt)
{
    g_time_accum += dt;

    if (g_proj.active) {
        MapuVec3 g = mapu_v3f(0, -9.8, 0);
        MapuVec3 new_pos = mapu_trajc_q16(g_proj.pos, g_proj.vel, g, dt);
        
        /* Colisão com esfera (RAYSP) */
        MapuRay ray = { g_proj.pos, g_proj.vel };
        MapuSphere sp = { mapu_v3f(0, 0, 0), mapu_f2q(1.5) };
        MapuVec3 hit_n;
        MapuScalar hit_t = mapu_ray_sphere_q16(ray, sp, &hit_n);
        if (hit_t >= 0 && mapu_q2f(hit_t) < mapu_q2f(dt)) {
            MapuScalar dot = mapu_mul(g_proj.vel.x, hit_n.x) +
                             mapu_mul(g_proj.vel.y, hit_n.y) +
                             mapu_mul(g_proj.vel.z, hit_n.z);
            g_proj.vel.x -= mapu_mul(mapu_f2q(2.0), mapu_mul(dot, hit_n.x));
            g_proj.vel.y -= mapu_mul(mapu_f2q(2.0), mapu_mul(dot, hit_n.y));
            g_proj.vel.z -= mapu_mul(mapu_mul(mapu_f2q(2.0), dot), hit_n.z);
            new_pos = g_proj.pos;
        }
        
        /* Colisão com chão (TEVNT) */
        if (mapu_q2f(new_pos.y) < 0.5f) {
            new_pos.y = mapu_f2q(0.5);
            g_proj.vel.y = -mapu_mul(g_proj.vel.y, mapu_f2q(0.5));
        }
        
        g_proj.pos = new_pos;
        g_proj.life -= dt;
        if (mapu_q2f(g_proj.life) <= 0) g_proj.active = 0;
    }

    /* Mola (SPRG) */
    g_spring_x = mapu_sprg_q16(g_spring_x, g_spring_v, 
                               mapu_f2q(10.0), mapu_f2q(1.0),
                               mapu_f2q(0.3), dt);
}

static void draw_character(hglCtx *ctx, float angle_y, float bend)
{
    build_skin_palette(angle_y, bend);
    hglSkinPalette(ctx, g_skin_palette, 2);
    hglEnable(ctx, HGL_SKINNING);

    hglVertex v[8];
    for (int i = 0; i < 8; i++) {
        v[i] = g_cube_verts[i];
        v[i].bone[0] = (v[i].pos[1] < 0) ? 0 : 1;
        v[i].bw[0] = 1.0f;
        v[i].bone[1] = v[i].bone[2] = v[i].bone[3] = 0;
        v[i].bw[1] = v[i].bw[2] = v[i].bw[3] = 0;
    }

    hglDrawTrianglesIndexed(ctx, 36, g_cube_idx, v);
    hglDisable(ctx, HGL_SKINNING);
}

static void draw_projectile(hglCtx *ctx)
{
    if (!g_proj.active) return;
    
    hglVertex v[4];
    float x = mapu_q2f(g_proj.pos.x);
    float y = mapu_q2f(g_proj.pos.y);
    float z = mapu_q2f(g_proj.pos.z);
    float s = 0.2f;
    
    v[0].pos[0] = x-s; v[0].pos[1] = y-s; v[0].pos[2] = z;
    v[1].pos[0] = x+s; v[1].pos[1] = y-s; v[1].pos[2] = z;
    v[2].pos[0] = x+s; v[2].pos[1] = y+s; v[2].pos[2] = z;
    v[3].pos[0] = x-s; v[3].pos[1] = y+s; v[3].pos[2] = z;
    for (int i=0;i<4;i++) {
        v[i].col[0]=1; v[i].col[1]=1; v[i].col[2]=0; v[i].col[3]=1;
        v[i].nrm[0]=v[i].nrm[1]=v[i].nrm[2]=0;
        v[i].uv[0]=v[i].uv[1]=0;
        v[i].bone[0]=v[i].bone[1]=v[i].bone[2]=v[i].bone[3]=0;
        v[i].bw[0]=v[i].bw[1]=v[i].bw[2]=v[i].bw[3]=0;
    }
    
    static const uint32_t qidx[6] = {0,1,2, 0,2,3};
    hglDrawTrianglesIndexed(ctx, 6, qidx, v);
}

static void draw_spring(hglCtx *ctx)
{
    float x = mapu_q2f(g_spring_x);
    if (fabsf(x) < 0.01f) return;
    
    hglVertex v[4];
    float y = 2.0f;
    float s = 0.1f;
    
    v[0].pos[0] = -s; v[0].pos[1] = y;   v[0].pos[2] = x;
    v[1].pos[0] =  s; v[1].pos[1] = y;   v[1].pos[2] = x;
    v[2].pos[0] =  s; v[2].pos[1] = y+0.5f; v[2].pos[2] = x;
    v[3].pos[0] = -s; v[3].pos[1] = y+0.5f; v[3].pos[2] = x;
    for (int i=0;i<4;i++) {
        v[i].col[0]=0; v[i].col[1]=1; v[i].col[2]=1; v[i].col[3]=1;
        v[i].nrm[0]=v[i].nrm[1]=v[i].nrm[2]=0;
        v[i].uv[0]=v[i].uv[1]=0;
        v[i].bone[0]=v[i].bone[1]=v[i].bone[2]=v[i].bone[3]=0;
        v[i].bw[0]=v[i].bw[1]=v[i].bw[2]=v[i].bw[3]=0;
    }
    
    static const uint32_t qidx[6] = {0,1,2, 0,2,3};
    hglDrawTrianglesIndexed(ctx, 6, qidx, v);
}

static void draw_floor(hglCtx *ctx)
{
    hglVertex v[4];
    v[0].pos[0] = -10; v[0].pos[1] = 0; v[0].pos[2] = -10;
    v[1].pos[0] =  10; v[1].pos[1] = 0; v[1].pos[2] = -10;
    v[2].pos[0] =  10; v[2].pos[1] = 0; v[2].pos[2] =  10;
    v[3].pos[0] = -10; v[3].pos[1] = 0; v[3].pos[2] =  10;
    for (int i=0;i<4;i++) {
        v[i].col[0]=0.3f; v[i].col[1]=0.3f; v[i].col[2]=0.3f; v[i].col[3]=1;
        v[i].nrm[0]=0; v[i].nrm[1]=1; v[i].nrm[2]=0;
        v[i].uv[0]=v[i].uv[1]=0;
        v[i].bone[0]=v[i].bone[1]=v[i].bone[2]=v[i].bone[3]=0;
        v[i].bw[0]=v[i].bw[1]=v[i].bw[2]=v[i].bw[3]=0;
    }
    static const uint32_t qidx[6] = {0,1,2, 0,2,3};
    hglDrawTrianglesIndexed(ctx, 6, qidx, v);
}

static void draw_sphere(hglCtx *ctx)
{
    hglVertex v[4];
    float sr = 1.5f;
    v[0].pos[0] = -sr; v[0].pos[1] = -sr; v[0].pos[2] = 0;
    v[1].pos[0] =  sr; v[1].pos[1] = -sr; v[1].pos[2] = 0;
    v[2].pos[0] =  sr; v[2].pos[1] =  sr; v[2].pos[2] = 0;
    v[3].pos[0] = -sr; v[3].pos[1] =  sr; v[3].pos[2] = 0;
    for (int i=0;i<4;i++) {
        v[i].col[0]=1; v[i].col[1]=0; v[i].col[2]=0; v[i].col[3]=1;
        v[i].nrm[0]=0; v[i].nrm[1]=0; v[i].nrm[2]=1;
        v[i].uv[0]=v[i].uv[1]=0;
        v[i].bone[0]=v[i].bone[1]=v[i].bone[2]=v[i].bone[3]=0;
        v[i].bw[0]=v[i].bw[1]=v[i].bw[2]=v[i].bw[3]=0;
    }
    static const uint32_t qidx[6] = {0,1,2, 0,2,3};
    hglDrawTrianglesIndexed(ctx, 6, qidx, v);
}

/* ---- matriz de view (lookat) ---- */
static void mat4_lookat(float m[16], float ex, float ey, float ez,
                        float cx, float cy, float cz,
                        float ux, float uy, float uz)
{
    float fx = cx-ex, fy = cy-ey, fz = cz-ez;
    float flen = sqrtf(fx*fx+fy*fy+fz*fz);
    fx/=flen; fy/=flen; fz/=flen;
    float sx = fy*uz - fz*uy;
    float sy = fz*ux - fx*uz;
    float sz = fx*uy - fy*ux;
    float slen = sqrtf(sx*sx+sy*sy+sz*sz);
    sx/=slen; sy/=slen; sz/=slen;
    float ux2 = sy*fz - sz*fy;
    float uy2 = sz*fx - sx*fz;
    float uz2 = sx*fy - sy*fx;
    m[0]=sx; m[1]=ux2; m[2]=-fx; m[3]=0;
    m[4]=sy; m[5]=uy2; m[6]=-fy; m[7]=0;
    m[8]=sz; m[9]=uz2; m[10]=-fz; m[11]=0;
    m[12]=-(sx*ex+sy*ey+sz*ez);
    m[13]=-(ux2*ex+uy2*ey+uz2*ez);
    m[14]=fx*ex+fy*ey+fz*ez;
    m[15]=1;
}

int main(void)
{
    init_cube_verts();
    
    hglCtx *ctx = hglCreateContext(320, 240, 4);
    SysState *sys = sys_create();
    
    /* Câmera */
    hglMatrixMode(ctx, HGL_PROJECTION);
    hglLoadIdentity(ctx);
    hglPerspective(ctx, 60.0f * (float)HI_PI / 180.0f, 320.0f/240.0f, 0.1f, 50.0f);
    hglMatrixMode(ctx, HGL_MODELVIEW);
    hglLoadIdentity(ctx);
    hglLookAt(ctx, 0, 3, 10,  0, 1, 0,  0, 1, 0);
    
    /* Matriz de view base (lookat 0,3,10 -> 0,1,0, up 0,1,0) */
    float view_matrix[16];
    mat4_lookat(view_matrix, 0, 3, 10,  0, 1, 0,  0, 1, 0);
    
    /* Luz */
    hglEnable(ctx, HGL_BUMP_DOT3);
    hglLightDirf(ctx, 0.5f, -0.5f, 0.7f);
    hglLightColor4f(ctx, 1, 1, 1, 1);
    hglAmbient4f(ctx, 0.3f, 0.3f, 0.3f, 1);
    hglEnable(ctx, HGL_TEXTURE_2D);
    
    /* Textura */
    uint32_t tex_pix[64];
    for (int i=0;i<64;i++) tex_pix[i] = 0xFFFFFFFF;
    hglTex *tex = hglTexCreateRGBA8(8, 8, tex_pix);
    hglBindTexture(ctx, 0, tex);
    
    spawn_projectile();
    g_spring_x = 0; g_spring_v = mapu_f2q(2.0);
    
    MapuScalar frame_dt = MAPU_ONE / 60;
    float angle = 0, bend = 0;
    int frame = 0;
    
    while (frame < 300) {
        hglClearColor4f(ctx, 0.1f, 0.1f, 0.2f, 1);
        hglClearDepth(ctx, 0xFFFFFFFF);
        hglClearStencil(ctx, 0);
        
        angle += 0.02f;
        bend = sinf(frame * 0.05f) * 0.5f;
        
        update_physics(frame_dt);
        
        hglFrameBegin(ctx);
        
        /* Restore base view matrix */
        hglMatrixMode(ctx, HGL_MODELVIEW);
        hglLoadMatrixf(ctx, view_matrix);
        
        draw_floor(ctx);
        draw_sphere(ctx);
        
        /* Personagem */
        float m[16];
        mat4_translate(m, 0, 0.5f, 0);
        hglMultMatrixf(ctx, m);
        draw_character(ctx, angle, bend);
        
        /* Projétil e mola (usam view_matrix base) */
        hglLoadMatrixf(ctx, view_matrix);
        draw_projectile(ctx);
        draw_spring(ctx);
        
        hglFrameEnd(ctx);
        frame++;
    }
    
    /* Métricas finais */
    printf("Demo08: frames=%d\n", frame);
    printf("  Projétil final: (%.2f, %.2f, %.2f) ativo=%d\n",
           mapu_q2f(g_proj.pos.x), mapu_q2f(g_proj.pos.y), mapu_q2f(g_proj.pos.z), g_proj.active);
    printf("  Mola: x=%.3f v=%.3f\n", mapu_q2f(g_spring_x), mapu_q2f(g_spring_v));
    printf("  Tempo simulado: %.2fs\n", mapu_q2f(g_time_accum));
    
    hglTexDestroy(tex);
    hglDestroyContext(ctx);
    sys_destroy(sys);
    return 0;
}