/*
 * event_queue.c — single-threaded cycle-stamped audio event ring.
 */
#include "event_queue.h"
#include "../chip_trace.h"   /* [CHIP-TRACE] oracle-side FM/PSG stream tap */
#include <assert.h>
#include <stdio.h>

/* Capacity sized for the worst REAL case, not the steady state: steady state
 * is ~280 events/frame (Z80 DAC pairs + music), but Sonic 1's Sega-scream
 * V-int handler feeds the whole PCM sample through the 68K in ONE handler
 * call — ~118k port writes whose stamps span ~129 frames of machine time.
 * Those events stay queued and drain frame-by-frame (audio_event_requeue),
 * so the queue must hold the full burst. 8 bytes/event -> 2 MB static. */
#define QUEUE_CAP 262144

static AudioEvent s_ring[QUEUE_CAP];
static size_t     s_head = 0;  /* producer writes here */
static size_t     s_tail = 0;  /* consumer reads here */
static size_t     s_overflow_count = 0;

void audio_event_push(uint32_t cycle_stamp, uint8_t port, uint8_t value)
{
    /* [CHIP-TRACE] Shared capture point for BOTH builds: the clownmdemu fork
     * routes every FM/PSG write here (bus-z80.c FM, bus-main-m68k.c PSG), and
     * the own backend now pushes all its FM/PSG writes here too
     * (genesis_bus.c), so one tap captures either build's stream into the
     * shared ring for a direct diff. cycle_stamp is per-frame master cycles
     * (own backend 68K-origin stamps use the instruction-counter axis, so the
     * derived "line" is execution progress, not raster — fine for the dev
     * diff). Strip before commit. */
    g_snd_line = (unsigned)(cycle_stamp / 3420u);
    g_snd_mc   = (unsigned long)cycle_stamp;
    if (port == AUDIO_PORT_PSG)
        snd_trace_chip(CHIP_PSG, 0, value);
    else                                    /* FM ports 0..3 map 1:1 to a&3 */
        snd_trace_chip(CHIP_FM, port, value);

    size_t next_head = (s_head + 1) % QUEUE_CAP;
    if (next_head == s_tail) {
        s_overflow_count++;
        if (s_overflow_count < 10)
            fprintf(stderr, "[audio-queue] OVERFLOW cap=%d head=%zu tail=%zu\n",
                    QUEUE_CAP, s_head, s_tail);
        return;
    }
    s_ring[s_head].cycle_stamp = cycle_stamp;
    s_ring[s_head].port        = port;
    s_ring[s_head].value       = value;
    s_head = next_head;
}

int audio_event_pop(AudioEvent *out)
{
    if (s_tail == s_head) return 0;
    *out = s_ring[s_tail];
    s_tail = (s_tail + 1) % QUEUE_CAP;
    return 1;
}

void audio_event_queue_reset(void)
{
    s_head = s_tail = 0;
}

/* Re-queue events the mixer deferred to a later wall frame (stamps beyond
 * the frame just drained, already rebased by the caller). Single-threaded:
 * called right after a drain that emptied the queue, so capacity is free. */
void audio_event_requeue(const AudioEvent *evs, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        size_t next_head = (s_head + 1) % QUEUE_CAP;
        if (next_head == s_tail) { s_overflow_count++; return; }
        s_ring[s_head] = evs[i];
        s_head = next_head;
    }
}

size_t audio_event_queue_count(void)
{
    return (s_head >= s_tail) ? (s_head - s_tail) : (QUEUE_CAP - s_tail + s_head);
}
