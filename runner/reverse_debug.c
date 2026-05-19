/*
 * reverse_debug.c — Tier-1 reverse-debugger implementation.
 *
 * Only compiled when SONIC_REVERSE_DEBUG=1. The generator emits a
 * `g_rdb_current_func = 0xXXXXXXu;` assignment at the entry of every
 * recompiled function body; this file installs itself as the shared
 * bus-write callback (g_mem_write_trace_fn, defined in
 * clownmdemu-core/bus-main-m68k.c) and records every bus write with
 * (frame, adr, val, width, func, caller) attribution.
 *
 * Native + oracle share the same bus-callback path, so Tier 1 observes
 * both. Range filter is applied before recording so 1M-entry ring never
 * drowns on unfiltered traffic.
 */

#if SONIC_REVERSE_DEBUG

#include "reverse_debug.h"
#include "cmd_server.h"
#include "glue.h"
#include "clownmdemu.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern ClownMDEmu g_clownmdemu;

/* Read $FFFExxxx as a longword from clownmdemu's 68K work-RAM mirror.
 * Used to capture Vint_runcount (the cross-binary state-sync key) in
 * every Tier-1 store ring entry. */
static inline uint32_t rdb_read_wram32(uint16_t off)
{
    uint16_t hi = g_clownmdemu.state.m68k.ram[off / 2];
    uint16_t lo = g_clownmdemu.state.m68k.ram[(off + 2) / 2];
    return ((uint32_t)hi << 16) | (uint32_t)lo;
}

/* Shared hook slot in clownmdemu-core/source/bus-main-m68k.c. It is
 * defined unconditionally there; null when nobody has installed a tap. */
extern void (*g_mem_write_trace_fn)(uint32_t byte_address, uint8_t value, uint32_t target_cycle);

/* ---- Ring + ranges ---- */

#define RDB_LOG_SIZE     (1u << 20)   /* 1,048,576 entries */
#define RDB_MAX_RANGES   8

uint32_t g_rdb_current_func = 0;

typedef struct {
    uint32_t frame;
    uint32_t vint;    /* Vint_runcount at $FFFFFE0C — state-sync key
                       * across native vs oracle. */
    uint32_t adr;
    uint32_t val;     /* byte writes zero-extend into low 8 */
    uint32_t func;
    uint32_t caller;
    uint8_t  width;   /* bus tap fires per byte → width is always 1 today */
} RdbEntry;

static struct {
    int      active;
    int      nranges;
    struct { uint32_t lo, hi; } ranges[RDB_MAX_RANGES];
    uint32_t write_idx;
    uint32_t count;
    uint32_t snap_start;
    uint32_t snap_count;
    int      snap_active;
    int      tap_installed;
    /* Previous callback, so --mem-write-log and rdb can coexist. */
    void (*prev_trace_fn)(uint32_t, uint8_t, uint32_t);
    RdbEntry log[RDB_LOG_SIZE];
} s_rdb = {0};

/* ---- Hot path ---- */

static int rdb_range_hit(uint32_t adr)
{
    if (s_rdb.nranges == 0) return 1;
    for (int i = 0; i < s_rdb.nranges; i++) {
        if (adr >= s_rdb.ranges[i].lo && adr <= s_rdb.ranges[i].hi) return 1;
    }
    return 0;
}

static void rdb_bus_tap(uint32_t byte_address, uint8_t value, uint32_t target_cycle)
{
    /* Forward to previously-installed tracer (e.g. --mem-write-log) first
     * so we don't starve other instrumentation while Tier 1 is armed. */
    if (s_rdb.prev_trace_fn) s_rdb.prev_trace_fn(byte_address, value, target_cycle);

    if (!s_rdb.active) return;
    uint32_t adr = byte_address & 0xFFFFFFu;
    if (!rdb_range_hit(adr)) return;

    uint32_t idx = s_rdb.write_idx % RDB_LOG_SIZE;
    uint32_t a7  = cmd_server_current_a7();

    s_rdb.log[idx].frame  = cmd_server_current_frame();
    s_rdb.log[idx].vint   = rdb_read_wram32(0xFE0C);
    s_rdb.log[idx].adr    = adr;
    s_rdb.log[idx].val    = value;
    s_rdb.log[idx].width  = 1;
    s_rdb.log[idx].func   = g_rdb_current_func;
    s_rdb.log[idx].caller = cmd_server_stack_read32(a7);

    s_rdb.write_idx++;
    if (s_rdb.count < RDB_LOG_SIZE) s_rdb.count++;
}

/* ---- Bus-tap (de)install ---- */

void rdb_install_bus_tap(void)
{
    if (s_rdb.tap_installed) return;
    s_rdb.prev_trace_fn = g_mem_write_trace_fn;
    g_mem_write_trace_fn = rdb_bus_tap;
    s_rdb.tap_installed = 1;
}

void rdb_uninstall_bus_tap(void)
{
    if (!s_rdb.tap_installed) return;
    /* Only restore if we're still the current callback; if someone else
     * installed on top of us, leave the chain alone. */
    if (g_mem_write_trace_fn == rdb_bus_tap)
        g_mem_write_trace_fn = s_rdb.prev_trace_fn;
    s_rdb.prev_trace_fn = NULL;
    s_rdb.tap_installed = 0;
}

/* ---- Control API ---- */

int rdb_add_range(uint32_t lo, uint32_t hi)
{
    if (s_rdb.nranges >= RDB_MAX_RANGES) return 0;
    if (hi < lo) { uint32_t t = lo; lo = hi; hi = t; }
    s_rdb.ranges[s_rdb.nranges].lo = lo & 0xFFFFFFu;
    s_rdb.ranges[s_rdb.nranges].hi = hi & 0xFFFFFFu;
    s_rdb.nranges++;
    s_rdb.active = 1;
    rdb_install_bus_tap();
    return 1;
}

void rdb_reset(void)
{
    s_rdb.active = 0;
    s_rdb.nranges = 0;
    s_rdb.write_idx = 0;
    s_rdb.count = 0;
    s_rdb.snap_start = 0;
    s_rdb.snap_count = 0;
    s_rdb.snap_active = 0;
    rdb_uninstall_bus_tap();
}

void rdb_autostart(void)
{
    /* Default-on with a single WRAM-wide range ($FF0000-$FFFFFF). The
     * 1M-entry ring is enough for ~5 minutes of typical SMPS+gameplay
     * write traffic before eviction; anything older was already
     * uninteresting by the time a probe runs. Probes that want a
     * narrower view call rdb_reset+rdb_range as before — but the
     * default capture means probes work even on first connect with
     * no prior arming. */
    if (s_rdb.tap_installed) return;
    rdb_add_range(0xFF0000u, 0xFFFFFFu);
}

int rdb_range_count(void) { return s_rdb.nranges; }

int rdb_range_get(int i, uint32_t *lo_out, uint32_t *hi_out)
{
    if (i < 0 || i >= s_rdb.nranges) return 0;
    if (lo_out) *lo_out = s_rdb.ranges[i].lo;
    if (hi_out) *hi_out = s_rdb.ranges[i].hi;
    return 1;
}

void rdb_snapshot_begin(void)
{
    if (s_rdb.count < RDB_LOG_SIZE)
        s_rdb.snap_start = 0;
    else
        s_rdb.snap_start = s_rdb.write_idx % RDB_LOG_SIZE;
    s_rdb.snap_count  = s_rdb.count;
    s_rdb.snap_active = 1;
}

void rdb_snapshot_end(void)  { s_rdb.snap_active = 0; }

uint32_t rdb_snapshot_count(void)
{
    return s_rdb.snap_active ? s_rdb.snap_count : 0;
}

int rdb_format_entry(uint32_t i, char *buf, size_t buflen)
{
    if (!s_rdb.snap_active || i >= s_rdb.snap_count) return -1;
    uint32_t idx = (s_rdb.snap_start + i) % RDB_LOG_SIZE;
    const RdbEntry *e = &s_rdb.log[idx];
    int n = snprintf(buf, buflen,
        "{\"f\":%u,\"vint\":%u,\"adr\":\"0x%06X\",\"val\":\"0x%02X\",\"w\":%u,"
         "\"func\":\"0x%06X\",\"caller\":\"0x%06X\"}",
        (unsigned)e->frame,
        (unsigned)e->vint,
        (unsigned)e->adr,
        (unsigned)(e->val & 0xFFu),
        (unsigned)e->width,
        (unsigned)(e->func   & 0xFFFFFFu),
        (unsigned)(e->caller & 0xFFFFFFu));
    return (n < 0 || (size_t)n >= buflen) ? -1 : n;
}

/* =========================================================================
 * Tier 2: breakpoints + stepping
 *
 * Execution model: the game fiber runs on the same OS thread as the
 * main loop (Windows Fibers, cooperative). rdb_on_block_slow yields
 * to the main fiber via glue_yield_for_break(); the main loop drains
 * cmd_server until a TCP resume command sets g_rdb_resume_now and
 * SwitchToFiber(s_game_fiber)s back. We return from the yield on the
 * exact same C expression in the exact same function — block-level
 * resume with no state reconstruction needed.
 * ========================================================================= */

#define RDB_MAX_BREAKS 64

enum { STEP_NONE = 0, STEP_ONE_BLOCK, STEP_OVER };

int g_rdb_break_pending = 0;
int g_rdb_resume_now    = 0;

/* Diagnostic counters for smoke-testing the hook path. Visible via
 * rdb_break_list. If slow_count stays 0 while a break is armed, the
 * inline is either not inlined or the compiler elided it. */
uint64_t g_rdb_slow_count = 0;
uint64_t g_rdb_park_count = 0;

static struct {
    int      step_mode;
    /* For STEP_OVER: snapshot of the 68K call-stack depth at the moment
     * the user issued the command. We park again when depth <= snapshot
     * (caller re-entered). Depth is approximated as (initial_ssp - A7)/4
     * — works because 68K code stores 4-byte return addresses. */
    int32_t  step_over_depth;

    uint32_t breaks[RDB_MAX_BREAKS];
    int      break_count;

    int      parked;
    uint32_t parked_block;
    uint32_t last_block;
} s_tier2 = {0};

/* Approximate 68K call-stack depth for step-over. Sonic 1's initial SSP
 * is $FFFE00 (from ROM vectors); depth = (initial_ssp - A7) / 4, where
 * 4 is the size of each pushed return address. An off-by-one level in
 * interrupt context is acceptable — step-over's job is just to run
 * until we return to the calling frame, and spurious early-unpark is
 * user-recoverable via another step-over. */
static int32_t rdb_call_depth(void)
{
    const int32_t initial_ssp = 0xFFFE00;
    int32_t a7 = (int32_t)cmd_server_current_a7();
    return (initial_ssp - a7) / 4;
}

static void rdb_refresh_pending(void)
{
    g_rdb_break_pending = (s_tier2.break_count > 0
                        || s_tier2.step_mode != STEP_NONE);
}

void rdb_on_block_slow(uint32_t block_id)
{
    g_rdb_slow_count++;
    s_tier2.last_block = block_id;

    int park = 0;
    switch (s_tier2.step_mode) {
    case STEP_NONE:
        for (int i = 0; i < s_tier2.break_count; i++) {
            if (s_tier2.breaks[i] == block_id) { park = 1; break; }
        }
        break;
    case STEP_ONE_BLOCK:
        park = 1;
        s_tier2.step_mode = STEP_NONE;
        break;
    case STEP_OVER: {
        int32_t d = rdb_call_depth();
        if (d <= s_tier2.step_over_depth) {
            park = 1;
            s_tier2.step_mode = STEP_NONE;
        }
        break;
    }
    }
    rdb_refresh_pending();
    if (!park) return;

    g_rdb_park_count++;
    s_tier2.parked       = 1;
    s_tier2.parked_block = block_id;

    /* Yield to main fiber. Returns when main loop SwitchToFiber's back
     * — meaning a resume command came in. */
    glue_yield_for_break();

    s_tier2.parked = 0;
}

int  rdb_break_add(uint32_t block_id)
{
    if (s_tier2.break_count >= RDB_MAX_BREAKS) return 0;
    /* Dedupe — adding the same block twice is idempotent. */
    for (int i = 0; i < s_tier2.break_count; i++)
        if (s_tier2.breaks[i] == block_id) return 1;
    s_tier2.breaks[s_tier2.break_count++] = block_id & 0xFFFFFFu;
    rdb_refresh_pending();
    return 1;
}

void rdb_break_clear_all(void)
{
    s_tier2.break_count = 0;
    rdb_refresh_pending();
}

int rdb_break_count(void)            { return s_tier2.break_count; }
uint32_t rdb_break_get(int i)
{
    if (i < 0 || i >= s_tier2.break_count) return 0;
    return s_tier2.breaks[i];
}

void rdb_cmd_step_one(void)
{
    s_tier2.step_mode = STEP_ONE_BLOCK;
    rdb_refresh_pending();
    g_rdb_resume_now = 1;
}

void rdb_cmd_step_over(void)
{
    s_tier2.step_mode = STEP_OVER;
    /* Park when we return to a shallower (or equal) depth than *now*. */
    s_tier2.step_over_depth = rdb_call_depth();
    rdb_refresh_pending();
    g_rdb_resume_now = 1;
}

void rdb_cmd_continue(void)
{
    s_tier2.step_mode = STEP_NONE;
    rdb_refresh_pending();
    /* Tier-4 step_insn is one-shot, cleared in rdb_on_insn_slow when a
     * park fires. By the time continue is legal (we must be parked),
     * step_insn is already 0, so no extra clear needed here. */
    g_rdb_resume_now = 1;
}

int      rdb_is_parked   (void) { return s_tier2.parked; }
uint32_t rdb_parked_block(void) { return s_tier2.parked_block; }

/* =========================================================================
 * Tier 4: per-instruction step / break.
 * Reuses Tier 2's park (glue_yield_for_break + main-loop drain). The
 * only new mechanisms are (1) an insn-level break list keyed by PC,
 * (2) a step-one-insn flag, and (3) a fast-path gate g_rdb_insn_pending.
 * ========================================================================= */

#define RDB_INSN_MAX_BREAKS 128

int g_rdb_insn_pending = 0;

static struct {
    int      step_insn;      /* one-shot: next rdb_on_insn parks */
    uint32_t breaks[RDB_INSN_MAX_BREAKS];
    int      break_count;
    uint32_t parked_pc;
} s_tier4 = {0};

static void rdb_insn_refresh_pending(void)
{
    g_rdb_insn_pending = s_tier4.step_insn || s_tier4.break_count > 0;
}

void rdb_on_insn_slow(uint32_t pc)
{
    int park = 0;
    if (s_tier4.step_insn) {
        park = 1;
        s_tier4.step_insn = 0;
    } else {
        for (int i = 0; i < s_tier4.break_count; i++) {
            if (s_tier4.breaks[i] == pc) { park = 1; break; }
        }
    }
    rdb_insn_refresh_pending();
    if (!park) return;

    s_tier4.parked_pc = pc;
    s_tier2.parked       = 1;   /* reuse Tier 2's parked flag for main.c */
    s_tier2.parked_block = pc;  /* for rdb_get_state */
    glue_yield_for_break();
    s_tier2.parked       = 0;
}

int rdb_insn_break_add(uint32_t pc)
{
    if (s_tier4.break_count >= RDB_INSN_MAX_BREAKS) return 0;
    for (int i = 0; i < s_tier4.break_count; i++)
        if (s_tier4.breaks[i] == pc) return 1;  /* dedupe */
    s_tier4.breaks[s_tier4.break_count++] = pc & 0xFFFFFFu;
    rdb_insn_refresh_pending();
    return 1;
}

void rdb_insn_break_clear_all(void)
{
    s_tier4.break_count = 0;
    rdb_insn_refresh_pending();
}

int rdb_insn_break_count(void) { return s_tier4.break_count; }
uint32_t rdb_insn_break_get(int i)
{
    if (i < 0 || i >= s_tier4.break_count) return 0;
    return s_tier4.breaks[i];
}

void rdb_cmd_step_insn(void)
{
    s_tier4.step_insn = 1;
    rdb_insn_refresh_pending();
    g_rdb_resume_now = 1;
}

uint32_t rdb_parked_pc(void) { return s_tier4.parked_pc; }

/* =========================================================================
 * Stage-A: VBla-fire event ring + Iterate counter.
 *
 * Records each native-side VBla handler fire with the cycle accumulator
 * value at fire time, the native game-frame counter, and the wall-frame
 * counter from cmd_server. Distribution tells us whether native multi-
 * fires per wall frame.
 *
 * Oracle is 1:1 by architecture but we also expose an Iterate counter for
 * sanity. On oracle the fire-ring stays empty (glue.c's native-only code
 * path doesn't execute).
 * ========================================================================= */

#define RDB_FIRE_RING_SIZE  (1u << 16)   /* 65536 entries, ~1MB */

typedef struct {
    uint32_t wall;
    uint32_t acc;
    uint64_t game;
    uint8_t  reason;
} FireEntry;

static struct {
    uint32_t   write_idx;
    uint32_t   count;
    uint32_t   snap_start;
    uint32_t   snap_count;
    int        snap_active;
    FireEntry  log[RDB_FIRE_RING_SIZE];
} s_fires = {0};

static uint64_t s_iterate_count = 0;

void rdb_record_vbla_fire(uint32_t cycle_acc, uint64_t game_frame, int reason)
{
    uint32_t idx = s_fires.write_idx % RDB_FIRE_RING_SIZE;
    FireEntry *e = &s_fires.log[idx];
    e->wall   = cmd_server_current_frame();
    e->acc    = cycle_acc;
    e->game   = game_frame;
    e->reason = (uint8_t)reason;
    s_fires.write_idx++;
    if (s_fires.count < RDB_FIRE_RING_SIZE) s_fires.count++;
}

void rdb_record_iterate(void) { s_iterate_count++; }

void rdb_vbla_snapshot_begin(void)
{
    if (s_fires.count < RDB_FIRE_RING_SIZE)
        s_fires.snap_start = 0;
    else
        s_fires.snap_start = s_fires.write_idx % RDB_FIRE_RING_SIZE;
    s_fires.snap_count  = s_fires.count;
    s_fires.snap_active = 1;
}

void rdb_vbla_snapshot_end(void) { s_fires.snap_active = 0; }

uint32_t rdb_vbla_snapshot_count(void)
{
    return s_fires.snap_active ? s_fires.snap_count : 0;
}

uint64_t rdb_iterate_count(void) { return s_iterate_count; }

/* Stage C accessors. g_native_insn_count lives in runner/glue.c (always
 * defined — incremented by generated C). g_oracle_insn_count lives in
 * runner/oracle_trace.c (oracle-only build). On native, oracle_trace.c
 * is not linked; provide a weak fallback so the accessor still returns
 * 0 there. */
extern uint64_t g_native_insn_count;
#if defined(SONIC_ORACLE_BUILD)
extern uint64_t g_oracle_insn_count;
#else
static uint64_t g_oracle_insn_count = 0;
#endif

uint64_t rdb_native_insn_count(void) { return g_native_insn_count; }
uint64_t rdb_oracle_insn_count(void) { return g_oracle_insn_count; }

int rdb_vbla_format_entry(uint32_t i, char *buf, size_t buflen)
{
    if (!s_fires.snap_active || i >= s_fires.snap_count) return -1;
    uint32_t idx = (s_fires.snap_start + i) % RDB_FIRE_RING_SIZE;
    const FireEntry *e = &s_fires.log[idx];
    int n = snprintf(buf, buflen,
        "{\"wall\":%u,\"acc\":%u,\"game\":%llu,\"reason\":%u}",
        (unsigned)e->wall, (unsigned)e->acc,
        (unsigned long long)e->game, (unsigned)e->reason);
    return (n < 0 || (size_t)n >= buflen) ? -1 : n;
}

#endif /* SONIC_REVERSE_DEBUG */
