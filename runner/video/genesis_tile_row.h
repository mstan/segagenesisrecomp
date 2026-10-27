#ifndef GENESIS_TILE_ROW_H
#define GENESIS_TILE_ROW_H
#include <stdint.h>

/* Immutable eight-pixel VRAM pattern row. Rebuild at each tile boundary in
 * the caller's current scanline/snapshot; no cross-row cache/invalidation. */
typedef struct GenesisTileRow {
    uint32_t bits;
    uint16_t attr;
    int shift, step, remaining;
    uint8_t palette;
} GenesisTileRow;

static inline void gtile_row_begin(GenesisTileRow *r, const uint8_t *vram,
                                   uint16_t attr, int x, int y)
{
    unsigned fy = (unsigned)y & 7u;
    if (attr & 0x1000) fy ^= 7u;
    unsigned a = (attr & 2047u) * 32u + fy * 4u;
    r->bits = ((uint32_t)vram[a] << 24) | ((uint32_t)vram[a+1] << 16) |
              ((uint32_t)vram[a+2] << 8) | vram[a+3];
    int fx = x & 7;
    r->shift = (attr & 0x0800) ? fx * 4 : (7 - fx) * 4;
    r->step = (attr & 0x0800) ? 4 : -4;
    r->remaining = 8 - fx;
    r->palette = (uint8_t)((attr >> 9) & 48);
    r->attr = attr;
}

static inline uint8_t gtile_row_pixel(GenesisTileRow *r)
{
    unsigned p = (r->bits >> r->shift) & 15u;
    r->shift += r->step;
    --r->remaining;
    return p ? (uint8_t)(r->palette | p) : 0;
}
#endif
