# ColdFire V4æ — Dossiê do CPU do SnowFlame · MontêLauro, 1999

> Variante **Amiga Enhanced** do ColdFire **V4e**, fabricada sob licença pela
> MontêLauro, com duas unidades de processamento proprietárias fundidas ao
> núcleo: **MLVU** (vetores · versores · ponto fixo quantizado) e **MAPU**
> (física analítica). Objetivo de engenharia declarado: **custo de silício
> baixo sem console fraco** — a força vem de especialização, não de clock.

---

## 1. Identidade e posicionamento

| Item | Definição |
|---|---|
| Classe | CPU 32-bit RISC embarcada, big-endian (herança Amiga preservada) |
| Base | Motorola ColdFire V4e (ISA-C com MAC/EMAC, divisão e FPU em hardware) |
| Clock do console | 266 MHz |
| Processo alvo | 0,25 µm CMOS (mesma node da Hot-ice → economia de máscara e embalagem) |
| Cache | 16 KB instrução + 16 KB dados (harvard interno), SRAM rápida |
| MMU/PMMU | **Omitido** (console não precisa de memória virtual; corta área) |
| Barramento | 64-bit externo a 66 MHz para os 20 MB unificados; glue próprio *Amiga Enhanced* com prioridades de DMA estilo chipset clássico |

O V4e real já entregava, em ordem (in-order), divisão inteira, MAC/EMAC
saturante e FPU single/double. O V4æ mantém **tudo isso igual** ("as mesmas
características") e acrescenta as duas unidades MontêLauro descritas abaixo,
acessadas pelo espaço de coprocessador já previsto na ISA — nenhum modo
privilegiado novo, nenhuma mudança no pipeline escalar. É essa disciplina que
mantém o custo baixo: as unidades são **microcódigo + datapath compartilhado
com o FPU**, não núcleos independentes.

## 2. MLVU — MontêLauro Vector & Versor Unit

A "evolução dos vetores": onde o EMAC multiplica escalares saturantes, a MLVU
opera **vetores e versores inteiros** com quantização explícita.

### 2.1 Formatos de ponto fixo quantizado

| Formato | Uso típico | Faixa / resolução |
|---|---|---|
| `Q16.16` | matrizes, posições de física | ±32768 · passo 1,5e-5 |
| `Q8.8` | cores, pesos de skin, blend | ±128 · passo 3,9e-3 |
| `Q2.14` | versores (componente de quat) | ±2 · passo 6,1e-5 |
| `Q0.15` | atributos normalizados [−1,1] | passo 3,05e-5 |

Toda operação declara o formato no opcode; o arredondamento é
**determinístico** (round-to-nearest-even fixo em microcódigo), requisito para
física reproduzível entre execuções — sem "float drift" entre máquinas.

### 2.2 Instruções principais (mnemônicos de referência)

```
VADD.Q d, s1, s2.fmt    ; vec4 add/sub/mul com saturação por lane
VDOT.Q  d, s1, s2       ; produto interno Q16.16 acumulado 64-bit
VCRS.Q  d, s1, s2       ; produto vetorial 3D
VNRM.Q  d, s            ; normalização via aproximação inversa-√ + 1 Newton
VROT.Q  vd, vs, vq      ; rotaciona vetor por versor (12 mul, sem trig)
VMUL.Q  vq1, vq2        ; multiplicação de versores (quat mul)
VSQR.Q  dq              ; conjugado/negativo
VLERP.Q vd, va, vb, t   ; interpolação linear por componente
VSLERP.Q vd, va, vb, t  ; slerp com branch-free (sin via tabela+CORDIC curto)
VMAT4Q. vout, m, vin    ; mat4·vec4 em 8 ciclos, encadeável p/ batch
VSKIN.Q vout, v, pal, w ; skinning 4-ossos direto na MLVU
```

### 2.3 Por que isso sustenta o console

- **HDE alimentado**: o mesmo caminho `mat4·vec`/skin/slerp que o renderizador
  de referência C99 implementa em software (`hiQuat`, `hiFix1616`) executa
  aqui em hardware — o HDE consome os buffers prontos.
- Animação por versor evita gimbal lock e comprime paletas de pose
  (8 bytes/pose vs 36 bytes de matriz).
- Saturação nativa elimina clamps manuais em blending e iluminação.

## 3. MAPU — MontêLauro Analytical Physics Unit

Física **analítica**: resolve em forma fechada aquilo que consoles rivais
integram numericamente frame a frame. Sem loops iterativos divergentes, sem
acúmulo de erro — a resposta é função pura das condições iniciais.

### 3.1 Instruções principais

```
ROOT2.P  d, a, b, c     ; raízes reais de ax²+bx+c (flags: nº de raízes)
ROOT3.P  d, a..d        ; cúbica (Cardano microcodificado)
RAYPL.P  dt, hit, ro, rd, n, d0    ; raio×plano
RAYSP.P  dt, hit, ro, rd, c, r     ; raio×esfera
RAYBB.P  dt, norm, ro, rd, min, max ; raio×AABB (+normal da face)
TRAJC.P  pos@t, ro, v0, g, t       ; projétil em forma fechada
TEVNT.P  t_hit, ro, v0, g, plano   ; QUANDO atinge (solução de evento)
SPRG.P   x@t, x0, v0, k, m, b, t   ; mola-amortecedor fechada
CSINC.P  d, ang         ; seno/cosseno CORDIC 24 bits em ~10 ciclos
EXP2.P / LOG2.P d, s    ; base-2 fixo Q16.16
ATAN2.P d, y, x         ; ângulo Q16.16
```

### 3.2 Microcódigo Q16.16 — especificação congelada

O microcódigo do MAPU está **especificado operação a operação** e implementado
como modelo de ouro bit-exato (`src/mapu.c`, API em `include/hotice/mapu.h`).
Todo resultado é função pura das entradas: o mesmo par de condições iniciais
produz os mesmos 32 bits em qualquer máquina — requisito de replays e
lock-step. Semântica fixada:

| Operação | Datapath congelado |
|---|---|
| `CSINC` | CORDIC vetorial de **16 iterações**, LUT `atan(2^-i)` em **turns** (1.0 = 360°) |
| `ATAN2` | CORDIC por vetorização + correção de quadrante (`x<0 ⇒ z = 1 − z`) |
| `LOG2` | LUT de **256 entradas** sobre [0,5; 1) + interpolação linear do mantissa; atalho exato para potências de 2 |
| `EXP2` | parte inteira por shift + Taylor grau 3 no frac (`a·ln2`, `a²/2`, `a³/6`) |
| `√` | Newton-Raphson, 8 iterações com break antecipado por convergência |
| `ROOT2` | discriminante `(b·b − 4·a·c) >> 16`; raízes pela divisão saturada dedicada |
| `TRAJC` | `p₀ + v₀t + ½gt²` com `½t²` por **shift** (`>>1`), jamais divisão pelo literal Q16 de 2 (armadilha clássica de escala) |
| `TEVNT` | reduz ao ROOT2 do eixo relevante e escolhe a menor raiz positiva |
| `SPRG` | forma fechada sub/super/criticamente amortecida via EXP2/CSINC |

Toda aritmética intermediária é int32/int64 **saturante** (herança MAC/EMAC):
overflow satura no limite do formato em vez de contornar — falhas visíveis,
nunca silenciosas.

### 3.3 O que isso compra em gameplay

- Colisão **contínua** (raio varre o movimento do frame inteiro): sem túnel
  através de paredes finas, sem substeps caros.
- Previsão determinística de trajetória (mira assistida, IA que "sabe" onde o
  projétil cai) resolvendo `TEVNT` em vez de simular.
- Molas/amortecedores exatos: câmeras, suspensões, panos — estáveis em
  qualquer framerate.
- Tudo em Q16.16/Q8.8 → resultados bit-idênticos em qualquer console
  (replays e lock-step multiplayer locais confiáveis).

## 4. Integração no SnowFlame

```
        ┌─────────────── V4æ @266 MHz ───────────────┐
 jogo → │ núcleo V4e ─ MLVU ─ MAPU ─ FPU/MAC ─ cache │── 20 MB ──┐
        └────────────────────────────────────────────┘           │
                 │ filas de vértices (MLVU)                       ▼
                 ▼                                          Hot-ice @143 MHz
             HDE (morph/skin/T&L) ── tiles ── CAA/HIQTC ── 8 MB VRAM
```

- **MLVU → HDE**: o motor de deformação consome streams já em Q16.16;
  morph targets ficam em ARAM barato (2 MB dedicados de áudio podem ser
  emprestados quando o DSP folga).
- **MAPU ↔ jogo**: colisões/analytics rodam no tempo sobrando do núcleo;
  sem segunda CPU genérica para pagar.
- **DSP 32ch** permanece soberano em áudio (ver análise §3.4).

## 5. Simulador do sistema (modelo funcional)

`src/sys.c` + `include/hotice/sys.h` modelam o fluxo de dados do console
para validar contratos antes do silício:

| Unidade | Modelo |
|---|---|
| Barramento | 64-bit @ 133 MHz, batidas de **64 bytes** |
| DMA | 4 canais, round-robin, descritores encadeados (`next_desc`) |
| MLVU | 1 instrução/ciclo: `VADD`, `VROT` (90°Y leva +X→−Z), `VLERP`, `VMUL` |
| MAPU | fila de submissão por opcode chamando o microcódigo Q16.16 direto |
| GPU Hot-ice | anel de comandos (1024 entradas) |

Deliberadamente **não ciclo-exato** (latências unitárias): o objetivo é
contrato funcional e ordem de grandeza de tráfego, não contagem de ciclos.
8 verificações próprias na suíte (`tests/test_sys_funcs.c`).

## 6. Custo: como ser barato sem ser fraco

| Decisão | Economia | Contrapartida aceita |
|---|---|---|
| Microcódigo em ROM p/ MLVU/MAPU sobre datapath do FPU | uma ALU 64-bit serve tudo | latência maior que ALU dedicada (aceitável: 4–20 ciclos) |
| Sem PMMU/MMU virtual | ~10–15% da área (estimativa) | SO bare-metal, como toda console da era |
| In-order escalar (herdado do V4e) | sem janela OoO nem rename | dependência do programador/agrupamento manual |
| Mesma node e embalagem da Hot-ice | mascaras/testes compartilhados | — |
| Especialização > clock | 266 MHz suficientes | títulos precisam usar MLVU/MAPU p/ brilhar |

Resultado estimado: die na casa de **~28–35 mm²** em 0,25 µm (estimativa
interna; números públicos do V4e servem só de referência de ordem de grandeza).

## 7. Riscos

1. **Compiladores**: tirar proveito da MLVU exige intrinsics/bibliotecas —
   mitigado pela biblioteca runtime MontêLauro (mesmos tipos do renderizador
   de referência: `hiVec*`, `hiQuat`, `hiFix1616`).
2. **Portabilidade de física analítica**: designers acostumados a integrar
   numericamente podem estranhar solução fechada — documentar receitas.
3. **Licença Motorola/Veio**: dependência de fornecimento — dual-source
   planejado para 2001.

## 8. Implementação de referência — status

| Bloco | Referência | Verificação medida |
|---|---|---|
| MAPU microcódigo Q16.16 (§3) | `src/mapu.c`, `include/hotice/mapu.h` | 20 verificações: CSINC 0°/45°/90° exatos, ATAN2 por quadrante, LOG2/EXP2 inversos, ROOT2 {2,3}, TRAJC y(1s)=5,1, TEVNT t=2v₀/g, RAYSP dt≈4 + normal −x, SPRG decai a ~0 |
| Versores MLVU (espelho software) | `include/hotice/types.h` (`hiQuat`) | rotação 90°Y leva +X→−Z; mat4 do versor = rotate_y; slerp médio de ±90°X = identidade |
| Simulador V4æ (§5) | `src/sys.c` | DMA memcpy, MLVU VADD/VLERP/VROT, submissão MAPU CSINC@45° e TRAJC, anel GPU — 8/8 |
| Integração HDE+MAPU | `demos/demo08_skinning_physics.c` | personagem 2 ossos (skinning) + projétil TRAJC com colisão contínua RAYSP + mola SPRG com tempo cumulativo: 300 frames estáveis, mola assenta em x≈0,005 após 5 s |
| Física em jogo | `demos/demo07_mapu.c` | chão em t=1,618 s · acerto de esfera com dt=3,895 |

---
*Documento fictício integrante do projeto SnowFlame. Características do V4e
real citadas como conhecimento público de 1999; números de área/latência são
estimativas internas.*
