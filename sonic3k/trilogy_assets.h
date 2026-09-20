#pragma once
#include <stddef.h>
#include <stdint.h>

/* Bounded donor decoders; zero means malformed input or insufficient output.
 * No pointers into the input survive decoding. */
size_t tr_kosinski(const uint8_t *src,size_t size,uint8_t *out,size_t capacity);
size_t tr_enigma(const uint8_t *src,size_t size,uint16_t *out,size_t capacity,unsigned base);
size_t tr_nemesis(const uint8_t *src,size_t size,uint8_t *out,size_t capacity);

enum { TR_MAX_OBJECTS=512, TR_MAX_RINGS=1024 };
typedef struct TrPlacement { uint16_t x,y; uint8_t id,subtype,flags; } TrPlacement;
typedef struct TrRing { uint16_t x,y; } TrRing;
typedef struct TrStageAssets {
    unsigned id, chunk_count, block_count, tile_bytes, object_count, ring_count;
    uint16_t start_x,start_y,max_x,max_y;
    uint8_t tiles[0xB000];
    uint8_t blocks[0x1800];
    uint8_t chunks[0x8000];
    uint8_t layout[0x1000];
    uint8_t alternate_layout[0x1000]; /* S1's back-of-loop collision path */
    uint8_t collision[0xC00]; /* big-endian 16-bit indexes, primary then secondary */
    uint8_t heights[0x1000], widths[0x1000], angles[256];
    uint16_t palette[48];
    TrPlacement objects[TR_MAX_OBJECTS];
    TrRing rings[TR_MAX_RINGS];
} TrStageAssets;
/* Requires the exact supported, independently verified donor revision. */
int tr_stage_decode(unsigned id,const uint8_t *rom,size_t size,TrStageAssets *out,
                    char *error,size_t error_size);
