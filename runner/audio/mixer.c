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
#include "fm_shadow.h"   /* verified-enhancement FM shadow (default OFF) */
#include <stdlib.h>
#include <string.h>

void audio_mixer_init(void)
{
    ym2612_init();
    psg_init();
    /* Arm the FM shadow if opted in (GENESIS_AUDIO_SHADOW). Default OFF ⇒
     * every fm_shadow_* call below is a cheap no-op and audio is
     * byte-identical to the canon path. */
    fm_shadow_init();
}

/* The own backend pushes events from two stamp axes (68K instruction counter
 * for 68K-origin writes, raster cursor for Z80-origin writes — see
 * genesis_machine.c "Audio write stamping"), and its scheduler runs the 68K
 * V-int handler as one lump out of raster order. So the queue's PUSH order is
 * not stamp order. Applying in push order with the monotonic clamp below
 * would collapse whichever source arrives late — the write-collapse bug.
 * Sorting by (stamp, push index) reconstructs the modeled timeline. The
 * oracle build's stamps come from one clock already in order, so the sort is
 * a no-op there. */
typedef struct { AudioEvent e; uint32_t idx; } SortedEvt;
static int evt_cmp(const void *pa, const void *pb)
{
    const SortedEvt *a = (const SortedEvt *)pa, *b = (const SortedEvt *)pb;
    if (a->e.cycle_stamp != b->e.cycle_stamp)
        return a->e.cycle_stamp < b->e.cycle_stamp ? -1 : 1;
    return a->idx < b->idx ? -1 : (a->idx > b->idx ? 1 : 0);   /* stable */
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

    /* Collect the frame's events, then sort by stamp (stable). */
    enum { DRAIN_CAP = 262144 };   /* matches event_queue.c QUEUE_CAP */
    static SortedEvt evs[DRAIN_CAP];
    size_t n = 0;
    {
        AudioEvent pe;
        while (n < DRAIN_CAP && audio_event_pop(&pe)) {
            evs[n].e = pe; evs[n].idx = (uint32_t)n; n++;
        }
    }

    /* Pair guard: give each FM ADDRESS write the stamp of its matching DATA
     * write (the next FM event on the same part, which is its adjacent push
     * in practice). With equal stamps and the stable tiebreak, no foreign
     * event can sort between an address write and the data write that
     * completes it, so the synth's address latch can never be clobbered
     * mid-pair by cross-axis interleaving. */
    for (size_t i = 0; i < n; i++) {
        uint8_t p = evs[i].e.port;
        if (p == AUDIO_PORT_FM1_ADDR || p == AUDIO_PORT_FM2_ADDR) {
            for (size_t j = i + 1; j < n; j++) {
                uint8_t q = evs[j].e.port;
                if (q == p + 1) { evs[i].e.cycle_stamp = evs[j].e.cycle_stamp; break; }
                if (q == p)     break;   /* re-latched address; keep own stamp */
            }
        }
    }

    qsort(evs, n, sizeof(SortedEvt), evt_cmp);

    /* Multi-frame spreading: a handler that runs long (the Sega-scream PCM
     * V-int feeds ~118k DAC writes whose stamps span ~129 frames of machine
     * time) queues events far beyond this wall frame. Apply only events
     * stamped within the frame; DEFER the rest to subsequent drains with
     * the frame's span subtracted — the burst then plays at its real pace
     * across the debt-repayment frames, exactly as on hardware, instead of
     * collapsing into one frame (or, pre-clamp, stretching the sample
     * budget). The split point backs up over a trailing incomplete FM
     * (address, data) pair so a pair never straddles the boundary with a
     * foreign write able to sort between its halves next frame. */
    size_t n_apply = n;
    while (n_apply > 0 && evs[n_apply - 1].e.cycle_stamp > frame_end_cycle)
        n_apply--;
    if (n_apply > 0) {
        uint8_t lastp = evs[n_apply - 1].e.port;
        if (lastp == AUDIO_PORT_FM1_ADDR || lastp == AUDIO_PORT_FM2_ADDR)
            n_apply--;   /* keep the orphan address with its deferred data */
    }
    if (n_apply < n) {
        static AudioEvent defer[DRAIN_CAP];
        size_t d = 0;
        for (size_t i = n_apply; i < n; i++) {
            AudioEvent e = evs[i].e;
            e.cycle_stamp = e.cycle_stamp > frame_end_cycle
                            ? e.cycle_stamp - frame_end_cycle : 0;
            defer[d++] = e;
        }
        audio_event_requeue(defer, d);
    }

    for (size_t i = 0; i < n_apply; i++) {
        AudioEvent e = evs[i].e;
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
            if (delta > 0) { ym2612_advance(delta); fm_shadow_advance(delta); }
            /* Mirror the identical port/value into the shadow chip (no-op
             * unless GENESIS_AUDIO_SHADOW is on) so it tracks the same
             * register-write timeline as canon. */
            switch (e.port) {
                case AUDIO_PORT_FM1_ADDR: ym2612_write(0, e.value); fm_shadow_write(0, e.value); break;
                case AUDIO_PORT_FM1_DATA: ym2612_write(1, e.value); fm_shadow_write(1, e.value); break;
                case AUDIO_PORT_FM2_ADDR: ym2612_write(2, e.value); fm_shadow_write(2, e.value); break;
                case AUDIO_PORT_FM2_DATA: ym2612_write(3, e.value); fm_shadow_write(3, e.value); break;
                default: break;
            }
            fm_prev = e.cycle_stamp;
        }
    }

    /* Tail-advance each chip to end of wall frame. */
    if (frame_end_cycle > fm_prev) {
        ym2612_advance(frame_end_cycle - fm_prev);
        fm_shadow_advance(frame_end_cycle - fm_prev);
    }
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

    /* Verified-enhancement FM shadow: feed (canon, shadow) to the verifier and
     * substitute the shadow FM samples into fm_stereo_out only when proven;
     * revert loudly (DEGRADED) on divergence. No-op unless GENESIS_AUDIO_SHADOW
     * is on, so this keeps fm_stereo_out byte-identical by default. Canon FM
     * (just rendered above) remains the authoritative stream and the oracle. */
    if (fm_stereo_out && fm_n > 0) fm_shadow_verify_and_substitute(fm_stereo_out, fm_n);

    if (fm_written)  *fm_written  = fm_n;
    if (psg_written) *psg_written = psg_n;
}
