#include "audio/event_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "queue state line%d: %s\n", __LINE__, #c); exit(1); } } while (0)
unsigned long g_snd_frame, g_snd_mc;
unsigned g_snd_line, g_snd_pcz;
void snd_trace_chip(int kind, uint8_t port, uint8_t value)
{ (void)kind; (void)port; (void)value; }

enum { COUNT = 5 };
static uint32_t stamp(unsigned i) { return i == 0 ? UINT32_MAX : i * 29876u; }
static void active_event(uint8_t *dst, unsigned i)
{
    uint32_t cycle = stamp(i);
    uint8_t port = (uint8_t)i, value = (uint8_t)(0xA0u + i);
    memcpy(dst + offsetof(AudioEvent, cycle_stamp), &cycle, sizeof cycle);
    memcpy(dst + offsetof(AudioEvent, port), &port, sizeof port);
    memcpy(dst + offsetof(AudioEvent, value), &value, sizeof value);
}
static void check_events(void)
{
    AudioEvent event;
    CHECK(audio_event_queue_count() == COUNT);
    for (unsigned i = 0; i < COUNT; ++i) {
        CHECK(audio_event_pop(&event));
        CHECK(event.cycle_stamp == stamp(i) && event.port == i && event.value == 0xA0u + i);
    }
    CHECK(!audio_event_pop(&event));
}
int main(void)
{
    const size_t compact_size = sizeof(uint32_t) + COUNT * sizeof(AudioEvent);
    const size_t fixed_size = audio_event_state_size();
    uint8_t compact[sizeof(uint32_t) + COUNT * sizeof(AudioEvent)];
    uint8_t canonical[sizeof compact], again[sizeof compact];
    uint8_t *fixed = malloc(fixed_size), *fixed_again = malloc(fixed_size);
    uint32_t count = COUNT;
    CHECK(fixed && fixed_again);
    memset(canonical, 0, sizeof canonical);
    memcpy(canonical, &count, sizeof count);
    for (unsigned i = 0; i < COUNT; ++i)
        active_event(canonical + sizeof count + i * sizeof(AudioEvent), i);
    /* The same pending writes can arrive with different host padding. Both
     * snapshot APIs must preserve the writes and produce the same bytes. */
    for (unsigned poison = 0; poison < 2; ++poison) {
        memset(compact, poison ? 0x5A : 0xA5, sizeof compact);
        memcpy(compact, &count, sizeof count);
        for (unsigned i = 0; i < COUNT; ++i)
            active_event(compact + sizeof count + i * sizeof(AudioEvent), i);
        CHECK(audio_event_rb_load(compact, compact_size));
        CHECK(audio_event_rb_save(again, sizeof again) == sizeof again);
        CHECK(!memcmp(again, canonical, sizeof again));
        CHECK(audio_event_state_save(fixed, fixed_size));
        CHECK(!memcmp(fixed, canonical, sizeof canonical));
        for (size_t i = sizeof canonical; i < fixed_size; ++i) CHECK(fixed[i] == 0);
        check_events();
        CHECK(audio_event_state_load(fixed, fixed_size, 1));
        CHECK(audio_event_state_save(fixed_again, fixed_size));
        CHECK(!memcmp(fixed, fixed_again, fixed_size));
        check_events();
    }
    /* Physical wrap and deferred requeue retain FIFO cycles and values. */
    audio_event_queue_reset();
    for (unsigned i = 0; i < 262142; ++i) {
        AudioEvent event;
        audio_event_push(i, AUDIO_PORT_PSG, (uint8_t)i);
        CHECK(audio_event_pop(&event));
    }
    for (unsigned i = 0; i < COUNT; ++i) audio_event_push(stamp(i), (uint8_t)i, (uint8_t)(0xA0u + i));
    CHECK(audio_event_rb_save(again, sizeof again) == sizeof again);
    CHECK(!memcmp(again, canonical, sizeof again));
    AudioEvent deferred[COUNT];
    for (unsigned i = 0; i < COUNT; ++i) CHECK(audio_event_pop(&deferred[i]));
    audio_event_requeue(deferred, COUNT);
    CHECK(audio_event_rb_save(again, sizeof again) == sizeof again);
    CHECK(!memcmp(again, canonical, sizeof again));
    /* Invalid loads and insufficient save capacity cannot change the queue. */
    memcpy(compact, canonical, sizeof compact);
    compact[sizeof count + offsetof(AudioEvent, port)] = 0xFF;
    CHECK(!audio_event_rb_load(compact, sizeof compact));
    CHECK(!audio_event_rb_load(canonical, sizeof canonical - 1));
    CHECK(!audio_event_rb_save(again, sizeof again - 1));
    memcpy(fixed_again, fixed, fixed_size);
    fixed_again[sizeof count + offsetof(AudioEvent, port)] = 0xFF;
    CHECK(!audio_event_state_load(fixed_again, fixed_size, 1));
    CHECK(audio_event_rb_save(again, sizeof again) == sizeof again);
    CHECK(!memcmp(again, canonical, sizeof again));
    check_events();
    free(fixed); free(fixed_again);
    puts("audio queue state: canonical padding, full pending writes, wrap/requeue, both formats and invalid loads PASS");
    return 0;
}
