# Hot-ice — Renderizador de referência (C99)

Implementação da GPU **Hot-ice** (arquitetura *Homotopia*, MontêLauro)
descrita em `../../Hot-ice-gpu-dossie.md`, como biblioteca C99 sem dependências
externas + API convencional estilo OpenGL (**HGL**) + suíte de testes +
demos que gravam PNG.

## Status

| Componente do dossiê | Estado | Onde |
|---|---|---|
| TBR — tiles 32×32, color+Z em scratch, resolve p/ framebuffer | ✅ | `src/raster.c` |
| CAA — AA por tile quase grátis (grades rotacionadas 2×/4×) | ✅ | `src/context.c`, `raster.c` |
| HDE — T&L unificado: morph targets + skinning + Gouraud | ✅ | `src/hde.c` |
| CCB — culling por continuidade (versão simplificada: bbox/clip) | ⚠️ parcial | `hde.c` |
| HIQTC — compressão de textura 4:1 (âncoras RGB565, PCA) | ✅ | `src/hiqtc.c` |
| HIQTC modo paleta 8:1 (índice 8bpp + paleta 256 cores median-cut) | ✅ | `src/hiqtc.c` |
| Mipmaps + filtragem trilinear analítica | ✅ | `src/texture.c` |
| Depth 24 bits + stencil 8 (word por amostra `(z24<<8)|st`) | ✅ | `raster.c` |
| Clipping homogêneo no near (Sutherland–Hodgman) | ✅ | `hde.c` |
| API HGL (matrizes, texturas, stencil, stats, PNG out) | ✅ | `include/hotice/hgl.h` |
| HDE: T&L — DOT3 bump (normal map, luz por pixel) | ✅ | `raster.c`, `hde.c` |
| Env map esférico (sphere map via normal view-space) | ✅ | `raster.c`, `hde.c` |
| MAPU — Unidade de Física Analítica (CORDIC, ATAN2, EXP2/LOG2, ROOT2, RAYSP/TEVNT/TRAJC/SPRG) | ✅ | `src/mapu.c`, `include/hotice/mapu.h` |
| Testes automatizados | ✅ **57/57** | `tests/test_main.c` |

## Build

```sh
make            # lib + testes
./build/test_all
make run-demos  # gera build/demo0{1..4}.png
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
| `demo02_hiqtc` | mesma cena RGBA8 vs HIQTC lado a lado | PSNR impresso ≈ 34.2 dB |
| `demo03_mipmap` | campo distante sem mip vs trilinear | Laplaciano médio 131 → 62 (~2.1× mais suave) |
| `demo04_reflection` | reflexão planar via stencil multipasse intra-frame | centróides obj/refl ≈ iguais; reflexo confinado ao espelho |

## Decisões documentadas

- **Regra de preenchimento top-left** real (`HI_EDGE_OK`): elimina rachaduras E
  repintadas em arestas compartilhadas sem depender do teste de depth.
- **Estado de stencil congelado por draw** na submissão (como a textura):
  multipasse funciona dentro de um frame trocando o estado entre draws.
  Depth/stencil reiniciam dos valores de clear a cada `FrameBegin`
  (equivalente a "limpar todo frame", padrão comum em consoles).
- **Luz**: `hglLightDirf` recebe o vetor **apontando para a luz**, em espaço
  da câmera (transforme a direção do mundo pela rotação da view).
  Iluminação é Gouraud (por vértice), como o HDE propõe.
- **Mipmaps**: cadeia box-filter RGBA8; trilinear com derivadas analíticas
  de tela sobre varyings pré-divididos.
- Simplificações conhecidas: normais não recebem a model matrix separada
  (iluminação assume espaço câmera já composto); sem backface culling.
- CPU SnowFlame: ColdFire V4æ (V4e Amiga Enhanced) com unidades MontêLauro
  MLVU (vetores · versores · ponto fixo quantizado) e MAPU (física analítica).
  Ver `../../../cpu/ColdFire-V4ae-cpu-dossie.md` (raiz do projeto SnowFlame).
- MAPU implementado como referência host (ponto flutuante) validando a especificação
  Q16.16 do dossiê V4æ; a versão fixa determinística é descrita no microcode
  do MAPU (CORDIC 16 iterações, aritmética saturada).

## Próximos passos (roadmap)

DOT3 bump por texel · env map cúbico · modo paleta HIQTC 8:1 ·
texturas ≥512² · modelo DMA/barramento → simulador do sistema ColdFire V4æ.
