/*
 * sn76489.h — cycle-stamped SN76489 PSG wrapper API.
 *
 * Parallel to ym2612.h. Named after the chip rather than "psg" to avoid
 * filename collision with clownmdemu-core/source/psg.h on the include
 * path. The `psg_*` function names stay because callers see only this.
 */
#ifndef AUDIO_SN76489_H
#define AUDIO_SN76489_H

#include <stdint.h>
#include <stddef.h>

void   psg_init(void);
void   psg_advance(uint32_t cycles_master);
void   psg_write(uint8_t value);
size_t psg_render(int16_t *out, size_t sample_count);
size_t psg_samples_available(void);
uint32_t psg_sample_rate(void);
/* Reset per-frame leftover cycle accumulator. Matches clownmdemu's
 * per-Iterate sync.psg.current_cycle reset. */
void   psg_reset_leftover(void);

#endif
