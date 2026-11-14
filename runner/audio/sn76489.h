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
/* Presentation audition: bits0..2 tones, bit3 noise; counters keep clocking. */
void   psg_set_output_mask(unsigned mask);
void   psg_advance(uint32_t cycles_master);
void   psg_write(uint8_t value);
size_t psg_render(int16_t *out, size_t sample_count);
/* One drain, two optional outputs; the canonical filter always advances. */
size_t psg_render_pair(int16_t *filtered, int16_t *unfiltered, size_t sample_count);
size_t psg_samples_available(void);
uint32_t psg_sample_rate(void);
/* Reset per-frame leftover cycle accumulator. Matches clownmdemu's
 * per-Iterate sync.psg.current_cycle reset. */
void   psg_reset_leftover(void);

/* Save-state hooks (own-backend snapshots). Returns 1 on success. */
#include <stdio.h>
int psg_save_state(FILE *f);
int psg_load_state(FILE *f);

/* Rollback snapshots (runner/rb_state.c): psg_save_state's state plus the
 * rendered-but-undrained samples, in memory. Same contract as ym2612_rb_*. */
#include <stddef.h>
size_t psg_rb_save(void *dst, size_t cap);
int    psg_rb_load(const void *src, size_t len);

#endif
