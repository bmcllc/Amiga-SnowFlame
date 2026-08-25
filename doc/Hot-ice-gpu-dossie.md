# MontêLauro **Hot-ice** — Dossiê Técnico da GPU

**Arquitetura Homotopia · GPU oficial do SnowFlame (1999)**
*"A deformação contínua como princípio de silício."*

---

## 1. Identidade

| | |
|---|---|
| Fabricante | MontêLauro (fabless, fundição parceira de 0,25 µm) |
| Arquitetura | **Homotopia** — processamento por mapas contínuos |
| Modelo | **Hot-ice** (parte MT-H1-143) |
| Clock núcleo/memória | 143 MHz / 143 MHz |
| Transistores | ~15 M · processo 0,25 µm 5 camadas · die ~95–120 mm² |
| Consumo | ~8–12 W (passivo + fluxo do gabinete) |
| Barramento local | 128-bit SDR @ 143 MHz ≈ **2,29 GB/s** para as 8 MB VRAM |
| Posicionamento | À frente do Dreamcast em rasterização e geometria; classe Graphics Synthesizer em fill efetivo |

O nome é proposital e espelha o console: **SnowFlame** roda num **Hot-ice**. Fogo na neve, gelo quente — a identidade visual da marca inteira nasce desse paradoxo.

---

## 2. O princípio Homotopia

Em topologia, uma homotopia é uma **deformação contínua entre dois mapas**: H(p,t) transforma um estado no outro sem cortes. A aposta da MontêLauro foi tratar isso não como curiosidade matemática, mas como **unidade de trabalho fundamental do chip**: em vez de processar estados isolados (vértice transformado, pixel final), o silício processa *trajetórias* entre estados e amostra onde interessa.

Três tecnologias proprietárias derivam desse princípio — **todas invisíveis para o programador comum**, que usa a Hot-ice exatamente como usaria uma GPU convencional (matrizes, luzes, materiais, texturas, framebuffer). A novidade vive dentro do chip; a API é padrão.

### 2.1 HDE — Homotopy Deformation Engine (geometria)

O bloco de vértices não executa apenas "matriz × vértice". Ele recebe do jogo **pares de estados ligados por um parâmetro t** e interpola continuamente antes/depois da transformação:

```
H(p, t) = Lerp(F₀(p), F₁(p), campo_de_controle(t))
```

Na prática, um único hardware unifica três técnicas que em 1999 eram feitas à mão no CPU:

| Técnica tradicional | Como o jogo pede à HDE |
|---|---|
| Morfing de keyframes (rostos, explosões) | `hdeMorph(meshA, meshB, canal)` |
| Skinning esquelético (palette de até 8 matrizes/vértice) | `hdeBind(palette, pesos)` |
| Transição suave de LOD (sem "pop") | `hdeLod(meshHi, meshLo, d)` |

- Throughput de pico: **~10–12 M vértices/s com T&L completo** (transformação + 8 luzes direcionais/point + clipping de frustum).
- Formatos aceitos: FP32 **e** ponto fixo 16.16 — o ColdFire alimenta a HDE sem conversão custosa.
- Clipping e backface por **lote**, não por triângulo (ver 2.2).

### 2.2 CCB — Continuity Coherence Buffer (culling por continuidade)

Como os meshes chegam à HDE com sua conectividade intacta (não como saco de triângulos soltos), o chip mantém um mapa topológico da malha e elimina **regiões inteiras** (patches) fora do frustum ou de costas, propagando a decisão pela fronteira do patch em vez de testar cada polígono.

Mecânica do buffer (congelada nesta especificação):

1. **Construção** (`hglCcbBuild`): união-busca sobre arestas compartilhadas
   agrupa os triângulos em *patches* conexos; componentes maiores são
   fatiados em chunks limitados (`maxPatchTris`) para manter coerência
   espacial. Cada patch guarda: faixa de índices, bbox model-space,
   centróide ponderado por área e **cone de normais** (eixo = média
   ponderada por área; meia-abertura θ = pior desvio facial).
2. **Frustum por patch**: os 8 cantos do bbox atravessam MV·P; se todos
   caem fora do **mesmo** plano (near/far/left/right/top/bottom), o patch
   é descartado antes de qualquer trabalho de vértice — sem morph, sem
   skinning, sem luz, sem setup.
3. **Backface por patch**: com φ = ângulo entre o eixo do cone e a direção
   câmera→patch (em view space), o patch está 100% de costas quando
   `φ + θ ≤ 90°`, isto é `dot(eixo, D̂) ≥ sen θ`. Um único teste substitui
   dezenas de triângulos.
4. Patches sobreviventes emitem seus triângulos na ordem original do stream:
   a imagem final é **bit-idêntica** à renderização sem CCB (contrato
   verificado por teste automático comparando os framebuffers).

- Economia típica medida nos kits internos: **40–60% dos vértices nunca saem do stream** (na cena de referência demo09 — sala com pilares internos e externos ao frustum — a implementação de referência mede **72%**, pois soma o ganho de backface ao de frustum).
- Efeito colateral valioso: display lists menores → menos tráfego no barramento de 2,29 GB/s.
- Limitação assumida: bbox/cone usam as posições BASE do modelo; malhas cujo morphing/skinning desloque vértices para fora do envelope base devem ser submetidas pelo caminho direto da HDE.

### 2.3 HIQTC — compressão de textura por ancoragem homotópica

Cada bloco 4×4 guarda **duas cores-âncora e um índice de 2 bits por texel**; o texel real é lido como um ponto ao longo da trajetória contínua entre as âncoras.

- Modo opaco: **4:1** · modo com alfa de 1 bit: 4:1 + máscara · modo paletizado: **8:1**.
- Descompressão no próprio endereçador: textura comprimida ocupa banda igual à não-comprimida.
- Em 1999 isso coloca o Hot-ice anos à frente da Voodoo3 (zero compressão) e no clube restrito do S3TC/VQ do Dreamcast.

### 2.4 CAA — Continuous Adaptive Anti-aliasing

Renderizador **tile-based diferido**: a cena é fatiada em tiles de 32×32 resolvidos inteiramente on-chip (cor + Z vivem em SRAM interna). Dentro do tile, cada primitivo é amostrado numa grade rotacionada 2× ou 4× e resolvido antes de tocar a memória externa.

- AA 2×/4× praticamente **grátis em banda** — o custo que matava supersampling convencional simplesmente não existe aqui.
- Overdraw quase gratuito: primitivas ocultas morrem dentro do tile, sem escrever pixel nenhum.
- Bônus estrutural: correção de perspectiva e blend por pixel com precisão de subpixel real.

---

## 3. Especificação completa

| Bloco | Especificação |
|---|---|
| Rasterização | Tile-based diferido, tiles 32×32, 4 px/ciclo → **~572 Mpx/s brutos, ~1,1–1,5 Gpx/s efetivos** (overdraw ~grátis) |
| Cor de saída | **RGBA 32-bit nativo** (modo rápido 16-bit disponível) |
| Depth/stencil | Z de 24-bit + **stencil de 8-bit** (sombra volumétrica, reflexos planares) · W-buffer opcional |
| Texturas | Até **512×512** · HIQTC 4:1/8:1 · trilinear **single-pass** · DOT3 bump mapping · env-map esférico e cúbico · projeção |
| Geometria | HDE: T&L completo, ~10–12 M vértices/s pico · setup ~10 M triângulos/s · jogos reais: **2–5 M triângulos/s** |
| Anti-aliasing | CAA 2×/4× por tile, custo de banda ≈ zero |
| Vídeo | Assistência MPEG-2 completa (iDCT + motion compensation) → **DVD do SnowFlame toca sem comer CPU** · escala horizontal/vertical |
| Saída de vídeo | 480p componente · VGA RGB · NTSC/PAL composto + S-Video |
| Memória | 8 MB SDR 128-bit @ 143 MHz (~2,29 GB/s); framebuffer resolvido + texturas + display lists |
| API | **HGL — Hot-ice Graphics Library** (ver §4) |

### Orçamento das 8 MB VRAM sob arquitetura tile-based

| Resolução (32-bit) | Scanout duplo | Display list/staging | **Sobra p/ texturas+geometria** |
|---|---|---|---|
| **640×480** | 2,34 MB | 0,5 MB | **~5,2 MB** ≈ 40 × 512² HIQTC ou ~300 × 128² 32-bit |
| 800×600 | 3,66 MB | 0,5 MB | ~3,9 MB |
| 1024×768 (VGA) | 5,87 MB | 0,5 MB | ~1,6 MB (modo 16-bit recomendado) |

Leitura: diferente da Voodoo3, aqui **dobrar a cor não dobra a dor** — o overdraw e o depth ficam presos no chip, então os 8 MB rendem quase o dobro em conteúdo útil.

---

## 4. Modelo de desenvolvimento: "convencional por fora"

A lição que a 3dfx ensinou com o Glide e a NVIDIA consolidou com OpenGL/D3D: **devs adotam APIs familiares, não paradigmas exóticos**. O HGL foi desenhado sobre essa regra:

```c
/* Um frame no HGL parece DirectX 6 / Glide de propósito */
hglBegin(HGL_TRIANGLES);
  hglSetMatrix(HGL_MODELVIEW, mv);
  hglBindTexture(0, chao_hiqt);
  hglSkinPalette(bones);            /* opcional: cai na HDE */
  hglMorphChannel(face, 0.35f);     /* opcional: cai na HDE */
  hglVertexStream(stream_vbo);      /* malha indexada com conectividade */
hglEnd();
```

- Pipeline mental idêntico ao PC: mundo→visão→projeção, luzes, materiais, texturas, framebuffer. **Zero conceito novo obrigatório.**
- As features Homotopia (morph, skin, LOD contínuo, HIQTC, CAA) são **opt-in** e expostas como chamadas normais.
- Ferramentas: exportadores para 3D Studio Max e Maya, conversor HIQTC em linha de comando, profiler de tiles.
- **Estratégia de maturidade:** cartões PCI "Hot-ice Developer" vendidos a estúdios de PC 12 meses antes do console → drivers HGL amadurecem no mercado de PC antes do dia 1 do SnowFlame. (É o mesmo truque que criou o catálogo Glide — agora com dono.)

---

## 5. Comparativo da geração

| | **Hot-ice @143 MHz** | PowerVR2 (Dreamcast) | Voodoo3 2000 | GeForce 256 (PC) | GS (PS2) |
|---|---|---|---|---|---|
| T&L em hardware | ✅ HDE (~10–12 M vtx/s) | ❌ (CPU SH-4) | ❌ | ✅ (~15 M vtx/s) | ⚠️ via Vector Units |
| Fill efetivo | ~1,1–1,5 Gpx/s | ~0,4 Gpx/s | 0,29 Gpx/s | ~0,48 Gpx/s | ~1,2–2,4 Gpx/s |
| Cor nativa | 32-bit | 32-bit | 16-bit | 32-bit | 32-bit |
| Compressão de textura | HIQTC 4:1/8:1 | VQ | ❌ | S3TC | ❌ |
| Stencil | 8-bit | ❌ | ❌ | 8-bit | ❌ |
| AA barato | ✅ CAA por tile | parcial | ❌ | ❌ (RSX só em 2005) | ❌ |
| Textura máx. | 512² | 1024² | 256² | 2048² | variável |
| Assistência DVD | ✅ MPEG-2 | ✅ parcial | ❌ | ❌ (CPU) | ✅ |

Posição resultante: **acima do Dreamcast em tudo que aparece na tela, abaixo do PS2 em fill bruto e flexibilidade vetorial** — exatamente onde um console de 1999 precisava estar para ser levado a sério sem ser ficção científica.

---

## 6. Riscos específicos do projeto

| Risco | Natureza | Mitigação |
|---|---|---|
| Primeiro silício (bring-up) | Novo chip = bugs de integração | stepping A1→B0 planejado; kits dev com FPGAs de depuração |
| Drivers imaturos | O assassino clássico de GPUs novas | Cartões PCI de PC 12 meses antes (§4); equipe HGL dedicada |
| NRE elevado | Projeto inédito vs comprar catálogo | Amortizar com linha de cartões PC e futuras iterações (Hot-ice II) |
| Dependência de fundição única | 0,25 µm contratada | Contrato de capacidade plurianual + segunda fonte qualificada |
| Expectativa vs entrega | Marketing promete "GS killer" | Mensagem honesta: *"T&L de verdade, cor de verdade, AA de graça"* |

---

## 7. Resumo executivo da arquitetura



> A Hot-ice resolve, uma a uma, as seis feridas que a análise da Voodoo3 expôs: **cor 16-bit → 32-bit nativo; texturas 256² → 512²; zero compressão → HIQTC; sem stencil → stencil 8-bit; sem T&L → HDE; banda desperdiçada em overdraw → tiles diferidos com CAA grátis.** E o faz sem pedir nada de estranho ao desenvolvedor: por fora é Glide/DirectX como sempre foi; por dentro é a primeira GPU construída sobre deformação contínua. O gargalo sistêmico do SnowFlame deixa de ser a GPU — e vira a disciplina de manter o barramento de 2,29 GB/s alimentado.

---

## 8. Implementação de referência (C99) — status por bloco

> A implementação vive no mesmo repositório: `src/`, `demos/`, `tests/` — ver
> também o `README.md` na raiz para build e índice completo.

A arquitetura está implementada como **modelo de ouro em software**: C99 puro
sem dependências externas (`src/`), API HGL convencional
(`include/hotice/hgl.h`), suíte automatizada com **76 verificações** e 9 demos
que medem cada bloco. É o espécime contra o qual o silício real seria validado.

| Bloco do dossiê | Referência | Métrica medida |
|---|---|---|
| TBR tiles 32×32, resolve por tile | `src/raster.c` | quad dividido cobre área exata (top-left rule); zero pintura fora do alvo |
| CAA grades rotacionadas 2×/4× | `src/context.c`, `raster.c` | tons na diagonal 1×=2 → CAA4×=3 |
| HDE — morph contínuo + skinning + Gouraud | `src/hde.c` | morfing interpola posição exata; skin translada vértice |
| **CCB §2.2** — culling por continuidade | `src/ccb.c` | **72% dos vértices poupados** (demo09); frustum+backface por patch; saída bit-idêntica |
| HIQTC 4:1 (âncoras RGB565) | `src/hiqtc.c` | PSNR gradiente 64² = **36,4 dB** |
| HIQTC paleta 8:1 | `src/hiqtc.c` | paleta global median-cut; PSNR gradiente = **34,2 dB** |
| Mipmaps + trilinear analítica | `src/texture.c` | cadeia box-filter exata (8→4→2→1); minificação extrema cai no último nível |
| Depth 24-bit + stencil 8-bit | `src/raster.c` | reflexão planar multipasse intra-frame confinada ao espelho (demo04) |
| DOT3 bump (luz por pixel) + env map esférico | `raster.c`, `hde.c` | bump ON/OFF: tons na parede 5 → 81 (demo05) |

Contrato de estado congelado por draw (stencil, texturas, bump/env, luz):
multipasse funciona dentro de um frame trocando o estado ENTRE draws — o que
o silício faria capturando o estado no início de cada display list.
Depth/stencil reiniciam dos valores de clear a cada frame (padrão console).

### 8.1 O que ficou fora do modelo de ouro

Assistência MPEG-2, saída de vídeo (480p/VGA/NTSC), modo 16-bit e W-buffer:
fora do escopo do renderizador de referência, sem impacto nos contratos
testados acima. CCB completo exige malhas indexadas — saco de triângulos
solto usa o caminho direto da HDE, como previsto no §4.

---

*Documento de conceito fictício. Figuras são estimativas coerentes com o estado da arte de 1999 (processo 0,25 µm, SRAM on-chip viável, codecs por interpolação de âncoras já conhecidos), não medidas.*
