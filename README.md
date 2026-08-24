# Hot-ice — Renderizador de referência (C99)

Implementação da GPU **Hot-ice** (arquitetura *Homotopia*, MontêLauro)
descrita em `../Hot-ice-gpu-dossie.md`, como biblioteca C99 sem dependências
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
| Mipmaps + filtragem trilinear analítica | ✅ | `src/texture.c` |
| Depth 24 bits + stencil 8 (word por amostra `(z24<<8)|st`) | ✅ | `raster.c` |
| Clipping homogêneo no near (Sutherland–Hodgman) | ✅ | `hde.c` |
| API HGL (matrizes, texturas, stencil, stats, PNG out) | ✅ | `include/hotice/hgl.h` |
| Testes automatizados | ✅ **28/28** | `tests/test_main.c` |

## Build

```sh
make            # lib + testes
./build/test_all
make run-demos  # gera build/demo0{1..4}.png
```

## Demos

| Demo | Mostra | Verificação |
|---|---|---|
| `demo01_tbr_morph` | cena completa: chão HIQTC com trilinear, esfera morfando (HDE), CAA 2× | mapa visual; tris in/out no console |
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
  (iluminação assume espaço câmera já composto); sem backface culling;
  HIQTC sem modo paleta 8:1 ainda.

## Próximos passos (roadmap)

DOT3 bump por texel · env map cúbico · modo paleta HIQTC 8:1 ·
texturas ≥512² · modelo DMA/barramento → simulador do sistema ColdFire V4æ.
