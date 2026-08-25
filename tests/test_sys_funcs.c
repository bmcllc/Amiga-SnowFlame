/* =====================================================================
 * test_sys_funcs.c — Testes do simulador de sistema V4æ (sem main)
 * ===================================================================== */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "hotice/sys.h"
#include "hotice/mapu.h"

#define MPEPS 0.01

static inline uint8_t *sys_test_addr_ptr(SysState *s, uint32_t addr)
{
    if ((addr - SYS_ADDR_MAIN_BASE) < SYS_MAIN_RAM_SIZE)
        return &s->main_ram[addr - SYS_ADDR_MAIN_BASE];
    return NULL;
}

static int test_sys_create_destroy(void)
{
    SysState *s = sys_create();
    if (!s) return 0;
    sys_destroy(s);
    return 1;
}

static int test_dma_memcpy(void)
{
    SysState *s = sys_create();
    if (!s) return 0;

    uint32_t src = SYS_ADDR_MAIN_BASE + 1024;
    uint32_t dst = SYS_ADDR_MAIN_BASE + 2048;
    const char *msg = "Hello DMA";
    memcpy(sys_test_addr_ptr(s, src), msg, 10);

    SysDmaDesc desc = { src, dst, 10, 0 };
    if (sys_dma_start(s, 0, SYS_DMA_MEM_TO_MEM, &desc) != 0) {
        sys_destroy(s); return 0;
    }

    for (int i = 0; i < 1000; i++) sys_step(s, 1);
    if (sys_dma_status(s, 0) != SYS_DMA_DONE) {
        sys_destroy(s); return 0;
    }

    int ok = memcmp(sys_test_addr_ptr(s, dst), msg, 10) == 0;
    sys_destroy(s);
    return ok;
}

static int test_mlvu_vadd(void)
{
    SysState *s = sys_create();
    if (!s) return 0;

    MlvuInstruction prog[] = {
        { MLVU_OP_VADD, 1, 2, 3, 0 },
        { 0xFF, 0, 0, 0, 0 }
    };

    s->mlvu.regs[2][0] = mapu_f2q(1.0); s->mlvu.regs[2][1] = mapu_f2q(2.0);
    s->mlvu.regs[2][2] = mapu_f2q(3.0); s->mlvu.regs[2][3] = mapu_f2q(4.0);
    s->mlvu.regs[3][0] = mapu_f2q(5.0); s->mlvu.regs[3][1] = mapu_f2q(6.0);
    s->mlvu.regs[3][2] = mapu_f2q(7.0); s->mlvu.regs[3][3] = mapu_f2q(8.0);

    sys_mlvu_submit(s, prog, 2);
    for (int i = 0; i < 10; i++) sys_step(s, 1);

    int ok = fabs(mapu_q2f(s->mlvu.regs[1][0]) - 6.0) < MPEPS &&
             fabs(mapu_q2f(s->mlvu.regs[1][1]) - 8.0) < MPEPS &&
             fabs(mapu_q2f(s->mlvu.regs[1][2]) - 10.0) < MPEPS &&
             fabs(mapu_q2f(s->mlvu.regs[1][3]) - 12.0) < MPEPS;
    sys_destroy(s);
    return ok;
}

static int test_mlvu_vlerp(void)
{
    SysState *s = sys_create();
    if (!s) return 0;

    MlvuInstruction prog[] = {
        { MLVU_OP_VLERP, 1, 2, 3, mapu_f2q(0.5) },
        { 0xFF, 0, 0, 0, 0 }
    };

    s->mlvu.regs[2][0] = mapu_f2q(0.0); s->mlvu.regs[2][1] = mapu_f2q(0.0);
    s->mlvu.regs[3][0] = mapu_f2q(10.0); s->mlvu.regs[3][1] = mapu_f2q(10.0);

    sys_mlvu_submit(s, prog, 2);
    for (int i = 0; i < 10; i++) sys_step(s, 1);

    int ok = fabs(mapu_q2f(s->mlvu.regs[1][0]) - 5.0) < MPEPS &&
             fabs(mapu_q2f(s->mlvu.regs[1][1]) - 5.0) < MPEPS;
    sys_destroy(s);
    return ok;
}

static int test_mlvu_vrot(void)
{
    SysState *s = sys_create();
    if (!s) return 0;

    MlvuInstruction prog[] = {
        { MLVU_OP_VROT, 1, 2, 3, 0 },
        { 0xFF, 0, 0, 0, 0 }
    };

    s->mlvu.regs[2][0] = mapu_f2q(1.0); s->mlvu.regs[2][2] = mapu_f2q(0.0);
    s->mlvu.regs[3][3] = MAPU_ONE >> 2;

    sys_mlvu_submit(s, prog, 2);
    for (int i = 0; i < 10; i++) sys_step(s, 1);

    int ok = fabs(mapu_q2f(s->mlvu.regs[1][0]) - 0.0) < 0.02 &&
             fabs(mapu_q2f(s->mlvu.regs[1][2]) - (-1.0)) < 0.02;
    sys_destroy(s);
    return ok;
}

static int test_mapu_submit(void)
{
    SysState *s = sys_create();
    if (!s) return 0;

    /* 45° = 0.125 turns = MAPU_ONE >> 3 */
    MapuScalar args[2] = { MAPU_ONE >> 3, MAPU_ONE >> 3 };
    MapuScalar ret[2];
    int retc = 2;

    if (sys_mapu_submit(s, 0, args, 1, ret, &retc) != 0) {
        sys_destroy(s); return 0;
    }
    sys_step(s, 1);

    int ok = fabs(mapu_q2f(ret[0]) - 0.7071) < 0.02 &&
             fabs(mapu_q2f(ret[1]) - 0.7071) < 0.02;
    sys_destroy(s);
    return ok;
}

static int test_mapu_trajc(void)
{
    SysState *s = sys_create();
    if (!s) return 0;

    MapuScalar args[] = {
        0, mapu_f2q(10.0), 0,
        0, 0, 0,
        0, mapu_f2q(-9.8), 0,
        MAPU_ONE
    };
    MapuScalar ret[3];
    int retc = 3;

    if (sys_mapu_submit(s, 6, args, 10, ret, &retc) != 0) {
        sys_destroy(s); return 0;
    }
    sys_step(s, 1);

    int ok = fabs(mapu_q2f(ret[1]) - 5.1) < 0.02;
    sys_destroy(s);
    return ok;
}

static int test_gpu_submit(void)
{
    SysState *s = sys_create();
    if (!s) return 0;

    GpuCommand cmd = { GPU_CMD_CLEAR, {0} };
    if (sys_gpu_submit(s, cmd) != 0) { sys_destroy(s); return 0; }

    sys_step(s, 100);
    int ok = 1;
    sys_destroy(s);
    return ok;
}

int run_sys_tests(void)
{
    int pass = 0, fail = 0;

    #define RUN(t) do { \
        if (t()) { printf("ok: %s\n", #t); pass++; } \
        else { printf("FAIL: %s\n", #t); fail++; } \
    } while(0)

    RUN(test_sys_create_destroy);
    RUN(test_dma_memcpy);
    RUN(test_mlvu_vadd);
    RUN(test_mlvu_vlerp);
    RUN(test_mlvu_vrot);
    RUN(test_mapu_submit);
    RUN(test_mapu_trajc);
    RUN(test_gpu_submit);

    printf("  sys: %d passed, %d failed\n", pass, fail);
    return fail;
}