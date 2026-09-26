/*
 * rb_probe.c -- single-process rollback determinism probe (GENESIS_RB_PROBE).
 *
 * Rollback rests on one property: save a snapshot, run N ticks, restore it,
 * run the same N ticks with the same sealed inputs, and every tick lands on
 * the same state. When it is false the symptom otherwise appears only in a
 * two-peer session, as a fork, which is the most expensive place to find it.
 *
 *   GENESIS_RB_PROBE=<period>[:<depth>]   every <period> ticks: save S0, let
 *        the LIVE run proceed <depth> ticks (default 8) recording each tick's
 *        sealed input and partitioned digest, then restore S0 and replay the
 *        same inputs with presentation suppressed (no render, scratch audio),
 *        comparing every tick. The live timeline is the one netplay would
 *        have predicted; the replay is the one rollback would produce. After
 *        a clean pass the machine is where the live run left it (bit-for-bit,
 *        which is the claim being tested); after a divergence it carries the
 *        replay's state and the run is no longer comparable -- the probe is a
 *        diagnostic, not a player-facing mode, and never arms online.
 *   Checks per pass: symmetry (the digest right after the restore equals the
 *        digest S0 was saved at), then per tick the master digest; the first
 *        differing tick names every differing partition (NETPLAY.md 5: the
 *        first divergence is the only one with a root cause).
 *   GENESIS_RB_PROBE_STATICS=1  carrier hunt: image the executable's writable
 *        data (.data+.bss) at S0 and again right after the restore, and list
 *        what the restore did NOT put back. Resolve addresses with nm; the
 *        known-diagnostic ranges are expected to differ.
 *   GENESIS_RB_PROBE_STACKSCAN=1  after the restore, scan the game fiber's
 *        live stack for words that point into the heap (malloc'd memory a
 *        snapshot does not own): such a reference outlives a free/realloc
 *        between save and load and makes the restored stack stale.
 *   GENESIS_RB_SELFTEST=<tick>  run the rb_state mutation self-test once at
 *        that tick (exit status unaffected; lines are "[rb_selftest] ...").
 *
 * Lines are stable ("rb_probe: pass ... OK" / "rb_probe: DIVERGE ...") and
 * read by tools/rb_probe.sh. A final "rb_probe: summary" line is printed at
 * exit.
 */
#include "rb_probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rb_state.h"
#include "sim_step.h"

#define RB_PROBE_MAX_DEPTH 64

/* Cost of the rollback primitives, measured on every probe pass (the
 * capability matrix's "snapshot fast path" row). */
#include <SDL2/SDL.h>
static double now_ms(void)
{
    return (double)SDL_GetPerformanceCounter() * 1000.0 /
           (double)SDL_GetPerformanceFrequency();
}
#define RB_PROBE_TIMES 8192
typedef struct { double v[RB_PROBE_TIMES]; unsigned n; } TimeSet;
static TimeSet s_t_save, s_t_load, s_t_digest, s_t_tick;
static size_t s_bytes_min = (size_t)-1, s_bytes_max;
static void tset_add(TimeSet *t, double ms) { if (t->n < RB_PROBE_TIMES) t->v[t->n++] = ms; }
static int dcmp(const void *a, const void *b)
{ double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static void tset_print(const char *name, TimeSet *t)
{
    if (!t->n) return;
    qsort(t->v, t->n, sizeof t->v[0], dcmp);
    fprintf(stderr, "rb_probe: cost %-6s n=%u p50=%.3fms p99=%.3fms max=%.3fms\n", name, t->n,
            t->v[t->n / 2], t->v[(t->n * 99) / 100], t->v[t->n - 1]);
}

static int s_period = -1;          /* -1 unread, 0 disarmed */
static int s_depth = 8;
static int s_statics, s_stackscan;
static long s_selftest_tick = -1;
static const GenesisSimHooks *s_resim_hooks;

static uint8_t *s_s0;
static size_t   s_s0_len, s_s0_cap;
static GenesisRbDigest s_s0_digest;
static int      s_recording;        /* ticks recorded in the current pass */
static GenesisSimInput s_rec_in[RB_PROBE_MAX_DEPTH];
static GenesisRbDigest s_rec_dig[RB_PROBE_MAX_DEPTH];
static unsigned s_passes, s_fails, s_sym_fails;
static uint32_t s_ticks_seen;

void rb_probe_set_resim_hooks(const GenesisSimHooks *hooks) { s_resim_hooks = hooks; }

static void probe_read_env(void)
{
    const char *v = getenv("GENESIS_RB_PROBE");
    s_period = 0;
    s_statics = getenv("GENESIS_RB_PROBE_STATICS") && atoi(getenv("GENESIS_RB_PROBE_STATICS"));
    s_stackscan = getenv("GENESIS_RB_PROBE_STACKSCAN") && atoi(getenv("GENESIS_RB_PROBE_STACKSCAN"));
    { const char *t = getenv("GENESIS_RB_SELFTEST"); if (t && *t) s_selftest_tick = atol(t); }
    if (!v || !v[0]) return;
    s_period = atoi(v);
    if (s_period < 1) { s_period = 0; return; }
    const char *colon = strchr(v, ':');
    if (colon && colon[1]) {
        s_depth = atoi(colon + 1);
        if (s_depth < 1) s_depth = 1;
        if (s_depth > RB_PROBE_MAX_DEPTH) s_depth = RB_PROBE_MAX_DEPTH;
    }
    if (s_depth >= s_period) s_depth = s_period - 1 > 0 ? s_period - 1 : 1;
    fprintf(stderr, "rb_probe: armed period=%d depth=%d statics=%d stackscan=%d\n",
            s_period, s_depth, s_statics, s_stackscan);
}

int rb_probe_armed(void)
{
    if (s_period < 0) probe_read_env();
    return s_period > 0 || s_selftest_tick >= 0;
}

/* ---- carrier finders ------------------------------------------------------- */

#if defined(__linux__) && defined(__GNUC__)
#define RB_PROBE_HAVE_IMAGE 1
extern char __data_start[], _edata[], __bss_start[], _end[];
static uint8_t *s_img;
static size_t   s_img_len;
static void statics_grab(void)
{
    size_t dn = (size_t)(_edata - __data_start), bn = (size_t)(_end - __bss_start);
    if (s_img_len != dn + bn) { free(s_img); s_img = (uint8_t *)malloc(dn + bn); s_img_len = s_img ? dn + bn : 0; }
    if (!s_img) return;
    memcpy(s_img, __data_start, dn);
    memcpy(s_img + dn, __bss_start, bn);
}
/* Report differing runs as [start,end) ranges, which nm resolves to symbols. */
static void statics_diff(unsigned pass)
{
    size_t dn = (size_t)(_edata - __data_start), bn = (size_t)(_end - __bss_start);
    size_t runs = 0, bytes = 0;
    if (!s_img || s_img_len != dn + bn) return;
    fprintf(stderr, "rb_probe: statics pass=%u data=%p bss=%p (addresses for nm)\n",
            pass, (void *)__data_start, (void *)__bss_start);
    for (int seg = 0; seg < 2; seg++) {
        const uint8_t *live = seg ? (const uint8_t *)__bss_start : (const uint8_t *)__data_start;
        const uint8_t *was = seg ? s_img + dn : s_img;
        size_t n = seg ? bn : dn;
        for (size_t k = 0; k < n; ) {
            if (live[k] == was[k]) { k++; continue; }
            size_t a = k;
            /* Coalesce differences closer than 8 bytes into one run. */
            size_t last = k;
            while (k < n && k - last < 8) { if (live[k] != was[k]) last = k; k++; }
            k = last + 1;
            bytes += k - a;
            if (runs++ < 200000)
                fprintf(stderr, "rb_probe:   %s %p +%zu\n", seg ? ".bss " : ".data",
                        (void *)(live + a), k - a);
        }
    }
    fprintf(stderr, "rb_probe: statics pass=%u not-restored runs=%zu bytes=%zu\n", pass, runs, bytes);
}
#else
#define RB_PROBE_HAVE_IMAGE 0
static void statics_grab(void) {}
static void statics_diff(unsigned pass) { (void)pass; }
#endif

#include <stdint.h>
static void stack_scan(unsigned pass)
{
    uintptr_t lo = 0, top = 0;
    if (glue_rb_fiber_stack_range(&lo, &top) != 0) return;
    /* The saved span starts at the suspended SP; find it from the exec blob's
     * size is not exposed, so scan the whole usable range above the lowest
     * non-zero word (untouched stack is zero-filled). */
    const uintptr_t *w = (const uintptr_t *)lo, *e = (const uintptr_t *)top;
    while (w < e && *w == 0) w++;
    void *probe = malloc(64);
    uintptr_t heap_hint = (uintptr_t)probe;
    free(probe);
#if RB_PROBE_HAVE_IMAGE
    uintptr_t img_lo = (uintptr_t)__data_start, img_hi = (uintptr_t)_end;
#else
    uintptr_t img_lo = 0, img_hi = 0;
#endif
    size_t words = 0, heapish = 0, shown = 0;
    for (; w < e; w++, words++) {
        uintptr_t v = *w;
        if (v >= lo && v < top) continue;                 /* into the fiber stack itself */
        if (v >= img_lo && v < img_hi) continue;          /* into the image's statics */
        /* Within 1 GiB of a fresh heap block: treat as a heap reference. */
        uintptr_t d = v > heap_hint ? v - heap_hint : heap_hint - v;
        if (v > 0x10000 && d < ((uintptr_t)1 << 30)) {
            heapish++;
            if (shown++ < 16)
                fprintf(stderr, "rb_probe:   stack+%#zx -> %#zx (heap?)\n",
                        (size_t)((uintptr_t)w - lo), (size_t)v);
        }
    }
    fprintf(stderr, "rb_probe: stackscan pass=%u words=%zu heap_refs=%zu\n", pass, words, heapish);
}

/* ---- the pass --------------------------------------------------------------- */

static int first_diff_parts(const GenesisRbDigest *a, const GenesisRbDigest *b, char *out, size_t cap)
{
    int n = 0;
    out[0] = 0;
    for (int p = 0; p < GENESIS_RB_PART_COUNT; p++) {
        if (a->part[p] == b->part[p]) continue;
        size_t l = strlen(out);
        snprintf(out + l, cap - l, "%s%s", n ? "," : "", genesis_rb_part_name(p));
        n++;
    }
    return n;
}

void rb_probe_pre_tick(void)
{
    if (!rb_probe_armed()) return;
    if (s_selftest_tick >= 0 && (long)s_ticks_seen == s_selftest_tick) {
        fprintf(stderr, "rb_probe: selftest at tick %u\n", s_ticks_seen);
        (void)genesis_rb_selftest();
    }
    if (s_period <= 0 || s_recording) return;
    if (s_ticks_seen == 0 || (s_ticks_seen % (uint32_t)s_period) != 0) return;
    size_t need = genesis_rb_bound();
    if (s_s0_cap < need) {
        uint8_t *nb = (uint8_t *)realloc(s_s0, need);
        if (!nb) return;
        s_s0 = nb; s_s0_cap = need;
    }
    double t0 = now_ms();
    s_s0_len = genesis_rb_save(s_s0, s_s0_cap);
    tset_add(&s_t_save, now_ms() - t0);
    if (s_s0_len < s_bytes_min) s_bytes_min = s_s0_len;
    if (s_s0_len > s_bytes_max) s_bytes_max = s_s0_len;
    if (!s_s0_len) { fprintf(stderr, "rb_probe: save failed at tick %u\n", s_ticks_seen); return; }
    t0 = now_ms();
    genesis_rb_digest(&s_s0_digest);
    tset_add(&s_t_digest, now_ms() - t0);
    if (s_statics) statics_grab();
    s_recording = 1;   /* armed: the next live tick is recorded as step 0 */
}

void rb_probe_post_tick(void)
{
    s_ticks_seen++;
    if (s_period <= 0 || !s_recording) return;
    int k = s_recording - 1;
    s_rec_in[k] = *genesis_sim_input();
    genesis_rb_digest(&s_rec_dig[k]);
    if (++s_recording <= s_depth) return;

    /* Replay. */
    unsigned pass = ++s_passes;
    uint32_t at = s_ticks_seen - (uint32_t)s_depth;
    s_recording = 0;
    double t0 = now_ms();
    int loaded = genesis_rb_load(s_s0, s_s0_len);
    tset_add(&s_t_load, now_ms() - t0);
    if (!loaded) {
        fprintf(stderr, "rb_probe: DIVERGE pass=%u tick=%u restore failed\n", pass, at);
        s_fails++;
        return;
    }
    if (s_statics) statics_diff(pass);
    if (s_stackscan) stack_scan(pass);
    GenesisRbDigest d;
    char parts[256];
    genesis_rb_digest(&d);
    if (d.master != s_s0_digest.master) {
        first_diff_parts(&s_s0_digest, &d, parts, sizeof parts);
        fprintf(stderr, "rb_probe: ASYMMETRIC pass=%u tick=%u restore digest differs in %s\n",
                pass, at, parts);
        s_sym_fails++;
    }
    int diverged = -1;
    for (int i = 0; i < s_depth; i++) {
        double ts = now_ms();
        genesis_sim_step(&s_rec_in[i], NULL, s_resim_hooks);
        tset_add(&s_t_tick, now_ms() - ts);
        genesis_rb_digest(&d);
        if (d.master != s_rec_dig[i].master && diverged < 0) {
            diverged = i;
            first_diff_parts(&s_rec_dig[i], &d, parts, sizeof parts);
            fprintf(stderr, "rb_probe: DIVERGE pass=%u tick=%u step=%d/%d parts=%s "
                    "live=%016llx replay=%016llx\n", pass, at + (uint32_t)i, i + 1, s_depth,
                    parts, (unsigned long long)s_rec_dig[i].master, (unsigned long long)d.master);
        }
    }
    if (diverged >= 0) s_fails++;
    else fprintf(stderr, "rb_probe: pass=%u tick=%u depth=%d OK\n", pass, at, s_depth);
}

void rb_probe_summary(void)
{
    if (s_period <= 0) return;
    tset_print("save", &s_t_save);
    tset_print("load", &s_t_load);
    tset_print("digest", &s_t_digest);
    tset_print("resim", &s_t_tick);
    if (s_bytes_max) fprintf(stderr, "rb_probe: snapshot bytes min=%zu max=%zu\n", s_bytes_min, s_bytes_max);
    fprintf(stderr, "rb_probe: summary passes=%u divergences=%u asymmetric=%u period=%d depth=%d\n",
            s_passes, s_fails, s_sym_fails, s_period, s_depth);
}
