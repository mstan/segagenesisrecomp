/*
 * ym2612.h — our own YM2612 (FM) implementation, taking cycle-stamped writes.
 *
 * Port of clownmdemu's fm.c but with a cycle-driven advance API:
 * the renderer ticks the envelope/phase state continuously and register
 * writes can be applied at arbitrary sub-sample cycle offsets. This is
 * what prevents the "boop/squelch" artifact from register batches
 * collapsing onto a single sample boundary.
 *
 * Phase 1: header stub. Implementation arrives in Phase 3.
 */
#ifndef AUDIO_YM2612_H
#define AUDIO_YM2612_H

#include <stdint.h>
#include <stddef.h>

/* Lifecycle */
void ym2612_init(void);

/* Advance internal state by `cycles_68k` 68K cycles without any register
 * writes. Ticks envelope/phase/LFO for each operator. */
void ym2612_advance(uint32_t cycles_68k);

/* Apply a register write. Write to the A-latch (reg=latched_reg, chip-wide
 * state like $28 key-on, LFO, timers) or the D-latch (channel-specific
 * params). port = AUDIO_PORT_FM1_* or AUDIO_PORT_FM2_* from event_queue.h. */
void ym2612_write(uint8_t port, uint8_t value);

/* Render `sample_count` stereo samples (L, R interleaved, 16-bit signed)
 * into `out`. Returns the number of samples actually copied (may be less
 * than `sample_count` if the internal scratch didn't have that many). */
size_t ym2612_render(int16_t *out, size_t sample_count);

/* How many stereo samples are sitting in the internal scratch waiting to
 * be rendered. Call after ym2612_advance / write to know how many to
 * request from ym2612_render. */
size_t ym2612_samples_available(void);

/* Sample rate the renderer emits at. Clownmdemu uses master/144 internally
 * upsampled — we'll match at init time. */
uint32_t ym2612_sample_rate(void);

#endif
