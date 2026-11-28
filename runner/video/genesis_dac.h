/* genesis_dac.h — authentic Genesis/Mega Drive VDP output-DAC color ladder.
 *
 * The VDP's 3-bit-per-channel color is converted to 8-bit by a NONLINEAR DAC
 * ladder, not the linear ×36 approximation the runner used previously. This
 * matches real hardware and the BlastEm oracle (vdp.c `levels[]`):
 *
 *   levels[] = {0,27,49,71,87,103,119,130,146,157,174,190,206,228,255}
 *   normal    channel c -> levels[2c]   = {0,49,87,119,146,174,206,255}
 *   shadow    channel c -> levels[c]     = {0,27,49,71,87,103,119,130}
 *   highlight channel c -> levels[c+7]   = {130,146,157,174,190,206,228,255}
 *
 * Shared by every color-conversion site (genesis_machine.c own backend,
 * vdp_integration.c oracle path, main.c, color_lut.c RAW table) so they stay
 * consistent. Verified channel-for-channel against BlastEm vdp.c.
 */
#ifndef GENESIS_DAC_H
#define GENESIS_DAC_H

#include <stdint.h>
#include "genesis_dac_table.h"

#define GENESIS_DAC_NORMAL    0
#define GENESIS_DAC_SHADOW    1
#define GENESIS_DAC_HIGHLIGHT 2

/* 3-bit channel (0..7) at the given shading level -> 8-bit DAC output. */
static inline uint8_t genesis_dac_level(int chan3, int level) {
    static const uint8_t N[8] = {0, 49, 87, 119, 146, 174, 206, 255};
    static const uint8_t S[8] = {0, 27, 49, 71, 87, 103, 119, 130};
    static const uint8_t H[8] = {130, 146, 157, 174, 190, 206, 228, 255};
    const uint8_t *t = (level == GENESIS_DAC_SHADOW) ? S
                     : (level == GENESIS_DAC_HIGHLIGHT) ? H : N;
    return t[chan3 & 7];
}

/* Genesis CRAM word (0x0EEE: R bits 3:1, G 7:5, B 11:9) -> 0xFFRRGGBB. */
static inline uint32_t genesis_dac_cram_to_argb(uint16_t cram, int level) {
    /* The complete finite input identity replaces repeated channel derivation.
     * Unused CRAM bits and unknown levels retain the scalar DAC contract. */
    unsigned index=((cram>>1)&7u)|((cram>>2)&0x38u)|((cram>>3)&0x1c0u);
    unsigned shade=level==GENESIS_DAC_SHADOW?1u:level==GENESIS_DAC_HIGHLIGHT?2u:0u;
    return genesis_dac_colors[shade][index];
}

/* Nearest normal-level 3-bit index for an 8-bit DAC value (inverse of the
 * normal ladder; used by the present-time color LUT to recover a channel from
 * an already-expanded framebuffer pixel). */
static inline int genesis_dac_nearest_normal_idx(uint32_t v) {
    static const uint8_t N[8] = {0, 49, 87, 119, 146, 174, 206, 255};
    int best = 0; uint32_t bd = 0x100;
    for (int i = 0; i < 8; ++i) {
        uint32_t d = v > N[i] ? v - N[i] : N[i] - v;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

#endif /* GENESIS_DAC_H */
