#include "frame_pacer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    const uint64_t frequency = 10000000;
    const double fps = 60.0 / 1.001;
    const double period = (double)frequency / fps;
    const uint64_t origin = 1234567;
    FramePacer p;
    frame_pacer_init(&p, frequency, fps);

    /* Variable rendering work must not move presentation deadlines. Over a
     * million frames the fractional NTSC period stays within one clock tick. */
    for (uint64_t i = 0; i < 1000000; ++i) {
        uint64_t expected = origin + (uint64_t)((double)i * period);
        uint64_t ready = i ? expected - (1000 + i % 90000) : origin;
        uint64_t actual = frame_pacer_deadline(&p, ready);
        assert(fabs((double)actual - ((double)origin + (double)i * period)) < 1.01);
    }

    /* A slightly late frame does not permanently shift all later frames. */
    uint64_t next = p.next;
    assert(frame_pacer_deadline(&p, next + 100) == next);
    assert(frame_pacer_deadline(&p, p.next - 1000) >= next + p.period);

    /* A stall resumes at the current time without a burst of overdue frames. */
    uint64_t resume = p.next + frequency * 3;
    assert(frame_pacer_deadline(&p, resume) == resume);
    assert(frame_pacer_deadline(&p, resume + 1000) == resume + p.period);

    /* Leaving turbo discards its old deadline; a new session is independent. */
    frame_pacer_reset(&p);
    resume += frequency * 10;
    assert(frame_pacer_deadline(&p, resume) == resume);
    frame_pacer_init(&p, frequency, 120.0);
    assert(frame_pacer_deadline(&p, 1) == 1);
    assert(frame_pacer_deadline(&p, 2) == 83334);
    const uint64_t frequencies[] = {1000000, 10000000, 1000000000};
    for (unsigned f = 0; f < sizeof frequencies / sizeof frequencies[0]; ++f) {
        uint64_t hz = frequencies[f], exact = origin, phase = 0;
        assert(frame_pacer_init_ratio(&p, hz, 896040, 53693175));
        for (uint64_t i = 0; i < 1000000; ++i) {
            uint64_t ready = i ? exact - p.period / 4 : origin;
            assert(frame_pacer_deadline(&p, ready) == exact);
            phase += hz * 896040;
            exact += phase / 53693175;
            phase %= 53693175;
        }
        /* A blocking present or paused frame must not cause a catch-up burst. */
        resume = p.next + hz;
        assert(frame_pacer_deadline(&p, resume) == resume);
        frame_pacer_reset(&p);
        assert(frame_pacer_deadline(&p, resume + 1) == resume + 1);
    }
    assert(!frame_pacer_init_ratio(&p, 0, 1, 1));
    assert(!frame_pacer_init_ratio(&p, 1, 0, 1));
    assert(!frame_pacer_init_ratio(&p, 1, 1, 0));
    assert(!frame_pacer_init_ratio(&p, UINT64_MAX, UINT32_MAX, 1));
    puts("frame presentation cadence OK");
    return 0;
}
