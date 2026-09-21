#pragma once
#include <stddef.h>
#include <stdint.h>

/* Bounded donor decoders; zero means malformed input or insufficient output.
 * No pointers into the input survive decoding. */
size_t tr_kosinski(const uint8_t *src,size_t size,uint8_t *out,size_t capacity);
size_t tr_enigma(const uint8_t *src,size_t size,uint16_t *out,size_t capacity,unsigned base);
size_t tr_nemesis(const uint8_t *src,size_t size,uint8_t *out,size_t capacity);

enum { TR_MAX_OBJECTS=512, TR_MAX_RINGS=1024 };
enum { TR_ART_PLATFORM,TR_ART_BRIDGE,TR_ART_ROCK,TR_ART_SWING,TR_ART_LEDGE,
    TR_ART_WALL,TR_ART_POLE,TR_ART_EDGE,TR_ART_CRAB,TR_ART_BUZZ,TR_ART_CHOP,
    TR_ART_NEWT,TR_ART_MOTO,TR_ART_EGGMAN,TR_ART_BOSS_ITEMS,TR_ART_BALL,
    TR_ART_CAPSULE,TR_ART_STAKE,TR_ART_WATERFALL,TR_ART_COCO,TR_ART_SIGN,TR_ART_MISSILE,TR_ART_SPECIAL_STARS,TR_ART_COUNT };
typedef struct TrObjectArt { unsigned mapping,frames,tile_bytes; uint16_t palette; uint8_t tiles[4096]; } TrObjectArt;
typedef struct TrPlacement { uint16_t x,y; uint8_t id,subtype,flags; } TrPlacement;
typedef struct TrRing { uint16_t x,y; } TrRing;
typedef struct TrTileAnimation {
    unsigned tile,tiles,frames;
    uint8_t offset[8],duration[8],data[0x500];
} TrTileAnimation;
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
    uint16_t sprite_palette[16],water_palette[16];
    TrTileAnimation animations[5];
    unsigned animation_count;
    TrPlacement objects[TR_MAX_OBJECTS];
    TrRing rings[TR_MAX_RINGS];
    unsigned map_bytes;
    uint8_t object_maps[0x8000];
    TrObjectArt art[TR_ART_COUNT];
    uint8_t s1_chunks[128*8]; /* original 256px IDs for loop/path logic */
    unsigned s1_width,s1_height;
    uint8_t spiral_y[416],spiral_flip[52];
} TrStageAssets;
int tr_object_assets(const uint8_t *rom,size_t size,TrStageAssets *out);
void tr_stage_animate(const TrStageAssets *assets,unsigned frame,uint8_t *vram);
/* Requires the exact supported, independently verified donor revision. */
int tr_stage_decode(unsigned id,const uint8_t *rom,size_t size,TrStageAssets *out,
                    char *error,size_t error_size);
