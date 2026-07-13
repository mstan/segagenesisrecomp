/*
 * glue.c — bridges genesis_runtime.h with clownmdemu-core.
 *
 * Step 1 (ENABLE_RECOMPILED_CODE not set):
 *   Provides all symbols required by genesis_runtime.h so the generated code
 *   links.  Memory functions call through clownmdemu's bus layer.  No game
 *   thread is started; the interpreter still drives 68K execution.
 *
 * Step 2 (ENABLE_RECOMPILED_CODE defined):
 *   Starts the game thread that calls func_000206() continuously.
 *   m68k_read/write go through clownmdemu's M68kReadCallback / M68kWriteCallback.
 *   VBlank is cooperative: main thread sets g_vblank_pending, game thread checks
 *   it at every memory access and calls service_vblank() when it fires.
 */

#include "glue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

/* Cross-platform cooperative fibers (Win32 Fibers / POSIX ucontext) +
 * the FIBER_NORETURN attribute used by the trap-die helper below. */
#include "fiber_compat.h"

/* genesis_runtime.h interface */
#include "genesis_runtime.h"

/* Tier-3 clean-room 68000 interpreter — the runtime correctness floor. On a
 * dispatch miss the native build used to silently no-op the missed function;
 * the floor runs it correctly instead (validated 0-divergence vs clown68000).
 * Permissive/AGPL-free: reuses the recompiler's own decoder. */
#include "m68k_interp.h"

/* clownmdemu bus layer (oracle/hybrid builds only — native has no
 * clownmdemu include paths; the own backend routes the bus through
 * genesis_bus.c instead) */
#if !OWN_BACKEND
#include "bus-main-m68k.h"
#include "bus-common.h"
#endif

/* clowncommon types */
#include "clowncommon.h"

/* Audio event queue (cycle-stamped FM/PSG writes) */
#include "audio/event_queue.h"

#if HYBRID_RECOMPILED_CODE
#include "hybrid.h"
#include "verify.h"
#endif

#include "frame_record.h"
#include "game_layout.h"
#include "game_spec.h"

void recomp_push_return(uint32_t ret_addr)
{
    if (g_game_layout.initial_ssp && g_cpu.A[7] > g_game_layout.initial_ssp)
        g_cpu.A[7] = g_game_layout.initial_ssp;

    g_cpu.A[7] -= 4;
    m68k_write32(g_cpu.A[7], ret_addr & 0xFFFFFFu);
}

/* =========================================================================
 * Global state required by genesis_runtime.h
 * ========================================================================= */

M68KState g_cpu;
uint8_t   g_rom[0x400000];   /* 4 MB ROM shadow — ROM bytes (big-endian, byte-addressed) */
uint8_t   g_ram[0x10000];    /* 64 KB work RAM shadow (not authoritative in Step 2) */
int       g_ws_margin = 0;   /* widescreen extra px/side (set per-frame by main.c gate; 0 = 4:3) */

uint64_t  g_frame_count       = 0;
uint8_t   g_controller1_buttons = 0;
uint8_t   g_controller2_buttons = 0;

/* Contextual recompiler cycle tracking */
uint32_t  g_cycle_accumulator  = 0;
uint32_t  g_vblank_threshold   = 109312;  /* scanline 224 × 488 cycles */

/* Audio event-queue cycle stamp: 68K cycles since wall-frame start. Bumped
 * per-instruction by the generator (same cycle-table source as
 * g_cycle_accumulator). Does NOT reset at game-frame yield — resets only at
 * wall-frame boundary in glue_end_of_wall_frame. Monotonic within each wall,
 * so FM/PSG writes during the VBla handler get cycle stamps that reflect
 * their actual spacing in the handler's ~20k-cycle span. That's what lets
 * the new audio backend (runner/audio/) render samples *between* writes
 * instead of collapsing the handler's register burst onto a single
 * FM-sample boundary (the "boop/squelch" artifact). */
uint32_t  g_audio_cycle_counter = 0;
/* NTSC wall-frame cycle budget — used in CYCLE_ACCURATE mode to cap
 * game-fiber work at hardware rate. 262 scanlines × 488 cycles each. */
#define NTSC_CYCLES_PER_WALL_FRAME 127856u
static int s_vblank_fired_this_frame   = 0;  /* cycle-budget latch: 1 means
                                                * the wall frame's VBla budget
                                                * has been consumed (whether or
                                                * not the handler body ran). */
static int s_vblank_executed_this_frame = 0; /* did the recompiled VBla body
                                                * actually run this wall
                                                * frame. False when imask>=6
                                                * suppressed the call. */

/* Instruction-count telemetry (Stage C). Incremented by generated C
 * once per decoded 68K instruction via the generator's cycle-bump
 * emission. Used by rdb_insn_counts TCP command + rdb_insn_diff.py to
 * compare native per-wall-frame instruction throughput against oracle.
 * Stays 0 on oracle builds (generated func_* bodies never run there). */
uint64_t g_native_insn_count = 0;

/* Cycle-pacing telemetry. Updated once per wall frame in
 * glue_end_of_wall_frame. cmd_server_record_frame copies this into
 * the FrameRecord ring so frame_timeseries can serve it retroactively
 * (replaces the legacy [FPACE] stderr line). */
PaceSnap g_pace_snap = {0};

/* Recompiler emits calls to these for any opcode that doesn't decode
 * as a known 68K instruction (canonical ILLEGAL $4AFC, A-line, F-line,
 * or just unknown bytes mid-stream when codegen seeded a label that
 * happens to be data). On real hardware these would push an exception
 * frame and vector through $00010-$00040; in the recomp model we just
 * loud-abort with the PC and opcode so the cause is obvious. Without
 * this Sonic 2's broader function set fails to link at every address
 * whose label-seed pulled in non-code bytes. */
FIBER_NORETURN static void s_m68k_trap_die(unsigned vec, uint32_t pc, unsigned opcode)
{
    fprintf(stderr, "[ILLEGAL] m68k trap vec=%u pc=%06X opcode=%04X — abort\n",
            vec, pc, opcode);
    fflush(stderr);
    abort();
}
void m68k_trap_vector(uint8_t vec)              { s_m68k_trap_die(vec, g_cpu.PC, 0); }
void m68k_illegal_trap(uint32_t pc, uint16_t op) {
    int top4 = (op >> 12) & 0xF;
    unsigned vec = (op == 0x4AFC) ? 4u :
                   (top4 == 0xA)  ? 10u :
                   (top4 == 0xF)  ? 11u : 4u;
    s_m68k_trap_die(vec, pc, op);
}

/* Pacing mode (see glue.h). Default stays FIBER_FULL because
 * CYCLE_ACCURATE measurement (Stage B, this branch) shows the cap
 * eliminates multi-fire (1.074 → 1.000 fires/wall) but also halves
 * native's FM-write count (~50% tempo slowdown). The cap mechanism
 * itself is correct; the slowdown means the cycle-cost signal still
 * has a bias somewhere. CYCLE_ACCURATE remains opt-in via
 * --pacing=accurate while we hunt the bias. */
GluePacingMode g_pacing_mode = GLUE_PACING_FIBER_FULL;

uint32_t  g_miss_count_any    = 0;
int       g_step2_active      = 0;  /* set to 1 in Step 2 mode */
uint32_t  g_miss_last_addr    = 0;
uint64_t  g_miss_last_frame   = 0;
uint32_t  g_miss_unique_addrs[MAX_MISS_UNIQUE];
int       g_miss_unique_count  = 0;

/* g_rte_pending via pointer indirection (see genesis_runtime.h).
 * During VBlank service, we redirect to s_rte_dummy so RTE propagation
 * inside the handler chain is suppressed — the handler's stack management
 * is handled by force-restoring A7. */
static int s_rte_real  = 0;
static int s_rte_dummy = 0;
int *g_rte_pending_ptr = &s_rte_real;

int       g_early_return      = 0;

int       g_dbg_b64_count     = 0;
int       g_dbg_b5e_count     = 0;
int       g_dbg_b88_count     = 0;

/* =========================================================================
 * Bus access watchdog
 *
 * Counts bus accesses between yields.  If the game fiber does > N million
 * accesses without calling glue_yield_for_vblank(), something is stuck in
 * an infinite loop.  We log full CPU state and exit cleanly instead of
 * hanging forever.
 * ========================================================================= */

#define WATCHDOG_LIMIT  10000000u  /* 10M bus ops ≈ way too many for one frame */
static uint32_t s_watchdog_counter = 0;

#if SONIC_REVERSE_DEBUG
extern uint32_t g_rdb_current_func;
#else
/* Stub so interior-label miss diagnostics in genesis_log_dispatch_miss
 * compile in builds without reverse_debug (Sonic 1 default). The value
 * stays 0 there — the addr + frame fields still pinpoint the failure. */
static uint32_t g_rdb_current_func = 0;
#endif

#include "crash_report.h"

#if OWN_BACKEND
#include "genesis_machine.h"
/* Debug write-trace hooks (--mem-write-log, FM trace) are normally defined in
 * the clownmdemu fork's bus / FM code. The own backend links no clownmdemu, so
 * define them here to satisfy cmd_server's assignments. They default NULL and
 * stay inert until gbus/ym2612 are wired to call them (own-backend trace is a
 * follow-up; this keeps the AGPL-free link resolved). */
void (*g_fm_write_trace_fn )(uint32_t address,      uint8_t value, uint32_t target_cycle) = NULL;
void (*g_mem_write_trace_fn)(uint32_t byte_address, uint8_t value, uint32_t target_cycle) = NULL;
#endif

#define INSN_WATCHDOG_LIMIT 20000000ull
static uint64_t s_insn_watchdog_base = 0;
static void dump_bus_ring(void);   /* defined after the bus ring below; both watchdogs use it */

static void instruction_watchdog_reset(void)
{
    s_insn_watchdog_base = g_native_insn_count;
}

static void instruction_watchdog_check(void)
{
    if (g_native_insn_count - s_insn_watchdog_base < INSN_WATCHDOG_LIMIT)
        return;

    char reason[160];
#if SONIC_REVERSE_DEBUG
    snprintf(reason, sizeof(reason),
             "instruction watchdog: %llu native insns without yielding (func=$%06X)",
             (unsigned long long)(g_native_insn_count - s_insn_watchdog_base),
             (unsigned)(g_rdb_current_func & 0xFFFFFFu));
#else
    snprintf(reason, sizeof(reason),
             "instruction watchdog: %llu native insns without yielding",
             (unsigned long long)(g_native_insn_count - s_insn_watchdog_base));
#endif
#if OWN_BACKEND
    {
        int nz_z80ram = 0;
        for (int i = 0; i < 0x2000; i++) if (g_machine.bus.z80_ram[i]) nz_z80ram++;
        fprintf(stderr,
            "[OWN-DIAG] cpuPC=$%06X z80pc=$%04X z80_run=%d busreq=%d reset_off=%d "
            "bank=$%03X nz_z80ram=%d ram_F00D=$%02X ram_F00A=$%02X%02X\n",
            (unsigned)g_cpu.PC, (unsigned)g_machine.z80.pc,
            (g_machine.bus.z80_reset_off && !g_machine.bus.z80_busreq) ? 1 : 0,
            g_machine.bus.z80_busreq, g_machine.bus.z80_reset_off,
            (unsigned)g_machine.bus.z80_bank, nz_z80ram,
            g_ram[0xF00D], g_ram[0xF00A], g_ram[0xF00B]);
        /* Z80 driver entry bytes — decode why z80pc is stuck. */
        fprintf(stderr, "[OWN-DIAG] z80_ram[0..0x1F]:");
        for (int i = 0; i < 0x20; i++) fprintf(stderr, " %02X", g_machine.bus.z80_ram[i]);
        fprintf(stderr, "  | around z80pc[$%04X-8..+8]:", (unsigned)g_machine.z80.pc);
        for (int i = (int)g_machine.z80.pc - 8; i <= (int)g_machine.z80.pc + 8; i++)
            if (i >= 0 && i < 0x2000) fprintf(stderr, " %02X", g_machine.bus.z80_ram[i]);
        fprintf(stderr, "\n");
    }
#endif
    crash_report_dump_persistent(reason, &g_cpu, 0, 0, g_frame_count);
    dump_bus_ring();   /* last 64 bus accesses — pins what a non-yielding spin is polling */
    exit(2);
}

/* VDP control-port capture used to live here as a separate
 * file-backed printf log (--vdp-ctrl-log PATH). Retired in favor of
 * the always-on Tier 1 store ring: arm `rdb_range 0xC00000 0xC00007`
 * via TCP and dump it with `rdb_dump`. The ring has the same write
 * coverage plus per-entry attribution (frame, vint_runcount, func,
 * caller) and works retroactively without rebuilding or restarting.
 * Reads of $C00004 (VDP status) are reconstructable from the
 * FrameRecord vdp snapshot. */

/* Ring buffer of the last N bus accesses, populated by every
 * m68k_read / m68k_write call. Dumped by the watchdog so we can see
 * exactly what address sequence the game thread is touching when it
 * stalls — particularly useful for spin-loop diagnosis where the
 * spin_check counter shows 0 (something is interleaving). */
#define BUS_RING_SIZE 64
typedef struct { uint32_t addr; uint8_t kind; } BusRingEntry;
static BusRingEntry s_bus_ring[BUS_RING_SIZE];
static uint32_t     s_bus_ring_head = 0;     /* next write slot */
static uint64_t     s_bus_ring_total = 0;    /* total events ever recorded */

static inline void bus_ring_push(uint32_t addr, uint8_t kind) {
    s_bus_ring[s_bus_ring_head & (BUS_RING_SIZE - 1)].addr = addr;
    s_bus_ring[s_bus_ring_head & (BUS_RING_SIZE - 1)].kind = kind;
    s_bus_ring_head++;
    s_bus_ring_total++;
}
/* kind: 0=R8 1=R16 2=R32 3=W8 4=W16 5=W32 */

/* Dump the always-on bus-access ring (low-level access pattern) — the post-hoc
 * view of what a stall is touching. Called by BOTH watchdogs (bus + instruction)
 * so a non-yielding spin reveals exactly what it is polling. */
static void dump_bus_ring(void)
{
    static const char *kind_str[] = {"R8","R16","R32","W8","W16","W32"};
    uint64_t total = s_bus_ring_total;
    uint32_t window = (total < BUS_RING_SIZE) ? (uint32_t)total : BUS_RING_SIZE;
    fprintf(stderr, "\n  Bus ring (last %u of %llu accesses):\n",
            window, (unsigned long long)total);
    for (uint32_t i = 0; i < window; i++) {
        uint32_t idx = (s_bus_ring_head - window + i) & (BUS_RING_SIZE - 1);
        const BusRingEntry *e = &s_bus_ring[idx];
        const char *k = (e->kind < 6) ? kind_str[e->kind] : "??";
        fprintf(stderr, "    [%2u] %s $%06X\n", i, k, e->addr);
    }
}

static void watchdog_check(uint32_t addr, int is_write, uint32_t val)
{
    (void)val;
    if (++s_watchdog_counter != WATCHDOG_LIMIT)
        return;

    char reason[128];
    snprintf(reason, sizeof(reason),
             "watchdog: %u bus accesses without yield", s_watchdog_counter);

    crash_report_dump_persistent(reason, &g_cpu, addr, is_write, g_frame_count);
    dump_bus_ring();
    exit(2);
}

/* =========================================================================
 * Internal glue state
 * ========================================================================= */

static ClownMDEmu        *s_emu      = NULL;   /* NULL on the own backend */
#if !OWN_BACKEND
static CPUCallbackUserData s_cpu_data;   /* passed to M68kReadCallback / M68kWriteCallback */
#endif

/* Bus cycle counter — declared in generated code or glue, used by clownmdemu sync */
extern cc_u32f g_hybrid_cycle_counter;

/* Reset bus sync state to frame start. Called at frame boundaries
 * so that cycle-based VDP/Z80/FM/PSG sync stays within one frame. */
static uint16_t recomp_ram_read16_direct(uint32_t addr)
{
    uint16_t off = (uint16_t)(addr & 0xFFFFu);
#if OWN_BACKEND
    /* Own backend: g_ram IS the authoritative work RAM (s_emu is NULL here —
     * reading through it returned $FFFF, so RAM JMP trampolines like S3's
     * H-int stub at $FFF608 never resolved and the handler dispatch missed). */
    return (uint16_t)(((uint16_t)g_ram[off] << 8) | g_ram[(uint16_t)(off + 1u)]);
#else
    return s_emu ? (uint16_t)s_emu->state.m68k.ram[off / 2u] : 0xFFFFu;
#endif
}

static uint32_t recomp_ram_read32_direct(uint32_t addr)
{
    return ((uint32_t)recomp_ram_read16_direct(addr) << 16) |
           (uint32_t)recomp_ram_read16_direct(addr + 2u);
}

uint32_t recomp_resolve_ram_trampoline(uint32_t addr)
{
    uint32_t resolved = addr & 0xFFFFFFu;

    for (int guard = 0; guard < 4; guard++) {
        if (resolved < RAM_BASE)
            break;

        uint16_t opcode = recomp_ram_read16_direct(resolved);
        uint32_t next;

        if (opcode == 0x4EF9u || opcode == 0x4EB9u) {
            next = recomp_ram_read32_direct(resolved + 2u) & 0xFFFFFFu;
        } else if (opcode == 0x4EF8u || opcode == 0x4EB8u) {
            uint16_t aw = recomp_ram_read16_direct(resolved + 2u);
            next = (aw & 0x8000u) ? (0xFF0000u | (uint32_t)aw) : (uint32_t)aw;
        } else {
            break;
        }

        next &= 0xFFFFFFu;
        if (next == resolved)
            break;
        resolved = next;
    }

    return resolved;
}

typedef struct {
    uint32_t ram_addr;
    uint32_t rom_addr;
    unsigned word_count;
} CopiedRomStub;

static CopiedRomStub s_copied_rom_stubs[8];

static uint16_t recomp_rom_read16_direct(uint32_t addr)
{
    return (uint16_t)(((uint16_t)g_rom[addr] << 8) | g_rom[addr + 1u]);
}

/* Some games install short interrupt handlers by copying a ROM routine into
 * work RAM up to (but excluding) a $FFFF marker. Resolve those copies back to
 * their static ROM source so the generated implementation can execute them.
 * Exact full-body comparison avoids treating arbitrary writable data as code. */
static uint32_t recomp_resolve_copied_rom_stub(uint32_t ram_addr,
                                               unsigned *word_count_out)
{
    uint32_t rom_limit = g_game_spec.expected_rom_size
                             ? g_game_spec.expected_rom_size
                             : (uint32_t)sizeof(g_rom);

    for (unsigned i = 0; i < sizeof(s_copied_rom_stubs) / sizeof(s_copied_rom_stubs[0]); i++) {
        CopiedRomStub *cached = &s_copied_rom_stubs[i];
        if (cached->ram_addr != ram_addr || cached->word_count == 0)
            continue;
        /* Copied raster handlers may intentionally patch operands in their
         * own body (RKA advances the source word at RAM+$20 each HBlank).
         * The first 12 words are the stable identifying prefix and include
         * the command word that distinguishes its near-identical variants. */
        unsigned signature_words = cached->word_count < 12u
                                       ? cached->word_count : 12u;
        unsigned w;
        for (w = 0; w < signature_words; w++) {
            if (recomp_ram_read16_direct(ram_addr + 2u * w) !=
                recomp_rom_read16_direct(cached->rom_addr + 2u * w))
                break;
        }
        if (w == signature_words) {
            *word_count_out = cached->word_count;
            return cached->rom_addr;
        }
    }

    uint16_t first = recomp_ram_read16_direct(ram_addr);
    for (uint32_t source = 0; source + 10u < rom_limit; source += 2u) {
        if (recomp_rom_read16_direct(source) != first)
            continue;

        unsigned words;
        for (words = 0; words < 64u && source + 2u * words + 1u < rom_limit; words++) {
            uint16_t rom_word = recomp_rom_read16_direct(source + 2u * words);
            if (rom_word == 0xFFFFu)
                break;
            if (recomp_ram_read16_direct(ram_addr + 2u * words) != rom_word)
                break;
        }
        if (words < 4u || words >= 64u ||
            recomp_rom_read16_direct(source + 2u * words) != 0xFFFFu)
            continue;

        CopiedRomStub *slot = &s_copied_rom_stubs[0];
        for (unsigned i = 0; i < sizeof(s_copied_rom_stubs) / sizeof(s_copied_rom_stubs[0]); i++) {
            if (s_copied_rom_stubs[i].word_count == 0 ||
                s_copied_rom_stubs[i].ram_addr == ram_addr) {
                slot = &s_copied_rom_stubs[i];
                break;
            }
        }
        slot->ram_addr = ram_addr;
        slot->rom_addr = source;
        slot->word_count = words;
        *word_count_out = words;
        fprintf(stderr, "[dispatch][RAM] copied handler $%06X -> ROM $%06X (%u words)\n",
                ram_addr, source, words);
        return source;
    }
    return 0;
}

int recomp_dispatch_ram_stub(uint32_t addr)
{
    static uint32_t s_reported_addr;
    uint32_t result;
    uint32_t carry;
    uint32_t overflow = 0;
    unsigned shift_count = 0;
    int shift_left = 0;
    addr &= 0xFFFFFFu;
    if (addr < RAM_BASE)
        return 0;

    /* A generated JSR/JMP dispatcher already models the surrounding loose-A7
     * call edge. For a RAM-resident RTS stub, returning from this C helper is
     * therefore the complete instruction semantics: the generated caller (or
     * its enclosing JMP caller) performs the single guest-stack pop exactly as
     * it does for a native ROM function. RKA installs this canonical null
     * handler at $FFB1F2. */
    uint16_t opcode = recomp_ram_read16_direct(addr);
    if (opcode == 0x4E75u) {
        g_native_insn_count++;
        g_cycle_accumulator += 16u;
        g_audio_cycle_counter += 16u;
        if (g_cycle_accumulator >= g_vblank_threshold)
            glue_check_vblank();
        return 1;
    }

    /* RKA's configuration loader copies one of these two-instruction helpers
     * to $FFB1F2. They scale D1 before collision-table lookups. Keep this
     * decoder deliberately narrow: only the five exact opcodes present in the
     * ROM's configuration records, and only when immediately followed by RTS. */
    if (recomp_ram_read16_direct(addr + 2u) == 0x4E75u) {
        switch (opcode) {
        case 0xE381u: shift_left = 1; shift_count = 1; break; /* ASL.L #1,D1 */
        case 0xE581u: shift_left = 1; shift_count = 2; break; /* ASL.L #2,D1 */
        case 0xE781u: shift_left = 1; shift_count = 3; break; /* ASL.L #3,D1 */
        case 0xE981u: shift_left = 1; shift_count = 4; break; /* ASL.L #4,D1 */
        case 0xE289u: shift_left = 0; shift_count = 1; break; /* LSR.L #1,D1 */
        default: break;
        }
    }

    if (shift_count != 0) {
        uint32_t source = g_cpu.D[1];
        if (shift_left) {
            result = source << shift_count;
            carry = (source >> (32u - shift_count)) & 1u;

            /* ASL sets V when the sign changes at any intermediate step. */
            uint32_t top = source >> (31u - shift_count);
            uint32_t all_same = (1u << (shift_count + 1u)) - 1u;
            overflow = (top != 0u && top != all_same) ? 1u : 0u;
        } else {
            result = source >> shift_count;
            carry = (source >> (shift_count - 1u)) & 1u;
        }

        g_cpu.D[1] = result;
        g_cpu.SR &= ~0x1Fu;
        if (result == 0)
            g_cpu.SR |= 1u << 2;
        if (result >> 31)
            g_cpu.SR |= 1u << 3;
        if (carry)
            g_cpu.SR |= (1u << 0) | (1u << 4);
        if (overflow)
            g_cpu.SR |= 1u << 1;

        /* One register shift (8 + 2*n cycles) followed by RTS (16 cycles). */
        uint32_t cycles = 24u + 2u * shift_count;
        g_native_insn_count += 2u;
        g_cycle_accumulator += cycles;
        g_audio_cycle_counter += cycles;
        if (g_cycle_accumulator >= g_vblank_threshold)
            glue_check_vblank();
        return 1;
    }

    {
        unsigned copied_words = 0;
        uint32_t source = recomp_resolve_copied_rom_stub(addr, &copied_words);
        if (source != 0 && copied_words != 0) {
            call_by_address(source);
            return 1;
        }
    }

    if (s_reported_addr != addr) {
        s_reported_addr = addr;
        fprintf(stderr,
                "[dispatch][RAM] unresolved stub $%06X words=%04X %04X %04X %04X "
                "mode=$%04X\n",
                addr, opcode,
                recomp_ram_read16_direct(addr + 2u),
                recomp_ram_read16_direct(addr + 4u),
                recomp_ram_read16_direct(addr + 6u),
                recomp_ram_read16_direct(0xFFB1D0u));
    }
    return 0;
}

void glue_reset_frame_sync(void)
{
    g_hybrid_cycle_counter = 0;
#if !OWN_BACKEND
    s_cpu_data.sync.m68k.current_cycle = 0;
    s_cpu_data.sync.m68k.base_cycle = 0;
    /* Reset audio and IO sync states to match the cycle counter reset.
     * Without this, SyncCommon computes (0/divisor - old_value) = huge
     * negative delta (wraps unsigned), causing audio overgeneration. */
    s_cpu_data.sync.fm.current_cycle = 0;
    s_cpu_data.sync.psg.current_cycle = 0;
    s_cpu_data.sync.pcm.current_cycle = 0;
    s_cpu_data.sync.io_ports[0].current_cycle = 0;
    s_cpu_data.sync.io_ports[1].current_cycle = 0;
    s_cpu_data.sync.io_ports[2].current_cycle = 0;
#endif
}

/* Hybrid verifier sync snapshot/restore — saves s_cpu_data sync state */
#if HYBRID_RECOMPILED_CODE
static CPUCallbackUserData s_sync_snapshot;

void glue_snapshot_sync(void)
{
    memcpy(&s_sync_snapshot, &s_cpu_data, sizeof(s_cpu_data));
}

void glue_restore_sync(void)
{
    memcpy(&s_cpu_data, &s_sync_snapshot, sizeof(s_cpu_data));
}
#endif

/* =========================================================================
 * VBlank / single-threaded fiber sync (Step 2)
 *
 * Game code and VDP rendering alternate on the same thread using Windows
 * Fibers.  WaitForVBla (pattern-detected per-game in code_generator.c —
 * any function matching `move #imm,sr; tst.b mem; bne self; rts`) yields
 * to the main fiber; the main loop runs Iterate + VBlank handlers, then
 * resumes the game fiber. No threads, no semaphores, no races.
 * ========================================================================= */

/* Re-entrancy guard (used by glue_check_vblank, must be in global scope) */
static int s_in_vblank_service = 0;

#include "game_spec.h"      /* g_game_spec.call_entry_point / vblank / hblank / periodic */

#if ENABLE_RECOMPILED_CODE

void glue_log_frame_state(uint64_t frame);  /* defined below */

static fiber_t s_main_fiber = NULL;
static fiber_t s_game_fiber = NULL;
static int    s_game_running = 0;   /* 1 once the game fiber has started */
static uint32_t s_game_fiber_resume_pc = 0;

#define GAME_FIBER_STACK_COMMIT  (1u * 1024u * 1024u)
#define GAME_FIBER_STACK_RESERVE (32u * 1024u * 1024u)
#define GAME_FIBER_STACK_WARN_STEP (1024u * 1024u)
#define GAME_FIBER_STACK_ABORT     (28u * 1024u * 1024u)

static uintptr_t s_game_stack_top = 0;
static size_t    s_game_stack_last_report = 0;

static void game_stack_note(const char *reason, const void *stack_marker)
{
    if (!s_game_stack_top || !stack_marker)
        return;

    uintptr_t marker = (uintptr_t)stack_marker;
    size_t used = (s_game_stack_top >= marker)
        ? (size_t)(s_game_stack_top - marker)
        : (size_t)(marker - s_game_stack_top);

    if (used >= s_game_stack_last_report + GAME_FIBER_STACK_WARN_STEP ||
        used >= GAME_FIBER_STACK_ABORT) {
        fprintf(stderr,
                "[STACK] reason=%s used=%zu frame=%" PRIu64
                " a7=%06X vblank_service=%d\n",
                reason, used, g_frame_count, (unsigned)(g_cpu.A[7] & 0xFFFFFFu),
                s_in_vblank_service);
        s_game_stack_last_report = used;
    }

    if (used >= GAME_FIBER_STACK_ABORT) {
        char crash_reason[160];
        snprintf(crash_reason, sizeof(crash_reason),
                 "game fiber stack runaway: %zu bytes used at %s",
                 used, reason);
        crash_report_dump_persistent(crash_reason, &g_cpu, 0, 0, g_frame_count);
        exit(2);
    }
}

/* Game fiber entry point. */
static void game_fiber_func(void *param)
{
    (void)param;
    char stack_top_marker;
    s_game_stack_top = (uintptr_t)&stack_top_marker;
    s_game_stack_last_report = 0;
    if (s_game_fiber_resume_pc) {
        uint32_t resume_pc = s_game_fiber_resume_pc & 0xFFFFFFu;
        s_game_fiber_resume_pc = 0;
        g_cpu.A[7] = g_game_layout.initial_ssp;
        recomp_call_addr(resume_pc);
        if (g_game_spec.dispatch_main_loop_pc) {
            uint32_t dispatch_pc = g_game_spec.dispatch_main_loop_pc & 0xFFFFFFu;
            fprintf(stderr, "[GAME] %s resume loop returned at $%06X; continuing at dispatcher $%06X\n",
                    g_game_spec.short_name, resume_pc, dispatch_pc);
            recomp_call_addr(dispatch_pc);
        }
        fprintf(stderr, "[GAME] %s resume/dispatch returned unexpectedly!\n",
                g_game_spec.short_name);
        for (;;) fiber_switch(s_main_fiber);
    }

    g_cpu.A[7] =   ((uint32_t)g_rom[0] << 24)
                  | ((uint32_t)g_rom[1] << 16)
                  | ((uint32_t)g_rom[2] <<  8)
                  |  (uint32_t)g_rom[3];
    g_cpu.SR  = 0x2700u;

    g_game_spec.call_entry_point();

    /* call_entry_point should never return — it contains the main game loop.
     * If it does, just yield back to main forever. */
    fprintf(stderr, "[GAME] %s entry point returned unexpectedly!\n",
            g_game_spec.short_name);
    for (;;) fiber_switch(s_main_fiber);
}

static int create_game_fiber(uint32_t resume_pc)
{
    s_game_fiber_resume_pc = resume_pc & 0xFFFFFFu;
    s_game_fiber = fiber_create(GAME_FIBER_STACK_COMMIT,
                                GAME_FIBER_STACK_RESERVE,
                                game_fiber_func,
                                NULL);
    if (!s_game_fiber) {
        fprintf(stderr, "glue: fiber_create failed\n");
        s_game_fiber_resume_pc = 0;
        s_game_running = 0;
        return 0;
    }

    s_game_running = 1;
    return 1;
}

/* Scanline interleave state */
static int32_t s_cycle_budget = 0;
static int     s_game_yielded_vblank = 0;
/* 68K cycles spent inside an interrupt handler (V-int/H-int), still owed to
 * the raster. own_deliver_vint runs the whole handler atomically at the
 * vblank scanline with budget yields gated (s_in_vblank_service), so without
 * this the handler consumes ZERO raster time and the main loop resumes at
 * line ~225 — tens of lines earlier than hardware, where the level V-int
 * (DMA + decompression) occupies the CPU well into the next frame. That
 * early resume put a Z80 mailbox write (Play_SFX) BEFORE a driver tick's
 * queue-read where hardware places it AFTER, splitting sfx_EnterSS and the
 * following cmd_Stop across two ticks and killing the S3 giant-ring entry
 * sound. glue_run_game_chunk drains this debt at M68K_PER_LINE per scanline
 * before resuming main-loop work. Only ever nonzero on the own backend. */
static uint32_t s_irq_cycle_debt = 0;
/* Interrupt level whose atomically-executed handler owns the outstanding
 * raster debt. A second level-6 interrupt cannot be accepted while the real
 * 68000 would still be inside the previous level-6 handler. */
static int s_irq_cycle_debt_level = 0;
/* g_audio_cycle_counter value at the last budget drain (see
 * check_cycle_budget — the budget drains by real elapsed 68K cycles). */
static uint32_t s_budget_cyc_seen = 0;
#if SONIC_REVERSE_DEBUG
/* Tier-2 reverse debugger: set by glue_yield_for_break when the game
 * fiber parks at a block-entry hook. Main loop reads via
 * glue_game_yielded_for_break() after each SwitchToFiber return and
 * drains cmd_server until a resume command clears it. */
static int     s_game_yielded_break = 0;
#endif
static int     s_interleave_active = 0;

/* Called from DoCycles (inside Iterate). Runs game code for a chunk. */
static cc_u32f s_chunk_cycles = 0;  /* budget for current chunk */

void glue_run_game_chunk(cc_u32f cycles)
{
    if (!s_game_running || !s_game_fiber)
        return;
    if (s_game_yielded_vblank)
        return;
#if SONIC_REVERSE_DEBUG
    /* Tier 2: game fiber has parked at a breakpoint. Keep DoCycles
     * no-opping until Iterate returns to main.c, where rdb_park_drain
     * polls cmd_server until a resume command arrives. Without this
     * gate the yield is effectively a no-op — DoCycles would switch
     * right back into the game fiber on the next chunk. */
    if (s_game_yielded_break)
        return;
#endif

    /* Pay down interrupt-handler raster debt first: while the 68K is
     * (logically) still inside the V-int/H-int handler, the main loop does
     * not advance — matching hardware, where the handler occupies the CPU
     * for those scanlines. */
    if (s_irq_cycle_debt) {
        if (s_irq_cycle_debt >= cycles) {
            s_irq_cycle_debt -= (uint32_t)cycles;
            if (s_irq_cycle_debt == 0)
                s_irq_cycle_debt_level = 0;
            return;
        }
        cycles -= s_irq_cycle_debt;
        s_irq_cycle_debt = 0;
        s_irq_cycle_debt_level = 0;
    }

    s_chunk_cycles = cycles;
    s_cycle_budget = (int32_t)cycles;
    s_budget_cyc_seen = g_audio_cycle_counter;  /* drain from here (see check_cycle_budget) */
    s_interleave_active = 1;
    instruction_watchdog_reset();
    fiber_switch(s_game_fiber);
    instruction_watchdog_reset();
    s_interleave_active = 0;
}

/* Called from bus access macro to check the interleave budget (separate
 * from g_hybrid_cycle_counter, which is now bumped per-instruction by
 * the generator). Budget drives WHEN to yield back to the scheduler; cycle
 * counter drives WHAT cycle timestamps to report.
 *
 * The budget drains by REAL elapsed 68K cycles: the generated code bumps
 * g_audio_cycle_counter per instruction with PRM-accurate costs (plus DMA
 * freeze charges via glue_charge_68k_stall), so the delta since the last
 * check is the true cycle cost of the code just executed. The old scheme
 * (flat 10 per data access) ignored instruction-fetch time entirely and ran
 * the main loop ~2.5x faster than hardware against the raster — early enough
 * that a Play_SFX mailbox write could land BEFORE a Z80 driver tick's
 * queue-read where hardware places it after (the S3 giant-ring entry-sound
 * kill). */
uint64_t g_chunk_yield_count = 0;
static void check_cycle_budget(void)
{
    if (s_interleave_active && !s_in_vblank_service) {
        uint32_t now = g_audio_cycle_counter;
        if (now > s_budget_cyc_seen)
            s_cycle_budget -= (int32_t)(now - s_budget_cyc_seen);
        s_budget_cyc_seen = now;   /* also re-syncs after the per-frame reset */
        if (s_cycle_budget <= 0) {
            /* This is a real cooperative yield to the scanline scheduler.
             * Keep the bus watchdog's "without yield" window aligned with
             * the instruction watchdog, which glue_run_game_chunk resets on
             * every fiber return. Games such as RKA advance exclusively via
             * this budget path rather than a recognized WaitForVBlank hook;
             * without this reset, normal bus traffic accumulates over
             * thousands of frames and eventually produces a false hang. */
            s_watchdog_counter = 0;
            g_chunk_yield_count++;
            { char stack_marker; game_stack_note("cycle-budget", &stack_marker); }
            fiber_switch(s_main_fiber);
        }
    }
}

/* Charge 68K freeze cycles (a 68K->VDP DMA transfer) to the recompiled CPU's
 * accounting by advancing the cycle counter: that advances the audio-stamp
 * axis (hardware time passes during the freeze), drains the interleave budget
 * via check_cycle_budget's delta on the next bus access (main-loop DMA), and
 * is picked up as raster debt by the own-backend handler paths (V-int DMA). */
void glue_charge_68k_stall(uint32_t cycles)
{
    g_audio_cycle_counter += cycles;
}

/* Called from Clown68000_Interrupt during Iterate when VBlank/HBlank fires. */
#if !OWN_BACKEND
/* clownmdemu-Iterate interrupt path; the own backend delivers through
 * glue_own_interrupt instead. */
void glue_handle_interrupt(cc_u16f level)
{
    if (!s_game_running)
        return;

    int imask = (g_cpu.SR >> 8) & 7;

    if (level == 6 && imask < 6) {
        if (!s_game_yielded_vblank)
            return;

        /* VBlank interrupt — run handler with register save/restore */
        M68KState saved = g_cpu;

        /* Per-game IRQ-handler stack base (game_layout.intr_stack).
         * 256 bytes immediately below it are saved before the call and
         * restored after. The base must NOT lie inside the live object
         * table — see Sonic 2's game.toml comment for why. */
        const uint32_t INTR_STACK_ADDR = g_game_layout.intr_stack;
        #define INTR_SAVE 128
        cc_u16l intr_ram[INTR_SAVE];
        uint32_t base = ((INTR_STACK_ADDR - INTR_SAVE * 2) & 0xFFFF) / 2;
        for (int i = 0; i < INTR_SAVE; i++)
            intr_ram[i] = s_emu->state.m68k.ram[base + i];

        g_cpu.A[7] = INTR_STACK_ADDR;
        s_in_vblank_service = 1;
        g_rte_pending = 0;
        g_rte_pending_ptr = &s_rte_dummy;
        g_rte_pending = 0;
        /* Pin clownmdemu's VDP "currently in VBlank" flag for the
         * duration of the handler. By the time we reach this point in
         * Iterate, the VDP simulator has already advanced past
         * scanline -1 (where it clears the flag), so a Sonic-2-style
         * V_Int handler that reads $C00004 expecting bit 3 set would
         * spin forever. On real hardware the IRQ fires AT scanline
         * 224 with the VBlank flag still raised. Sonic 1 doesn't read
         * the bit so it doesn't trip on this. */
        cc_bool saved_vblank_flag = s_emu->vdp.state.currently_in_vblank;
        s_emu->vdp.state.currently_in_vblank = cc_true;
        if (g_game_spec.call_vblank) g_game_spec.call_vblank();
        s_emu->vdp.state.currently_in_vblank = saved_vblank_flag;
        g_rte_pending_ptr = &s_rte_real;
        g_rte_pending = 0;
        s_in_vblank_service = 0;

        for (int i = 0; i < INTR_SAVE; i++)
            s_emu->state.m68k.ram[base + i] = intr_ram[i];

        g_cpu = saved;

        /* Wake game — it can now continue past WaitForVBlank */
        s_game_yielded_vblank = 0;
    }
    if (level == 4 && imask < 4) {
        /* HBlank — run with save/restore */
        M68KState saved = g_cpu;

        const uint32_t HBL_STACK_ADDR = g_game_layout.intr_stack;
        uint32_t base = ((HBL_STACK_ADDR - INTR_SAVE * 2) & 0xFFFF) / 2;
        cc_u16l hbl_ram[INTR_SAVE];
        for (int i = 0; i < INTR_SAVE; i++)
            hbl_ram[i] = s_emu->state.m68k.ram[base + i];

        g_cpu.A[7] = HBL_STACK_ADDR;
        s_in_vblank_service = 1;
        g_rte_pending = 0;
        g_rte_pending_ptr = &s_rte_dummy;
        g_rte_pending = 0;
        if (g_game_spec.call_hblank) g_game_spec.call_hblank();
        g_rte_pending_ptr = &s_rte_real;
        g_rte_pending = 0;
        s_in_vblank_service = 0;

        for (int i = 0; i < INTR_SAVE; i++)
            s_emu->state.m68k.ram[base + i] = hbl_ram[i];

        g_cpu = saved;
    }
}
#endif /* !OWN_BACKEND */

#if OWN_BACKEND
#include "genesis_machine.h"

/* V-int raised while the 68K had IRQs masked (imask >= 6, e.g. the
 * move #$2700,sr the game runs across screen transitions). On hardware / in the
 * clownmdemu interpreter the VDP holds the V-int line asserted and the CPU takes
 * it the instant the mask drops; the own backend used to DROP it outright,
 * losing one v_vblank_count per masked transition. That one-frame skew sent the
 * attract demo down a different branch (GM_Demo vs the oracle's GM_Level) and
 * desynced VRAM. We now LATCH it and deliver at the next scanline whose mask
 * permits — see glue_own_vint_service_latched(). */
static int s_own_vint_latched = 0;

/* Run the game's V_Int(6) handler atomically against OUR work RAM + VDP. Saves
 * /restores g_cpu and gates budget yields (s_in_vblank_service), so it is safe
 * to fire while the fiber is parked mid-instruction at a budget yield. */
/* Audio-stamp re-base for interrupt handlers (see genesis_bus.c STAMP_68K):
 * the handler runs at its delivery raster line, but the 68K instruction
 * counter sits wherever the main loop parked (typically ~line 60) or
 * mid-frame on a lag frame. Stamping handler chip writes from the raw
 * counter placed the whole 68K SMPS driver tick 100+ lines away from its
 * true raster position — same VALUES, wrong position on the frame's
 * timeline relative to the Z80 DAC stream. Re-base so the handler's first
 * write stamps at the raster cursor of delivery, advancing per-instruction
 * from there, exactly like hardware. */
uint32_t g_68k_stamp_rebase = 0;
extern uint32_t machine_z80_stamp(void);   /* raster cursor, master cycles */

static void own_deliver_vint(GVDP *vdp)
{
    const uint32_t STK = g_game_layout.intr_stack;
    uint32_t byteoff = (STK - 256u) & 0xFFFFu;
    uint8_t save[256];
    M68KState saved = g_cpu;
    for (int i = 0; i < 256; i++) save[i] = g_ram[(byteoff + i) & 0xFFFFu];
    g_cpu.A[7] = STK;
    s_in_vblank_service = 1;
    uint32_t saved_rebase = g_68k_stamp_rebase;
    g_68k_stamp_rebase = machine_z80_stamp() - g_audio_cycle_counter * 7u;
    g_rte_pending = 0; g_rte_pending_ptr = &s_rte_dummy; g_rte_pending = 0;
    uint8_t saved_vb = vdp->in_vblank; vdp->in_vblank = 1;
    /* Charge the handler's executed 68K cycles to the raster debt (the
     * generated code bumps g_audio_cycle_counter per instruction, including
     * through the handler). See s_irq_cycle_debt. */
    uint32_t cyc_before = g_audio_cycle_counter;
#ifdef GEN_DEV_TRACE
    /* Snapshot the VBlank routine byte BEFORE the handler consumes it — the
     * giant-spin probe needs to know whether the dispatch input was valid. */
    uint8_t vbla_routine_at_entry =
        m68k_read8(g_game_layout.vint_routine_addr & 0xFFFFFF);
#endif
    if (g_game_spec.call_vblank) g_game_spec.call_vblank();
    s_irq_cycle_debt += g_audio_cycle_counter - cyc_before;
    if (s_irq_cycle_debt)
        s_irq_cycle_debt_level = 6;
#ifdef GEN_DEV_TRACE
    /* [IRQ-DEBT] a V-int handler owing more than one full frame of raster
     * debt freezes the main loop for multiple wall frames (and with it the
     * SMPS driver if the next V-ints get latched away) — dump it. For the
     * multi-million-cycle pathological case, also dump the crash-report
     * block ring: the recent recompiled function entries localize WHERE
     * the handler spun. */
    if (s_irq_cycle_debt > 127856u) {
        extern unsigned long g_snd_frame;
        uint32_t hc = (uint32_t)(g_audio_cycle_counter - cyc_before);
        fprintf(stderr, "[IRQ-DEBT] wf=%lu handler_cycles=%u debt=%u (~%u frames) vbla_routine=$%02X gmode=$%02X\n",
                g_snd_frame, hc,
                (unsigned)s_irq_cycle_debt, (unsigned)(s_irq_cycle_debt / 127856u),
                vbla_routine_at_entry,
                m68k_read8(g_game_layout.game_mode_addr & 0xFFFFFF));
        /* The one KNOWN legit multi-million-cycle handler is the Sega-screen
         * PCM scream (routine $14) — real hardware busy-feeds the DAC for
         * ~2s inside the V-int with everything else frozen. Only dump the
         * block ring for giants that are NOT that. */
        if (hc > 1000000u && vbla_routine_at_entry != 0x14u) {
            extern void crash_report_dump(FILE *, const char *, const M68KState *,
                                          uint32_t, int, uint64_t);
            crash_report_dump(stderr, "giant V-int handler (IRQ-DEBT probe)",
                              &g_cpu, 0, 0, g_frame_count);
        }
    }
#endif
    g_68k_stamp_rebase = saved_rebase;
    vdp->in_vblank = saved_vb;
    g_rte_pending_ptr = &s_rte_real; g_rte_pending = 0;
    s_in_vblank_service = 0;
    for (int i = 0; i < 256; i++) g_ram[(byteoff + i) & 0xFFFFu] = save[i];
    g_cpu = saved;
    s_game_yielded_vblank = 0;
}

/* Own-backend interrupt delivery — the clownmdemu-free twin of
 * glue_handle_interrupt: runs the game's V-int(6)/H-int(4) handler using OUR
 * work RAM (g_ram) for the IRQ-stack save/restore and OUR VDP's vblank flag. */
void glue_own_interrupt(int level, GVDP *vdp)
{
    if (!s_game_running) return;
    int imask = (g_cpu.SR >> 8) & 7;

    if (level == 6) {
        /* Fire V_Int once per wall frame, exactly like hardware — NOT only when
         * the game has parked at WaitForVBlank. If the 68K currently has IRQs
         * masked, latch it and deliver when the mask drops instead of losing it. */
        if (s_irq_cycle_debt && s_irq_cycle_debt_level >= 6) {
            /* The prior V-int still occupies the CPU on the raster timeline.
             * Multiple assertions merge into one level-triggered pending
             * interrupt, just as they do while SR masks level 6. */
            s_own_vint_latched = 1;
        } else if (imask < 6) { s_own_vint_latched = 0; own_deliver_vint(vdp); }
        else           {
#ifdef GEN_DEV_TRACE
            /* [VINT-MASK] V-int latched because the main-context 68K has IRQs
             * masked at the vblank line. Consecutive masked frames merge into
             * ONE delivery (hardware semantics) — so long masked spans are
             * where v_vblank_count freezes and the SMPS driver stalls. Rare
             * event print: each occurrence is one lost-or-merged V-int. */
            { extern unsigned long g_snd_frame;
              fprintf(stderr, "[VINT-MASK] wf=%lu SR=%04X PC=%08X latched=%d\n",
                      g_snd_frame, (unsigned)g_cpu.SR, (unsigned)g_cpu.PC,
                      s_own_vint_latched); }
#endif
            s_own_vint_latched = 1;
        }
    } else if (level == 4 && imask < 4) {
        const uint32_t STK = g_game_layout.intr_stack;
        uint32_t byteoff = (STK - 256u) & 0xFFFFu;
        uint8_t save[256];
        M68KState saved = g_cpu;
        for (int i = 0; i < 256; i++) save[i] = g_ram[(byteoff + i) & 0xFFFFu];
        g_cpu.A[7] = STK;
        s_in_vblank_service = 1;
        /* Same audio-stamp re-base as own_deliver_vint: H-int handler chip
         * writes stamp at the delivery raster, not the parked counter. */
        uint32_t saved_rebase = g_68k_stamp_rebase;
        g_68k_stamp_rebase = machine_z80_stamp() - g_audio_cycle_counter * 7u;
        g_rte_pending = 0; g_rte_pending_ptr = &s_rte_dummy; g_rte_pending = 0;
        /* H-int handler cycles owe raster time too (same rule as V-int). */
        uint32_t cyc_before = g_audio_cycle_counter;
        if (g_game_spec.call_hblank) g_game_spec.call_hblank();
        s_irq_cycle_debt += g_audio_cycle_counter - cyc_before;
        if (s_irq_cycle_debt && s_irq_cycle_debt_level < 4)
            s_irq_cycle_debt_level = 4;
        g_rte_pending_ptr = &s_rte_real; g_rte_pending = 0;
        g_68k_stamp_rebase = saved_rebase;
        s_in_vblank_service = 0;
        for (int i = 0; i < 256; i++) g_ram[(byteoff + i) & 0xFFFFu] = save[i];
        g_cpu = saved;
    }
}

/* Deliver a previously-latched V-int if the 68K mask now permits. Called by the
 * scheduler at each scanline boundary (after the 68K has advanced and may have
 * dropped its mask). Returns 1 if it fired. Delivers at most one — matching the
 * single level-triggered VDP V-int line (a masked span across >1 vblank still
 * yields exactly one V-int when unmasked, same as hardware/clownmdemu). */
int glue_own_vint_service_latched(GVDP *vdp)
{
    if (!s_own_vint_latched || !s_game_running) return 0;
    if (s_irq_cycle_debt && s_irq_cycle_debt_level >= 6) return 0;
    if (((g_cpu.SR >> 8) & 7) >= 6) return 0;   /* still masked — keep latched */
    s_own_vint_latched = 0;
    own_deliver_vint(vdp);
    return 1;
}
#endif /* OWN_BACKEND */

/* Yield-site cycle-accumulator log.  Each line records the state of
 * g_cycle_accumulator at the moment the game fiber yields for VBlank.
 * This captures native's belief about how many 68K cycles it spent
 * executing since the last frame reset — including any sub-VBla work
 * before the game's own WaitForVBlank call.  Paired oracle run writes
 * equivalent data (interpreter master_cycle counter at same yield).
 * Column layout:
 *   frame cycle_acc v_vblank_count vbla_routine
 */
FILE *g_yield_log_file = NULL;
extern uint16_t m68k_read16(uint32_t);
extern uint8_t  m68k_read8(uint32_t);
extern uint32_t m68k_read32(uint32_t);

/* Called from each game's WaitForVBla function (pattern-detected in
 * code_generator.c; see Sonic 1 $0029A8, Sonic 2 $003384, etc.):
 * yield to main loop for one frame. */
void glue_yield_for_vblank(void)
{
    if (s_in_vblank_service)
        return;
    s_watchdog_counter = 0;
    if (g_yield_log_file) {
        uint32_t vbc = m68k_read32(g_game_layout.vint_runcount_addr & 0xFFFF);
        uint8_t  vr  = m68k_read8 (g_game_layout.vint_routine_addr  & 0xFFFF);
        fprintf(g_yield_log_file,
                "%llu %u %u %u\n",
                (unsigned long long)g_frame_count,
                g_cycle_accumulator,
                vbc,
                vr);
    }
    /* At yield, the game is inside WaitForVBla (pattern-detected in
     * code_generator.c), which was called via JSR from the main loop.
     * Expected A7 at yield is initial_SSP - 8: one return address for
     * the JSR to WaitForVBla, plus 4 more for the dispatch JSR. But
     * A7 can drift above initial_SSP if a mode handler's internal
     * restart bypasses the dispatch's A7 pop, accumulating +4 per
     * restart. Clamp to prevent stack/variable collision. */
    if (g_cpu.A[7] > g_game_layout.initial_ssp)
        g_cpu.A[7] = g_game_layout.initial_ssp;
    s_game_yielded_vblank = 1;
    { char stack_marker; game_stack_note("WaitForVint", &stack_marker); }
    fiber_switch(s_main_fiber);
    /* Resumed here when next frame's DoCycles calls glue_run_game_chunk.
     *
     * Simulate WaitForVBlank polling overhead: the real 68K spins in a
     * tst.b/bne.s loop (~18 cycles per iteration) until VBlank fires.
     * This consumes ~10,000-20,000 cycles of the frame budget.  Without
     * this penalty, the game gets extra cycles → runs too fast →
     * transitions too quick → more BSRs per frame than real hardware.
     *
     * Set accumulator to simulate that VBlank fired at scanline 224
     * and the game wasted cycles polling until the handler cleared $F62A. */
    /* If VBlank hasn't fired yet this frame (game yielded early, e.g. during
     * init when frames are very short), fire it now.  This matches the
     * interpreter where WaitForVBlank polls until VBlank fires — the game
     * doesn't continue until the handler has run.
     *
     * glue_check_vblank now requires accumulator >= threshold to fire, so
     * bump the accumulator just past threshold to force one fire here. */
    if (!s_vblank_fired_this_frame) {
        if (g_cycle_accumulator < g_vblank_threshold)
            g_cycle_accumulator = g_vblank_threshold;
        glue_check_vblank();
        s_vblank_fired_this_frame = 1;  /* ensure it's marked */
    }

    /* In FIBER_FULL: reset accumulator at game-frame boundary so the next
     * game frame starts with a fresh cycle budget. Do NOT reset the
     * s_vblank_fired_this_frame latch here — it's a per-WALL-frame latch
     * (wall frame != game frame; a game can yield multiple times per wall
     * frame under heavy compute). Only glue_end_of_wall_frame resets it. */
    if (g_pacing_mode == GLUE_PACING_FIBER_FULL) {
        g_cycle_accumulator = 0;
    }

    /* PLC tile processing — periodic hook (Sonic 1: RunPLC).
     * Gated on the PLC pending counter being non-zero so games that
     * don't run the SMPS-style PLC system pay nothing. plc_pending=0
     * disables the gate and the hook never fires (Sonic 2 path). */
    {
        extern uint16_t m68k_read16(uint32_t);
        if (g_game_spec.call_periodic && g_game_layout.plc_pending_addr &&
            m68k_read16(g_game_layout.plc_pending_addr & 0xFFFF) != 0) {
            M68KState plc_save = g_cpu;
            g_game_spec.call_periodic();
            g_cpu = plc_save;
        }
    }
}

/* Yield from a short hardware-polling loop without declaring a game-frame
 * boundary. This lets clownmdemu advance to the next scanline and deliver
 * IRQ4/HBlank or update device status while the game remains in the same
 * frame. True WaitForVint sites still use glue_yield_for_vblank(). */
void glue_yield_for_interrupt_poll(void)
{
    if (s_in_vblank_service || !s_main_fiber)
        return;

    s_watchdog_counter = 0;
    { char stack_marker; game_stack_note("irq-poll", &stack_marker); }
    fiber_switch(s_main_fiber);
}

/* Called from main loop: start the game frame. With interleave mode,
 * the game runs in small chunks during Iterate's DoCycles calls.
 * Without interleave, it runs until WaitForVBlank as before. */
void glue_run_game_frame(void)
{
#if OWN_BACKEND
    /* Own backend: the game fiber is woken exactly ONCE per wall frame, by the
     * V-int at the vblank scanline (glue_own_interrupt) — like hardware, where
     * V-int is what releases the WaitForVBlank spin. Clearing the yield flag
     * here too would add a SECOND wake per frame (frame-start AND vblank), so a
     * light main-loop iteration (Sega screen, "Sonic Team presents", title
     * finger-wag) would complete ~2x per wall frame while heavy gameplay frames
     * stayed ~1x — the "intros/finger-wag run fast, gameplay looks right"
     * symptom. The first frame still runs: s_game_yielded_vblank starts 0. */
#else
    s_game_yielded_vblank = 0;
    /* Don't switch to game fiber here — DoCycles will do it during Iterate.
     * But we need to handle the first frame and any code that runs before
     * the first DoCycles call. */
#endif
}

/* Service VBlank: called from main loop AFTER Iterate.
 * Resumes game fiber so handlers + PLC run, then does joypad + bookkeeping. */
void glue_service_vblank(void)
{
    /* Handlers now fire from glue_check_vblank (contextual recompiler)
     * at the exact cycle count. No handler here — just bookkeeping. */

#if !OWN_BACKEND
    /* Own backend leaves the yield flag owned solely by glue_yield_for_vblank
     * (sets it) and glue_own_interrupt (clears it at the vblank scanline).
     * Resetting it here would re-introduce the frame-start wake — see
     * glue_run_game_frame(). */
    s_game_yielded_vblank = 0;
#endif

    glue_reset_frame_sync();

    /* Joypad copy REMOVED — the VBlank handler's ReadJoypads handles
     * $F602/$F603 natively. Our manual copy was overwriting $F603
     * (pressed-this-frame) after the handler set it, causing a
     * one-frame delay in button edge detection. */

    glue_log_frame_state(g_frame_count);
    g_frame_count++;
}

#if SONIC_REVERSE_DEBUG
void glue_yield_for_break(void)
{
    /* Block-entry hook in the game fiber decided to park. We're at a
     * label boundary, so g_cpu and g_ram are consistent. Yield to main
     * fiber — main loop will drain cmd_server until a resume command
     * arrives, then fiber_switch(s_game_fiber) re-enters here and we
     * continue the interrupted function. Clear the flag on return so
     * the next yield can be detected. */
    s_game_yielded_break = 1;
    fiber_switch(s_main_fiber);
    s_game_yielded_break = 0;
}

int glue_game_yielded_for_break(void)
{
    return s_game_yielded_break;
}

void glue_resume_from_break(void)
{
    if (!s_game_running || !s_game_fiber) return;
    fiber_switch(s_game_fiber);
    /* Returns here when the game fiber yields again (any reason). */
}
#endif

#endif /* ENABLE_RECOMPILED_CODE */

/* In hybrid mode, VBlank is handled by the interpreter — yield is a no-op. */
#if !ENABLE_RECOMPILED_CODE
void glue_yield_for_vblank(void) { /* stub */ }
void glue_yield_for_interrupt_poll(void) { /* stub */ }
#if SONIC_REVERSE_DEBUG
/* Tier 2 is native-only. Oracle never enters recompiled C so block-entry
 * hooks don't fire, but the symbols must exist for reverse_debug.c to
 * link into both builds. */
void glue_yield_for_break(void) { /* native-only; unreachable from oracle */ }
int  glue_game_yielded_for_break(void) { return 0; }
void glue_resume_from_break(void) { /* native-only */ }
#endif
#endif

/* Hybrid dispatch is now handled via the pre-instruction hook in
 * clown68000.c.  See hybrid.c / HybridInit(). */

/* =========================================================================
 * glue_init / glue_signal_* / glue_wait_vblank_done / glue_shutdown
 * ========================================================================= */

void glue_init(ClownMDEmu *emu, const cc_u8l *rom_bytes, cc_u32l rom_byte_len)
{
    s_emu = emu;

#if !OWN_BACKEND
    /* Build the CPUCallbackUserData clownmdemu expects */
    memset(&s_cpu_data, 0, sizeof(s_cpu_data));
    s_cpu_data.clownmdemu = emu;

    /* Wire up cycle_countdown pointers so Z80/DMA sync doesn't deref NULL */
    s_cpu_data.sync.z80.cycle_countdown          = &emu->state.z80.cycle_countdown;
    s_cpu_data.sync.vdp_dma_transfer.cycle_countdown =
        &emu->state.vdp_dma_transfer_countdown;
#endif

    /* Copy ROM bytes into g_rom so recompiled code can inspect ROM data
     * directly (e.g. tables copied from ROM to RAM at startup). */
    if (rom_bytes && rom_byte_len) {
        cc_u32l copy_len = rom_byte_len < sizeof(g_rom) ? rom_byte_len : sizeof(g_rom);
        memcpy(g_rom, rom_bytes, copy_len);
    }

#if HYBRID_RECOMPILED_CODE
    /* Install the pre-instruction dispatch hook. */
    HybridInit(emu);
    /* JMP table interpreter fallback. */
    {
        extern void hybrid_jmp_init(ClownMDEmu *emu, CPUCallbackUserData *cpu_data);
        hybrid_jmp_init(emu, &s_cpu_data);
    }
    VerifyInit(emu, &s_cpu_data);
#endif

#if ENABLE_RECOMPILED_CODE
    g_step2_active = 1;
    s_main_fiber = fiber_convert_thread();
    if (!s_main_fiber) {
        fprintf(stderr, "glue: fiber_convert_thread failed\n");
        return;
    }
    if (!create_game_fiber(0)) {
        return;
    }
    fprintf(stderr, "[fiber] game stack reserve=%u commit=%u bytes\n",
            GAME_FIBER_STACK_RESERVE, GAME_FIBER_STACK_COMMIT);
#endif
}

void glue_signal_vblank(void)
{
    /* In single-threaded Step 2, VBlank is delivered explicitly
     * by the main loop — this function is no longer needed. */
}

void glue_signal_hblank(void)
{
    /* HBlank is handled via the interrupt mask check inside service_vblank().
     * No additional signalling needed here. */
    (void)0;
}

/* Contextual recompiler: called from generated code when cycle accumulator
 * crosses the VBlank threshold.  Fires VBlank handler between instructions
 * on the game fiber — matching the interpreter's interrupt behavior.
 *
 * Previously gated by s_vblank_fired_this_frame, causing native to fire at
 * most ONE VBla per wall frame.  Measured consequence: heavy boot / init
 * blocks that execute N × threshold cycles of 68K work in a single wall
 * frame generated 1 VBla fire on native vs N fires on hardware/oracle,
 * making native's game_state-per-VBla-count overshoot.  ISSUE-003 round 6.
 *
 * New behavior: consume threshold-worth of cycles per fire.  Re-fire while
 * accumulator still has threshold-worth available.  s_in_vblank_service
 * prevents recursion from handler-internal accumulator crosses. */
uint64_t g_cvblank_fires_total = 0;

/* Fire the VBla handler once. Caller manages g_cycle_accumulator per
 * mode (FIBER_FULL subtracts threshold per fire; CYCLE_ACCURATE leaves
 * the accumulator running until the wall-frame cap is hit). The
 * Stage-A instrumentation hook records the fire for telemetry.
 * Hybrid/oracle only: the own backend fires V-int from the scanline
 * scheduler (glue_own_interrupt) and never takes this path. */
#if !OWN_BACKEND
static void fire_vblank_handler_once(void)
{
    s_vblank_fired_this_frame = 1;
    g_cvblank_fires_total++;
    { static int s_cv = 0; if (s_cv < 50) { s_cv++;
      fprintf(stderr, "[CVBLANK] fired at cycle %u (frame %"PRIu64") [#%llu]\n",
              g_cycle_accumulator, g_frame_count,
              (unsigned long long)g_cvblank_fires_total); } }

    int imask = (g_cpu.SR >> 8) & 7;
#if SONIC_REVERSE_DEBUG
    {
        extern void rdb_record_vbla_fire(uint32_t, uint64_t, int);
        rdb_record_vbla_fire(g_cycle_accumulator, g_frame_count,
            imask >= 6 ? 1 /*SUPPRESSED*/ : 0 /*THRESHOLD*/);
    }
#endif
    if (imask >= 6)
        return;  /* interrupts masked — cycles consumed, handler suppressed */

    M68KState saved = g_cpu;

    /* Per-game VBlank-handler stack (game_layout.vbla_stack). See the
     * commentary at the IRQ-stack site above; same shape, same
     * per-game caveats. Sonic 2 must NOT use $FFD000 — Object_RAM_End
     * lives there and the save window would land inside the dynamic
     * object table. */
    const uint32_t VBLK_STACK_ADDR = g_game_layout.vbla_stack;
    #define VBLK_SAVE  128
    cc_u16l vblk_ram[VBLK_SAVE];
    uint32_t vbase = ((VBLK_STACK_ADDR - VBLK_SAVE * 2) & 0xFFFF) / 2;
    for (int i = 0; i < VBLK_SAVE; i++)
        vblk_ram[i] = s_emu->state.m68k.ram[vbase + i];

    g_cpu.A[7] = VBLK_STACK_ADDR;
    s_in_vblank_service = 1;
    g_rte_pending = 0;
    g_rte_pending_ptr = &s_rte_dummy;
    g_rte_pending = 0;
    uint32_t acc_saved = g_cycle_accumulator;
    /* Pin VDP VBlank flag while the handler runs — see glue_run_irq
     * for rationale. Sonic 2's V_Int reads $C00004 expecting bit 3
     * set; without this it spins forever. */
    cc_bool saved_vblank_flag = s_emu->vdp.state.currently_in_vblank;
    s_emu->vdp.state.currently_in_vblank = cc_true;
    if (g_game_spec.call_vblank) g_game_spec.call_vblank();
    s_vblank_executed_this_frame = 1;
    s_emu->vdp.state.currently_in_vblank = saved_vblank_flag;
    g_cycle_accumulator = acc_saved;
    g_rte_pending_ptr = &s_rte_real;
    g_rte_pending = 0;
    s_in_vblank_service = 0;

    for (int i = 0; i < VBLK_SAVE; i++)
        s_emu->state.m68k.ram[vbase + i] = vblk_ram[i];

    g_cpu = saved;
}
#endif /* !OWN_BACKEND */

void glue_check_vblank(void)
{
    instruction_watchdog_check();

#if OWN_BACKEND
    /* Own backend: the scanline scheduler (machine_run_frame) is the SOLE
     * V-int driver, via glue_own_interrupt() — which uses our g_ram for the
     * handler stack save/restore. Per-scanline and per-frame fiber yielding
     * is handled by check_cycle_budget() and glue_yield_for_vblank(). Firing
     * the handler here too would run V_Int twice per wall frame (this
     * threshold path PLUS the scheduler path), doubling everything the
     * handler drives — music tempo, sprite-animation timers, v_vblank_count —
     * while main-loop object physics stays 1x. That mismatch is the
     * "some animations run too fast, some look right" symptom. It would also
     * run V_Int against the wrong RAM buffer (fire_vblank_handler_once saves
     * s_emu->state.m68k.ram, but recompiled code mutates g_ram here). So under
     * the own backend this routine does watchdog bookkeeping only —
     *
     * EXCEPT the non-yielding-loop safety: the own backend yields the 68K fiber
     * only at recompiler-emitted yield sites (WaitForVBlank). A tight loop with
     * no such site — e.g. Rocket Knight's sound-init busy-wait for the Z80 to
     * raise its ready flag — would never let the scanline scheduler run, so the
     * Z80 never steps, the flag never sets, and the loop deadlocks until the
     * instruction watchdog kills it. If the game has burned more than two wall
     * frames of 68K cycles without yielding, force the DESIGNED cycle-budget
     * yield (check_cycle_budget -> fiber_switch to the scheduler), which steps
     * the Z80 and lets the scheduler fire V-int via its own path (no doubling).
     * Well-behaved games yield every frame (g_cycle_accumulator resets in
     * glue_end_of_wall_frame) and never reach this. */
    if (s_interleave_active && !s_in_vblank_service
        && g_cycle_accumulator >= 2u * NTSC_CYCLES_PER_WALL_FRAME) {
        check_cycle_budget();
    }
    return;
#else
    if (s_in_vblank_service)
        return;  /* already servicing — don't re-enter from handler's own accumulator */

    if (g_pacing_mode == GLUE_PACING_CYCLE_ACCURATE) {
        /* Accurate mode: fire once per wall frame at threshold crossing,
         * then cap game-fiber execution at NTSC_CYCLES_PER_WALL_FRAME —
         * yield to main at that point, matching hardware's wall-clock-
         * paced 68K execution. */
        if (!s_vblank_fired_this_frame &&
            g_cycle_accumulator >= g_vblank_threshold) {
            fire_vblank_handler_once();
        }
        if (g_cycle_accumulator >= NTSC_CYCLES_PER_WALL_FRAME) {
#if ENABLE_RECOMPILED_CODE
            /* Game has consumed a full NTSC frame's worth of cycles —
             * yield the fiber. Carry the excess over to next wall frame.
             * Native-only: oracle doesn't have a game fiber. */
            g_cycle_accumulator -= NTSC_CYCLES_PER_WALL_FRAME;
            s_game_yielded_vblank = 1;
            fiber_switch(s_main_fiber);
#endif
        }
        return;
    }

    /* FIBER_FULL mode: cap at ONE handler fire per wall frame. Latch
     * prevents multi-fire when heavy-compute frames accumulate past
     * 2x threshold before the game next yields. Excess cycles are
     * still subtracted from the accumulator (budget is spent), but
     * the handler only runs once — matching hardware's 1-VBla-per-
     * wall-frame invariant. Latch is cleared by glue_end_of_wall_frame. */
    while (g_cycle_accumulator >= g_vblank_threshold) {
        g_cycle_accumulator -= g_vblank_threshold;
        if (!s_vblank_fired_this_frame)
            fire_vblank_handler_once();
    }
#endif /* OWN_BACKEND */
}

void glue_end_of_wall_frame(void)
{
    /* Hardware fires VBla every wall frame regardless of what 68K code
     * is doing. If nothing has fired the handler this wall frame (game
     * accumulator didn't reach threshold AND the yield path didn't
     * trigger — e.g., boot ROM copy or a pathological non-yielding
     * loop), force one fire now. Works in both pacing modes; ensures
     * v_vblank_count and any other VBla-handler side effects advance
     * every wall frame. */
    if (!s_vblank_fired_this_frame) {
        if (g_cycle_accumulator < g_vblank_threshold)
            g_cycle_accumulator = g_vblank_threshold;
        glue_check_vblank();
    }
    /* Per-frame cycle-pacing telemetry. Captured into the always-on
     * FrameRecord ring (PaceSnap) so divergence_diff queries it
     * retroactively via frame_timeseries — no per-frame fprintf. The
     * "insns_delta vs audio_cyc" ratio surfaces slow-music symptoms
     * cleanly when comparing native and oracle. */
    {
        static uint64_t s_prev_insns     = 0;
        static uint64_t s_prev_bus_total = 0;
        uint64_t insns_now = g_native_insn_count;
        uint64_t bus_now   = s_bus_ring_total;
        g_pace_snap.insns_delta  = insns_now - s_prev_insns;
        g_pace_snap.bus_delta    = bus_now   - s_prev_bus_total;
        g_pace_snap.audio_cyc    = g_audio_cycle_counter;
        g_pace_snap.vblanks_fired = (uint8_t)s_vblank_executed_this_frame;
        s_prev_insns     = insns_now;
        s_prev_bus_total = bus_now;
    }
    /* Reset the per-wall-frame latches for the next wall frame. */
    s_vblank_fired_this_frame    = 0;
    s_vblank_executed_this_frame = 0;

    /* audio_mixer_drain consumes events stamped within the frame and now
     * legitimately LEAVES deferred events queued (multi-frame spreading of
     * giant-handler bursts like the Sega scream — see mixer.c). The old
     * "sanity" reset here would wipe them; the only remaining full reset
     * is save-state load (main.c), which really does want to drop the
     * interrupted frame's writes. */

    /* Reset audio cycle stamp for next wall frame. After Phase 5 switchover,
     * audio_mixer_drain() is called right before this so the queue has
     * already been consumed with the old stamps. */
    g_audio_cycle_counter = 0;
}

void glue_set_callbacks(const void *callbacks)
{
    /* The callbacks pointer that clownmdemu passed to Clown68000_DoCycles.
     * In Step 2 we use M68kReadCallback / M68kWriteCallback directly, so we
     * don't need these.  Stored for completeness. */
    (void)callbacks;
}

void glue_wait_vblank_done(void)
{
    /* In single-threaded Step 2, not needed — main loop drives everything. */
}

void glue_shutdown(void)
{
#if ENABLE_RECOMPILED_CODE
    if (s_game_fiber) {
        fiber_destroy(s_game_fiber);
        s_game_fiber = NULL;
    }
    /* s_main_fiber is the thread itself — revert it to clean up. */
    if (s_main_fiber) {
        fiber_revert_thread();
        s_main_fiber = NULL;
    }
    s_game_running = 0;
#endif
}

/* =========================================================================
 * Save state helpers — called from main.c F6/F7 handlers.
 * Saves/restores recompiled game state that lives outside ClownMDEmu.
 * ========================================================================= */

void glue_save_state(FILE *sf)
{
    fwrite(&g_cpu, 1, sizeof(g_cpu), sf);
    fwrite(&g_frame_count, 1, sizeof(g_frame_count), sf);
    fwrite(&g_cycle_accumulator, 1, sizeof(g_cycle_accumulator), sf);
    fwrite(&g_vblank_threshold, 1, sizeof(g_vblank_threshold), sf);
#if OWN_BACKEND
    /* A V-int latched across the save point (masked transition) must survive
     * the restore or the game loses one v_vblank_count. */
    { uint8_t latched = (uint8_t)s_own_vint_latched;
      fwrite(&latched, 1, 1, sf); }
#endif
}

void glue_load_state(FILE *sf)
{
    fread(&g_cpu, 1, sizeof(g_cpu), sf);
    fread(&g_frame_count, 1, sizeof(g_frame_count), sf);
    fread(&g_cycle_accumulator, 1, sizeof(g_cycle_accumulator), sf);
    fread(&g_vblank_threshold, 1, sizeof(g_vblank_threshold), sf);
#if OWN_BACKEND
    { uint8_t latched = 0;
      fread(&latched, 1, 1, sf);
      s_own_vint_latched = latched ? 1 : 0; }
#endif
}

void glue_restart_game_fiber(uint32_t resume_pc)
{
#if ENABLE_RECOMPILED_CODE
    if (!s_main_fiber)
        return;

    if (s_game_fiber) {
        fiber_destroy(s_game_fiber);
        s_game_fiber = NULL;
    }

    s_game_running = 0;
    s_game_yielded_vblank = 0;
#if SONIC_REVERSE_DEBUG
    s_game_yielded_break = 0;
#endif
    s_interleave_active = 0;
    s_cycle_budget = 0;
    s_chunk_cycles = 0;
    s_irq_cycle_debt = 0;
    s_irq_cycle_debt_level = 0;
    s_watchdog_counter = 0;
    s_vblank_fired_this_frame = 0;
    s_vblank_executed_this_frame = 0;
    g_cycle_accumulator = 0;
    g_audio_cycle_counter = 0;
    s_game_stack_top = 0;
    s_game_stack_last_report = 0;
    g_rte_pending = 0;

    if (create_game_fiber(resume_pc)) {
        fprintf(stderr, "[fiber] restarted game fiber at $%06X\n",
                (unsigned)(resume_pc & 0xFFFFFFu));
    }
#else
    (void)resume_pc;
#endif
}

/* =========================================================================
 * Memory access — route through clownmdemu's bus layer (M68kReadCallback /
 * M68kWriteCallback), which handles ROM, work RAM, VDP, IO, Z80 bus, etc.
 * ========================================================================= */

/* Cycle counter for bus timing.
 *
 * clownmdemu's M68kRead/WriteCallback receives current_cycle (68K cycles)
 * and computes target_cycle = base_cycle + current_cycle * 7 (master cycles).
 * This drives VDP/Z80/FM/PSG sync — realistic timing is critical for:
 *   - DMA completion (collision data, art loading)
 *   - Z80 sound driver advancement (audio quality)
 *   - FM/PSG sample generation (audio timing)
 *
 * The 68K runs at ~7.67 MHz (master / 7).  A typical instruction takes
 * 4-20 cycles with ~1.5 bus accesses.  Average cycles per bus access ≈ 8.
 * We reset to 0 at frame boundaries so cycle values stay within one frame. */
/* Cycle tracking for clownmdemu sync timing.
 *
 * Iterate calls DoCycles(N) per scanline (~488 68K cycles).  We distribute
 * these cycles across bus accesses proportionally: each access advances
 * g_hybrid_cycle_counter by (budget / expected_accesses_per_chunk).
 *
 * With ~48 bus accesses per 488-cycle chunk (68K averages ~10 cycles per
 * access including non-bus instructions), we use budget/48 ≈ 10 per access.
 * This keeps g_hybrid_cycle_counter aligned with Iterate's scanline timing. */
#define CYCLES_PER_BUS_ACCESS 10u
#if ENABLE_RECOMPILED_CODE
#define HYBRID_BUMP_CYCLES() do { g_hybrid_cycle_counter += CYCLES_PER_BUS_ACCESS; check_cycle_budget(); } while(0)
#else
#define HYBRID_BUMP_CYCLES() do { g_hybrid_cycle_counter += CYCLES_PER_BUS_ACCESS; } while(0)
#endif

/* Audio event-queue detour.
 *
 * All audio-bus writes are captured inside clownmdemu (bus-z80.c for FM,
 * bus-main-m68k.c for PSG) where target_cycle is available as a single
 * consistent timestamp (68K-equivalent cycles since Iterate start).
 * That path catches 68K-bus FM writes (which clownmdemu internally routes
 * through the Z80 bus), Z80-native FM writes (SMPS Z80 driver for DAC +
 * part-2 channels), AND PSG writes. No m68k_write detour needed here. */
static inline void audio_detour_write(uint32_t byte_addr, uint8_t value)
{
    (void)byte_addr; (void)value;
}

/*
 * Spin-detect-and-yield: when the game fiber reads the same address
 * repeatedly without either writing, reading a different address, or
 * yielding for VBlank, force a fiber yield so the main loop can
 * advance clownmdemu (which may then update the value the spin is
 * waiting on — VDP DMA-busy clears, Z80 bus-request resolves, etc.).
 *
 * This catches any busy-wait pattern in recompiled code without
 * needing a per-pattern detector. Invariant: a legitimate access
 * sequence reads multiple addresses or interleaves reads with
 * writes; only true spin loops repeat the same address indefinitely.
 *
 * The threshold balances false positives (too low — yields during
 * legit hot loops touching one address) against latency (too high
 * — game spends millions of bus accesses spinning before unjamming).
 * Set high enough to clear normal data-shuffling loops, low enough
 * to react before the watchdog kills us.
 */
#define SPIN_YIELD_THRESHOLD 256
static uint32_t s_spin_addr = 0;
static int      s_spin_count = 0;

static inline void spin_check(uint32_t byte_addr, int is_write)
{
#if ENABLE_RECOMPILED_CODE
    if (is_write || s_in_vblank_service) {
        s_spin_addr  = 0;
        s_spin_count = 0;
        return;
    }
    if (byte_addr == s_spin_addr) {
        if (++s_spin_count > SPIN_YIELD_THRESHOLD) {
            /* Yield to the main fiber so DoCycles ticks clownmdemu;
             * resume returns here and the subsequent read sees a
             * possibly-updated value. Reset the streak so we don't
             * yield on every single read once over threshold — the
             * yield itself is the act we want, not a repeated one.
             * s_main_fiber is the file-static set in glue_install_*. */
            if (s_main_fiber)
                { char stack_marker; game_stack_note("spin-read", &stack_marker); }
            if (s_main_fiber)
                fiber_switch(s_main_fiber);
            s_spin_count = 0;
        }
    } else {
        s_spin_addr  = byte_addr;
        s_spin_count = 1;
    }
#else
    (void)byte_addr; (void)is_write;
#endif
}

#if PERMISSIVE_VDP
#include "vdp_integration.h"   /* clean-room VDP shadow seam (toggle build) */
/* Authoritative word read for the shadow VDP's DMA source: ROM from g_rom,
 * live work RAM from clownmdemu's state.m68k.ram (the word array the recompiled
 * code actually uses — g_ram is only a non-authoritative shadow). Phase-2 will
 * point RAM at our own memory once clownmdemu is gone. */
uint16_t glue_bus_read_word(uint32_t addr)
{
    addr &= 0xFFFFFFu;
    if (addr >= RAM_BASE)
        return s_emu ? (uint16_t)s_emu->state.m68k.ram[(addr & 0xFFFFu) >> 1] : 0;
    if (addr < 0x400000u)
        return (uint16_t)((g_rom[addr] << 8) | g_rom[(addr + 1) & 0x3FFFFFu]);
    return 0;
}
#endif

uint16_t m68k_read16(uint32_t byte_addr)
{
    byte_addr &= 0xFFFFFFu;
#if ENABLE_RECOMPILED_CODE
    watchdog_check(byte_addr, 0, 0);
    bus_ring_push(byte_addr, 1);
    spin_check(byte_addr, 0);
#endif
    HYBRID_BUMP_CYCLES();
#if OWN_BACKEND
    return gbus_read16(&g_machine.bus, byte_addr);
#else
    uint16_t r16_result = (uint16_t)M68kReadCallback(&s_cpu_data,
                                       byte_addr >> 1,
                                       cc_true, cc_true,
                                       g_hybrid_cycle_counter);
    return r16_result;
#endif
}

/* IO port access logging for joypad debugging */
int s_io_log_enabled = 0;  /* set via TCP command */
int s_io_log_count   = 0;

unsigned long g_z80poll_fallback_hits = 0; /* [POLL-DIAG] 256-poll bound fired */
unsigned long g_z80poll_yields = 0;        /* [POLL-DIAG] total z80-poll yields */
uint8_t m68k_read8(uint32_t byte_addr)
{
    byte_addr &= 0xFFFFFFu;
#if ENABLE_RECOMPILED_CODE
    watchdog_check(byte_addr, 0, 0);
    bus_ring_push(byte_addr, 0);
    /* Z80 sync polls — the SMPS sound driver and Z80 bus arbiter both
     * require the Z80 to actually advance before responding. In native
     * mode the game fiber can run thousands of instructions without
     * yielding, so Z80 never gets cycles inside a polling loop.
     *
     * Old behavior: return constant 0 → 68K loop exits immediately,
     * but never actually waits for Z80 → SMPS commands queue up faster
     * than Z80 can drain them → notes drop ("squelching").
     *
     * New behavior: yield game fiber → Iterate's next DoCycles advances
     * Z80 by one scanline → resume → re-read the real Z80 RAM / bus
     * register. Loop self-paces against actual Z80 throughput.
     *
     * Bounded fallback: if a single read polls > 256 times without
     * resolving (corrupted Z80 state, dead driver), return 0 so we
     * don't hang. Counter resets across distinct read addresses. */
    static uint32_t last_poll_addr  = 0;
    static int      poll_streak     = 0;
    if (byte_addr == 0xA01FFDu || byte_addr == 0xA01FFFu || byte_addr == 0xA11100u) {
        if (s_interleave_active && !s_in_vblank_service) {
            if (byte_addr == last_poll_addr) {
                if (++poll_streak > 256) {
                    extern unsigned long g_z80poll_fallback_hits; /* [POLL-DIAG] */
                    g_z80poll_fallback_hits++;
                    return 0x00u;
                }
            } else {
                last_poll_addr = byte_addr;
                poll_streak    = 1;
            }
            { extern unsigned long g_z80poll_yields; g_z80poll_yields++; } /* [POLL-DIAG] */
            { char stack_marker; game_stack_note("z80-poll", &stack_marker); }
            fiber_switch(s_main_fiber);
            /* fall through to real read */
        } else {
            return 0x00u;  /* outside interleave: keep old shortcut */
        }
    } else {
        last_poll_addr = 0;
        poll_streak    = 0;
    }
#endif
    HYBRID_BUMP_CYCLES();
    cc_bool hi = (byte_addr & 1) == 0;
    cc_bool lo = !hi;
#if OWN_BACKEND
    cc_u16f word = gbus_read16(&g_machine.bus, byte_addr & ~1u);
    (void)lo;
#else
    cc_u16f word = M68kReadCallback(&s_cpu_data,
                                     byte_addr >> 1,
                                     hi, lo,
                                     g_hybrid_cycle_counter);
#endif
    uint8_t result = hi ? (uint8_t)(word >> 8) : (uint8_t)(word & 0xFF);
    if (s_io_log_enabled && byte_addr >= 0xA10000u && byte_addr <= 0xA1001Fu) {
        if (s_io_log_count < 200) {
            fprintf(stderr, "[IO-R] $%06X => 0x%02X (vblk=%d frame=%"PRIu64")\n",
                    byte_addr, result, s_in_vblank_service, g_frame_count);
            s_io_log_count++;
        }
    }
    return result;
}

uint32_t m68k_read32(uint32_t byte_addr)
{
    byte_addr &= 0xFFFFFFu;
#if ENABLE_RECOMPILED_CODE
    watchdog_check(byte_addr, 0, 0);
    bus_ring_push(byte_addr, 2);
    spin_check(byte_addr, 0);
#endif
    /* Bump once for the whole 32-bit op — both halves at same cycle.
     * Prevents VDP/Z80 sync between the two 16-bit reads. */
    HYBRID_BUMP_CYCLES();
#if OWN_BACKEND
    uint16_t hi = gbus_read16(&g_machine.bus, byte_addr);
    uint16_t lo = gbus_read16(&g_machine.bus, byte_addr + 2);
#else
    uint16_t hi = (uint16_t)M68kReadCallback(&s_cpu_data,
                                              byte_addr >> 1,
                                              cc_true, cc_true,
                                              g_hybrid_cycle_counter);
    uint16_t lo = (uint16_t)M68kReadCallback(&s_cpu_data,
                                              (byte_addr + 2) >> 1,
                                              cc_true, cc_true,
                                              g_hybrid_cycle_counter);
#endif
    return ((uint32_t)hi << 16) | (uint32_t)lo;
}

void m68k_write16(uint32_t byte_addr, uint16_t val)
{
    byte_addr &= 0xFFFFFFu;
#if ENABLE_RECOMPILED_CODE
    watchdog_check(byte_addr, 1, val);
    bus_ring_push(byte_addr, 4);
    /* 16-bit FM writes land one byte per port per cycle on hardware. The
     * YM2612 only takes 8-bit data; SMPS always uses 8-bit writes. Be
     * defensive: if a 16-bit write hits the FM bus, treat the low byte
     * as the meaningful one (matches clownmdemu's bus routing). */
    audio_detour_write(byte_addr, (uint8_t)val);
#endif
    HYBRID_BUMP_CYCLES();
#if OWN_BACKEND
    if (g_mem_write_trace_fn) {
        g_mem_write_trace_fn(byte_addr,      (uint8_t)(val >> 8), g_audio_cycle_counter);
        g_mem_write_trace_fn(byte_addr + 1u, (uint8_t)val,        g_audio_cycle_counter);
    }
    gbus_write16(&g_machine.bus, byte_addr, val);
#else
    M68kWriteCallback(&s_cpu_data,
                       byte_addr >> 1,
                       cc_true, cc_true,
                       g_hybrid_cycle_counter, (cc_u16f)val);
#endif
#if PERMISSIVE_VDP
    gvdp_on_bus_write(byte_addr, (uint16_t)val);
#endif
}

void m68k_write8(uint32_t byte_addr, uint8_t val)
{
    byte_addr &= 0xFFFFFFu;
#if ENABLE_RECOMPILED_CODE
    watchdog_check(byte_addr, 1, val);
    bus_ring_push(byte_addr, 3);
    audio_detour_write(byte_addr, val);
#endif
    if (s_io_log_enabled && byte_addr >= 0xA10000u && byte_addr <= 0xA1001Fu) {
        if (s_io_log_count < 200) {
            fprintf(stderr, "[IO-W] $%06X <= 0x%02X (vblk=%d frame=%"PRIu64")\n",
                    byte_addr, val, s_in_vblank_service, g_frame_count);
            s_io_log_count++;
        }
    }
    HYBRID_BUMP_CYCLES();
#if OWN_BACKEND
    if (g_mem_write_trace_fn)
        g_mem_write_trace_fn(byte_addr, val, g_audio_cycle_counter);
    gbus_write8(&g_machine.bus, byte_addr, val);
#else
    cc_bool hi = (byte_addr & 1) == 0;
    cc_bool lo = !hi;
    /* Replicate the byte on both halves of the word */
    cc_u16f word = (cc_u16f)val | ((cc_u16f)val << 8);
    M68kWriteCallback(&s_cpu_data,
                       byte_addr >> 1,
                       hi, lo,
                       g_hybrid_cycle_counter, word);
#endif
}

void m68k_write32(uint32_t byte_addr, uint32_t val)
{
    byte_addr &= 0xFFFFFFu;
#if ENABLE_RECOMPILED_CODE
    watchdog_check(byte_addr, 1, (uint32_t)(val >> 16));
    bus_ring_push(byte_addr, 5);
    /* 32-bit write = two consecutive 16-bit writes. Only matters for FM
     * bus; SMPS never uses 32-bit to write FM regs so this is defensive
     * only. */
    audio_detour_write(byte_addr,       (uint8_t)(val >> 16));
    audio_detour_write(byte_addr + 2,   (uint8_t)val);
#endif
    /* Bump once for the whole 32-bit op — both halves at same cycle.
     * Critical for VDP control port: the VDP latches a 32-bit command
     * from two consecutive 16-bit writes. If VDP sync runs between
     * them, the half-written command corrupts VDP state. */
    HYBRID_BUMP_CYCLES();
#if OWN_BACKEND
    if (g_mem_write_trace_fn) {
        g_mem_write_trace_fn(byte_addr,      (uint8_t)(val >> 24), g_audio_cycle_counter);
        g_mem_write_trace_fn(byte_addr + 1u, (uint8_t)(val >> 16), g_audio_cycle_counter);
        g_mem_write_trace_fn(byte_addr + 2u, (uint8_t)(val >> 8),  g_audio_cycle_counter);
        g_mem_write_trace_fn(byte_addr + 3u, (uint8_t)val,         g_audio_cycle_counter);
    }
    gbus_write16(&g_machine.bus, byte_addr,     (uint16_t)(val >> 16));
    gbus_write16(&g_machine.bus, byte_addr + 2, (uint16_t)(val & 0xFFFF));
#else
    M68kWriteCallback(&s_cpu_data,
                       byte_addr >> 1,
                       cc_true, cc_true,
                       g_hybrid_cycle_counter, (cc_u16f)(val >> 16));
    M68kWriteCallback(&s_cpu_data,
                       (byte_addr + 2) >> 1,
                       cc_true, cc_true,
                       g_hybrid_cycle_counter, (cc_u16f)(val & 0xFFFF));
#endif
#if PERMISSIVE_VDP
    gvdp_on_bus_write(byte_addr,     (uint16_t)(val >> 16));
    gvdp_on_bus_write(byte_addr + 2, (uint16_t)(val & 0xFFFF));
#endif
}

/* =========================================================================
 * Dispatch
 * ========================================================================= */

#if ENABLE_RECOMPILED_CODE
static void log_true_miss(uint32_t target_pc);  /* forward decl — defined below */
#else
static void log_true_miss(uint32_t target_pc) { (void)target_pc; }
#endif

/* Check if addr falls inside an existing compiled function's range.
 * Uses the dispatch table exported by game_dispatch_get_table(). */
static int is_interior_label(uint32_t addr)
{
    /* game_dispatch_get_table returns a NULL-terminated array of
     * {addr, fn} pairs sorted by address.  Check if addr falls
     * between two consecutive entries. */
    extern int game_dispatch_table_size(void);
    extern uint32_t game_dispatch_table_addr(int i);

    int count = game_dispatch_table_size();
    if (count == 0) return 0;

    /* Binary search for the largest entry <= addr */
    int lo = 0, hi = count - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (game_dispatch_table_addr(mid) <= addr)
            lo = mid;
        else
            hi = mid - 1;
    }

    uint32_t func_start = game_dispatch_table_addr(lo);
    if (func_start == addr)
        return 0;  /* exact match = it IS a function, not interior */
    if (func_start < addr) {
        /* addr is between func_start and the next function — interior label */
        return 1;
    }
    return 0;
}

/* Is the 68K instruction at `addr` a `bra.w` trampoline?  bra.w is encoded
 * as 0x6000 <disp16>; it has zero prerequisite state and zero fall-through,
 * so seeding its address as an extra_func always produces a correct
 * single-tail-call function body.  This distinguishes real jmp-table
 * trampolines (which MUST be callable) from true interior labels (loop
 * tops, conditional-branch joins) that are never valid JSR targets. */
static int is_bra_w_trampoline(uint32_t addr)
{
    /* m68k_read16 goes through the bus callback so this works for ROM
     * (< $800000) as well as RAM. */
    uint16_t opcode = m68k_read16(addr);
    return opcode == 0x6000u;
}

/* Re-entrancy guard for the floor. The interpreter is self-contained (it does
 * not re-enter dispatch), but never recurse the floor defensively. */
static int s_in_floor = 0;

#if ENABLE_RECOMPILED_CODE
/* ── Coverage manifest ──────────────────────────────────────────────────────
 * Records every in-ROM address the floor executed (the missed entry + the
 * JSR/BSR/JMP subtree it traversed) to floor_coverage.txt, deduplicated for the
 * session. These are LEADS to grow static coverage: validate each against the
 * disasm (PRINCIPLES.md #16), then fold confirmed entries into game.toml
 * [funcs] extra_funcs / the gen_disasm seed pipeline so the recompiler discovers
 * them and they become Tier-1 native. (Interior-label misses are deliberately
 * NOT here — they live in interior_label_misses.log and need a codegen fix, not
 * a seed.) The subtree matters because the interpreter runs callees inline, so
 * an undiscovered callee never logs its own dispatch miss — this is the only
 * place it surfaces. */
#define FLOOR_COV_MAX 8192
static uint32_t s_floor_cov[FLOOR_COV_MAX];
static int      s_floor_cov_count  = 0;
static int      s_floor_cov_header = 0;
static void floor_record_coverage(uint32_t addr)
{
    addr &= 0xFFFFFFu;
    uint32_t rl = g_game_spec.expected_rom_size
                      ? g_game_spec.expected_rom_size : (uint32_t)sizeof(g_rom);
    if (addr >= rl) return;
    for (int i = 0; i < s_floor_cov_count; i++) if (s_floor_cov[i] == addr) return;
    if (s_floor_cov_count >= FLOOR_COV_MAX) return;
    s_floor_cov[s_floor_cov_count++] = addr;

    extern const char *exe_relative(const char *);
    FILE *f = fopen(exe_relative("floor_coverage.txt"), "a");
    if (!f) return;
    if (!s_floor_cov_header) {
        s_floor_cov_header = 1;
        fprintf(f,
            "# floor_coverage.txt — addresses executed by the Tier-3 interpreter\n"
            "# floor (a missed function entry, or a JSR/BSR/JMP target in its\n"
            "# subtree). LEADS to grow static coverage: validate each against the\n"
            "# disasm (PRINCIPLES.md #16 — disasm is ground truth), then fold the\n"
            "# confirmed code entries into game.toml [funcs] extra_funcs / the\n"
            "# gen_disasm seed pipeline so they recompile to Tier-1 native.\n");
    }
    fprintf(f, "extra_func 0x%06X\n", addr);
    fclose(f);
}

/* Halt blacklist: a missed address the floor could not run (its first/early
 * bytes decode as illegal/F-line — i.e. the JMP-table dispatched to DATA, or a
 * misaligned PC). Re-running it every frame would just halt again and spam the
 * log, so remember it and decline silently thereafter. Declining is safe: it
 * is exactly the old no-op behaviour for that (non-code) target. */
#define FLOOR_BL_MAX 2048
static uint32_t s_floor_bl[FLOOR_BL_MAX];
static int      s_floor_bl_count = 0;
static int floor_blacklisted(uint32_t a) {
    for (int i = 0; i < s_floor_bl_count; i++) if (s_floor_bl[i] == a) return 1;
    return 0;
}
static void floor_blacklist_add(uint32_t a) {
    if (s_floor_bl_count < FLOOR_BL_MAX) s_floor_bl[s_floor_bl_count++] = a;
}

/* UNSAFE_EXIT receipt: the framed capsule ran a missed target to its depth-0
 * return, but that return did NOT land on the native loose-A7 return the caller
 * is about to pop (an unbalanced A7 — a skip-return / stack pivot — or a
 * mis-decode). Continuing would let native resume with a desynced stack. Per
 * the oracle-parity charter we never silently corrupt: record the full state
 * loudly so the first-divergence harness can classify it, then DECLINE (the old
 * no-op for that target). The capsule itself is A7-neutral, so declining leaves
 * the stack exactly as native expects. */
static int s_floor_unsafe_count = 0;
static void floor_unsafe_record(uint32_t miss_addr, uint32_t run_at,
                                uint32_t exit_pc, uint32_t expected_ret,
                                const char *why)
{
    s_floor_unsafe_count++;
    fprintf(stderr,
            "[FLOOR][UNSAFE] miss $%06X (ran $%06X): %s — exit_pc=$%06X "
            "expected_ret=$%06X A7=$%06X frame=%" PRIu64 " — declined\n",
            miss_addr, run_at, why, exit_pc, expected_ret,
            g_cpu.A[7] & 0xFFFFFFu, g_frame_count);

    extern const char *exe_relative(const char *);
    FILE *f = fopen(exe_relative("floor_unsafe.log"), "a");
    if (!f) return;
    fprintf(f,
            "miss=0x%06X run_at=0x%06X exit_pc=0x%06X expected_ret=0x%06X "
            "A7=0x%06X D0=0x%08X D1=0x%08X A0=0x%08X A1=0x%08X SR=0x%04X "
            "frame=%" PRIu64 " why=\"%s\"\n",
            miss_addr, run_at, exit_pc, expected_ret, g_cpu.A[7] & 0xFFFFFFu,
            g_cpu.D[0], g_cpu.D[1], g_cpu.A[0], g_cpu.A[1], g_cpu.SR,
            g_frame_count, why ? why : "");
    fclose(f);
}
/* Floor enable switch (GENESIS_FLOOR=1/on/yes). DEFAULT OFF: opt-in.
 *
 * The interpreter is validated 0-divergence vs clown68000 and works in-game on
 * Sonic 1 (it correctly executes interior-label "Duff's-device" misses that the
 * static dispatch can only no-op). But it does NOT yet model the per-instruction
 * cycle accounting + glue_check_vblank() that the generated native code emits,
 * so a floor run that should span a VBlank skips it — harmless for short
 * mid-game handlers, but it desyncs timing-sensitive code (Sonic 3&K froze at
 * frame ~2014 from a frame-5 boot miss; A/B-confirmed it's the floor's
 * EXECUTION, not pre-existing). Until that interaction is root-caused, the floor
 * ships OFF by default so no game regresses; enable per-game where validated. */
static int floor_enabled(void) {
    static int e = -1;
    if (e < 0) {
        const char *v = getenv("GENESIS_FLOOR");
        e = (v && (v[0] == '1' || v[0] == 'o' || v[0] == 'O' || v[0] == 'y' || v[0] == 'Y')) ? 1 : 0;
        if (e) fprintf(stderr, "[FLOOR] ENABLED via GENESIS_FLOOR=%s\n", v);
    }
    return e;
}
#endif

void genesis_log_dispatch_miss(uint32_t addr)
{
    g_miss_count_any++;
    g_miss_last_addr  = addr;
    g_miss_last_frame = g_frame_count;

#if ENABLE_RECOMPILED_CODE
    /* ── EDGE-AWARE TIER-3 FALLBACK ─────────────────────────────────────────
     * Every computed-dispatch miss — computed JSR, computed JMP-tail, and the
     * interior-label JMP (the Duff's-device codegen gap) — funnels here via
     * call_by_address. Instead of silently no-op'ing it (dead object code,
     * the gameplay-garble cause) we run the missed code on the A7-NEUTRAL
     * framed capsule (m68k_interp_run_framed): it runs the target to its
     * depth-0 return and PEEKS that return without popping, so the native
     * loose-A7 caller performs the single pop. That one capsule is correct for
     * all three miss shapes — and being A7-neutral it FIXES the interior-label
     * double-pop that the old single-model floor had to skip (the S3K freeze),
     * so no charter guard is needed.
     *
     * A RAM-resident target is resolved to its ROM destination first (the
     * capsule decodes from the ROM image only). A capsule exit that does NOT
     * land on the native loose-A7 return the caller is about to pop is an
     * UNSAFE_EXIT (unbalanced A7 — skip-return / stack pivot — or mis-decode):
     * recorded loudly and declined, never silently resumed (oracle-parity
     * charter: interpreter fallback is fine, silent corruption is defeat).
     *
     * Default OFF (GENESIS_FLOOR); enabled per game where validated. */
    if (floor_enabled() && !s_in_floor && !floor_blacklisted(addr)) {
        uint32_t rl = g_game_spec.expected_rom_size
                          ? g_game_spec.expected_rom_size : (uint32_t)sizeof(g_rom);
        /* The native loose-A7 return the caller (JSR site / enclosing JSR) is
         * about to pop — the capsule must return exactly here to be safe. */
        uint32_t expected_ret = m68k_read32(g_cpu.A[7]) & 0xFFFFFFu;
        uint32_t run_at = addr;

        /* RAM-resident computed target: follow the JMP/JSR trampoline chain to
         * a ROM entry the capsule can decode. If it stays in RAM the floor
         * cannot fetch it from the ROM image — record + decline (a real
         * RAM-code execution strategy is future work; a RAM target is never a
         * static native function). */
        if (run_at >= RAM_BASE) {
            uint32_t resolved = recomp_resolve_ram_trampoline(run_at) & 0xFFFFFFu;
            if (resolved < rl && !(resolved & 1u)) {
                run_at = resolved;
            } else {
                floor_unsafe_record(addr, run_at, resolved, expected_ret,
                                    "RAM target did not resolve to a ROM entry");
                floor_blacklist_add(addr);
                run_at = 0;  /* skip the capsule */
            }
        }

        if (run_at && run_at < rl && !(run_at & 1u)) {
            uint32_t exit_pc = 0;
            s_in_floor = 1;
            M68kiStatus st = m68k_interp_run_framed(run_at, &exit_pc);
            s_in_floor = 0;

            if (st == M68KI_OK) {
                int plausible = exit_pc && !(exit_pc & 1u) && exit_pc < rl;
                if (plausible && exit_pc == expected_ret) {
                    /* Clean balanced return to the native continuation. Manifest
                     * the entry + its call/jump subtree as real code leads. */
                    floor_record_coverage(addr);
                    for (int i = 0; i < g_m68ki_discover_count; i++)
                        floor_record_coverage(g_m68ki_discover[i]);
                    return;  /* handled; native caller performs the single A7 pop */
                }
                floor_unsafe_record(addr, run_at, exit_pc, expected_ret,
                                    "capsule exit_pc != native loose-A7 return");
                floor_blacklist_add(addr);
            } else {
                /* Not runnable as code (illegal/F-line first bytes => DATA
                 * target), or runaway/bad fetch. Decline + remember (the old
                 * no-op for that non-code target, which the game tolerates). */
                floor_blacklist_add(addr);
                fprintf(stderr, "[FLOOR] declined miss $%06X (ran $%06X, status %d, "
                        "opcode $%04X at $%06X) — target not runnable code; blacklisted\n",
                        addr, run_at, (int)st, g_m68ki_bad_op, g_m68ki_bad_pc);
            }
        }
    }
#endif

    /* TRUE interior labels — addresses inside an existing function but not
     * its entry. They are NEVER valid extra_func seeds (the recompiler
     * would split the parent function and produce broken code).  But they
     * ARE a real runtime failure: the recompiler punted some indirect
     * dispatch to hybrid_jmp_interpret -> call_by_address, and that
     * looked up an interior PC that isn't in the dispatch table.  This
     * is the JMP-into-uniform-sequence (Duff's device) class of bug.
     *
     * Log to a SEPARATE file + stderr so the failure is loud without
     * polluting dispatch_misses.log (which the recompiler consumes as
     * extra_func candidates).  bra.w trampolines fall through to the
     * regular path — they ARE valid extra_func seeds. */
    if (is_interior_label(addr) && !is_bra_w_trampoline(addr)) {
        /* Per-address dedup so we don't spam: same s_miss_unique_addrs[]
         * pool the regular-miss path uses (separate dedup would just
         * double the bookkeeping). */
        for (int i = 0; i < g_miss_unique_count; i++)
            if (g_miss_unique_addrs[i] == addr)
                return;
        if (g_miss_unique_count < MAX_MISS_UNIQUE)
            g_miss_unique_addrs[g_miss_unique_count++] = addr;

        fprintf(stderr,
                "[dispatch] interior-label miss: $%06X inside func $%06X "
                "at frame %" PRIu64 " — likely JMP-table into uniform "
                "instruction sequence (e.g. Duff's device). Recompiler "
                "should emit an in-function switch, not call_by_address.\n",
                addr, g_rdb_current_func, g_frame_count);

        extern const char *exe_relative(const char *);
        FILE *mf = fopen(exe_relative("interior_label_misses.log"), "a");
        if (mf) {
            fprintf(mf, "addr=0x%06X in_func=0x%06X frame=%" PRIu64 "\n",
                    addr, g_rdb_current_func, g_frame_count);
            fclose(mf);
        }
        return;
    }

    /* Skip out-of-ROM addresses. Gate on the per-game ROM size (Principle 21)
     * — NOT a literal. The old hardcode was 0x80000 (Sonic 1's 512 KB), which
     * silently swallowed every miss past 512 KB for Sonic 2 (1 MB) and all of
     * the S3 half for S3K (4 MB). expected_rom_size is 0x80000 / 0x100000 /
     * 0x400000 for S1 / S2 / S3K respectively. */
    {
        uint32_t rom_limit = g_game_spec.expected_rom_size
                                 ? g_game_spec.expected_rom_size
                                 : (uint32_t)sizeof(g_rom);
        if (addr >= rom_limit) return;
    }

    /* Only process each unique address once */
    for (int i = 0; i < g_miss_unique_count; i++)
        if (g_miss_unique_addrs[i] == addr)
            return;  /* already reported */

    fprintf(stderr, "dispatch miss: $%06X (frame %" PRIu64 ")\n",
            addr, g_frame_count);

    if (g_miss_unique_count < MAX_MISS_UNIQUE)
        g_miss_unique_addrs[g_miss_unique_count++] = addr;

    /* Append to dispatch_misses.log (one address per line).
     * This file can be fed back to the recompiler via game.cfg extra_func. */
    extern const char *exe_relative(const char *);
    FILE *mf = fopen(exe_relative("dispatch_misses.log"), "a");
    if (mf) {
        fprintf(mf, "extra_func 0x%06X\n", addr);
        fclose(mf);
    }

    /* Also log to interp_fallbacks.log (same format, for convergence tools) */
    log_true_miss(addr);
}

/* NOTE: call_by_address() is implemented by sonic_dispatch.c (generated).
 * Do not define it here; it would conflict with the generated implementation. */

/* =========================================================================
 * VDP helpers (not called by generated code, provided for completeness)
 * ========================================================================= */

void     vdp_write_data(uint16_t val)   { m68k_write16(0xC00000, val); }
void     vdp_write_ctrl(uint16_t val)   { m68k_write16(0xC00004, val); }
uint16_t vdp_read_data(void)            { return m68k_read16(0xC00000); }
uint16_t vdp_read_status(void)          { return m68k_read16(0xC00004); }
void     vdp_render_frame(uint32_t *fb) { (void)fb; /* rendering via clownmdemu callbacks */ }

/* =========================================================================
 * Runtime init / VBlank request (old runner interface; not used by main.c)
 * ========================================================================= */

/* =========================================================================
 * Frame state logger — dumps key game state at each VBlank for comparison
 * between hybrid and Step 2 modes.
 * ========================================================================= */

static FILE *s_framelog = NULL;

void glue_log_frame_state(uint64_t frame)
{
    if (!s_framelog) {
#if ENABLE_RECOMPILED_CODE
        s_framelog = fopen("framelog_step2.txt", "w");
#else
        s_framelog = fopen("framelog_hybrid.txt", "w");
#endif
        if (!s_framelog) return;
    }
    if (frame > 9999) return;  /* cap framelog at 10000 frames */

#if OWN_BACKEND
    /* Own backend: g_ram is the authoritative WRAM (byte array, big-endian). */
    #define EMU_RAM_BYTE(addr) (g_ram[(addr) & 0xFFFF])
    #define EMU_RAM_WORD(addr) \
        ((uint16_t)(((uint16_t)g_ram[(addr) & 0xFFFF] << 8) | \
                    g_ram[((addr) + 1) & 0xFFFF]))
#else
    /* Read directly from clownmdemu's RAM (word-addressed, big-endian).
     * This avoids triggering SyncM68k in hybrid mode. */
    #define EMU_RAM_BYTE(addr) \
        ((uint8_t)(s_emu->state.m68k.ram[((addr) & 0xFFFF) / 2] >> \
                   (((addr) & 1) ? 0 : 8)))
    #define EMU_RAM_WORD(addr) \
        ((uint16_t)(s_emu->state.m68k.ram[((addr) & 0xFFFF) / 2]))
#endif
    #define EMU_RAM_LONG(addr) \
        (((uint32_t)EMU_RAM_WORD(addr) << 16) | EMU_RAM_WORD((addr)+2))

    uint8_t  game_mode = EMU_RAM_BYTE(g_game_layout.game_mode_addr);
    uint8_t  vbl_flag  = EMU_RAM_BYTE(g_game_layout.vint_routine_addr);
    /* The next three (vbl_count $F628, scroll_x $F700, plc_ptr $F680)
     * are Sonic-1-specific debug fields whose semantics differ on other
     * games — addresses kept literal here so the S1 framelog stays
     * verbatim. For Sonic 2+ these values are decorative noise; rely
     * on FrameRecord/per-game extras for game-specific telemetry. */
    uint16_t vbl_count = EMU_RAM_WORD(0xF628);
    uint16_t scroll_x  = EMU_RAM_WORD(0xF700);
    uint16_t plc_ptr   = EMU_RAM_WORD(0xF680);
    uint32_t frame_cnt = EMU_RAM_LONG(g_game_layout.vint_runcount_addr);
    uint8_t  obj0_id   = EMU_RAM_BYTE(g_game_layout.player_object_addr + 0);
    uint8_t  obj0_rt   = EMU_RAM_BYTE(g_game_layout.player_object_addr + 1);

    fprintf(s_framelog,
            "F%03llu mode=%02X vbl=%02X cnt=%04X scrl=%04X plc=%04X "
            "fcnt=%08X obj0=%02X/%02X\n",
            (unsigned long long)frame,
            game_mode, vbl_flag, vbl_count, scroll_x, plc_ptr,
            frame_cnt, obj0_id, obj0_rt);
    fflush(s_framelog);
}

void runtime_init(void)             { /* nothing; glue_init() serves this role */ }
void runtime_request_vblank(void)   { glue_signal_vblank(); }

/* =========================================================================
 * Logger helper
 * ========================================================================= */

void log_on_change(const char *label, uint32_t value)
{
    static uint32_t prev = ~0u;
    static const char *prev_label = NULL;
    if (prev_label != label || prev != value) {
        fprintf(stderr, "LOG %s = $%08X\n", label, value);
        prev_label = label;
        prev = value;
    }
}

/* =========================================================================
 * Step 2: hybrid_jmp_interpret / hybrid_call_interpret → call_by_address
 *
 * In hybrid mode these run the interpreter as a fallback.  In Step 2 there
 * is no interpreter — redirect to call_by_address() which has every
 * generated function in its dispatch table.
 * ========================================================================= */

#if ENABLE_RECOMPILED_CODE

extern void call_by_address(uint32_t addr);

/* Track indirect dispatch calls.
 * These go through hybrid_jmp/call_interpret → call_by_address.
 * We only log addresses that FAIL dispatch (true misses that need
 * new extra_func entries).  Addresses that dispatch successfully
 * are interior labels of existing functions — logging them would
 * cause the recompiler to split functions incorrectly. */
#define MAX_INTERP_SEEN 1024
static uint32_t s_interp_seen[MAX_INTERP_SEEN];
static int      s_interp_seen_count = 0;
int             g_interp_total_calls = 0;

/* Called from genesis_log_dispatch_miss — these are REAL misses */
static void log_true_miss(uint32_t target_pc)
{
    for (int i = 0; i < s_interp_seen_count; i++)
        if (s_interp_seen[i] == target_pc) return;
    if (s_interp_seen_count < MAX_INTERP_SEEN)
        s_interp_seen[s_interp_seen_count++] = target_pc;
    extern const char *exe_relative(const char *);
    FILE *f = fopen(exe_relative("interp_fallbacks.log"), "a");
    if (f) { fprintf(f, "extra_func 0x%06X\n", target_pc); fclose(f); }
}

int glue_interp_seen_count(void) { return s_interp_seen_count; }
int glue_interp_total_calls(void) { return g_interp_total_calls; }
uint64_t glue_miss_count_any(void) { return (uint64_t)g_miss_count_any; }
uint32_t glue_interp_seen_addr(int i) {
    return (i >= 0 && i < s_interp_seen_count) ? s_interp_seen[i] : 0;
}

void hybrid_jmp_interpret(uint32_t target_pc)
{
    g_interp_total_calls++;
    call_by_address(target_pc);
    /* If call_by_address didn't find it, genesis_log_dispatch_miss
     * was called, which logs it as a true miss via log_true_miss. */
}

void hybrid_call_interpret(uint32_t target_pc)
{
    g_interp_total_calls++;
    call_by_address(target_pc);
}

#endif /* ENABLE_RECOMPILED_CODE */
