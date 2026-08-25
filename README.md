# SnowFlame — Console Amiga hipotético (1999)

Projeto completo do console fictício **SnowFlame**: análise de viabilidade,
dossiês de hardware e implementação de referência em C99 da GPU **Hot-ice**
(arquitetura *Homotopia*, MontêLauro) com as unidades de CPU **MLVU/MAPU**
do ColdFire **V4æ**.

## Estrutura

```
.
├── README.md                  ← este arquivo
├── doc/                       ← dossiês e análise
│   ├── ColdFire-V4ae-cpu-dossie.md          ← CPU V4æ + MLVU + MAPU
│   ├── Hot-ice-gpu-dossie.md                ← GPU Hot-ice (Homotopia)
│   └── SnowFlame-analise-de-viabilidade.md  ← viabilidade v2.1
├── include/hotice/            ← APIs públicas
│   ├── types.h                ← mat4, vetores, hiQuat (versores MLVU)
│   ├── hgl.h                  ← API HGL estilo OpenGL imediato
│   ├── mapu.h                 ← MAPU física analítica Q16.16
│   └── sys.h                  ← simulador barramento/DMA V4æ
├── src/                       ← implementação C99
├── tests/                     ← suíte de testes (65 verificações)
├── demos/                     ← 9 demos verificáveis (gravam PNG)
└── Makefile
```

## Status do hardware implementado

| Componente do dossiê | Estado | Onde |
|---|---|---|
| TBR — tiles 32×32, color+Z em scratch, resolve p/ framebuffer | ✅ | `src/raster.c` |
| CAA — AA por tile quase grátis (grades rotacionadas 2×/4×) | ✅ | `src/context.c`, `raster.c` |
| HDE — T&L unificado: morph targets + skinning + Gouraud | ✅ | `src/hde.c` |
| CCB — culling por continuidade (patches topológicos: frustum por bbox + cone de normais em lote) | ✅ | `src/ccb.c` |
| HIQTC — compressão de textura 4:1 (âncoras RGB565, PCA) | ✅ | `src/hiqtc.c` |
| HIQTC modo paleta 8:1 (índice 8bpp + paleta 256 cores median-cut) | ✅ | `src/hiqtc.c` |
| Mipmaps + filtragem trilinear analítica | ✅ | `src/texture.c` |
| Depth 24 bits + stencil 8 (word por amostra `(z24<<8)|st`) | ✅ | `raster.c` |
| Clipping homogêneo no near (Sutherland–Hodgman) | ✅ | `hde.c` |
| DOT3 bump (normal map, luz por pixel) | ✅ | `raster.c`, `hde.c` |
| Env map esférico (sphere map via normal view-space) | ✅ | `raster.c`, `hde.c` |
| Versores hiQuat (rotação, slerp, mat4) — espelho das ops MLVU | ✅ | `include/hotice/types.h` |
| **MAPU** física analítica — microcódigo **Q16.16 fixo**: CORDIC 16 iter (CSINC/ATAN2), EXP2/LOG2 (LUT 256 + interp), sqrt Newton-Raphson, ROOT2, RAYSP/RAYPL/RAYBB, TRAJC, TEVNT, SPRG | ✅ | `src/mapu.c`, `include/hotice/mapu.h` |
| **Simulador V4æ** — CPU @266 MHz, MLVU (VADD/VROT/VLERP/VMUL), MAPU, GPU @143 MHz, DMA 4 canais round-robin 64-bit @133 MHz | ✅ | `src/sys.c`, `include/hotice/sys.h` |
| Testes automatizados | ✅ **76/76** (68 renderizador+MAPU+CCB · 8 sistema) | `tests/` |

## Build & execução

```sh
make            # lib + 9 demos + test_all
./build/test_all
make run-demos  # roda todas as demos (geram PNG em build/)
```

## Demos

| Demo | Mostra | Verificação |
|---|---|---|
| `demo01_tbr_morph` | chão HIQTC trilinear + esfera morfando (HDE) + CAA 2× | céu/horizonte/esfera visíveis; tris in/out no console |
| `demo02_hiqtc` | cena lado a lado RGBA8 vs HIQTC | PSNR ≈ 34.2 dB |
| `demo03_mipmap` | campo distante sem mip vs trilinear | Laplaciano 131 → 62 (~2.1× mais suave) |
| `demo04_reflection` | reflexão planar via stencil multipasse | centroides obj≈refl; limitado ao espelho |
| `demo05_shading` | bump ON/OFF + esfera cromada env map | tons parede 5 → 81 (luz por pixel) |
| `demo06_hiqtc_p8` | RGBA8 × HIQTC-4:1 × HIQTC-P8 | baseline 138 dB · HIQTC 38.4 dB · P8 33.2 dB |
| `demo07_mapu` | projétil TRAJC vs esfera (RAYSP) + chão (TEVNT) | t_chao=1.618s · hit esfera dt=3.895 → acerto |
| `demo08_skinning_physics` | personagem 2 ossos (skinning MLVU/HDE) + projétil TRAJC + colisão RAYSP/TEVNT + mola SPRG | 300 frames integrando animação esquelética e física analítica |
| `demo09_ccb` | sala com 4 pilares via CCB (patches descartados inteiros: frustum + cone de normais) | **72% dos vértices poupados** (meta dossiê 40–60%) · saída bit-idêntica ao caminho direto |

## Decisões documentadas

- **Regra de preenchimento top-left** real (`HI_EDGE_OK`): elimina rachaduras E
  repintadas em arestas compartilhadas sem depender do teste de depth.
- **Estado congelado por draw** na submissão (stencil, texturas, bump/env,
  luz): multipasse funciona dentro de um frame trocando o estado entre draws.
  Depth/stencil reiniciam dos valores de clear a cada `FrameBegin`.
- **Luz**: `hglLightDirf` recebe o vetor **apontando para a luz**, em espaço
  da câmera. Iluminação é Gouraud (por vértice), como o HDE propõe.
- **Mipmaps**: cadeia box-filter RGBA8; trilinear com derivadas analíticas
  de tela sobre varyings pré-divididos.
- **CCB (dossiê §2.2)**: malhas com conectividade intacta viram PATCHES
  topológicos (union-find sobre arestas, chunk limitado). Por draw, cada
  patch é testado inteiro — frustum pelos 8 cantos do bbox e backface pelo
  cone de normais (`φ + θ ≤ 90°`); patches rejeitados nunca mandam vértice
  ao pipeline. A saída é bit-idêntica ao caminho direto. Limitação: usa as
  posições BASE do modelo (malhas com morph/skin fora do bbox base devem
  usar o caminho direto).
- **MAPU Q16.16 bit-exato**: sem ponto flutuante — CORDIC 16 iterações com
  LUT atan(2^-i) em turns, LOG2 via tabela 256 entradas + interpolação linear,
  EXP2 Taylor grau 3, aritmética saturada estilo MAC/EMAC do V4e. Determinístico
  entre builds.
- **Simplificações conhecidas**: normais não recebem a model matrix separada;
  raster não culle triângulos individuais isolados (o backface vive no CCB,
  em lote); simulador é funcional (não ciclo-exato).

## Próximos passos (roadmap)

Env map cúbico · texturas ≥512² · VMAT4/VSKIN completos no simulador MLVU ·
perfilador de barramento (largura de banda por subsistema) ·
bbox hierárquico por patch (AABB tree) para culling mais fino.
