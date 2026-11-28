#include "sector.h"

#include <stddef.h>
#include <string.h>

enum { RAW_BYTES = 2352, EDC_BYTES = 2064, EDC_OFFSET = 2064 };

static uint32_t sector_edc(const uint8_t *raw)
{
    uint32_t crc = 0;
    size_t i;
    for (i = 0; i < EDC_BYTES; ++i) {
        unsigned bit;
        crc ^= raw[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) != 0 ? UINT32_C(0xd8018001) : 0u);
    }
    return crc;
}

SegaCDSectorResult segacd_sector_validate(const uint8_t raw[RAW_BYTES])
{
    static const uint8_t sync[12] = {0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00};
    uint32_t stored;
    if (raw == NULL || memcmp(raw, sync, sizeof(sync)) != 0)
        return SEGACD_SECTOR_BAD_SYNC;
    if (raw[15] != 1)
        return SEGACD_SECTOR_UNSUPPORTED_MODE;
    stored = (uint32_t)raw[EDC_OFFSET] | ((uint32_t)raw[EDC_OFFSET+1] << 8) |
             ((uint32_t)raw[EDC_OFFSET+2] << 16) | ((uint32_t)raw[EDC_OFFSET+3] << 24);
    return sector_edc(raw) == stored ? SEGACD_SECTOR_MODE1_OK : SEGACD_SECTOR_BAD_EDC;
}
