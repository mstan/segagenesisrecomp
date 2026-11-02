#ifndef GENESIS_FRAME_PACER_H
#define GENESIS_FRAME_PACER_H

#include <stdint.h>

/* Host presentation deadlines, independent of guest cycles. Keep the fractional
 * counter ticks so NTSC pacing does not accumulate truncation error. */
typedef struct FramePacer {
    uint64_t next;
    uint64_t period;
    double fraction;
    double remainder;
    int active;
} FramePacer;

static inline void frame_pacer_init(FramePacer *p, uint64_t frequency, double fps)
{
    double ticks = (double)frequency / fps;
    p->period = (uint64_t)ticks;
    p->fraction = ticks - (double)p->period;
    p->next = 0;
    p->remainder = 0.0;
    p->active = 0;
}

static inline void frame_pacer_reset(FramePacer *p)
{
    p->active = 0;
}

/* Called when a frame is ready to present. A small scheduling delay retains
 * the cadence; a full frame overrun (including a pause) starts a fresh cadence
 * instead of issuing a burst of catch-up frames. Turbo resets this schedule. */
static inline uint64_t frame_pacer_deadline(FramePacer *p, uint64_t now)
{
    uint64_t deadline;
    if (!p->active || (now > p->next && now - p->next > p->period)) {
        p->next = now;
        p->remainder = 0.0;
        p->active = 1;
    }
    deadline = p->next;
    p->next += p->period;
    p->remainder += p->fraction;
    if (p->remainder >= 1.0) {
        ++p->next;
        p->remainder -= 1.0;
    }
    return deadline;
}

/* A second clock enumerates display deadlines inside each guest interval.
 * Never round display/guest to an integer: 144 Hz needs alternating two/three
 * presentations per NTSC tick. This clock cannot advance the simulation. */
typedef struct PresentationClock {
    FramePacer display;
    uint64_t frequency;
    double rate;
    int active;
} PresentationClock;
static inline void presentation_clock_reset(PresentationClock *p)
{ p->active = 0; }
static inline void presentation_clock_begin(PresentationClock *p, uint64_t frequency,
                                            double rate, uint64_t start)
{
    if (!p->active || p->rate != rate || p->frequency != frequency ||
        (start > p->display.next && start - p->display.next > frequency / 4)) {
        frame_pacer_init(&p->display, frequency, rate);
        p->display.next = start; p->display.active = 1;
        p->frequency = frequency; p->rate = rate; p->active = 1;
    }
    /* Drop overdue output, never emit a burst or speed the guest up. */
    while (p->display.next < start)
        (void)frame_pacer_deadline(&p->display, p->display.next);
}
static inline int presentation_clock_next(PresentationClock *p, uint64_t start,
                                          double guest_period, uint64_t *deadline,
                                          double *phase)
{
    uint64_t at = p->display.next;
    if ((double)(at - start) >= guest_period) return 0;
    *deadline = frame_pacer_deadline(&p->display, at);
    *phase = (double)(at - start) / guest_period;
    return 1;
}

#endif
