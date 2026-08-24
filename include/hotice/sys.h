/* =====================================================================
 * sys.h — Simulador de barramento/DMA ColdFire V4æ + MontêLauro
 *
 * Modelo de sistema para o console SnowFlame (1999):
 * - CPU: ColdFire V4æ @ 266 MHz (in-order, MAC/EMAC, FPU, sem MMU)
 * - MLVU: Unidade vetorial/versor Q16.16/Q8.8/Q2.14 (VADD, VROT, VLERP, VMUL, VMAT4, VSKIN)
 * - MAPU: Unidade física analítica (CORDIC, ATAN2, EXP2/LOG2, ROOT2, RAY*, TRAJC, TEVNT, SPRG)
 * - GPU: Hot-ice @ 143 MHz (HDE T&L, TBR 32x32, HIQTC 4:1/8:1, CAA)
 *
 * Barramento: 64-bit @ 133 MHz → 1.06 GB/s peak
 * DMA: 4 canais independentes, scatter/gather, prioridade round-robin
 * Memória: 20 MB RAM principal + 8 MB VRAM + 2 MB ARAM
 * ===================================================================== */
#ifndef HI_SYS_H
#define HI_SYS_H

#include <stdint.h>
#include "hotice/mapu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =====================================================================
 * Constantes de sistema
 * ===================================================================== */
#define SYS_CPU_FREQ_HZ      266000000   /* 266 MHz */
#define SYS_GPU_FREQ_HZ      143000000   /* 143 MHz */
#define SYS_BUS_FREQ_HZ      133000000   /* 133 MHz */
#define SYS_BUS_WIDTH_BITS   64          /* 64-bit barramento */
#define SYS_BUS_BW_BYTES_S   (SYS_BUS_FREQ_HZ * SYS_BUS_WIDTH_BITS / 8)  /* 1.06 GB/s */

#define SYS_MAIN_RAM_SIZE    (20 * 1024 * 1024)   /* 20 MB */
#define SYS_VRAM_SIZE        (8 * 1024 * 1024)    /* 8 MB */
#define SYS_ARAM_SIZE        (2 * 1024 * 1024)    /* 2 MB */
#define SYS_MEMCARD_SIZE     (16 * 1024 * 1024)   /* 16 MB per card */

/* Endereçamento físico (simplificado) */
#define SYS_ADDR_MAIN_BASE   0x00000000
#define SYS_ADDR_VRAM_BASE   0x10000000
#define SYS_ADDR_ARAM_BASE   0x18000000
#define SYS_ADDR_IO_BASE     0x1C000000
#define SYS_ADDR_MLVU_BASE   0x1C001000
#define SYS_ADDR_MAPU_BASE   0x1C002000
#define SYS_ADDR_GPU_BASE    0x1C003000

/* =====================================================================
 * Tipos de transação DMA
 * ===================================================================== */
typedef enum {
    SYS_DMA_MEM_TO_MEM = 0,
    SYS_DMA_MEM_TO_MLVU = 1,
    SYS_DMA_MEM_TO_MAPU = 2,
    SYS_DMA_MEM_TO_GPU  = 3,
    SYS_DMA_MLVU_TO_MEM = 4,
    SYS_DMA_MAPU_TO_MEM = 5,
    SYS_DMA_GPU_TO_MEM  = 6,
} SysDmaType;

typedef enum {
    SYS_DMA_IDLE = 0,
    SYS_DMA_RUNNING = 1,
    SYS_DMA_DONE = 2,
    SYS_DMA_ERROR = 3,
} SysDmaStatus;

/* Descritor scatter/gather */
typedef struct {
    uint32_t src_addr;
    uint32_t dst_addr;
    uint32_t byte_count;
    uint32_t next_desc;  /* 0 = último */
} SysDmaDesc;

/* Canal DMA */
typedef struct {
    SysDmaType type;
    SysDmaStatus status;
    uint32_t cur_desc;
    uint32_t bytes_done;
    uint32_t total_bytes;
    int priority;  /* 0=alto, 3=baixo */
} SysDmaChannel;

/* =====================================================================
 * MLVU — comandos (interface simplificada)
 * ===================================================================== */
typedef enum {
    MLVU_OP_VADD = 0,    /* vadd vd, vs, vt */
    MLVU_OP_VROT = 1,    /* vrot vd, vs, q  (versor rotation) */
    MLVU_OP_VLERP = 2,   /* vlerp vd, vs, vt, t */
    MLVU_OP_VMUL = 3,    /* vmul vd, vs, vt (component-wise) */
    MLVU_OP_VMAT4 = 4,   /* vmat4 vd, M, vs (matrix-vector) */
    MLVU_OP_VSKIN = 5,   /* vskin vd, vs, weights, bones (skinning) */
} MlvuOpcode;

typedef struct {
    MlvuOpcode op;
    uint8_t vd, vs, vt;
    uint32_t imm;  /* imediato ou endereço de parâmetros */
} MlvuInstruction;

/* Estado MLVU */
typedef struct {
    MapuScalar regs[32][4];  /* 32 registradores vetoriais Q16.16 (x,y,z,w) */
    MlvuInstruction *program;
    uint32_t pc;
    int running;
} MlvuState;

/* =====================================================================
 * MAPU — interface (usa mapu.h existente)
 * ===================================================================== */
typedef struct {
    int running;
    uint32_t cycles;
} MapuState;

/* =====================================================================
 * GPU — comandos HGL (interface simplificada)
 * ===================================================================== */
typedef enum {
    GPU_CMD_CLEAR = 0,
    GPU_CMD_DRAW_TRI = 1,
    GPU_CMD_DRAW_MESH = 2,
    GPU_CMD_SET_TEX = 3,
    GPU_CMD_SET_STATE = 4,
    GPU_CMD_SWAP = 5,
} GpuCmdType;

typedef struct {
    GpuCmdType type;
    uint32_t param[8];
} GpuCommand;

/* Estado GPU */
typedef struct {
    GpuCommand *cmd_buffer;
    uint32_t cmd_head, cmd_tail;
    int running;
} GpuState;

/* =====================================================================
 * Sistema completo
 * ===================================================================== */
typedef struct {
    /* Memória */
    uint8_t *main_ram;
    uint8_t *vram;
    uint8_t *aram;

    /* CPU (modelo simplificado: apenas contadores de ciclo) */
    uint64_t cpu_cycles;
    uint64_t cpu_inst_count;

    /* MLVU */
    MlvuState mlvu;

    /* MAPU */
    MapuState mapu;

    /* GPU */
    GpuState gpu;

    /* DMA */
    SysDmaChannel dma[4];
    SysDmaDesc *dma_descs;
    int dma_chan_pri;  /* canal atual (round-robin) */

    /* Estatísticas */
    uint64_t bus_cycles;
    uint64_t bus_bytes;
    uint64_t dma_bytes[4];
} SysState;

/* =====================================================================
 * API do simulador
 * ===================================================================== */

SysState *sys_create(void);
void sys_destroy(SysState *s);

/* Executa N ciclos de CPU + periféricos */
void sys_step(SysState *s, uint32_t cycles);

/* Submete comando MLVU */
int sys_mlvu_submit(SysState *s, MlvuInstruction *prog, uint32_t count);

/* Submete comando MAPU (opcode + args) */
int sys_mapu_submit(SysState *s, int opcode, MapuScalar *args, int argc, MapuScalar *ret, int *retc);

/* Submete comando GPU */
int sys_gpu_submit(SysState *s, GpuCommand cmd);

/* Inicia DMA */
int sys_dma_start(SysState *s, int chan, SysDmaType type, SysDmaDesc *desc);

/* Verifica conclusão DMA */
SysDmaStatus sys_dma_status(SysState *s, int chan);

/* Estatísticas */
void sys_get_stats(SysState *s, uint64_t *bus_bw, uint64_t *cpu_util, uint64_t *gpu_util);

#ifdef __cplusplus
}
#endif
#endif /* HI_SYS_H */