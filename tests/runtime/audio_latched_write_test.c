#include "audio/event_order.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static AudioOrderedEvent events[64], scratch[64];
static unsigned expected[64];
static size_t count;

void audio_event_push(uint32_t stamp, uint8_t port, uint8_t value)
{
    CHECK(count < 64);
    events[count].e.cycle_stamp = stamp;
    events[count].e.port = port;
    events[count].e.value = value;
    events[count].idx = (uint32_t)count;
    expected[count++] = 0xFFFF;
}
static void data(uint32_t stamp, uint8_t port, uint8_t reg, uint8_t value, int latched)
{
    size_t before = count;
    if (latched) audio_event_push_latched(stamp, port, reg, value);
    else audio_event_push(stamp, port, value);
    expected[count - 1] = reg | ((port >> 1) << 8);
    if (latched) {
        CHECK(count == before + 2);
        CHECK(events[before].e.cycle_stamp == stamp);
        CHECK(events[before + 1].e.cycle_stamp == stamp);
        CHECK(events[before].e.port + 1 == port);
    }
}
static unsigned wrong_destinations(void)
{
    unsigned address = 0, wrong = 0;
    audio_event_order(events, scratch, count, 1);
    for (size_t i = 0; i < count; ++i) {
        AudioEvent e = events[i].e;
        if (e.port == AUDIO_PORT_PSG) continue;
        if (!(e.port & 1)) address = e.value | ((e.port >> 1) << 8);
        else if (expected[events[i].idx] != address) ++wrong;
    }
    return wrong;
}
static unsigned mixed_clocks(int latched)
{
    count = 0;
    /* 68K tick emits an FM update and restores the PCM address. */
    audio_event_push(100, AUDIO_PORT_FM1_ADDR, 0x28);
    data(110, AUDIO_PORT_FM1_DATA, 0x28, 0, 0);
    audio_event_push(120, AUDIO_PORT_FM1_ADDR, 0x2A);
    audio_event_push(200, AUDIO_PORT_FM2_ADDR, 0xB0);
    data(210, AUDIO_PORT_FM2_DATA, 0xB0, 0x33, 0);
    audio_event_push(220, AUDIO_PORT_FM1_ADDR, 0x2A);
    /* Z80 executes later, with stamps spanning the same tick. Its real
     * address remains $2A, including when the sorted FM update falls between
     * successive data-only writes. */
    data(50, AUDIO_PORT_FM1_DATA, 0x2A, 0x80, latched);
    data(150, AUDIO_PORT_FM1_DATA, 0x2A, 0x90, latched);
    data(250, AUDIO_PORT_FM1_DATA, 0x2A, 0xA0, latched);
    return wrong_destinations();
}
int main(void)
{
    CHECK(mixed_clocks(0) > 0);  /* fixture actually reproduces corruption */
    CHECK(mixed_clocks(1) == 0);
    count = 0;
    data(UINT32_MAX, AUDIO_PORT_FM2_DATA, 0xB5, 0xC0, 1);
    data(0, AUDIO_PORT_FM1_DATA, 0x2A, 0x80, 1);
    CHECK(wrong_destinations() == 0);
    puts("Latched FM writes retain both bank destinations across mixed clocks");
    return 0;
}
