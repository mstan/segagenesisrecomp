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
    uint64_t phase;
    uint32_t phase_step, phase_den;
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
    p->phase = p->phase_step = p->phase_den = 0;
}

/* Exact counter ticks per rational guest frame. The two 32-bit factors in
 * the remainder product cannot overflow; reject an unrepresentable period. */
static inline int frame_pacer_init_ratio(FramePacer *p, uint64_t frequency,
                                         uint32_t ticks, uint32_t clock)
{
    p->active = 0;
    if (!frequency || !ticks || !clock) return 0;
    uint64_t q = frequency / clock, part = (frequency % clock) * ticks;
    if (q > UINT64_MAX / ticks) return 0;
    uint64_t whole = q * ticks, tail = part / clock;
    if (whole > UINT64_MAX - tail) return 0;
    p->period = whole + tail;
    p->phase_step = (uint32_t)(part % clock);
    p->phase_den = clock;
    p->phase = 0;
    p->fraction = p->remainder = 0.0;
    p->next = 0;
    return p->period != 0;
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
        p->phase = 0;
        p->active = 1;
    }
    deadline = p->next;
    p->next += p->period;
    if (p->phase_den) {
        p->phase += p->phase_step;
        if (p->phase >= p->phase_den) {
            ++p->next;
            p->phase -= p->phase_den;
        }
    } else {
        p->remainder += p->fraction;
        if (p->remainder >= 1.0) {
            ++p->next;
            p->remainder -= 1.0;
        }
    }
    return deadline;
}

#endif
