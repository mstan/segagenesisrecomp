/*
 * fm_shadow.h — verified-enhancement FM (YM2612) shadow re-render.
 *
 * QoL-on-top-of-correctness per PRINCIPLES.md "Verified-Enhancement HLE Is
 * Allowed; Load-Bearing HLE Is Not". The canon YM2612 (ym2612_ymfm.cpp) still
 * runs and still produces the authoritative FM stream (which stays the verify
 * oracle / [FBHASH]-equivalent WAV reference). This shadow renders a SECOND
 * ymfm YM2612 fed the IDENTICAL cycle-stamped register-write stream, but:
 *
 *   - the hardware FM-output low-pass that the canon wrapper applies to TAME
 *     ymfm's excess highs (and match the clownmdemu reference) is RELAXED —
 *     the shadow keeps the cleaner, less-aliased high-frequency content the
 *     chip model actually produces;
 *   - optionally the YM2612 "ladder effect" DAC quantization quirk is
 *     corrected (GENESIS_FM_LADDER=off). ymfm models the ladder effect; the
 *     correction is an explicit, opt-in option, NOT a guess at hardware.
 *
 * It is policed every grid sample by the engine-agnostic ShadowVerifier
 * (audio_shadow.h) against the canon FM stream, substitutes only after a
 * proven window, and reverts loudly (DEGRADED) the instant it stops matching.
 *
 * DEFAULT OFF. When off, fm_shadow_active() is false, the mixer never touches
 * the shadow, and output is byte-identical to the canon path. Opt in with
 * GENESIS_AUDIO_SHADOW=1 (and optionally GENESIS_FM_LADDER=off).
 *
 * Attribution: the verifier is the JRickey/gba-recomp shadow.rs port (see
 * audio_shadow.h). The FM chip is ymfm (Aaron Giles, BSD-3-Clause). The
 * parallel-instance + verify wiring is ours.
 */
#ifndef AUDIO_FM_SHADOW_H
#define AUDIO_FM_SHADOW_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read GENESIS_AUDIO_SHADOW / GENESIS_FM_LADDER from the environment and arm
 * the shadow if requested. Off by default. Idempotent. */
void fm_shadow_init(void);

/* True only when: opted in AND the verifier has proven a window. While false,
 * the mixer must use the canon FM samples verbatim (byte-identical). */
int fm_shadow_active(void);

/* True if the user opted in at all (regardless of proven state) — used to
 * decide whether to keep the shadow chip ticking. */
int fm_shadow_enabled(void);

/* Mirror a canon ym2612_write into the shadow chip (same port/value). No-op
 * unless enabled. */
void fm_shadow_write(uint8_t port, uint8_t value);

/* Mirror a canon ym2612_advance into the shadow chip (same MASTER cycles),
 * emitting the shadow's own samples into its scratch ring. No-op unless
 * enabled. */
void fm_shadow_advance(uint32_t cycles_master);

/* After the canon FM samples for this drain are rendered, feed the verifier
 * the (canon, shadow) stereo pairs sample-by-sample and, when proven,
 * substitute the shadow samples into `fm_stereo_out` in place.
 *
 * `fm_stereo_out` holds `fm_n` canon stereo samples (L,R int16 interleaved) —
 * the same buffer the mixer is about to hand to audio.c. On entry it is canon;
 * on return it is either unchanged (not proven / off) or replaced by the
 * gain-calibrated shadow render (proven). If the verifier just reverted, the
 * reason is logged to stderr ([AUDIO-SHADOW] DEGRADED ...). No-op unless
 * enabled. */
void fm_shadow_verify_and_substitute(int16_t *fm_stereo_out, size_t fm_n);

/* Reset shadow chip + verifier (lifecycle / save-state load). */
void fm_shadow_reset(void);

#ifdef __cplusplus
}
#endif

#endif  /* AUDIO_FM_SHADOW_H */
