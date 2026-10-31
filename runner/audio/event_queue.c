/*
 * event_queue.c — single-threaded cycle-stamped audio event ring.
 */
#include "event_queue.h"
#include "../chip_trace.h"   /* [CHIP-TRACE] oracle-side FM/PSG stream tap */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>   /* getenv for the write-stream dump */
#include <string.h>

/* Capacity sized for the worst REAL case, not the steady state: steady state
 * is ~280 events/frame (Z80 DAC pairs + music), but Sonic 1's Sega-scream
 * V-int handler feeds the whole PCM sample through the 68K in ONE handler
 * call — ~118k port writes whose stamps span ~129 frames of machine time.
 * Those events stay queued and drain frame-by-frame (audio_event_requeue),
 * so the queue must hold the full burst. 8 bytes/event -> 2 MB static. */
#define QUEUE_CAP 262144

static AudioEvent s_ring_storage[QUEUE_CAP],s_deferred_storage[QUEUE_CAP];
static AudioEvent *s_ring=s_ring_storage,*s_deferred=s_deferred_storage;
static size_t     s_head = 0;  /* producer writes here */
static size_t     s_tail = 0;  /* consumer reads here */
static size_t     s_overflow_count = 0;
static size_t s_deferred_first,s_deferred_count;
static uint32_t s_epoch;
static int s_deferred_sealed=1;
static size_t incoming_count(void)
{return s_head>=s_tail?s_head-s_tail:QUEUE_CAP-s_tail+s_head;}
static AudioEvent pending_event(size_t i)
{
    AudioEvent e=i<s_deferred_count?s_deferred[s_deferred_first+i]:
        s_ring[(s_tail+i-s_deferred_count)%QUEUE_CAP];
    e.cycle_stamp-=s_epoch;return e;
}
static void pending_reset(void)
{s_head=s_tail=s_deferred_first=s_deferred_count=0;s_epoch=0;s_deferred_sealed=1;}
size_t audio_event_state_size(void) { return sizeof(uint32_t)+sizeof s_ring_storage; }
size_t audio_event_rb_save(void *dst, size_t cap)
{
    uint32_t n = (uint32_t)audio_event_queue_count();
    size_t need = sizeof n + (size_t)n * sizeof(AudioEvent);
    if (!dst) return need;
    if (cap < need) return 0;
    memcpy(dst, &n, sizeof n);
    for (uint32_t i = 0; i < n; ++i){
        AudioEvent e=pending_event(i);
        memcpy((uint8_t *)dst+sizeof n+(size_t)i*sizeof e,&e,sizeof e);
    }
    return need;
}
int audio_event_rb_load(const void *src, size_t len)
{
    uint32_t n;
    if (!src || len < sizeof n) return 0;
    memcpy(&n, src, sizeof n);
    if (n >= QUEUE_CAP || len != sizeof n + (size_t)n * sizeof(AudioEvent)) return 0;
    for (uint32_t i = 0; i < n; ++i) {
        AudioEvent e;
        memcpy(&e, (const uint8_t *)src + sizeof n + (size_t)i * sizeof e, sizeof e);
        if (e.port > AUDIO_PORT_PSG) return 0;
    }
    pending_reset();
    memcpy(s_ring, (const uint8_t *)src + sizeof n, (size_t)n * sizeof(AudioEvent));
    s_tail = 0;
    s_head = n;
    return 1;
}
int audio_event_state_save(void *data,size_t size)
{
    if (!data || size!=audio_event_state_size()) return 0;
    uint32_t n=(uint32_t)audio_event_queue_count();
    memset(data,0,size); memcpy(data,&n,sizeof n);
    for (uint32_t i=0;i<n;++i){
        AudioEvent e=pending_event(i);
        memcpy((uint8_t *)data+sizeof n+i*sizeof e,&e,sizeof e);
    }
    return 1;
}
int audio_event_state_load(const void *data,size_t size,int apply)
{
    if (!data || size!=audio_event_state_size()) return 0;
    uint32_t n; memcpy(&n,data,sizeof n);
    if (n>=QUEUE_CAP) return 0;
    for (uint32_t i=0;i<n;++i) {
        AudioEvent e; memcpy(&e,(const uint8_t *)data+sizeof n+i*sizeof e,sizeof e);
        if (e.port>AUDIO_PORT_PSG) return 0;
    }
    if (apply) {
        pending_reset();
        memcpy(s_ring,(const uint8_t *)data+sizeof n,n*sizeof(AudioEvent));
        s_tail=0; s_head=n;
    }
    return 1;
}

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
                fprintf(s_wd, "f=%lu sl=%u PSG $%02X mc=%lu wf=%lu pcz=$%04X\n",
                        g_snd_frame, (unsigned)(cycle_stamp/3420u), value,
                        (unsigned long)cycle_stamp, g_snd_frame, g_snd_pcz);
            else
                fprintf(s_wd, "f=%lu sl=%u FM  p%u $%02X mc=%lu wf=%lu pcz=$%04X\n",
                        g_snd_frame, (unsigned)(cycle_stamp/3420u), port, value,
                        (unsigned long)cycle_stamp, g_snd_frame, g_snd_pcz);
        }
    }

    size_t next_head = (s_head + 1) % QUEUE_CAP;
    if (audio_event_queue_count() == QUEUE_CAP-1u) {
        s_overflow_count++;
        if (s_overflow_count < 10)
            fprintf(stderr, "[audio-queue] OVERFLOW cap=%d head=%zu tail=%zu\n",
                    QUEUE_CAP, s_head, s_tail);
        return;
    }
    s_ring[s_head].cycle_stamp = cycle_stamp+s_epoch;
    s_ring[s_head].port        = port;
    s_ring[s_head].value       = value;
    s_head = next_head;
}

int audio_event_pop(AudioEvent *out)
{
    if(!audio_event_queue_count())return 0;
    *out=pending_event(0);
    if(s_deferred_count){
        ++s_deferred_first;--s_deferred_count;
        if(s_deferred_count)s_deferred_sealed=0;
        else {s_deferred_first=0;s_deferred_sealed=1;}
    }else s_tail=(s_tail+1u)%QUEUE_CAP;
    return 1;
}

void audio_event_queue_reset(void)
{
    pending_reset();
}

size_t audio_event_take_ordered(AudioOrderedEvent *out,size_t capacity)
{
    if(!out || !capacity)return 0;
    size_t count=audio_event_queue_count();if(count>capacity)count=capacity;
    for(size_t i=0;i<count;i++){out[i].e=pending_event(i);out[i].idx=(uint32_t)i;}
    size_t old=count<s_deferred_count?count:s_deferred_count;
    s_deferred_first+=old;s_deferred_count-=old;
    if(old && s_deferred_count)s_deferred_sealed=0;
    if(!s_deferred_count){s_deferred_first=0;s_deferred_sealed=1;}
    s_tail=(s_tail+count-old)%QUEUE_CAP;return count;
}

static int deferred_sealed(void)
{
    unsigned address=0;uint32_t stamp=0;int have=0;
    for(size_t i=0;i<s_deferred_count;i++){
        AudioEvent e=s_deferred[s_deferred_first+i];
        if(e.port==AUDIO_PORT_PSG)continue;
        if(!have){if(e.port!=0u && e.port!=2u)return 0;address=e.port;stamp=e.cycle_stamp;have=1;}
        else {if(e.port!=address+1u || e.cycle_stamp!=stamp)return 0;have=0;}
    }
    return !have;
}
int audio_event_interval_begin(AudioDeferredSpan *span,AudioOrderedEvent *fresh,
    size_t capacity,size_t *count)
{
    size_t n=incoming_count();
    if(!span || !count || !s_deferred_sealed || n>capacity || (n && !fresh))return 0;
    span->events=s_deferred+s_deferred_first;span->count=s_deferred_count;span->epoch=s_epoch;
    for(size_t i=0;i<n;i++){
        fresh[i].e=s_ring[(s_tail+i)%QUEUE_CAP];fresh[i].e.cycle_stamp-=s_epoch;
        fresh[i].idx=(uint32_t)(s_deferred_count+i);
    }
    s_tail=s_head;*count=n;return 1;
}
void audio_event_interval_commit(size_t consumed,const AudioOrderedEvent *fresh,
    size_t n,uint32_t frame_span)
{
    assert(consumed<=s_deferred_count && !incoming_count());
    s_deferred_first+=consumed;s_deferred_count-=consumed;
    assert(s_deferred_count+n<QUEUE_CAP);
    uint32_t old_epoch=s_epoch;s_epoch+=frame_span;
    if(!n){if(!s_deferred_count)s_deferred_first=0;return;}
    size_t old=0,add=0,out=0;
    while(old<s_deferred_count || add<n){
        if(old<s_deferred_count && (add==n ||
           s_deferred[s_deferred_first+old].cycle_stamp-old_epoch<=fresh[add].e.cycle_stamp))
            s_ring[out++]=s_deferred[s_deferred_first+old++];
        else {
            AudioEvent e=fresh[add++].e;
            e.cycle_stamp=(e.cycle_stamp>frame_span?e.cycle_stamp:frame_span)+old_epoch;
            s_ring[out++]=e;
        }
    }
    AudioEvent *previous=s_deferred;s_deferred=s_ring;s_ring=previous;
    s_head=s_tail=s_deferred_first=0;s_deferred_count=out;
    s_deferred_sealed=deferred_sealed();
}

/* Re-queue events the mixer deferred to a later wall frame (stamps beyond
 * the frame just drained, already rebased by the caller). Single-threaded:
 * called right after a drain that emptied the queue, so capacity is free. */
void audio_event_requeue(const AudioEvent *evs, size_t n)
{
    size_t space=QUEUE_CAP-1u-audio_event_queue_count(),take=n<space?n:space;
    size_t first=QUEUE_CAP-s_head;if(first>take)first=take;
    if(first)memcpy(s_ring+s_head,evs,first*sizeof *evs);
    if(take>first)memcpy(s_ring,evs+first,(take-first)*sizeof *evs);
    if(s_epoch)for(size_t i=0;i<take;i++)s_ring[(s_head+i)%QUEUE_CAP].cycle_stamp+=s_epoch;
    s_head=(s_head+take)%QUEUE_CAP;
    if(take<n)++s_overflow_count;
}

size_t audio_event_queue_count(void)
{
    return s_deferred_count+incoming_count();
}

#include "../cosim.h"
/* Hash the PENDING (undrained) cycle-stamped writes, in FIFO order, plus the
 * queue depth. A phase error in the 68K-side stamp path shows up here as an
 * event carrying a different cycle_stamp between the two backends. */
uint64_t audio_event_cosim_hash(void)
{
    uint64_t h = cosim_fnv_init();
    size_t n = audio_event_queue_count();
    h = cosim_fnv_u64(h, (uint64_t)n);
    for (size_t i = 0; i < n; i++) {
        AudioEvent e=pending_event(i);
        h = cosim_fnv_u32(h, e.cycle_stamp);
        h = cosim_fnv_u8(h, e.port);
        h = cosim_fnv_u8(h, e.value);
    }
    return h;
}
