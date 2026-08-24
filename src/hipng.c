/* =====================================================================
 * hipng.c — escritor PNG minimalista (RGB8, deflate "stored")
 * Sem dependências externas: só para as demos gravarem imagens que
 * possam ser inspecionadas visualmente.
 * ===================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---------------- CRC32 ---------------- */
static uint32_t crc_table[256];
static int crc_ready = 0;

static void crc_init(void)
{
    uint32_t c;
    int n, k;
    for (n = 0; n < 256; n++) {
        c = (uint32_t)n;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[n] = c;
    }
    crc_ready = 1;
}

static uint32_t crc32_buf(const uint8_t *buf, size_t len)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    if (!crc_ready) crc_init();
    for (i = 0; i < len; i++)
        c = crc_table[(c ^ buf[i]) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void write_chunk(FILE *f, const char type[4], const uint8_t *data,
                        uint32_t len)
{
    uint8_t hdr[8], crcb[4];
    uint32_t crc;
    memcpy(hdr + 4, type, 4);
    be32(hdr, len);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    crc = crc32_buf((const uint8_t *)type, 4);
    {
        /* crc sobre type+len+data: recomputar sobre buffer combinado */
        uint8_t *all = (uint8_t *)malloc(4u + len);
        uint32_t i;
        if (!all) return;
        memcpy(all, type, 4);
        for (i = 0; i < len; i++) all[4 + i] = data[i];
        crc = crc32_buf(all, 4u + len);
        free(all);
    }
    be32(crcb, crc);
    fwrite(crcb, 1, 4, f);
}

/* ---------------- Adler32 ---------------- */
static uint32_t adler32_buf(const uint8_t *buf, size_t len)
{
    uint32_t a = 1, b = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        a = (a + buf[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

#define HI_PNG_SIG_LEN 8
static const uint8_t png_sig[HI_PNG_SIG_LEN] =
    { 137, 80, 78, 71, 13, 10, 26, 10 };

int hi_png_write_rgb8(const char *path, int w, int h, const uint8_t *rgb)
{
    FILE *f;
    uint8_t ihdr[13];
    size_t raw_len = (size_t)h * (size_t)(1 + w * 3);
    uint8_t *raw, *idat;
    size_t idat_len, off = 0, src = 0, pos = 0;
    int y, ok = 0;

    if (!path || !rgb || w <= 0 || h <= 0) return -1;
    f = fopen(path, "wb");
    if (!f) return -1;

    /* raw: filtro 0 por scanline */
    raw = (uint8_t *)malloc(raw_len);
    if (!raw) { fclose(f); return -1; }
    for (y = 0; y < h; y++) {
        raw[pos++] = 0;
        memcpy(raw + pos, rgb + (size_t)y * w * 3, (size_t)w * 3);
        pos += (size_t)w * 3;
    }

    /* zlib stream com blocos stored */
    {
        size_t nblocks = (raw_len + 65534) / 65535;
        idat_len = 2 + 4 + nblocks * 5 + raw_len;
        idat = (uint8_t *)malloc(idat_len);
        if (!idat) { free(raw); fclose(f); return -1; }
        idat[off++] = 0x78; idat[off++] = 0x01;
        src = 0;
        while (src < raw_len) {
            size_t chunk = raw_len - src;
            int last;
            if (chunk > 65535) chunk = 65535;
            last = (src + chunk >= raw_len);
            idat[off++] = last ? 1 : 0;
            idat[off++] = (uint8_t)(chunk & 255);
            idat[off++] = (uint8_t)(chunk >> 8);
            idat[off++] = (uint8_t)(~chunk & 255);
            idat[off++] = (uint8_t)((~chunk >> 8) & 255);
            memcpy(idat + off, raw + src, chunk);
            off += chunk; src += chunk;
        }
        {
            uint32_t ad = adler32_buf(raw, raw_len);
            be32(idat + off, ad);
            off += 4;
        }
        idat_len = off;
        free(raw);
    }

    fwrite(png_sig, 1, HI_PNG_SIG_LEN, f);

    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* color type RGB */
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    write_chunk(f, "IHDR", ihdr, 13);

    write_chunk(f, "IDAT", idat, (uint32_t)idat_len);
    write_chunk(f, "IEND", NULL, 0);

    free(idat);
    ok = (fflush(f) == 0);
    fclose(f);
    return ok ? 0 : -1;
}
