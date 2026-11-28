/*
 * png_write.h — minimal PNG writer (uncompressed / stored deflate blocks).
 * No external dependencies beyond <stdio.h>, <stdlib.h>, <string.h>.
 *
 * Usage:
 *   png_write("out.png", pixels, width, height, stride_bytes);
 *   pixels: ARGB8888 (uint32_t *), top-to-bottom rows
 *   stride_bytes: byte distance between rows (typically width * 4)
 */
#ifndef PNG_WRITE_H
#define PNG_WRITE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* CRC-32 (ISO 3309 / PNG spec) */
static uint32_t png_crc32(const uint8_t *buf, size_t len)
{
    static uint32_t table[256];
    static int ready = 0;
    if (!ready) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        ready = 1;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* Adler-32 (zlib checksum) */
static uint32_t png_adler32(const uint8_t *buf, size_t len)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + buf[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static void png_put32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

/* Write a PNG chunk: length (4) + type (4) + data (length) + crc (4) */
static void png_write_chunk(FILE *f, const char type[4],
                            const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    png_put32be(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    /* CRC covers type + data */
    uint8_t *crc_buf = (uint8_t *)malloc(4 + len);
    memcpy(crc_buf, type, 4);
    if (len) memcpy(crc_buf + 4, data, len);
    uint32_t crc = png_crc32(crc_buf, 4 + len);
    free(crc_buf);
    uint8_t crc_bytes[4];
    png_put32be(crc_bytes, crc);
    fwrite(crc_bytes, 1, 4, f);
}

/*
 * Write an ARGB8888 framebuffer as a 24-bit RGB PNG.
 * Uses zlib stored blocks (no compression) for simplicity.
 * Returns 0 on success, -1 on failure.
 */
static int png_write_argb(const char *path,
                          const uint32_t *pixels,
                          int width, int height,
                          int stride_pixels)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    /* PNG signature */
    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 1, 8, f);

    /* IHDR */
    {
        uint8_t ihdr[13];
        png_put32be(ihdr + 0, (uint32_t)width);
        png_put32be(ihdr + 4, (uint32_t)height);
        ihdr[8]  = 8;   /* bit depth */
        ihdr[9]  = 2;   /* colour type: RGB */
        ihdr[10] = 0;   /* compression */
        ihdr[11] = 0;   /* filter */
        ihdr[12] = 0;   /* interlace */
        png_write_chunk(f, "IHDR", ihdr, 13);
    }

    /* Build raw image data: filter_byte(1) + RGB(3*width) per row */
    size_t row_bytes = 1 + (size_t)width * 3;
    size_t raw_size  = row_bytes * (size_t)height;
    uint8_t *raw = (uint8_t *)malloc(raw_size);
    if (!raw) { fclose(f); return -1; }

    for (int y = 0; y < height; y++) {
        uint8_t *dst = raw + (size_t)y * row_bytes;
        const uint32_t *src = pixels + (size_t)y * stride_pixels;
        dst[0] = 0;  /* filter: None */
        for (int x = 0; x < width; x++) {
            uint32_t argb = src[x];
            dst[1 + x*3 + 0] = (uint8_t)((argb >> 16) & 0xFF); /* R */
            dst[1 + x*3 + 1] = (uint8_t)((argb >>  8) & 0xFF); /* G */
            dst[1 + x*3 + 2] = (uint8_t)((argb)       & 0xFF); /* B */
        }
    }

    /* Build zlib stream with stored (uncompressed) deflate blocks.
     * Format: zlib_header(2) + deflate_stored_blocks + adler32(4) */
    uint32_t adler = png_adler32(raw, raw_size);

    /* Max stored block payload = 65535 bytes */
    size_t n_blocks = (raw_size + 65534) / 65535;
    /* zlib stream size: 2 (header) + n_blocks * (5 + payload) + 4 (adler) */
    size_t zlib_size = 2 + 4;
    {
        size_t remaining = raw_size;
        for (size_t i = 0; i < n_blocks; i++) {
            size_t blen = remaining > 65535 ? 65535 : remaining;
            zlib_size += 5 + blen;
            remaining -= blen;
        }
    }

    uint8_t *zlib = (uint8_t *)malloc(zlib_size);
    if (!zlib) { free(raw); fclose(f); return -1; }

    size_t pos = 0;
    zlib[pos++] = 0x78; /* CMF: deflate, window 32K */
    zlib[pos++] = 0x01; /* FLG: check bits (0x7801 % 31 == 0) */

    {
        size_t remaining = raw_size;
        size_t src_pos = 0;
        for (size_t i = 0; i < n_blocks; i++) {
            size_t blen = remaining > 65535 ? 65535 : remaining;
            int last = (i == n_blocks - 1) ? 1 : 0;
            zlib[pos++] = (uint8_t)last;                   /* BFINAL + BTYPE=00 */
            zlib[pos++] = (uint8_t)(blen & 0xFF);          /* LEN lo */
            zlib[pos++] = (uint8_t)((blen >> 8) & 0xFF);   /* LEN hi */
            zlib[pos++] = (uint8_t)(~blen & 0xFF);         /* NLEN lo */
            zlib[pos++] = (uint8_t)((~blen >> 8) & 0xFF);  /* NLEN hi */
            memcpy(zlib + pos, raw + src_pos, blen);
            pos += blen;
            src_pos += blen;
            remaining -= blen;
        }
    }

    png_put32be(zlib + pos, adler);
    pos += 4;

    png_write_chunk(f, "IDAT", zlib, (uint32_t)pos);
    free(zlib);
    free(raw);

    /* IEND */
    png_write_chunk(f, "IEND", NULL, 0);

    fclose(f);
    return 0;
}

#endif /* PNG_WRITE_H */
