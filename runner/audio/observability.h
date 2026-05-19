/*
 * observability.h — audio output inspection + transient ("boop") detector.
 *
 * The rdb_* subsystem gives us thorough CPU-side observability: cycle
 * counts, memory writes, register state. This is the parallel for audio
 * OUTPUT. On every rendered sample, it tracks derivative (sample-to-sample
 * delta) statistics per wall frame and flags frames whose peak-derivative
 * or RMS-derivative exceeds a noise-floor baseline — those frames almost
 * certainly contain an audible boop/click.
 *
 * Usage: audio_obs_ingest_fm / audio_obs_ingest_psg feeds samples after
 * each wall frame's render. audio_obs_tick_frame flushes and (optionally)
 * emits a report line for anomalous frames.
 */
#ifndef AUDIO_OBSERVABILITY_H
#define AUDIO_OBSERVABILITY_H

#include <stdint.h>
#include <stddef.h>

/* Feed samples from a render pass. Safe to call multiple times per wall
 * frame (accumulates). */
void audio_obs_ingest_fm (const int16_t *stereo, size_t frames);
void audio_obs_ingest_psg(const int16_t *mono,   size_t frames);

/* Call once per wall frame after all ingests. If the frame's stats
 * exceed the anomaly threshold, writes a [BOOP] report line to stderr
 * and resets internal accumulators for the next frame.
 *
 * frame_num is the wall frame index (caller supplies).
 * game_mode + internal_frame are pass-through fields for cross-reference. */
void audio_obs_tick_frame(uint64_t frame_num,
                           uint32_t internal_frame,
                           uint8_t  game_mode);

/* Call once at init. */
void audio_obs_init(void);

/* Enable/disable reporting (off by default until --boop-detect flag or
 * programmatic enable). */
void audio_obs_set_enabled(int on);

#endif
