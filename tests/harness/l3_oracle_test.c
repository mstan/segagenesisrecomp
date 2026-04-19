/*
 * l3_oracle_test.c — per-function semantic correctness oracle.
 *
 * For each function in the L3 manifest:
 *   1. Initialize a sandbox (ROM + RAM + M68KState) to a controlled
 *      starting state (zeroed or seeded-random).
 *   2. Run the function via the clown68000 interpreter — that's the
 *      oracle. Capture the post-return CPU state and the set of RAM
 *      bytes that changed.
 *   3. Reset the sandbox to the same starting state.
 *   4. Call the recompiled `func_XXXXXX()` directly via the dispatch
 *      table. Capture the same post-state.
 *   5. Diff the two sides. Any mismatch in registers, flags, or RAM
 *      writes is a real recompiler bug.
 *
 * Termination: a sentinel return address is pushed onto the supervisor
 * stack before invoking; when PC reaches the sentinel the function has
 * RTSed. If it never does within a cycle budget, we report "infinite
 * loop / timeout" rather than hanging.
 *
 * This file links against the recompiled sonic_full.c +
 * sonic_dispatch.c, so the recompiled side runs the actual code that
 * ships in the game binary. The runtime stubs below provide
 * sandbox-backed implementations of every symbol the generated code
 * references; nothing in the runner/ tree is linked in (no SDL, no
 * VDP, no game state).
 *
 * Flags:
 *   --rom PATH       sonic.bin (default: ../../build/Release/sonic.bin)
 *   --manifest PATH  L3 manifest (default: tests/fixtures/sonic1/l3/manifest.txt)
 *   --tier N         only run a single tier
 *   --max N          stop after N functions
 *   --filter ADDR    only the function at this hex address
 *   --seeds N        run each function with N random seeds (default: 4,
 *                    plus a zero-state run)
 *   -v               print every divergence, not just bucket summaries
 *   --summary        single line + return code, no detail
 */
#include "clown68000.h"
#include "genesis_runtime.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ====================================================================
 * Sandbox memory + CPU state (shared between interpreter & recompiled)
 * ==================================================================== */

uint8_t   g_rom[0x400000];
uint8_t   g_ram[0x010000];
M68KState g_cpu;

static uint8_t  s_initial_ram[0x010000];
static M68KState s_initial_cpu;

/* Sentinel return address, lives inside RAM. We place a `JMP $SENTINEL`
 * at this address (a tight self-loop) so when the function RTSes and
 * PC lands here, the interpreter just spins in place — PC stays equal
 * to SENTINEL between cycles, easy to detect after each chunk. */
static const uint32_t SENTINEL = 0x00FFFE00;

/* ====================================================================
 * Runtime stubs the generated code expects
 * ==================================================================== */

uint8_t m68k_read8(uint32_t addr) {
    addr &= 0xFFFFFF;
    if (addr < 0x400000)                  return g_rom[addr];
    if (addr >= 0xFF0000)                 return g_ram[addr - 0xFF0000];
    return 0;
}
uint16_t m68k_read16(uint32_t addr) {
    addr &= 0xFFFFFE;
    if (addr < 0x400000)
        return ((uint16_t)g_rom[addr] << 8) | g_rom[addr + 1];
    if (addr >= 0xFF0000) {
        uint32_t off = addr - 0xFF0000;
        return ((uint16_t)g_ram[off] << 8) | g_ram[off + 1];
    }
    return 0;
}
uint32_t m68k_read32(uint32_t addr) {
    return ((uint32_t)m68k_read16(addr) << 16) | m68k_read16(addr + 2);
}

void m68k_write8(uint32_t addr, uint8_t val) {
    addr &= 0xFFFFFF;
    if (addr >= 0xFF0000) g_ram[addr - 0xFF0000] = val;
    /* Other writes (ROM/VDP/Z80/IO) are silently dropped — outside the
     * sandbox boundary. The interpreter callback drops them too. */
}
void m68k_write16(uint32_t addr, uint16_t val) {
    addr &= 0xFFFFFE;
    if (addr >= 0xFF0000) {
        uint32_t off = addr - 0xFF0000;
        g_ram[off]     = (uint8_t)(val >> 8);
        g_ram[off + 1] = (uint8_t)(val & 0xFF);
    }
}
void m68k_write32(uint32_t addr, uint32_t val) {
    m68k_write16(addr,     (uint16_t)(val >> 16));
    m68k_write16(addr + 2, (uint16_t)(val & 0xFFFF));
}

/* Cycle accounting — the recompiled code increments g_cycle_accumulator
 * after every instruction and may call glue_check_vblank(). For the
 * oracle, we make all of that inert: no VBlank service runs, no fiber
 * yields, no dispatch-miss logging. */
uint32_t g_cycle_accumulator = 0;
uint32_t g_vblank_threshold  = 0xFFFFFFFFu;
void glue_check_vblank(void) {}
void glue_yield_for_vblank(void) {}

/* g_rte_pending and g_early_return: leaf functions don't trigger these
 * paths, but the symbols are referenced. Provide a sink. */
static int s_rte_dummy = 0;
int *g_rte_pending_ptr = &s_rte_dummy;
int  g_early_return = 0;

/* Debug counters (referenced by 3 specific functions). */
int g_dbg_b64_count = 0;
int g_dbg_b5e_count = 0;
int g_dbg_b88_count = 0;

/* Frame counter and controller state — referenced from main game code
 * but irrelevant for leaf-function isolation. */
uint64_t g_frame_count = 0;
uint8_t  g_controller1_buttons = 0;
uint8_t  g_controller2_buttons = 0;

/* Dispatch-miss instrumentation. */
uint32_t g_miss_count_any = 0;
uint32_t g_miss_last_addr = 0;
uint64_t g_miss_last_frame = 0;
uint32_t g_miss_unique_addrs[MAX_MISS_UNIQUE];
int      g_miss_unique_count = 0;

void log_on_change(const char *label, uint32_t value) { (void)label; (void)value; }
void genesis_log_dispatch_miss(uint32_t addr) {
    (void)addr;
    /* Bump the counter so invoke_recompiled detects "function not in
     * dispatch table" and reports it as a no-dispatch failure rather
     * than silently returning a stale state. */
    g_miss_count_any++;
}
void hybrid_jmp_interpret(uint32_t target_pc) { (void)target_pc; }
void hybrid_call_interpret(uint32_t target_pc) { (void)target_pc; }

/* VDP/SDL stubs — leaf functions shouldn't reach these but provide for
 * link safety when expanding to non-leaf tiers later. */
void vdp_write_data(uint16_t v) { (void)v; }
void vdp_write_ctrl(uint16_t v) { (void)v; }
uint16_t vdp_read_data(void)   { return 0; }
uint16_t vdp_read_status(void) { return 0; }
void vdp_render_frame(uint32_t *fb) { (void)fb; }
void runtime_init(void) {}
void runtime_request_vblank(void) {}

/* call_by_address: provided by sonic_dispatch.c. It calls
 * game_dispatch_override() on a miss (hook for per-game extras.c) —
 * we stub it to "not handled" so unknown addresses just log + return. */
bool game_dispatch_override(uint32_t addr) { (void)addr; return false; }

/* clown68000 has hooks for the runner's hybrid mode. We don't run
 * hybrid here — provide null/zero stubs. */
void (*g_hybrid_pre_insn_fn)(void) = NULL;
unsigned int g_hybrid_cycle_counter = 0;

/* ====================================================================
 * Interpreter callbacks (clown68000)
 * ==================================================================== */

int g_l3_debug = 0;

/* clownmdemu convention: callback `address` is WORD-indexed (i.e.
 * divided by 2). Convert back to byte address. */
static cc_u16f int_read(const void *ud, cc_u32f addr, cc_bool hi, cc_bool lo, cc_u32f cyc) {
    (void)ud; (void)cyc;
    uint32_t a24 = ((uint32_t)addr * 2) & 0xFFFFFE;
    uint16_t hi_b = 0, lo_b = 0;
    if (a24 < 0x400000) {
        hi_b = g_rom[a24];
        lo_b = g_rom[a24 + 1];
    } else if (a24 >= 0xFF0000) {
        uint32_t off = a24 - 0xFF0000;
        hi_b = g_ram[off];
        lo_b = g_ram[off + 1];
    }
    return (cc_u16f)(((hi ? hi_b : 0) << 8) | (lo ? lo_b : 0));
}

static void int_write(const void *ud, cc_u32f addr, cc_bool hi, cc_bool lo, cc_u32f cyc, cc_u16f val) {
    (void)ud; (void)cyc;
    uint32_t a24 = ((uint32_t)addr * 2) & 0xFFFFFE;
    if (a24 >= 0xFF0000 && a24 + 1 < 0xFF0000 + 0x10000) {
        uint32_t off = a24 - 0xFF0000;
        if (hi) g_ram[off]     = (uint8_t)((val >> 8) & 0xFF);
        if (lo) g_ram[off + 1] = (uint8_t)( val       & 0xFF);
    }
}

static const Clown68000_ReadWriteCallbacks INT_CB = {
    .read_callback  = int_read,
    .write_callback = int_write,
    .user_data      = NULL,
};

/* ====================================================================
 * State helpers
 * ==================================================================== */

static void cpu_to_clown(const M68KState *m, Clown68000_State *c) {
    memset(c, 0, sizeof(*c));
    for (int i = 0; i < 8; i++) {
        c->data_registers[i]    = m->D[i];
        c->address_registers[i] = m->A[i];
    }
    c->status_register         = m->SR;
    c->program_counter         = m->PC;
    c->supervisor_stack_pointer = m->A[7];
    c->user_stack_pointer       = m->USP;
}
static void clown_to_cpu(const Clown68000_State *c, M68KState *m) {
    for (int i = 0; i < 8; i++) {
        m->D[i] = (uint32_t)c->data_registers[i];
        m->A[i] = (uint32_t)c->address_registers[i];
    }
    m->SR  = (uint16_t)c->status_register;
    m->PC  = (uint32_t)c->program_counter;
    m->USP = (uint32_t)c->user_stack_pointer;
}

/* Install a self-trap at SENTINEL: `JMP $SENTINEL` (4EF9 + abs.long).
 * Once PC lands here, interpreter spins forever at this PC. */
static void install_sentinel_trap(void) {
    uint32_t off = SENTINEL - 0xFF0000;
    g_ram[off + 0] = 0x4E;
    g_ram[off + 1] = 0xF9;
    g_ram[off + 2] = (uint8_t)((SENTINEL >> 24) & 0xFF);
    g_ram[off + 3] = (uint8_t)((SENTINEL >> 16) & 0xFF);
    g_ram[off + 4] = (uint8_t)((SENTINEL >>  8) & 0xFF);
    g_ram[off + 5] = (uint8_t)( SENTINEL        & 0xFF);
}

/* Place the sentinel return address at the top of the synthetic stack
 * and point A7 at it. The function's RTS will pop the sentinel into PC. */
static void install_sentinel_stack(uint32_t stack_top) {
    install_sentinel_trap();
    /* Stack grows downward. Reserve 4 bytes for the sentinel. */
    uint32_t sp = stack_top - 4;
    if (sp >= 0xFF0000 && sp + 4 <= 0xFF0000 + 0x10000) {
        uint32_t off = sp - 0xFF0000;
        g_ram[off + 0] = (uint8_t)((SENTINEL >> 24) & 0xFF);
        g_ram[off + 1] = (uint8_t)((SENTINEL >> 16) & 0xFF);
        g_ram[off + 2] = (uint8_t)((SENTINEL >>  8) & 0xFF);
        g_ram[off + 3] = (uint8_t)( SENTINEL        & 0xFF);
    }
    g_cpu.A[7] = sp;
}

/* ====================================================================
 * Per-function test
 * ==================================================================== */

typedef struct {
    uint32_t func_addr;
    uint32_t seed;          /* 0 = zero state */
    bool     timed_out;
    bool     match;
    /* differences (for reporting) */
    int      diff_D_mask;
    int      diff_A_mask;
    bool     diff_SR;
    uint16_t int_SR;
    uint16_t rec_SR;
    int      diff_RAM_count;
    /* exemplar: first divergent register/byte for verbose mode */
    int      first_D_idx;       /* -1 if none */
    uint32_t int_D_val, rec_D_val;
    int      first_A_idx;
    uint32_t int_A_val, rec_A_val;
} TestResult;

#define MAX_RUN_CYCLES   200000   /* generous; leaf functions finish in <100 */

static void seed_state(uint32_t seed) {
    /* Reset memory + CPU to controlled state. */
    memset(g_ram, 0, sizeof(g_ram));
    memset(&g_cpu, 0, sizeof(g_cpu));
    if (seed != 0) {
        /* xorshift32 fill — deterministic per seed. */
        uint32_t x = seed;
        for (int i = 0; i < 8; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            g_cpu.D[i] = x;
        }
        for (int i = 0; i < 7; i++) {  /* A0..A6, NOT A7 — that's the stack */
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            g_cpu.A[i] = x;
        }
        /* Sparse RAM seeding (every 256th byte) so functions reading
         * from arbitrary RAM offsets get non-zero values without
         * explosively growing the diff. */
        for (int off = 0; off < 0x10000; off += 256) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            g_ram[off] = (uint8_t)(x & 0xFF);
        }
    }
    g_cpu.SR = 0x2700;  /* supervisor mode, all interrupts masked */
    /* Snapshot for later reset. */
    memcpy(s_initial_ram, g_ram, sizeof(g_ram));
    memcpy(&s_initial_cpu, &g_cpu, sizeof(g_cpu));
}

static void restore_initial_state(void) {
    memcpy(g_ram, s_initial_ram, sizeof(g_ram));
    memcpy(&g_cpu, &s_initial_cpu, sizeof(g_cpu));
}

static int diff_ram(const uint8_t *a, const uint8_t *b) {
    int n = 0;
    for (int i = 0; i < 0x10000; i++) if (a[i] != b[i]) ++n;
    return n;
}

/* Lookup the recompiled function pointer by walking the dispatch table
 * via call_by_address. For our purposes, we just call call_by_address —
 * the dispatch table will look up the function and invoke it. */
static bool invoke_recompiled(uint32_t func_addr) {
    /* Must be in dispatch table. call_by_address logs misses but doesn't
     * abort — we have to detect by checking g_miss_count_any. */
    uint32_t miss_before = g_miss_count_any;
    call_by_address(func_addr);
    return g_miss_count_any == miss_before;
}

/* Run interpreter from PC=func_addr until PC hits SENTINEL or budget runs out. */
static bool run_interpreter(uint32_t func_addr, uint32_t cycle_budget) {
    Clown68000_State cs;
    /* Reset first so the interpreter's internal invariants are initialized
     * (halted/stopped/leftover_cycles). Then overwrite registers, PC,
     * SP and SR with the controlled state we want to test. */
    Clown68000_Reset(&cs, &INT_CB);
    cpu_to_clown(&g_cpu, &cs);
    cs.status_register |= 0x2700;          /* supervisor + I-mask=7 */
    cs.program_counter          = func_addr;
    cs.address_registers[7]     = g_cpu.A[7];
    cs.supervisor_stack_pointer = g_cpu.A[7];

    /* Step in small chunks so we notice PC == SENTINEL right after RTS,
     * before the JMP-self loop racks up many wasted cycles. */
    uint32_t remaining = cycle_budget;
    while (remaining > 0) {
        const uint32_t step = remaining < 256 ? remaining : 256;
        Clown68000_DoCycles(&cs, &INT_CB, step);
        remaining -= step;
        if (cs.program_counter == SENTINEL) {
            clown_to_cpu(&cs, &g_cpu);
            return true;
        }
        if (cs.halted || cs.stopped) {
            clown_to_cpu(&cs, &g_cpu);
            return false;
        }
    }
    clown_to_cpu(&cs, &g_cpu);
    return false;
}

static TestResult test_function(uint32_t func_addr, uint32_t seed) {
    TestResult r = {
        .func_addr   = func_addr,
        .seed        = seed,
        .first_D_idx = -1,
        .first_A_idx = -1,
    };
    const uint32_t STACK_TOP = 0xFFFF80;  /* well within RAM, plenty of headroom */

    /* --- Run interpreter side --- */
    seed_state(seed);
    install_sentinel_stack(STACK_TOP);
    if (!run_interpreter(func_addr, MAX_RUN_CYCLES)) {
        r.timed_out = true;
        return r;
    }
    M68KState int_cpu = g_cpu;
    uint8_t   int_ram[0x10000];
    memcpy(int_ram, g_ram, sizeof(int_ram));

    /* --- Run recompiled side from same initial state --- */
    restore_initial_state();
    install_sentinel_stack(STACK_TOP);
    g_cpu.PC = func_addr;
    if (!invoke_recompiled(func_addr)) {
        /* dispatch miss — function wasn't in dispatch table */
        r.timed_out = true;  /* repurpose flag for "couldn't run" */
        return r;
    }
    /* Recompiled code's RTS leaves PC unchanged; our state is post-return.
     * Compare g_cpu to int_cpu. */
    M68KState rec_cpu = g_cpu;

    /* --- Compare ---
     * A7 is excluded from the comparison: the recompiled function returns
     * via C `return`, which leaves A7 where the function left it. The
     * interpreter's RTS pops the sentinel return address, advancing A7
     * by 4. Both behaviors are correct implementations of "return to
     * caller"; the difference is bookkeeping, not computation. */
    r.diff_D_mask = 0;
    r.diff_A_mask = 0;
    r.first_D_idx = -1;
    r.first_A_idx = -1;
    for (int i = 0; i < 8; i++) {
        if (int_cpu.D[i] != rec_cpu.D[i]) {
            r.diff_D_mask |= (1 << i);
            if (r.first_D_idx < 0) {
                r.first_D_idx = i;
                r.int_D_val = int_cpu.D[i];
                r.rec_D_val = rec_cpu.D[i];
            }
        }
        if (i == 7) continue;
        if (int_cpu.A[i] != rec_cpu.A[i]) {
            r.diff_A_mask |= (1 << i);
            if (r.first_A_idx < 0) {
                r.first_A_idx = i;
                r.int_A_val = int_cpu.A[i];
                r.rec_A_val = rec_cpu.A[i];
            }
        }
    }
    /* Mask SR to user-visible flags + supervisor bit; ignore trace etc. */
    uint16_t sr_mask = 0x271F;
    r.int_SR = int_cpu.SR & sr_mask;
    r.rec_SR = rec_cpu.SR & sr_mask;
    r.diff_SR = r.int_SR != r.rec_SR;
    r.diff_RAM_count = diff_ram(int_ram, g_ram);
    r.match = (r.diff_D_mask == 0 && r.diff_A_mask == 0 && !r.diff_SR && r.diff_RAM_count == 0);
    return r;
}

/* ====================================================================
 * Manifest parsing
 * ==================================================================== */

typedef struct {
    int      tier;
    uint32_t addr;
} ManifestEntry;

#define MAX_MANIFEST 4096
static ManifestEntry g_manifest[MAX_MANIFEST];
static int g_manifest_count = 0;

static int parse_manifest(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "manifest open failed: %s: %s\n", path, strerror(errno));
        return 0;
    }
    char line[256];
    int cur_tier = -1;
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') ++s;
        if (*s == '\n' || *s == '\r' || *s == '\0') continue;
        if (*s == '#') {
            int t;
            if (sscanf(s, "# tier %d", &t) == 1) cur_tier = t;
            continue;
        }
        unsigned int a;
        if (sscanf(s, "%x", &a) != 1) continue;
        if (g_manifest_count < MAX_MANIFEST) {
            g_manifest[g_manifest_count++] = (ManifestEntry){ cur_tier, a };
        }
    }
    fclose(f);
    return g_manifest_count;
}

/* ====================================================================
 * Main
 * ==================================================================== */

typedef struct {
    const char *rom;
    const char *manifest;
    int         only_tier;
    int         max;
    uint32_t    only_addr;
    int         seeds;
    int         verbose;
    int         summary_only;
} Options;

static int load_rom(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "rom open failed: %s: %s\n", path, strerror(errno));
        return 0;
    }
    size_t n = fread(g_rom, 1, sizeof(g_rom), f);
    fclose(f);
    return (int)n > 0;
}

static int run(const Options *opt) {
    if (!load_rom(opt->rom)) return 2;
    if (!parse_manifest(opt->manifest)) return 2;

    int run_count = 0, pass = 0;
    int fail_diff = 0, fail_timeout = 0;
    int by_tier_pass[64] = {0}, by_tier_fail[64] = {0};

    /* Bucket failure reasons. */
    int reason_D = 0, reason_A = 0, reason_SR = 0, reason_RAM = 0;

    for (int i = 0; i < g_manifest_count; i++) {
        ManifestEntry *e = &g_manifest[i];
        if (opt->only_tier >= 0 && e->tier != opt->only_tier) continue;
        if (opt->only_addr && e->addr != opt->only_addr) continue;

        /* Per-function: zero-state + N random seeds */
        bool any_fail = false;
        TestResult worst = { .func_addr = e->addr };
        for (int s = 0; s <= opt->seeds; s++) {
            uint32_t seed = (s == 0) ? 0 : (0xDEAD0001u + (uint32_t)s * 0x9E3779B1u);
            TestResult r = test_function(e->addr, seed);
            if (!r.match) { any_fail = true; worst = r; break; }
        }

        ++run_count;
        if (any_fail) {
            if (worst.timed_out) ++fail_timeout;
            else                 ++fail_diff;
            if (worst.diff_D_mask)    ++reason_D;
            if (worst.diff_A_mask)    ++reason_A;
            if (worst.diff_SR)        ++reason_SR;
            if (worst.diff_RAM_count) ++reason_RAM;
            if (e->tier >= 0 && e->tier < 64) ++by_tier_fail[e->tier];
            if (opt->verbose) {
                fprintf(stderr,
                        "FAIL func_%06X seed=0x%08X tier=%d: ",
                        worst.func_addr, worst.seed, e->tier);
                if (worst.timed_out)         fprintf(stderr, "timeout/no-dispatch ");
                if (worst.diff_D_mask)       fprintf(stderr, "D=0x%02X ", worst.diff_D_mask);
                if (worst.diff_A_mask)       fprintf(stderr, "A=0x%02X ", worst.diff_A_mask);
                if (worst.diff_SR)           fprintf(stderr, "SR(int=%04X rec=%04X) ",
                                                     worst.int_SR, worst.rec_SR);
                if (worst.diff_RAM_count)    fprintf(stderr, "RAM=%d bytes", worst.diff_RAM_count);
                fprintf(stderr, "\n");
                if (worst.first_D_idx >= 0)
                    fprintf(stderr, "       first D[%d]: int=0x%08X rec=0x%08X\n",
                            worst.first_D_idx, worst.int_D_val, worst.rec_D_val);
                if (worst.first_A_idx >= 0)
                    fprintf(stderr, "       first A[%d]: int=0x%08X rec=0x%08X\n",
                            worst.first_A_idx, worst.int_A_val, worst.rec_A_val);
            }
        } else {
            ++pass;
            if (e->tier >= 0 && e->tier < 64) ++by_tier_pass[e->tier];
        }

        if (opt->max && run_count >= opt->max) break;
    }

    if (opt->summary_only) {
        printf("L3: %d/%d pass\n", pass, run_count);
        return (pass == run_count) ? 0 : 1;
    }

    printf("L3 oracle: %d/%d functions pass  (timeout=%d, semantic=%d)\n",
           pass, run_count, fail_timeout, fail_diff);
    printf("  failure reasons (one per failing func):  D=%d A=%d SR=%d RAM=%d\n",
           reason_D, reason_A, reason_SR, reason_RAM);
    printf("  per-tier pass/fail:\n");
    for (int t = 0; t < 64; t++) {
        if (by_tier_pass[t] + by_tier_fail[t] == 0) continue;
        printf("    tier %2d: %d/%d\n", t, by_tier_pass[t],
               by_tier_pass[t] + by_tier_fail[t]);
    }
    return (pass == run_count) ? 0 : 1;
}

int main(int argc, char **argv) {
    Options opt = {
        .rom        = "build/Release/sonic.bin",
        .manifest   = "segagenesisrecomp/tests/fixtures/sonic1/l3/manifest.txt",
        .only_tier  = -1,
        .max        = 0,
        .only_addr  = 0,
        .seeds      = 4,
        .verbose    = 0,
        .summary_only = 0,
    };
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if      (!strcmp(a, "--rom")      && i + 1 < argc) opt.rom = argv[++i];
        else if (!strcmp(a, "--manifest") && i + 1 < argc) opt.manifest = argv[++i];
        else if (!strcmp(a, "--tier")     && i + 1 < argc) opt.only_tier = atoi(argv[++i]);
        else if (!strcmp(a, "--max")      && i + 1 < argc) opt.max = atoi(argv[++i]);
        else if (!strcmp(a, "--filter")   && i + 1 < argc) opt.only_addr = (uint32_t)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(a, "--seeds")    && i + 1 < argc) opt.seeds = atoi(argv[++i]);
        else if (!strcmp(a, "-v"))                          opt.verbose = 1;
        else if (!strcmp(a, "--debug"))                     g_l3_debug = 1;
        else if (!strcmp(a, "--summary"))                   opt.summary_only = 1;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            fprintf(stderr,
                "usage: l3_oracle_test [--rom PATH] [--manifest PATH]\n"
                "       [--tier N] [--max N] [--filter ADDR_HEX]\n"
                "       [--seeds N] [-v] [--summary]\n");
            return 0;
        } else {
            fprintf(stderr, "unknown arg: %s\n", a);
            return 2;
        }
    }
    return run(&opt);
}
