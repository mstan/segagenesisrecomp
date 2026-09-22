#pragma once
#include <stddef.h>
#include <stdint.h>
enum { S2_MENU_MAP_FRAMES=36, S2_MENU_MAP_PIECES=20 };
typedef struct S2MenuPiece { int16_t x,y; uint16_t tile; uint8_t width,height; } S2MenuPiece;
typedef struct S2MenuFrame { unsigned count; S2MenuPiece pieces[S2_MENU_MAP_PIECES]; } S2MenuFrame;
typedef struct S2SaveAssets {
    uint8_t tiles[65536];
    uint16_t palette[64], background[40*28], layout[405];
    uint16_t new_card[70], static_card[4][70];
    S2MenuFrame frames[S2_MENU_MAP_FRAMES];
    /* Stock Sonic 2 REV01 terrain thumbnails, one per campaign act. Native
     * CRAM colours; generated privately from the running game's ROM. */
    uint16_t stages[20][80*56];
    int stages_ready;
} S2SaveAssets;
/* Caller verifies exact donor identity first. Transactional; *out unchanged
 * on failure. Only menu data is decoded; no donor code executes. */
int s2_save_assets_decode(const uint8_t *rom,size_t size,S2SaveAssets **out,char *error,size_t cap);
void s2_save_assets_free(S2SaveAssets **assets);
int s2_stage_images_decode(const uint8_t *rom,size_t size,S2SaveAssets *assets);
