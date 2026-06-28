/*
 * blastem_psg.h — minimal standalone SN76489 replay core, vendored from
 * BlastEm's psg.c/psg.h (Copyright 2013 Michael Pavone, GPLv3).
 *
 * ⚠️ LICENSING / POSTURE: BlastEm is GPLv3. This file is a verbatim extract of
 * its SN76489 model, used ONLY inside the DEV-ONLY synth_replay measurement
 * harness as a die-accurate-ish PSG truth reference. It is NEVER compiled into
 * or linked with our recompiler / runner / shipped binaries (those stay
 * AGPL/GPL-free). Same posture as the clownmdemu + Nuked-OPN2 dev oracles the
 * harness already links. Do not ship. Do not commit.
 *
 * Extraction: the psg_context fields that affect synthesis + psg_write() +
 * psg_run()'s per-sample LFSR/tone/volume generation, with BlastEm's renderer
 * (render_put_stereo_sample / audio_source / vgm / event_log / scope /
 * serialize) stripped and replaced by a thin "emit one mono sample" shim. The
 * NOISE math (output-flip-flop divide-by-2, LFSR rotate + tap, output tap
 * phase) and the volume table are reproduced bit-for-bit from upstream.
 */
#ifndef BLASTEM_PSG_H_
#define BLASTEM_PSG_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t lsfr;
    uint16_t counter_load[4];
    uint16_t counters[4];
    uint8_t  volume[4];
    uint8_t  output_state[4];
    uint8_t  noise_out;
    uint8_t  noise_use_tone;
    uint8_t  noise_type;
    uint8_t  latch;
    uint8_t  pan;
} bpsg_context;

/* Mirrors BlastEm psg_init (memset 0; volume[]=0xF; pan=0xFF). lsfr is left 0
 * exactly as upstream — it is seeded to 0x8000 on the first noise-control
 * write, which the captured ring performs ($E_). */
void bpsg_init(bpsg_context *ctx);

/* Verbatim BlastEm psg_write() (latch/data protocol, noise-control handling,
 * ch2-tracking of the noise period) minus vgm/event_log side effects. */
void bpsg_write(bpsg_context *ctx, uint8_t value);

/* Run nsamples PSG output samples (one counter-decrement step each, i.e. the
 * body of BlastEm psg_run's while loop) and emit them mono into out. With the
 * Genesis default pan (0xFF) left == right, so we emit the left accumulator.
 * One step == master/240 (= our PSG sample period), matching sn76489.c and the
 * clownmdemu PSG_Update step the harness already uses — the comparison is
 * rate-aligned by construction. */
void bpsg_run(bpsg_context *ctx, int16_t *out, size_t nsamples);

#ifdef __cplusplus
}
#endif

#endif /* BLASTEM_PSG_H_ */
