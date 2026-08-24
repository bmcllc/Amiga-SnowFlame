# Hot-ice — Renderizador de Referência (C99)

Implementação em software da arquitetura **Homotopia** descrita em
`../Hot-ice-gpu-dossie.md`: a GPU fictícia da MontêLauro que equipa o
console **SnowFlame** (1999). É o "modelo de ouro" funcional contra o qual
um futuro simulador de sistema/emulador será validado.

## Build e execução

```sh
make          # biblioteca + testes + demos
make test     # roda a suíte (17 verificações)
make run-demos
```

Saídas: `build/demo01.png` (TBR + morfing HDE + chão HIQTC, CAA 2×),
`build/demo02.png` (RGBA8 vs HIQTC lado a lado + PSNR no console).

## Estrutura

| Arquivo | Papel |
|---|---|
| `include/hotice/types.h` | Matemática: vec/mat row-major, perspectiva/lookat, ponto fixo 16.16, empacotamento de cor |
| `include/hotice/hgl.h` | API pública **HGL** — face convencional (estilo Glide/DirectX) |
| `src/internal.h` | Estruturas internas (tiles, triângulos binados, contexto) |
| `src/hde.c` | **HDE**: morfing contínuo → skinning (palette 8 ossos) → T&L → clip near → viewport |
| `src/raster.c` | **TBR**: bins 32×32, raster por amostra (CAA), depth LESS, resolve |
| `src/hiqtc.c` | Codec **HIQTC** 4:1 (âncoras RGB565 + índices 2bpp, eixo principal via power iteration) |
| `src/texture.c` | Sampler nearest/bilinear, wrap repeat/clamp, RGBA8 e HIQTC |
| `src/context.c` | Contexto, disciplina de frame (begin binando / end processando tiles) |
| `src/hgl.c` | Estado HGL + leitura de pixels + gravação PNG |
| `src/hipng.c` | Escritor PNG sem dependências (deflate stored) |

## Decisões da versão 0 (documentadas)

- **Regra de aresta estrita (`E > 0`)**: arestas compartilhadas avaliam
  idêntico dos dois lados; o depth test deduplica o repinte. Sem cracks
  para geometria opaca (provado pelo teste de cobertura exata).
- **CAA implementado como supersampling por amostra dentro do tile**
  (grades 2×/4×), resolvido antes de tocar o framebuffer — referência
  honesta do comportamento "banda quase grátis" do silício proposto.
- **Iluminação Gouraud por vértice** (o que a HDE faria em hardware);
  per-pixel fica para fase posterior.
- **Clip apenas do near plane** (Sutherland–Hodgman em clip space);
  laterais/topo são recortados por scissor do bbox.
- **Sem mipmaps ainda**: campo distante mostra minificação clássica
  (mistura bilinear em direção à média). Trilinear single-pass é o
  próximo marco de textura.

## Status contra o dossiê

| Feature do dossiê | Status |
|---|---|
| Tiles diferidos 32×32, overdraw ~grátis | ✅ implementado + testado |
| CAA 2×/4× resolvido por tile | ✅ implementado + testado |
| HDE morfing contínuo (`hglBindMorphTarget`/`hglMorphWeight`) | ✅ implementado + testado |
| HDE skinning palette (até 8 ossos/vértice) | ✅ implementado (teste c/ 1 osso) |
| HDE T&L + clipping | ✅ implementado + testado |
| HIQTC 4:1 encode/decode | ✅ implementado (PSNR gradiente 36,4 dB) |
| Cor 32-bit nativa | ✅ |
| Depth 24-bit LESS | ✅ implementado + testado |
| Trilinear single-pass / mipmaps | ⬜ próximo |
| DOT3 bump · stencil 8-bit · env-map | ⬜ |
| Modo paletizado HIQTC 8:1 · texturas 512²+ | ⬜ |
| MPEG-2 assistido · saída vídeo | n/a (simulador de sistema) |
| Ponto fixo 16.16 no caminho quente | ✅ tipos prontos (fronteira HDE) |

## Próximos marcos sugeridos

1. Mipmaps + trilinear single-pass (fecha o artefato de minificação).
2. Stencil 8-bit operacional + sombra volumétrica de demonstração.
3. Skinning multi-osso animado na demo (palette por frame).
4. Barramento modelado (2,29 GB/s, prioridades DMA) → caminho para o
   simulador de sistema completo com ColdFire V4æ interpretado.
