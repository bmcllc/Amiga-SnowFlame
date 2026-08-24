/* =====================================================================
 * HIQTC — Hot-ice Quasi-Continuous Texture Compression
 *
 * Compressão por ancoragem homotópica: cada bloco 4×4 guarda duas
 * cores-âncora (RGB565) e um índice de 2 bits por texel. O texel real
 * é lido como um ponto ao longo da trajetória contínua entre as âncoras:
 *
 *   P0 = c0 · P1 = c1 · P2 = (2·c0+c1)/3 · P3 = (2·c1+c0)/3
 *
 * Modo opaco: 4:1 (4 bits/texel). Layout do bloco (8 bytes):
 *   [0..1] c0 RGB565 little-endian · [2..3] c1 · [4..7] índices 2bpp,
 *   linha-major (texel (x,y) = bits 2*(4*y+x)).
 *
 * O codificador escolhe o eixo principal (PCA via power iteration) da
 * nuvem de cores do bloco e projeta os extremos como âncoras.
 * ===================================================================== */
#include <stdlib.h>
#include <string.h>
#include "internal.h"

static void rgb_to_565(int r, int g, int b, uint16_t *out565,
                       int *er, int *eg, int *eb)
{
    uint16_t c = hi_pack_rgb565(r, g, b);
    *out565 = c;
    hi_unpack_rgb565(c, er, eg, eb); /* quantização explícita */
}

static void block_axis_power_iteration(const float col[16][3], float axis[3])
{
    float cov[3][3] = { {0} };
    float mean[3] = {0, 0, 0};
    int i, it;

    for (i = 0; i < 16; i++) {
        mean[0] += col[i][0]; mean[1] += col[i][1]; mean[2] += col[i][2];
    }
    mean[0] /= 16.0f; mean[1] /= 16.0f; mean[2] /= 16.0f;

    for (i = 0; i < 16; i++) {
        float d[3];
        int r, c;
        d[0] = col[i][0] - mean[0];
        d[1] = col[i][1] - mean[1];
        d[2] = col[i][2] - mean[2];
        for (r = 0; r < 3; r++)
            for (c = 0; c < 3; c++)
                cov[r][c] += d[r] * d[c];
    }

    axis[0] = 1.0f; axis[1] = 0.6f; axis[2] = 0.3f;
    for (it = 0; it < 8; it++) {
        float v[3], len;
        int r, c;
        for (r = 0; r < 3; r++) {
            float s = 0.0f;
            for (c = 0; c < 3; c++) s += cov[r][c] * axis[c];
            v[r] = s;
        }
        len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (len < 1e-9f) break;
        axis[0] = v[0] / len; axis[1] = v[1] / len; axis[2] = v[2] / len;
    }
}

uint8_t *hi_hiqtc_encode_rgba8(int w, int h, const uint32_t *pix)
{
    int bw = w / 4, bh = h / 4;
    uint8_t *out;
    int bx, by;

    if ((w % 4) || (h % 4)) return NULL; /* referência: múltiplos de 4 */

    out = (uint8_t *)malloc((size_t)bw * bh * 8);
    if (!out) return NULL;

    for (by = 0; by < bh; by++) {
        for (bx = 0; bx < bw; bx++) {
            float col[16][3];
            float axis[3], mean[3] = {0, 0, 0};
            float tmin = 1e30f, tmax = -1e30f;
            float a0[3], a1[3];
            int   ar0, ag0, ab0, ar1, ag1, ab1;
            uint16_t c0, c1;
            uint32_t indices = 0;
            uint8_t *blk = out + ((size_t)by * bw + bx) * 8;
            int i, x, y;

            for (y = 0; y < 4; y++)
                for (x = 0; x < 4; x++) {
                    uint32_t px = pix[(size_t)(by * 4 + y) * w + bx * 4 + x];
                    int r = (int)(px & 255), g = (int)((px >> 8) & 255),
                        b = (int)((px >> 16) & 255);
                    i = y * 4 + x;
                    col[i][0] = (float)r; col[i][1] = (float)g; col[i][2] = (float)b;
                    mean[0] += col[i][0]; mean[1] += col[i][1]; mean[2] += col[i][2];
                }
            mean[0] /= 16.0f; mean[1] /= 16.0f; mean[2] /= 16.0f;

            block_axis_power_iteration(col, axis);

            /* projetar extremos sobre o eixo principal */
            {
                float lo[3], hi_[3];
                lo[0] = lo[1] = lo[2] = 1e30f;
                hi_[0] = hi_[1] = hi_[2] = -1e30f;
                for (i = 0; i < 16; i++) {
                    float d[3], t;
                    int k;
                    for (k = 0; k < 3; k++) d[k] = col[i][k] - mean[k];
                    t = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
                    if (t < tmin) { tmin = t; lo[0] = col[i][0]; lo[1] = col[i][1]; lo[2] = col[i][2]; }
                    if (t > tmax) { tmax = t; hi_[0] = col[i][0]; hi_[1] = col[i][1]; hi_[2] = col[i][2]; }
                }
                a0[0] = lo[0]; a0[1] = lo[1]; a0[2] = lo[2];
                a1[0] = hi_[0]; a1[1] = hi_[1]; a1[2] = hi_[2];
            }

            rgb_to_565((int)a0[0], (int)a0[1], (int)a0[2], &c0,
                       &ar0, &ag0, &ab0);
            rgb_to_565((int)a1[0], (int)a1[1], (int)a1[2], &c1,
                       &ar1, &ag1, &ab1);

            /* convenção opaca: c0 deve ser o MAIOR (ordem total preservada) */
            if (c0 < c1) {
                uint16_t ts = c0; c0 = c1; c1 = ts;
                { int tr = ar0, tg = ag0, tb = ab0;
                  ar0 = ar1; ag0 = ag1; ab0 = ab1;
                  ar1 = tr; ag1 = tg; ab1 = tb; }
            } else if (c0 == c1) {
                /* âncoras iguais: paleta degenerada, tudo aponta para c0 */
            }

            blk[0] = (uint8_t)(c0 & 255);
            blk[1] = (uint8_t)(c0 >> 8);
            blk[2] = (uint8_t)(c1 & 255);
            blk[3] = (uint8_t)(c1 >> 8);

            for (i = 0; i < 16; i++) {
                /* distância aos 4 pontos da trajetória */
                float pal[4][3];
                float bestd = 1e30f;
                int bestj = 0, j;
                pal[0][0] = (float)ar0; pal[0][1] = (float)ag0; pal[0][2] = (float)ab0;
                pal[1][0] = (float)ar1; pal[1][1] = (float)ag1; pal[1][2] = (float)ab1;
                for (j = 2; j < 4; j++) {
                    float wa = (j == 2) ? (2.0f / 3.0f) : (1.0f / 3.0f);
                    pal[j][0] = ar0 * wa + ar1 * (1.0f - wa);
                    pal[j][1] = ag0 * wa + ag1 * (1.0f - wa);
                    pal[j][2] = ab0 * wa + ab1 * (1.0f - wa);
                }
                for (j = 0; j < 4; j++) {
                    float dr = col[i][0] - pal[j][0];
                    float dg = col[i][1] - pal[j][1];
                    float db = col[i][2] - pal[j][2];
                    float d = dr * dr + dg * dg + db * db;
                    if (d < bestd) { bestd = d; bestj = j; }
                }
                indices |= (uint32_t)bestj << (2 * i);
            }

            blk[4] = (uint8_t)(indices & 255);
            blk[5] = (uint8_t)((indices >> 8) & 255);
            blk[6] = (uint8_t)((indices >> 16) & 255);
            blk[7] = (uint8_t)((indices >> 24) & 255);
        }
    }
    return out;
}

uint32_t *hi_hiqtc_decode_all(const hglTex *t)
{
    uint32_t *out;
    int x, y;
    if (!t || t->fmt != HI_FMT_HIQTC) return NULL;
    out = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)t->w * t->h);
    if (!out) return NULL;
    for (y = 0; y < t->h; y++)
        for (x = 0; x < t->w; x++)
            out[(size_t)y * t->w + x] = hi_hiqtc_decode_texel(t, x, y);
    return out;
}

uint32_t hi_hiqtc_decode_texel(const hglTex *t, int x, int y)
{
    int bw = t->w / 4;
    int bx = x >> 2, by = y >> 2;
    const uint8_t *blk = t->hiq + ((size_t)by * bw + bx) * 8;
    uint16_t c0 = (uint16_t)(blk[0] | (blk[1] << 8));
    uint16_t c1 = (uint16_t)(blk[2] | (blk[3] << 8));
    int code = (blk[4 + (y & 3)] >> (2 * (x & 3))) & 3;
    int r0, g0, b0, r1, g1, b1, r, g, b;
    hi_unpack_rgb565(c0, &r0, &g0, &b0);
    hi_unpack_rgb565(c1, &r1, &g1, &b1);

    switch (code) {
    case 0: r = r0; g = g0; b = b0; break;
    case 1: r = r1; g = g1; b = b1; break;
    case 2: r = (2 * r0 + r1) / 3; g = (2 * g0 + g1) / 3; b = (2 * b0 + b1) / 3; break;
    default: r = (2 * r1 + r0) / 3; g = (2 * g1 + g0) / 3; b = (2 * b1 + b0) / 3; break;
    }
    return hi_pack_rgba8(r, g, b, 255);
}
