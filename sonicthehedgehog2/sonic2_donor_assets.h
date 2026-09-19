#pragma once
#include <stddef.h>
#include <stdint.h>

/* Asset-only decoder: the donor never becomes g_rom and no donor entry point
 * executes. Bounds-checked normal gameplay mappings/DPLCs, not title assets. */
enum { S2_DONOR_MAX_FRAMES = 256, S2_DONOR_MAX_PIECES = 64 };
typedef struct S2DonorLayout {
    unsigned frames, mappings, dplc, art, palette, mapping_stride;
    unsigned animations, animation_count;
} S2DonorLayout;
typedef struct S2DonorFrame {
    int16_t x, y;
    uint16_t width, height;
    uint8_t *pixels; /* palette indices; zero transparent; owned by bank */
} S2DonorFrame;
typedef struct S2DonorBank {
    unsigned count;
    uint16_t palette[16];
    S2DonorFrame frames[S2_DONOR_MAX_FRAMES];
    unsigned animation_count;
    uint16_t animation_length[64];
    uint8_t animations[64][256];
} S2DonorBank;
extern const S2DonorLayout s2_amy_171_layout;
extern const S2DonorLayout s2_sk_knuckles_layout;
int s2_donor_decode(const uint8_t *rom, size_t size, const S2DonorLayout *layout,
                    S2DonorBank *out, char *error, size_t error_size);
void s2_donor_free(S2DonorBank *bank);
