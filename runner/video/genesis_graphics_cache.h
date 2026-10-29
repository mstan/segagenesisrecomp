#ifndef GENESIS_GRAPHICS_CACHE_H
#define GENESIS_GRAPHICS_CACHE_H

#include "genesis_tile_row.h"

/* Address chooses a bounded slot, never an identity. Every use reads all four
 * current bytes and compares their complete value. The result has no palette,
 * priority, mode, machine or provider identity: those inputs are applied by the
 * caller. Direct writes, imports, aliases and private art need no notification.
 * This is derived host scratch, not hardware or serialized state. */
typedef struct GenesisPatternEntry {
    uint32_t bits;
    unsigned valid;
    uint64_t pixels[2];
} GenesisPatternEntry;
typedef struct GenesisPatternCache {
    GenesisPatternEntry rows[512];
} GenesisPatternCache;

static inline uint64_t ggraphics_pattern_row(GenesisPatternCache *cache,
                                            unsigned address, uint32_t bits,
                                            uint8_t attr, int flip)
{
    GenesisPatternEntry *entry = &cache->rows[(address >> 2) & 511u];
    if (!entry->valid || entry->bits != bits) {
        entry->pixels[0] = gtile_packed_row(bits, 0, 0);
        entry->pixels[1] = gtile_packed_row(bits, 0, 1);
        entry->bits = bits;
        entry->valid = 1;
    }
    return entry->pixels[flip != 0] |
           (uint64_t)attr * UINT64_C(0x0101010101010101);
}

typedef struct GenesisGraphicsGeometry {
    int wt, ht, win_wt;
    uint16_t base_a, base_b, base_w, hbase;
    int hmode, two_cell, window_x, window_y, right, down;
} GenesisGraphicsGeometry;
typedef struct GenesisGeometryCache {
    uint8_t key[9];
    int valid;
    GenesisGraphicsGeometry geometry;
} GenesisGeometryCache;

static inline int ggraphics_plane_tiles(unsigned code)
{
    return (code & 3u) == 1u ? 64 : (code & 3u) == 3u ? 128 : 32;
}

/* Only register-derived constants live here. Scroll table bytes, VSRAM,
 * raster/output line, backdrop, providers and local presentation policy remain
 * live inputs at their original point. A complete value key also handles two
 * interleaved VDP instances and restores without a generation counter. */
static inline const GenesisGraphicsGeometry *
ggraphics_geometry(GenesisGeometryCache *cache, const uint8_t *reg)
{
    const uint8_t key[9] = {reg[2], reg[3], reg[4], reg[0x0b], reg[0x0c],
                            reg[0x0d], reg[0x10], reg[0x11], reg[0x12]};
    if (!cache->valid || memcmp(cache->key, key, sizeof key)) {
        GenesisGraphicsGeometry *g = &cache->geometry;
        int h40 = (reg[0x0c] & 1) != 0;
        g->wt = ggraphics_plane_tiles(reg[0x10]);
        g->ht = ggraphics_plane_tiles(reg[0x10] >> 4);
        g->base_a = (uint16_t)((reg[2] & 0x38) << 10);
        g->base_b = (uint16_t)((reg[4] & 7) << 13);
        g->win_wt = h40 ? 64 : 32;
        g->base_w = (uint16_t)((reg[3] & (h40 ? 0x3e : 0x3f)) << 10);
        g->hbase = (uint16_t)((reg[0x0d] & 63) << 10);
        g->hmode = reg[0x0b] & 3;
        g->two_cell = (reg[0x0b] & 4) != 0;
        g->window_x = (reg[0x11] & 31) * 16;
        g->window_y = (reg[0x12] & 31) * 8;
        g->right = (reg[0x11] & 128) != 0;
        g->down = (reg[0x12] & 128) != 0;
        memcpy(cache->key, key, sizeof key);
        cache->valid = 1;
    }
    return &cache->geometry;
}

#endif
