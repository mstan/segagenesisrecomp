/*
 * event_queue.c — single-threaded cycle-stamped audio event ring.
 */
#include "event_queue.h"
#include "../chip_trace.h"   /* [CHIP-TRACE] oracle-side FM/PSG stream tap */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>   /* getenv for the write-stream dump */

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

#ifdef GENESIS_COSIM
/* Always-on chip-write history ring (co-sim drill): records EVERY FM/PSG write
 * pushed through this single choke point — both 68K- and Z80-origin — with its
 * cycle stamp, indexed by a monotonic push counter so the Nth write is "the
 * same" write on both backends. Never drained (distinct from the mixer queue);
 * a cosim `chiphist` command dumps the last N to diff stamp-vs-value drift. */
#include "../cosim.h"
#include "../video/genesis_machine.h"   /* g_machine.master_cycle (raster ruler) */
#define HIST_N 131072u   /* power of two */
typedef struct { uint64_t idx; uint32_t stamp; uint32_t mcyc; uint8_t port, value; } HistEnt;
static HistEnt  s_hist[HIST_N];
static uint64_t s_hist_total = 0;
/* Stamp-INDEPENDENT cumulative hash of the FM/PSG write stream (port+value, in
 * order). Pairing #2 cross-backend audio comparable: two faithful backends issue
 * the same register writes in the same order even if their cycle stamps differ
 * (different timing models). Updated on every push. */
static uint64_t s_stream_hash = COSIM_FNV_OFFSET;
static void hist_record(uint32_t stamp, uint8_t port, uint8_t value) {
    HistEnt *h = &s_hist[s_hist_total & (HIST_N - 1u)];
    h->idx = s_hist_total; h->stamp = stamp;
    h->mcyc = g_machine.master_cycle;   /* raster ruler (own-backend drill only) */
    h->port = port; h->value = value;
    s_hist_total++;
    s_stream_hash = cosim_fnv_u8(cosim_fnv_u8(s_stream_hash, port), value);
}
uint64_t audio_event_cosim_stream_hash(uint64_t *out_count) {
    if (out_count) *out_count = s_hist_total;
    return s_stream_hash;
}
uint64_t audio_event_cosim_hist_total(void) { return s_hist_total; }
/* Fill caller arrays (>= n) with the last n pushes, oldest first. Returns count.
 * out_mcyc is g_machine.master_cycle at the write (the raster ruler) — vs
 * out_stamp (the g_audio_cycle_counter*7 axis the mixer actually uses). */
int audio_event_cosim_hist(uint64_t *out_idx, uint32_t *out_stamp, uint32_t *out_mcyc,
                           uint8_t *out_port, uint8_t *out_val, int n) {
    if (n > (int)HIST_N) n = (int)HIST_N;
    if ((uint64_t)n > s_hist_total) n = (int)s_hist_total;
    uint64_t start = s_hist_total - (uint64_t)n;
    for (int i = 0; i < n; i++) {
        HistEnt *h = &s_hist[(start + i) & (HIST_N - 1u)];
        out_idx[i] = h->idx; out_stamp[i] = h->stamp; out_mcyc[i] = h->mcyc;
        out_port[i] = h->port; out_val[i] = h->value;
    }
    return n;
}
#endif /* GENESIS_COSIM */

void audio_event_push(uint32_t cycle_stamp, uint8_t port, uint8_t value)
{
#ifdef GENESIS_COSIM
    hist_record(cycle_stamp, port, value);
#endif
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

    /* Always-on WHOLE-RUN write-stream dump for the render-differential measurement
     * (env GENESIS_AUDIO_WRITEDUMP=<path>, chip_ring format so tools/synth_replay
     * consumes it directly). Unlike the bounded chip_trace ring, this is the full
     * run, so synth_replay's canonical render aligns with the recomp's continuous
     * actual render (dump_predrc) for a per-window divergence map. No-op unless set. */
    {
        extern unsigned long g_snd_frame;
        static FILE *s_wd = NULL; static int s_wd_init = 0;
        if (!s_wd_init) {
            s_wd_init = 1;
            const char *p = getenv("GENESIS_AUDIO_WRITEDUMP");
            if (p && *p) s_wd = fopen(p, "w");
        }
        if (s_wd) {
            if (port == AUDIO_PORT_PSG)
                fprintf(s_wd, "f=%lu sl=%u PSG $%02X mc=%lu wf=%lu\n",
                        g_snd_frame, (unsigned)(cycle_stamp/3420u), value,
                        (unsigned long)cycle_stamp, g_snd_frame);
            else
                fprintf(s_wd, "f=%lu sl=%u FM  p%u $%02X mc=%lu wf=%lu\n",
                        g_snd_frame, (unsigned)(cycle_stamp/3420u), port, value,
                        (unsigned long)cycle_stamp, g_snd_frame);
        }
    }

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

#ifdef GENESIS_COSIM
#include "../cosim.h"
/* Hash the PENDING (undrained) cycle-stamped writes, in FIFO order, plus the
 * queue depth. A phase error in the 68K-side stamp path shows up here as an
 * event carrying a different cycle_stamp between the two backends. */
uint64_t audio_event_cosim_hash(void)
{
    uint64_t h = cosim_fnv_init();
    size_t n = audio_event_queue_count();
    h = cosim_fnv_u64(h, (uint64_t)n);
    for (size_t i = 0, idx = s_tail; i < n; i++, idx = (idx + 1) % QUEUE_CAP) {
        h = cosim_fnv_u32(h, s_ring[idx].cycle_stamp);
        h = cosim_fnv_u8(h, s_ring[idx].port);
        h = cosim_fnv_u8(h, s_ring[idx].value);
    }
    return h;
}
#endif /* GENESIS_COSIM */
