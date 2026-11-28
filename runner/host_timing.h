/* Host-only hitch history, independent of trace/debug builds. Keep a bounded
 * ring in memory and write only at session exit: diagnostic I/O must not be
 * the cause of the stall being measured. No clocks or state enter simulation. */
#ifndef GENESIS_HOST_TIMING_H
#define GENESIS_HOST_TIMING_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "audio.h"

#define HOST_HITCH_CAP 128
typedef struct {
    uint32_t frame, interval_us, phase_us[9];
    AudioHostTiming audio;
} HostHitch;
typedef struct {
    HostHitch ring[HOST_HITCH_CAP];
    uint32_t count, max_interval_us;
    uint64_t previous_conceals;
} HostTiming;

static void host_timing_record(HostTiming *t, uint32_t frame,
                               uint32_t interval_us, const uint32_t phase_us[9],
                               const AudioHostTiming *audio)
{
    if (interval_us > t->max_interval_us) t->max_interval_us = interval_us;
    if (interval_us >= 50000 || audio->conceal_events != t->previous_conceals) {
        HostHitch *h = &t->ring[t->count++ % HOST_HITCH_CAP];
        h->frame = frame; h->interval_us = interval_us; h->audio = *audio;
        for (unsigned i = 0; i < 9; ++i) h->phase_us[i] = phase_us[i];
    }
    t->previous_conceals = audio->conceal_events;
}

static int host_timing_dump_requested(void)
{
    const char *request = getenv("GENESIS_HOST_TIMING_DUMP");
    return request && *request && *request != '0';
}

static void host_timing_dump(const HostTiming *t, FILE *out)
{
    /* External observers retain normal playtest evidence without making
     * window-close wait for a burst of synchronous stderr writes. Explicit
     * agent captures can still request this richer phase history. */
    if (!host_timing_dump_requested()) return;
    uint32_t n = t->count < HOST_HITCH_CAP ? t->count : HOST_HITCH_CAP;
    fprintf(out, "[HOST-TIMING] events=%u retained=%u max_frame_us=%u "
                 "phases=input,simulation,reserved,post,audio,persistence,present,layout,pacer\n",
            t->count, n, t->max_interval_us);
    for (uint32_t i = t->count - n; i < t->count; ++i) {
        const HostHitch *h = &t->ring[i % HOST_HITCH_CAP];
        fprintf(out, "[HOST-HITCH] frame=%u total_us=%u phase_us=", h->frame, h->interval_us);
        for (unsigned j = 0; j < 9; ++j) fprintf(out, "%s%u", j ? "," : "", h->phase_us[j]);
        fprintf(out, " lock_us=%u callback_us=%u fill_us=%u conceals=%llu repeated=%llu\n",
                h->audio.lock_us, h->audio.callback_us, h->audio.fill_us,
                (unsigned long long)h->audio.conceal_events,
                (unsigned long long)h->audio.concealed_frames);
    }
}
#endif
