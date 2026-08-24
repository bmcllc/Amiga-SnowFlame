# SnowFlame (1999) — Análise de Viabilidade Técnica e Comercial · **v2**

**Console hipotético da Amiga, 6ª geração**
Especificação: ColdFire V4æ @ 266 MHz · **GPU MontêLauro Hot-ice (arquitetura Homotopia) @ 143 MHz** · 20 MB RAM · 8 MB VRAM · 2 MB ARAM · DSP 32 canais 32-bit 48,1 kHz · UDVD + CD-ROM 12× · 2× Memory Cards de 16 MB · controle digital + analógico de 4 eixos.

> **v2** — substitui a Voodoo3 2000 pela GPU própria da MontêLauro, modelo **Hot-ice**. O dossiê completo da arquitetura Homotopia está em `gpu/Hot-ice-gpu-dossie.md`. O apêndice A preserva as conclusões da v1 que motivaram a troca.

---

## 1. Sumário executivo

| Dimensão | v1 (Voodoo3) | **v2 (Hot-ice)** |
|---|---|---|
| Coerência interna da arquitetura | ✅ Boa | ✅ Boa |
| Rasterização / imagem | ✅ Excelente p/ 1998 | ✅✅ Classe PS2 em fill efetivo, 32-bit nativo, AA quase grátis |
| Geometria | 🔴 Gargalo central | ✅ HDE com T&L completo (~10–12 M vtx/s) — gargalo eliminado |
| Fornecimento de silício | 🔴 3dfx quebra em 2000 | ✅ Chip próprio MontêLauro + fundição contratada |
| Custo de fabricação | ⚠️ Alto | ⚠️ Alto (o DVD continua sendo o vilão, não a GPU) |
| Janela de mercado | 🔴 Curta demais | ⚠️ Viável — briga de igual com o Dreamcast, abaixo do PS2 |

**Veredito final:** a troca da Voodoo3 pela Hot-ice **transforma o SnowFlame de aposta romântica em produto competitivo**. O console passa a vencer o Dreamcast em rasterização (cor 32-bit, HIQTC, stencil, CAA), empata/supera em geometria graças ao motor de deformação HDE e perde apenas para o PS2 — que chega 6 meses depois custando mais caro. Os riscos remanescentes não são mais de arquitetura, são de **execução**: primeiro silício, maturidade dos drivers HGL e caixa da MontêLauro até a amortização do NRE.

---

## 2. Contexto histórico: o tabuleiro de 1999

- **Dreamcast** (nov/1998 JP): SH-4 @ 200 MHz (1,4 GFLOPS de pico), PowerVR2 tile-based, GD-ROM ~1 GB.
- **PlayStation 2** (mar/2000 JP): EE + Vector Units (~6 GFLOPS somados), Graphics Synthesizer, DVD nativo.
- **GeForce 256** (out/1999): inaugura o T&L em hardware no PC — prova de mercado de que "geometria no chip" é a onda.
- Uma Amiga real de 1999 oscilava entre o legado 68k/ColdFire, os aceleradores PowerPC da Phase5 e o AmigaDE/Elate.

A decisão de **não comprar uma GPU de catálogo e sim projetar a própria** é o que separa um console sério de um set-top box: é exatamente o caminho Sega+NEC/VideoLogic e Sony fizeram. A MontêLauro entra nesse clube com a Homotopia.

---

## 3. Análise por subsistema

### 3.1 CPU — ColdFire V4æ @ 266 MHz · ⚠️→✅ (rebaixado de protagonista)

O diagnóstico da v1 permanece válido como fato físico: FPU escalar (~130–250 MFLOPS sustentados), zero SIMD, ~500 DMIPS inteiros. Contra o SH-4 isso era fatal **quando o CPU fazia toda a geometria**.

Com a HDE fazendo T&L em hardware, o papel do ColdFire muda completamente:

| Função | v1 (sem T&L na GPU) | v2 (com HDE) |
|---|---|---|
| Transformação/iluminação de vértices | CPU — consumia o núcleo inteiro | **HDE** |
| Clipping/backface | CPU, por triângulo | **HDE + CCB, por patch** |
| Física, IA, colisão, streaming, áudio | Brigava por migalhas | **Donos exclusivos do CPU** |

Os mesmos ~500 DMIPS que antes afogavam em multiplicação de matriz agora sobram para gameplay. E o formato **ponto fixo 16.16 aceito nativamente pela HDE** casa perfeitamente com a vocação inteira do ColdFire — sem conversão FP↔fixo no caminho quente.

**Sobre o "æ":** de requisito de sobrevivência passou a *desejável* — um vetor unit AltiVec-lite continuaria ajudando física/colisão, mas o console não morre sem ele.

> **v2.1 — V4æ agora especificado.** O "æ" deixou de ser vago: é a variante
> **Amiga Enhanced do V4e** fabricada pela MontêLauro com as mesmas
> características do núcleo original **mais duas unidades proprietárias
> fundidas ao datapath do FPU**: **MLVU** (vetores, versores e ponto fixo
> quantizado — alimenta o HDE) e **MAPU** (física analítica em forma fechada
> — colisão contínua, trajetórias e eventos resolvidos exatamente, em Q16.16).
> Especialização em vez de clock mantém o die barato sem enfraquecer o
> console. Detalhes, instruções e riscos: ver **`cpu/ColdFire-V4ae-cpu-dossie.md`**.

**Veredito:** ✅ adequado no novo papel de orquestrador. O casamento ColdFire (streams fix-point) + HDE (matemática pesada) é coerente e barato.

### 3.2 GPU — MontêLauro Hot-ice @ 143 MHz · ✅✅ (ver dossiê)

Resumo das capacidades relevantes à viabilidade:

- **Rasterização tile-based diferida**, 32×32: overdraw quase gratuito, cor/Z resolvidos on-chip → os 8 MB VRAM rendem o dobro do conteúdo útil vs arquitetura imediata.
- **~572 Mpx/s brutos → ~1,1–1,5 Gpx/s efetivos**; jogos reais em **2–5 M triângulos/s** com iluminação de verdade.
- Cor **32-bit nativa**, Z 24-bit + stencil 8-bit, texturas até 512², compressão HIQTC 4:1/8:1, DOT3, trilinear single-pass.
- **CAA**: anti-aliasing 2×/4× por tile a custo de banda ≈ zero — feature impossível na classe Voodoo3 e inédita em consoles da época.
- **HDE**: T&L completo + unificação hardware de morphing, skinning (palette 8 ossos) e transição LOD contínua.
- **MPEG-2 assistido**: o UDVD toca filme sem consumir o ColdFire — detalhe que a v1 pagava em CPU.
- Modelo de desenvolvimento convencional (HGL estilo Glide/D3D): catálogo de terceiras partes alimentável por ports de PC, a tática que fez o Glide reinar.

**Risco residual específico:** primeiro silício da MontêLauro. Mitigação já prevista no dossiê — cartões PCI "Hot-ice Developer" no mercado de PC 12 meses antes do lançamento, amadurecendo drivers HGL longe da vitrine.

**Veredito:** ✅✅ acima do PowerVR2 em tudo que aparece na tela; abaixo apenas do GS em fill bruto. Estrategicamente sólida porque **o supply depende da própria empresa e de um contrato de fundição, não da saúde financeira alheia**.

### 3.3 Memória — 20 MB RAM + 8 MB VRAM · ✅ (melhorou de nota)

Topologia sugerida mantém a tradição da casa: 16 MB principais + 4 MB de trabalho do sistema (eco de Chip RAM/Fast RAM).

O orçamento de VRAM muda de figura com tiles diferidos — depth e overdraw nunca saem do chip:

| Resolução | Scanout duplo (32-bit) | Staging/display lists | Sobra p/ texturas+geometria | Equivalente |
|---|---|---|---|---|
| **640×480** | 2,34 MB | 0,5 MB | **~5,2 MB** | 40 × 512² HIQTC ou ~300 × 128² 32-bit |
| 800×600 | 3,66 MB | 0,5 MB | ~3,9 MB | confortável |
| 1024×768 VGA | 5,87 MB | 0,5 MB | ~1,6 MB | usar modo 16-bit |

Comparação direta com a v1: a 640×480, a Voodoo3 sobrevivia com ~6 MB *mas em 16-bit e texturas 256² sem compressão*; a Hot-ice entrega ~5,2 MB **em 32-bit, 512² comprimidas 4:1** — conteúdo real ~4× maior. Os 8 MB deixaram de ser aperto e viraram dimensionamento elegante.

Novo cuidado de arquitetura: barramento único de 2,29 GB/s compartilhado por texturas, geometria e display lists → exige **priorização DMA rígida** (geometria > texturas > listas). É o novo gargalo sistêmico, ver §4.

### 3.4 Áudio — DSP 32 canais 32-bit @ 48,1 kHz + 2 MB ARAM · ✅

Inalterado e continua o melhor subsistema discreto da máquina: mixagem 32-bit (headroom contra clipping), DSP programável para reverb/filtros/síntese, ~11 s PCM estéreo ou ~44 s ADPCM nos 2 MB. O detalhe "48,1 kHz" segue como sabor de datasheet que vai virar lenda urbana.

### 3.5 Armazenamento — UDVD + CD-ROM 12× · ⚠️

A análise da v1 vale integralmente: capacidade 4,7 GB um ano antes do PS2 é vantagem real; o custo do drive (~US$ 70–110) é o que empurra o BOM; mídia gravável DVD só vira ameaça ~2001+, o que protege o início do ciclo. Com a GPU própria liberando caixa que iria à 3dfx em royalties, **agora existe margem para absorver o drive** sem quebrar o US$ 299. Alternativa conservadora permanece válida: lançar CD/GD-only e entregar UDVD na revisão 2001.

### 3.6 Entrada e saves · ✅/⚠️

Controle digital + dual-analog 4 eixos: paridade DualShock (falta rumble). Memory Card 16 MB segue generoso demais para item de série — recomendação mantida: **1 MB incluso, 16 MB premium**.

---

## 4. O novo gargalo sistêmico: alimentar a HDE

Eliminada a fome de geometria, o gargalo migra para onde a física mora: **o barramento de 2,29 GB/s**. Cada triângulo visível precisa chegar da VRAM (ou da RAM via DMA) até a HDE antes do ciclo de clock dele.

```
Orçamento aproximado do barramento @ 30 fps:
  Display lists + streams de vértices   ~40–50%
  Fetch de texturas (cache miss tiles)  ~35–45%
  Scanout + atualizações                ~10–15%
Margem: apertada — exige malhas indexadas
com conectividade (CCB corta 40–60% dos vértices)
e texturas HIQTC sempre que possível.
```

Regras de ouro que o SDK precisa impor desde o dia 1:
1. Malha indexada em strips **sempre** (o CCB só trabalha com conectividade intacta);
2. HIQTC por padrão; textura crua é exceção justificada;
3. Streams de vértices em ponto fixo 16.16 residentes em VRAM;
4. Profiler de tiles incluso no kit — banda invisível é banda desperdiçada.

É um gargalo *gerenciável por disciplina de engenharia*, infinitamente melhor que o da v1, que era *gerenciável por milagre*.

---

## 5. Comparativo direto (estimativas de classe)

| Item | **SnowFlame v2** | Dreamcast (1998) | Nintendo 64 | PlayStation 2 (2000) |
|---|---|---|---|---|
| CPU | ColdFire V4æ @ 266 MHz<br>~500 DMIPS (orquestrador) | SH-4 @ 200 MHz<br>360 MIPS · 1,4 GFLOPS | R4300 @ 93,75 MHz | EE + VUs · ~6 GFLOPS |
| GPU | **Hot-ice @ 143 MHz**<br>HDE/T&L · TBR · CAA | PowerVR2 · TBR/HSR | RCP microcode | GS · gigapixels |
| Fill efetivo | ~1,1–1,5 Gpx/s | ~0,4 Gpx/s | baixo | ~1,2–2,4 Gpx/s |
| Cor | **32-bit nativo** | 16-bit | 16/21-bit | até 32-bit |
| Tris/s úteis | **~2–5 M** | ~1–3 M | ~100–300 k | vários M |
| Texturas | 512² · HIQTC 4:1/8:1 | 1024² · VQ | 256² TMEM | variável |
| Áudio | 32 vozes **32-bit** | 64 vozes 16-bit | 16–32 · 16-bit | 48 vozes 16-bit |
| Mídia | UDVD 4,7 GB + CD 12× | GD-ROM 1,2 GB | cartucho | DVD 4,7 GB |
| AA | **CAA 2×/4× ≈ grátis** | parcial | ❌ | ❌ |
| Preço típico | proposto US$ 249–299 | US$ 149–199 | US$ 99–129 | US$ 299 |

Leitura honesta da tabela: **vitória consistente sobre o Dreamcast** (fill, cor, AA, compressão, mídia, saves, profundidade de áudio), **empate técnico em geometria útil**, derrota esperada apenas no teto absoluto do PS2 — que custa mais caro e chega depois. Pela primeira vez desde a v1, o SnowFlame tem **argumento de venda positivo**, não defensivo.

---

## 6. Custo e risco comercial

BOM estimado (preços OEM fim de 1999):

| Componente | Custo estimado |
|---|---|
| ColdFire V4æ @ 266 MHz | US$ 25–35 |
| **Hot-ice MT-H1 (silício próprio)** | US$ 35–50 |
| 8 MB SDR 128-bit (VRAM) | US$ 10–15 |
| 20 MB SDRAM | US$ 15–22 |
| DSP + ARAM + áudio analógico | US$ 10–15 |
| Drive UDVD 12× | US$ 70–110 |
| Placa-mãe, gabinete, fonte, controle, 1× MC | US$ 40–60 |
| **BOM total** | **~US$ 205–310** |

O chip próprio custa o mesmo que custaria a Voodoo3 em volume, mas **não paga royalties nem margem de terceiro** — e o NRE se amortiza na linha de cartões PCI para PC (§3.2), que gera receita ANTES do console existir. O vilão do custo segue sendo o drive UDVD.

### Matriz de riscos v2

| Risco | Probabilidade | Impacto | Mitigação |
|---|---|---|---|
| Bring-up do primeiro silício | Média | Alto | Steppings planejados; kits FPGA; janela de 12 meses dos cartões PC |
| Drivers HGL imaturos no lançamento | Média | Fatal se falhar | Ecossistema PC prévio; equipe dedicada; API deliberadamente convencional |
| Capacidade de fundição 0,25 µm | Média | Alto | Contrato plurianual + segunda fonte qualificada |
| Guerra de preço com PS2 | Certa | Médio | Lançar a US$ 249; a linha PC subsidia o console |
| Caixa da MontêLauro até o breakeven | ? | Fatal se falhar | Receita dos cartões PCI; parceiro de distribuição regional forte |
| Pirataria | Baixa até 2001 | Médio | DVD não-gravável + boot security |

Sumiu da matriz o risco nº 1 da v1 — **colapso da 3dfx** — junto com sua mitigação impossível ("estoque de vida útil antecipado").

---

## 7. Veredito e cenários

### Cenário A — Lançar fim de 1999 a US$ 249, com linha PC já faturando
Melhor caso realista: o SnowFlame entra como **"o console que renderiza como next-gen hoje"** — 32-bit, AA, T&L de verdade, DVD — enquanto o PS2 ainda não chegou. Base instalada construída sobre ports de PC rápidos (HGL familiar) + exclusivos usando HDE (morphing/skinning baratos viram identidade visual própria: personagens deformando fluidamente era impossível na concorrência sem comer CPU).

### Cenário B — Adiar para out/2000, encostar na PS2
Arriscado: divide a vitrine com a Sony no mesmo trimestre e perde o título "primeiro com DVD". Só compensa se o bring-up da Hot-ice atrasar — e aí o marketing vira "esperamos para fazer certo".

### Cenário C — Fracasso de execução (silício ou drivers)
O único cenário de morte real. Sem drivers maduros no dia 1, o catálogo Glide-port não acontece e a máquina morre com ótimo hardware e três jogos. Por isso a estratégia dos cartões PCI não é acessória: **é o seguro de vida do console**.

### Frase final

> A v1 deste documento terminava com o SnowFlame dependendo de uma letra bonita — o "æ" valer 2 GFLOPS. A v2 termina diferente: a Hot-ice tira o peso da sorte da equação e coloca engenharia no lugar. O ColdFire orquestra, a HDE transforma, os tiles resolvem, o CAA suaviza — e o desenvolvedor programa tudo como sempre programou. Se a MontêLauro executar o bring-up e os drivers, o SnowFlame não é mais o melhor console da geração anterior lançado tarde: **é o rival que o Dreamcast não queria encontrar — e o aviso precoce de que a sexta geração teria sido outra guerra.**

---

## Apêndice A — Herança da v1 (configuração Voodoo3 2000)

A análise original (`v1`) concluiu que a Voodoo3 2000 era tecnicamente charmosa e estrategicamente insustentável: cor 16-bit forçada, texturas máx. 256², zero compressão/stencil/DOT3/T&L, W-buffer 16-bit — e o risco fatal do colapso da 3dfx (ativos vendidos à NVIDIA em dez/2000). O gargalo sistêmico era o par desequilibrado CPU-fraco ↔ GPU-sem-T&L, com o rasterizador ocioso a ~30%. Essas seis feridas foram o roteiro de requisitos que a arquitetura Homotopia resolveu um a um (ver `gpu/Hot-ice-gpu-dossie.md`, §7).

---

*Documento de conceito fictício. Figuras marcadas com ~ ou faixas são estimativas de ordem de grandeza baseadas em dados públicos das tecnologias citadas e no estado da arte de 1999 — não medidas.*
