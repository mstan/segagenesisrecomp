/*
 * oracle_trace.c — Tier-3 pre-instruction capture ring.
 *
 * Hot path: t3_pre_insn(pc) is called by clown68000 before every
 * instruction. It snapshots (pc, D, A, SR, frame) into the ring when
 * a configured PC range matches, then chains to whatever pre-insn
 * fn was installed before us (typically hybrid_pre_insn). Chaining
 * means --mem-write-log and Tier 2's hybrid dispatch continue to work
 * while Tier 3 is recording.
 *
 * Only compiled on SONIC_REVERSE_DEBUG=ON oracle builds. On native,
 * the interpreter isn't running (stub replaces DoCycles) so there's
 * nothing to hook.
 */

#if SONIC_REVERSE_DEBUG && defined(SONIC_ORACLE_BUILD)

#include "oracle_trace.h"
#include "cmd_server.h"
#include "clownmdemu.h"
#include "clown68000.h"
#include <stdio.h>
#include <string.h>

extern ClownMDEmu g_clownmdemu;   /* defined in main.c */

/* Hook slot on the interpreter side. Declared in runner/hybrid_global.c
 * and dereferenced from inside clown68000.c's per-instruction loop. */
extern void (*g_hybrid_pre_insn_fn)(cc_u32l pc);

#define T3_LOG_SIZE    (1u << 20)   /* 1,048,576 entries ≈ 80 MB */
#define T3_MAX_RANGES  8

typedef struct {
    uint32_t frame;
    uint32_t pc;
    uint32_t d[8];
    uint32_t a[8];
    uint16_t sr;
    uint16_t _pad;
} T3Entry;

static struct {
    int      active;
    int      nranges;
    struct { uint32_t lo, hi; } ranges[T3_MAX_RANGES];
    uint32_t write_idx;
    uint32_t count;
    uint32_t snap_start;
    uint32_t snap_count;
    int      snap_active;
    int      hook_installed;
    void   (*prev_fn)(cc_u32l pc);
    T3Entry  log[T3_LOG_SIZE];
} s_t3 = {0};

/* ---- Hot path ---- */

static int t3_range_hit(uint32_t pc)
{
    if (s_t3.nranges == 0) return 1;   /* no filter == log all */
    for (int i = 0; i < s_t3.nranges; i++) {
        if (pc >= s_t3.ranges[i].lo && pc <= s_t3.ranges[i].hi) return 1;
    }
    return 0;
}

/* Stage C: unconditional per-instruction counter on oracle. Ticked
 * from t3_pre_insn which fires once per 68K instruction in the
 * clown68000 interpreter's main loop. */
uint64_t g_oracle_insn_count = 0;

/* Phase 3: oracle break/step state. */
#define ORACLE_BREAK_MAX 128
static struct {
    uint32_t breaks[ORACLE_BREAK_MAX];
    int      break_count;
    int      step_insn;       /* one-shot: next insn parks */
    int      parked;
    uint32_t parked_pc;
    int      resume_flag;     /* set by rdb_oracle_continue/step */
} s_ob = {0};

int  oracle_break_add(uint32_t pc)
{
    if (s_ob.break_count >= ORACLE_BREAK_MAX) return 0;
    for (int i = 0; i < s_ob.break_count; i++)
        if (s_ob.breaks[i] == pc) return 1;
    s_ob.breaks[s_ob.break_count++] = pc & 0xFFFFFFu;
    return 1;
}
void oracle_break_clear_all(void) { s_ob.break_count = 0; }
int  oracle_break_count(void) { return s_ob.break_count; }
uint32_t oracle_break_get(int i)
{
    if (i < 0 || i >= s_ob.break_count) return 0;
    return s_ob.breaks[i];
}
void oracle_cmd_step_insn(void) { s_ob.step_insn = 1; s_ob.resume_flag = 1; }
void oracle_cmd_continue (void) { s_ob.step_insn = 0; s_ob.resume_flag = 1; }
int      oracle_is_parked (void) { return s_ob.parked; }
uint32_t oracle_parked_pc (void) { return s_ob.parked_pc; }

/* Break check — called from t3_pre_insn. Parks by spin-polling
 * cmd_server (safe: cmd_server_poll is non-blocking and runs on the
 * same thread as the interpreter). */
static void oracle_check_break(uint32_t pc)
{
    int hit = 0;
    if (s_ob.step_insn) { hit = 1; s_ob.step_insn = 0; }
    else {
        for (int i = 0; i < s_ob.break_count; i++) {
            if (s_ob.breaks[i] == pc) { hit = 1; break; }
        }
    }
    if (!hit) return;

    s_ob.parked_pc = pc;
    s_ob.parked = 1;
    s_ob.resume_flag = 0;
    /* Spin polling cmd_server until the user releases us. */
    extern CmdResult cmd_server_poll(void);
    while (!s_ob.resume_flag) {
        (void)cmd_server_poll();
        /* Small yield so we don't 100% a CPU core while parked. */
#if defined(_WIN32)
        extern __declspec(dllimport) void __stdcall Sleep(unsigned long);
        Sleep(1);
#endif
    }
    s_ob.parked = 0;
}

/* ---- Phase 4: snapshot ring ---- */
#define ORACLE_SNAP_INTERVAL 1000u
#define ORACLE_SNAP_RING       64

typedef struct {
    uint64_t   insn_count;
    ClownMDEmu state;
    int        valid;
} OracleSnap;

static OracleSnap s_snaps[ORACLE_SNAP_RING];
static int        s_snaps_write_idx = 0;

/* Capture the full emulator state. Restore-time we'll preserve the
 * callbacks/cartridge pointers that live outside the struct's saved
 * byte-range (they're heap/static pointers the sandbox version of this
 * same pattern in runner/hybrid.c uses). */
static void oracle_take_snapshot(void)
{
    OracleSnap *s = &s_snaps[s_snaps_write_idx % ORACLE_SNAP_RING];
    s->insn_count = g_oracle_insn_count;
    memcpy(&s->state, &g_clownmdemu, sizeof(g_clownmdemu));
    s->valid = 1;
    s_snaps_write_idx++;
}

int oracle_step_back(uint64_t *restored_insn_count_out)
{
    int best = -1;
    uint64_t best_ic = 0;
    uint64_t current = g_oracle_insn_count;
    for (int i = 0; i < ORACLE_SNAP_RING; i++) {
        if (!s_snaps[i].valid) continue;
        if (s_snaps[i].insn_count < current &&
            s_snaps[i].insn_count >= best_ic) {
            best = i;
            best_ic = s_snaps[i].insn_count;
        }
    }
    if (best < 0) return 0;

    /* Preserve pointers that are stored by-value inside the struct but
     * reference external heap/static memory — same pattern as
     * runner/hybrid.c's snapshot/restore pair (lines 97-120). */
    const ClownMDEmu_Callbacks *cb = g_clownmdemu.callbacks;
    const cc_u16l *cart = g_clownmdemu.cartridge_buffer;
    cc_u32l cart_len = g_clownmdemu.cartridge_buffer_length;
    memcpy(&g_clownmdemu, &s_snaps[best].state, sizeof(g_clownmdemu));
    g_clownmdemu.callbacks = cb;
    g_clownmdemu.cartridge_buffer = cart;
    g_clownmdemu.cartridge_buffer_length = cart_len;
    g_oracle_insn_count = s_snaps[best].insn_count;
    if (restored_insn_count_out) *restored_insn_count_out = best_ic;
    return 1;
}

int oracle_snap_count(void)
{
    int n = 0;
    for (int i = 0; i < ORACLE_SNAP_RING; i++) if (s_snaps[i].valid) n++;
    return n;
}

uint64_t oracle_snap_insn_at(int i)
{
    if (i < 0 || i >= ORACLE_SNAP_RING) return 0;
    return s_snaps[i].valid ? s_snaps[i].insn_count : 0;
}

static void t3_pre_insn(cc_u32l pc)
{
    g_oracle_insn_count++;
    if ((g_oracle_insn_count % ORACLE_SNAP_INTERVAL) == 0)
        oracle_take_snapshot();
    oracle_check_break((uint32_t)pc);
    if (s_t3.active && t3_range_hit((uint32_t)pc)) {
        const Clown68000_State *st = &g_clownmdemu.m68k;
        uint32_t idx = s_t3.write_idx % T3_LOG_SIZE;
        T3Entry *e = &s_t3.log[idx];
        e->pc    = (uint32_t)pc;
        e->sr    = (uint16_t)st->status_register;
        e->frame = cmd_server_current_frame();
        for (int i = 0; i < 8; i++) {
            e->d[i] = (uint32_t)st->data_registers[i];
            e->a[i] = (uint32_t)st->address_registers[i];
        }
        s_t3.write_idx++;
        if (s_t3.count < T3_LOG_SIZE) s_t3.count++;
    }
    /* Chain: forward to whatever pre-insn fn was installed before us
     * (hybrid_pre_insn for native dispatch, mostly a no-op on oracle
     * because the hybrid table is empty there). */
    if (s_t3.prev_fn) s_t3.prev_fn(pc);
}

/* ---- Install / uninstall ---- */

void t3_install_hook(void)
{
    if (s_t3.hook_installed) return;
    s_t3.prev_fn = g_hybrid_pre_insn_fn;
    g_hybrid_pre_insn_fn = t3_pre_insn;
    s_t3.hook_installed = 1;
}

void t3_uninstall_hook(void)
{
    if (!s_t3.hook_installed) return;
    /* Only restore if we're still the head of the chain; otherwise
     * leave whoever installed on top of us in place. */
    if (g_hybrid_pre_insn_fn == t3_pre_insn)
        g_hybrid_pre_insn_fn = s_t3.prev_fn;
    s_t3.prev_fn = NULL;
    s_t3.hook_installed = 0;
}

/* ---- Control API ---- */

int t3_add_range(uint32_t lo, uint32_t hi)
{
    if (s_t3.nranges >= T3_MAX_RANGES) return 0;
    if (hi < lo) { uint32_t t = lo; lo = hi; hi = t; }
    s_t3.ranges[s_t3.nranges].lo = lo & 0xFFFFFFu;
    s_t3.ranges[s_t3.nranges].hi = hi & 0xFFFFFFu;
    s_t3.nranges++;
    s_t3.active = 1;
    return 1;
}

void t3_reset(void)
{
    s_t3.active = 0;
    s_t3.nranges = 0;
    s_t3.write_idx = 0;
    s_t3.count = 0;
    s_t3.snap_start = 0;
    s_t3.snap_count = 0;
    s_t3.snap_active = 0;
}

int t3_range_count(void) { return s_t3.nranges; }

int t3_range_get(int i, uint32_t *lo_out, uint32_t *hi_out)
{
    if (i < 0 || i >= s_t3.nranges) return 0;
    if (lo_out) *lo_out = s_t3.ranges[i].lo;
    if (hi_out) *hi_out = s_t3.ranges[i].hi;
    return 1;
}

void t3_snapshot_begin(void)
{
    if (s_t3.count < T3_LOG_SIZE)
        s_t3.snap_start = 0;
    else
        s_t3.snap_start = s_t3.write_idx % T3_LOG_SIZE;
    s_t3.snap_count  = s_t3.count;
    s_t3.snap_active = 1;
}

void t3_snapshot_end(void)   { s_t3.snap_active = 0; }
uint32_t t3_snapshot_count(void)
{
    return s_t3.snap_active ? s_t3.snap_count : 0;
}

int t3_format_entry(uint32_t i, char *buf, size_t buflen)
{
    if (!s_t3.snap_active || i >= s_t3.snap_count) return -1;
    uint32_t idx = (s_t3.snap_start + i) % T3_LOG_SIZE;
    const T3Entry *e = &s_t3.log[idx];
    int n = snprintf(buf, buflen,
        "{\"f\":%u,\"pc\":\"0x%06X\",\"sr\":\"0x%04X\","
         "\"D\":[%u,%u,%u,%u,%u,%u,%u,%u],"
         "\"A\":[%u,%u,%u,%u,%u,%u,%u,%u]}",
        (unsigned)e->frame,
        (unsigned)(e->pc & 0xFFFFFFu),
        (unsigned)e->sr,
        (unsigned)e->d[0], (unsigned)e->d[1],
        (unsigned)e->d[2], (unsigned)e->d[3],
        (unsigned)e->d[4], (unsigned)e->d[5],
        (unsigned)e->d[6], (unsigned)e->d[7],
        (unsigned)e->a[0], (unsigned)e->a[1],
        (unsigned)e->a[2], (unsigned)e->a[3],
        (unsigned)e->a[4], (unsigned)e->a[5],
        (unsigned)e->a[6], (unsigned)e->a[7]);
    return (n < 0 || (size_t)n >= buflen) ? -1 : n;
}

#endif /* SONIC_REVERSE_DEBUG && SONIC_ORACLE_BUILD */
