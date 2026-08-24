/* =====================================================================
 * sys.c — Simulador de barramento/DMA ColdFire V4æ + MontêLauro
 *
 * Modelo funcional (não ciclo-exato) para validação de arquitetura.
 * ===================================================================== */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "hotice/sys.h"
#include "hotice/mapu.h"

/* =====================================================================
 * Criação/destruição
 * ===================================================================== */
SysState *sys_create(void)
{
    SysState *s = calloc(1, sizeof(SysState));
    if (!s) return NULL;

    s->main_ram = calloc(1, SYS_MAIN_RAM_SIZE);
    s->vram = calloc(1, SYS_VRAM_SIZE);
    s->aram = calloc(1, SYS_ARAM_SIZE);
    s->dma_descs = calloc(256, sizeof(SysDmaDesc));

    if (!s->main_ram || !s->vram || !s->aram || !s->dma_descs) {
        sys_destroy(s);
        return NULL;
    }

    /* MLVU: 32 regs vetoriais */
    s->mlvu.regs[0][0] = 0;  /* dummy init */

    return s;
}

void sys_destroy(SysState *s)
{
    if (!s) return;
    free(s->main_ram);
    free(s->vram);
    free(s->aram);
    free(s->dma_descs);
    free(s->gpu.cmd_buffer);
    free(s);
}

/* =====================================================================
 * Helpers de endereçamento
 * ===================================================================== */
static inline int sys_addr_in_main(uint32_t addr)
{
    return (addr >= SYS_ADDR_MAIN_BASE && addr < SYS_ADDR_MAIN_BASE + SYS_MAIN_RAM_SIZE);
}
static inline int sys_addr_in_vram(uint32_t addr)
{
    return (addr >= SYS_ADDR_VRAM_BASE && addr < SYS_ADDR_VRAM_BASE + SYS_VRAM_SIZE);
}
static inline int sys_addr_in_aram(uint32_t addr)
{
    return (addr >= SYS_ADDR_ARAM_BASE && addr < SYS_ADDR_ARAM_BASE + SYS_ARAM_SIZE);
}
static inline uint8_t *sys_addr_ptr(SysState *s, uint32_t addr)
{
    if (sys_addr_in_main(addr)) return &s->main_ram[addr - SYS_ADDR_MAIN_BASE];
    if (sys_addr_in_vram(addr)) return &s->vram[addr - SYS_ADDR_VRAM_BASE];
    if (sys_addr_in_aram(addr)) return &s->aram[addr - SYS_ADDR_ARAM_BASE];
    return NULL;
}

/* =====================================================================
 * DMA — round-robin, 1 transação por ciclo de barramento
 * ===================================================================== */
static void sys_dma_step(SysState *s)
{
    for (int i = 0; i < 4; i++) {
        int chan = (s->dma_chan_pri + i) & 3;
        SysDmaChannel *d = &s->dma[chan];
        if (d->status != SYS_DMA_RUNNING) continue;

        SysDmaDesc *desc = &s->dma_descs[d->cur_desc];
        uint32_t remain = desc->byte_count - d->bytes_done;
        uint32_t chunk = (remain > 64) ? 64 : remain;  /* 64 bytes = 1 beat 64-bit */

        uint8_t *src = sys_addr_ptr(s, desc->src_addr + d->bytes_done);
        uint8_t *dst = sys_addr_ptr(s, desc->dst_addr + d->bytes_done);

        if (!src || !dst) {
            d->status = SYS_DMA_ERROR;
            continue;
        }

        memcpy(dst, src, chunk);
        d->bytes_done += chunk;
        s->bus_bytes += chunk;
        s->dma_bytes[chan] += chunk;
        s->bus_cycles += 1;

        if (d->bytes_done >= desc->byte_count) {
            if (desc->next_desc) {
                d->cur_desc = desc->next_desc;
                d->bytes_done = 0;
            } else {
                d->status = SYS_DMA_DONE;
            }
        }
        s->dma_chan_pri = (chan + 1) & 3;
        break;  /* 1 canal por ciclo */
    }
}

/* =====================================================================
 * MLVU — execução simplificada
 * ===================================================================== */
static void sys_mlvu_step(SysState *s)
{
    if (!s->mlvu.running || !s->mlvu.program) return;

    MlvuInstruction *ins = &s->mlvu.program[s->mlvu.pc];
    MapuScalar *vd = s->mlvu.regs[ins->vd];
    MapuScalar *vs = s->mlvu.regs[ins->vs];
    MapuScalar *vt = s->mlvu.regs[ins->vt];

    switch (ins->op) {
        case MLVU_OP_VADD:
            vd[0] = vs[0] + vt[0];
            vd[1] = vs[1] + vt[1];
            vd[2] = vs[2] + vt[2];
            vd[3] = vs[3] + vt[3];
            break;
        case MLVU_OP_VMUL:
            vd[0] = mapu_mul(vs[0], vt[0]);
            vd[1] = mapu_mul(vs[1], vt[1]);
            vd[2] = mapu_mul(vs[2], vt[2]);
            vd[3] = mapu_mul(vs[3], vt[3]);
            break;
        case MLVU_OP_VLERP: {
            MapuScalar t = (MapuScalar)ins->imm;  /* t em Q16 */
            vd[0] = vs[0] + mapu_mul(t, vt[0] - vs[0]);
            vd[1] = vs[1] + mapu_mul(t, vt[1] - vs[1]);
            vd[2] = vs[2] + mapu_mul(t, vt[2] - vs[2]);
            vd[3] = vs[3] + mapu_mul(t, vt[3] - vs[3]);
            break;
        }
        case MLVU_OP_VROT: {
            /* vrot: vd = q * vs * q^-1 (versor rotation)
             * vs = vetor (x,y,z,0), imm = índice do versor em regs[vt] */
            MapuScalar *q = s->mlvu.regs[ins->vt];
            MapuScalar sin_q, cos_q;
            MapuScalar ang = q[3];  /* w = ângulo em turns Q16 */
            mapu_csinc_q16(ang, &sin_q, &cos_q);
            /* Implementação simplificada: rotação em torno de Y (sistema destro, +Y up) */
            MapuScalar x = vs[0], z = vs[2];
            vd[0] = mapu_mul(cos_q, x) + mapu_mul(sin_q, z);
            vd[2] = -mapu_mul(sin_q, x) + mapu_mul(cos_q, z);
            vd[1] = vs[1];
            vd[3] = 0;
            break;
        }
        case MLVU_OP_VMAT4:
            /* vmat4: vd = M * vs. M vem da memória em imm */
            break;
        case MLVU_OP_VSKIN:
            /* vskin: skinning de vértice com pesos/ossos */
            break;
    }

    s->mlvu.pc++;
    if (s->mlvu.pc >= 1024 || s->mlvu.program[s->mlvu.pc].op == 0xFF) {
        s->mlvu.running = 0;
    }
}

/* =====================================================================
 * MAPU — execução
 * ===================================================================== */
static void sys_mapu_step(SysState *s)
{
    if (!s->mapu.running) return;
    s->mapu.cycles++;

    /* MAPU é combinacional (single-cycle) no hardware real.
     * Aqui apenas modela latência de 1 ciclo. */
    s->mapu.running = 0;
}

/* =====================================================================
 * GPU — execução simplificada
 * ===================================================================== */
static void sys_gpu_step(SysState *s)
{
    if (!s->gpu.running || s->gpu.cmd_head == s->gpu.cmd_tail) return;

    GpuCommand *cmd = &s->gpu.cmd_buffer[s->gpu.cmd_tail];
    s->gpu.cmd_tail = (s->gpu.cmd_tail + 1) & 1023;

    switch (cmd->type) {
        case GPU_CMD_CLEAR:
            /* limpa framebuffer na VRAM */
            memset(s->vram, 0, SYS_VRAM_SIZE);
            s->bus_bytes += SYS_VRAM_SIZE;
            break;
        case GPU_CMD_DRAW_TRI:
            /* rasteriza triângulo (simplificado: conta como 100 ciclos GPU) */
            s->bus_cycles += 100;
            break;
        case GPU_CMD_SWAP:
            s->gpu.running = 0;  /* frame done */
            break;
        default:
            break;
    }
}

/* =====================================================================
 * Step principal
 * ===================================================================== */
void sys_step(SysState *s, uint32_t cycles)
{
    for (uint32_t i = 0; i < cycles; i++) {
        s->cpu_cycles++;
        s->cpu_inst_count += 1;  /* 1 instr/ciclo ideal */

        sys_dma_step(s);
        sys_mlvu_step(s);
        sys_mapu_step(s);
        sys_gpu_step(s);
    }
}

/* =====================================================================
 * Submissão MLVU
 * ===================================================================== */
int sys_mlvu_submit(SysState *s, MlvuInstruction *prog, uint32_t count)
{
    (void)count;
    if (s->mlvu.running) return -1;
    s->mlvu.program = prog;
    s->mlvu.pc = 0;
    s->mlvu.running = 1;
    return 0;
}

/* =====================================================================
 * Submissão MAPU
 * ===================================================================== */
int sys_mapu_submit(SysState *s, int opcode, MapuScalar *args, int argc, MapuScalar *ret, int *retc)
{
    if (s->mapu.running) return -1;

    switch (opcode) {
        case 0: /* CSINC */
            if (argc >= 1 && retc && *retc >= 2) {
                mapu_csinc_q16(args[0], &ret[0], &ret[1]);
                *retc = 2;
            }
            break;
        case 1: /* ATAN2 */
            if (argc >= 2 && retc && *retc >= 1) {
                ret[0] = mapu_atan2_q16(args[0], args[1]);
                *retc = 1;
            }
            break;
        case 2: /* EXP2 */
            if (argc >= 1 && retc && *retc >= 1) {
                ret[0] = mapu_exp2_q16(args[0]);
                *retc = 1;
            }
            break;
        case 3: /* LOG2 */
            if (argc >= 1 && retc && *retc >= 1) {
                ret[0] = mapu_log2_q16(args[0]);
                *retc = 1;
            }
            break;
        case 4: /* ROOT2 */
            if (argc >= 3 && retc && *retc >= 2) {
                *retc = mapu_root2_q16(args[0], args[1], args[2], ret);
            }
            break;
        case 5: /* RAYSP */
            if (argc >= 7 && retc && *retc >= 1) {
                MapuRay r = { {args[0], args[1], args[2]}, {args[3], args[4], args[5]} };
                MapuSphere sp = { {0,0,0}, args[6] };
                MapuVec3 n;
                ret[0] = mapu_ray_sphere_q16(r, sp, retc ? &n : NULL);
                if (retc && *retc >= 4) { ret[1]=n.x; ret[2]=n.y; ret[3]=n.z; *retc=4; }
                else *retc = 1;
            }
            break;
        case 6: /* TRAJC */
            if (argc >= 10 && retc && *retc >= 3) {
                MapuVec3 ro = {args[0], args[1], args[2]};
                MapuVec3 v0 = {args[3], args[4], args[5]};
                MapuVec3 g  = {args[6], args[7], args[8]};
                MapuVec3 p = mapu_trajc_q16(ro, v0, g, args[9]);
                ret[0] = p.x; ret[1] = p.y; ret[2] = p.z;
                *retc = 3;
            }
            break;
        case 7: /* TEVNT */
            if (argc >= 7 && retc && *retc >= 1) {
                MapuVec3 ro = {args[0], args[1], args[2]};
                MapuVec3 v0 = {args[3], args[4], args[5]};
                MapuScalar vy;
                ret[0] = mapu_tevnt_ground_q16(ro, v0, args[6], retc ? &vy : NULL);
                if (retc && *retc >= 2) { ret[1] = vy; *retc = 2; }
                else *retc = 1;
            }
            break;
        case 8: /* SPRG */
            if (argc >= 6 && retc && *retc >= 1) {
                ret[0] = mapu_sprg_q16(args[0], args[1], args[2], args[3], args[4], args[5]);
                *retc = 1;
            }
            break;
        default:
            return -1;
    }
    s->mapu.running = 1;
    s->mapu.cycles = 1;
    return 0;
}

/* =====================================================================
 * Submissão GPU
 * ===================================================================== */
int sys_gpu_submit(SysState *s, GpuCommand cmd)
{
    uint32_t next = (s->gpu.cmd_head + 1) & 1023;
    if (next == s->gpu.cmd_tail) return -1;  /* buffer cheio */

    if (!s->gpu.cmd_buffer) {
        s->gpu.cmd_buffer = calloc(1024, sizeof(GpuCommand));
    }
    s->gpu.cmd_buffer[s->gpu.cmd_head] = cmd;
    s->gpu.cmd_head = next;
    s->gpu.running = 1;
    return 0;
}

/* =====================================================================
 * DMA
 * ===================================================================== */
int sys_dma_start(SysState *s, int chan, SysDmaType type, SysDmaDesc *desc)
{
    if (chan < 0 || chan > 3) return -1;
    if (s->dma[chan].status == SYS_DMA_RUNNING) return -1;

    int idx = 0;
    while (idx < 255 && s->dma_descs[idx].byte_count) idx++;
    if (idx >= 255) return -1;

    s->dma_descs[idx] = *desc;
    s->dma[chan].type = type;
    s->dma[chan].status = SYS_DMA_RUNNING;
    s->dma[chan].cur_desc = idx;
    s->dma[chan].bytes_done = 0;
    s->dma[chan].total_bytes = desc->byte_count;
    s->dma[chan].priority = chan;
    return 0;
}

SysDmaStatus sys_dma_status(SysState *s, int chan)
{
    if (chan < 0 || chan > 3) return SYS_DMA_ERROR;
    return s->dma[chan].status;
}

/* =====================================================================
 * Estatísticas
 * ===================================================================== */
void sys_get_stats(SysState *s, uint64_t *bus_bw, uint64_t *cpu_util, uint64_t *gpu_util)
{
    if (bus_bw) *bus_bw = (s->bus_cycles > 0) ? (s->bus_bytes * SYS_BUS_FREQ_HZ) / s->bus_cycles : 0;
    if (cpu_util) *cpu_util = (s->cpu_cycles > 0) ? (s->cpu_inst_count * 100) / s->cpu_cycles : 0;
    if (gpu_util) *gpu_util = 0;  /* TODO */
}