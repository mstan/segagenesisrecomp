/* LC7883K/KM attenuation, Sanyo EN3884B p9; Sega Hardware Manual p31.
 * Encoded FD steps are not a linear gain: four codes share each upper
 * coefficient, except the final three codes before mute. */
#ifndef SEGACD_FADER_H
#define SEGACD_FADER_H

#include <stdint.h>

typedef struct SegaCDFader {
    uint16_t current_field;
    uint16_t target_field;
    uint16_t control_bits;
} SegaCDFader;

/* Reset the state to the encoded fader target in raw_word. */
void segacd_fader_reset(SegaCDFader *fader, uint16_t raw_word);

/* Change target and control bits immediately; the gain field slews by one
 * encoded field count per subsequent output sample. This unit does not model
 * EFDT transfer timing, deemphasis filtering, or transport state. */
void segacd_fader_set_target(SegaCDFader *fader, uint16_t raw_word);

/* Return composed words with FD in bits 14..4 and control bits in bits 3..1.
 * EFDT (bit 15) and reserved bit 0 are intentionally excluded. */
uint16_t segacd_fader_current_raw(const SegaCDFader *fader);
uint16_t segacd_fader_target_raw(const SegaCDFader *fader);
uint16_t segacd_fader_control(const SegaCDFader *fader);

/* Apply one gain to BOTH channels, then advance one FD field count for this
 * stereo frame. Rounding is nearest, ties away from zero; output clips to
 * int16_t. A null state passes the samples through unchanged. Sample pointers
 * must be valid. The caller supplies the DAC sample clock, including silence;
 * EFDT transfer completion is separate from the attenuation ramp. */
void segacd_fader_tick(SegaCDFader *fader, int16_t *left, int16_t *right);

#endif
