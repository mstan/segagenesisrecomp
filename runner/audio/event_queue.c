/*
 * event_queue.c — single-threaded cycle-stamped audio event ring.
 */
#include "event_queue.h"
#include "../chip_trace.h"   /* [CHIP-TRACE] oracle-side FM/PSG stream tap */
#include <assert.h>
#include <stdio.h>

/* Capacity sized for worst case: Z80-driven DAC sample writes hit reg 0x2A
 * at ~8 kHz, which is ~140 writes per wall frame (addr + data = 280). Plus
 * music FM writes, SFX bursts, PSG. 4096 gives 14x headroom on measured
 * peak (~280/frame). */
#define QUEUE_CAP 4096

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

size_t audio_event_queue_count(void)
{
    return (s_head >= s_tail) ? (s_head - s_tail) : (QUEUE_CAP - s_tail + s_head);
}
