/*
 * mixer.h — drain audio event queue and render samples via ym2612 + psg.
 *
 * Called once per wall frame after ClownMDEmu_Iterate returns. Walks the
 * queued (cycle_stamp, port, value) events in order, advancing the chips
 * between events so register writes land at accurate sub-sample cycle
 * positions. Fills caller-provided FM and PSG sample buffers.
 *
 * Phase 1: header stub. Real implementation arrives in Phase 5 switchover.
 */
#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include <stdint.h>
#include <stddef.h>

/* Drain all queued events up to `frame_end_cycle` (68K cycles since
 * wall-frame start). Advances ym2612+psg through events in order and
 * fills the sample buffers for this wall frame.
 *
 * Returns the sample count written to each (may be less than buffer cap
 * on short frames). */
void audio_mixer_drain(uint32_t frame_end_cycle,
                       int16_t *fm_stereo_out, size_t fm_cap, size_t *fm_written,
                       int16_t *psg_mono_out,  size_t psg_cap, size_t *psg_written);

/* Call once at boot. */
void audio_mixer_init(void);

#endif
