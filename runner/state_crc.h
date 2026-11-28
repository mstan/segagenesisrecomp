#ifndef GENESIS_STATE_CRC_H
#define GENESIS_STATE_CRC_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

static inline uint32_t genesis_state_crc32_update(uint32_t crc,
                                                 const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return crc;
}

/* Hash from the current file position without a second full state image.
 * A failed/short read leaves the caller's checksum unchanged. */
static inline int genesis_state_crc32_file(FILE *file, size_t bytes, uint32_t *out)
{
    uint8_t chunk[65536];
    uint32_t crc = UINT32_MAX;
    while (bytes) {
        size_t n = bytes < sizeof chunk ? bytes : sizeof chunk;
        if (fread(chunk, 1, n, file) != n) return 0;
        crc = genesis_state_crc32_update(crc, chunk, n);
        bytes -= n;
    }
    if (ferror(file)) return 0;
    *out = ~crc;
    return 1;
}

#endif
