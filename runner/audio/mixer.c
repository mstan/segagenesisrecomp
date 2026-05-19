/*
 * mixer.c — cycle-stamped event drain + sample render.
 *
 * Called once per wall frame from glue_end_of_wall_frame. Walks every
 * queued (cycle_stamp, port, value) event in order, advancing the YM2612
 * and PSG by the cycle delta BEFORE each write so envelope/LFO state
 * progresses between writes. After all events, advances to frame_end_cycle
 * and renders the full wall-frame's worth of samples.
 *
 * The advance-between-writes step is the architectural fix for the
 * boop/squelch artifact — writes no longer collapse onto a single FM-
 * sample boundary because the chip's sample clock genuinely ticks between
 * them at their real cycle spacing.
 */
#include "mixer.h"
#include "event_queue.h"
#include "ym2612.h"
#include "sn76489.h"
#include <string.h>

void audio_mixer_init(void)
{
    ym2612_init();
    psg_init();
}

void audio_mixer_drain(uint32_t frame_end_cycle,
                       int16_t *fm_stereo_out, size_t fm_cap, size_t *fm_written,
                       int16_t *psg_mono_out,  size_t psg_cap, size_t *psg_written)
{
    /* Per-chip monotonic cycle trackers. Each chip advances only at its
     * own write events (plus the frame tail), matching clownmdemu's
     * per-chip SyncFM / SyncPSG pattern. Advancing both chips on every
     * event (previous implementation) entangled their sample clocks and
     * introduced PSG noise-LFSR desync with clownmdemu's reference. */
    uint32_t fm_prev  = 0;
    uint32_t psg_prev = 0;
    AudioEvent e;

    while (audio_event_pop(&e)) {
        if (e.port == AUDIO_PORT_PSG) {
            if (e.cycle_stamp < psg_prev) e.cycle_stamp = psg_prev;
            uint32_t delta = e.cycle_stamp - psg_prev;
            if (delta > 0) psg_advance(delta);
            psg_write(e.value);
            psg_prev = e.cycle_stamp;
        } else {
            /* FM ports 0..3 */
            if (e.cycle_stamp < fm_prev) e.cycle_stamp = fm_prev;
            uint32_t delta = e.cycle_stamp - fm_prev;
            if (delta > 0) ym2612_advance(delta);
            switch (e.port) {
                case AUDIO_PORT_FM1_ADDR: ym2612_write(0, e.value); break;
                case AUDIO_PORT_FM1_DATA: ym2612_write(1, e.value); break;
                case AUDIO_PORT_FM2_ADDR: ym2612_write(2, e.value); break;
                case AUDIO_PORT_FM2_DATA: ym2612_write(3, e.value); break;
                default: break;
            }
            fm_prev = e.cycle_stamp;
        }
    }

    /* Tail-advance each chip to end of wall frame. */
    if (frame_end_cycle > fm_prev)  ym2612_advance(frame_end_cycle - fm_prev);
    if (frame_end_cycle > psg_prev) psg_advance (frame_end_cycle - psg_prev);

    /* Match clownmdemu's per-Iterate sync-state reset: the sub-sample
     * fractional cycle carried across is discarded at each frame
     * boundary there (sync.psg.current_cycle is a per-Iterate local
     * that resets to 0). Our persistent leftover caused sub-sample
     * drift that may have been the boop source on long runs. */
    psg_reset_leftover();

    /* Copy generated samples into the caller's buffers. */
    size_t fm_avail  = ym2612_samples_available();
    size_t psg_avail = psg_samples_available();
    size_t fm_n  = fm_avail  < fm_cap  ? fm_avail  : fm_cap;
    size_t psg_n = psg_avail < psg_cap ? psg_avail : psg_cap;

    if (fm_stereo_out && fm_n > 0)  ym2612_render(fm_stereo_out,  fm_n);
    if (psg_mono_out  && psg_n > 0) psg_render(psg_mono_out,   psg_n);

    if (fm_written)  *fm_written  = fm_n;
    if (psg_written) *psg_written = psg_n;
}
