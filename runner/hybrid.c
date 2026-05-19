/*
 * hybrid.c — Sandbox verification: interpreter oracle + native comparison.
 *
 * Adapted from the SMW snesrecomp-v2 bank04_validate pattern:
 * the interpreter ALWAYS drives execution.  At each function entry,
 * we run the native version in a sandbox, save its result, then let
 * the interpreter run the function naturally.  When the interpreter
 * reaches the return address, we compare outputs and log divergences.
 *
 * The native result is NEVER committed to the game state.  The game
 * always runs on the interpreter's (correct) output.  This means:
 * - A codegen bug in one function doesn't cascade
 * - Every function is tested against known-good input
 * - All divergences are independent and actionable
 */

#include "hybrid.h"
#include "clown68000.h"
#include "bus-main-m68k.h"   /* CPUCallbackUserData */
#include "genesis_runtime.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

extern M68KState g_cpu;
extern uint64_t  g_frame_count;

/* =========================================================================
 * Sandbox state
 * ========================================================================= */

static ClownMDEmu *s_emu = NULL;

/* Pre-call snapshot: FULL emulator state.
 * We must save/restore the entire ClownMDEmu struct because the native
 * function's bus accesses (m68k_read/write → M68kReadCallback) trigger
 * SyncM68k which advances VDP/Z80 cycle counters.  If we only restore
 * registers + RAM, the interpreter runs with corrupted cycle state. */
static ClownMDEmu s_snap_emu;
static int s_snap_valid = 0;

/* Native result after sandbox execution */
typedef struct {
    uint32_t D[8];
    uint32_t A[8];
    uint16_t SR;
    uint32_t func_addr;
    uint32_t ret_addr;
    uint64_t frame;
    int      valid;
    cc_u16l  ram[0x8000];   /* native's work RAM after execution */
} NativeResult;

static NativeResult s_native_result;

/* Stats */
static uint32_t s_total_tests      = 0;
static uint32_t s_total_divergences = 0;
static uint32_t s_total_ram_diffs   = 0;

/* Log file */
static FILE *s_log = NULL;

/* =========================================================================
 * State sync: interpreter ↔ g_cpu
 * ========================================================================= */

static void sync_to_native(const Clown68000_State *m68k)
{
    int i;
    for (i = 0; i < 8; i++) g_cpu.D[i] = m68k->data_registers[i];
    for (i = 0; i < 8; i++) g_cpu.A[i] = m68k->address_registers[i];
    g_cpu.SR = (uint16_t)m68k->status_register;
    g_cpu.PC = m68k->program_counter;
}

/* =========================================================================
 * Snapshot / restore
 * ========================================================================= */

/* Also save/restore glue.c's sync state — this is NOT inside ClownMDEmu
 * but affects bus access timing (SyncM68k uses these counters). Without
 * this, native sandbox execution advances the cycle counters, and the
 * interpreter then sees stale/advanced sync state → different DMA/Z80
 * timing → different RAM state → false divergences. */
extern cc_u32f g_hybrid_cycle_counter;
typedef struct {
    /* From glue.c's s_cpu_data — we can't access it directly, so we
     * save/restore the cycle counter which is the main source of drift. */
    cc_u32f cycle_counter;
} SyncSnapshot;
static SyncSnapshot s_snap_sync;

static void snapshot_full(void)
{
    const ClownMDEmu_Callbacks *cb = s_emu->callbacks;
    const cc_u16l *cart = s_emu->cartridge_buffer;
    cc_u32l cart_len = s_emu->cartridge_buffer_length;
    memcpy(&s_snap_emu, s_emu, sizeof(s_snap_emu));
    s_snap_emu.callbacks = cb;
    s_snap_emu.cartridge_buffer = cart;
    s_snap_emu.cartridge_buffer_length = cart_len;
    s_snap_sync.cycle_counter = g_hybrid_cycle_counter;
    { extern void glue_snapshot_sync(void); glue_snapshot_sync(); }
    s_snap_valid = 1;
}

static void restore_full(void)
{
    if (!s_snap_valid) return;
    const ClownMDEmu_Callbacks *cb = s_emu->callbacks;
    const cc_u16l *cart = s_emu->cartridge_buffer;
    cc_u32l cart_len = s_emu->cartridge_buffer_length;
    memcpy(s_emu, &s_snap_emu, sizeof(s_snap_emu));
    s_emu->callbacks = cb;
    s_emu->cartridge_buffer = cart;
    s_emu->cartridge_buffer_length = cart_len;
    g_hybrid_cycle_counter = s_snap_sync.cycle_counter;
    { extern void glue_restore_sync(void); glue_restore_sync(); }
}

/* =========================================================================
 * Comparison: native result vs interpreter result
 * ========================================================================= */

static void compare_and_log(uint32_t func_addr,
                            const NativeResult *native,
                            const Clown68000_State *interp_m68k)
{
    int diverged = 0;
    int i;

    /* Compare data registers */
    for (i = 0; i < 8; i++) {
        if (native->D[i] != (uint32_t)interp_m68k->data_registers[i]) {
            if (!diverged) {
                fprintf(stderr, "\n[DIV] func=$%06X frame=%"PRIu64"\n",
                        func_addr, native->frame);
                diverged = 1;
            }
            fprintf(stderr, "  D%d: native=$%08X interp=$%08X\n",
                    i, native->D[i], (uint32_t)interp_m68k->data_registers[i]);
        }
    }

    /* Compare address registers (A0-A6, skip A7/SP — stack depth may differ) */
    for (i = 0; i < 7; i++) {
        if (native->A[i] != (uint32_t)interp_m68k->address_registers[i]) {
            if (!diverged) {
                fprintf(stderr, "\n[DIV] func=$%06X frame=%"PRIu64"\n",
                        func_addr, native->frame);
                diverged = 1;
            }
            fprintf(stderr, "  A%d: native=$%08X interp=$%08X\n",
                    i, native->A[i], (uint32_t)interp_m68k->address_registers[i]);
        }
    }

    /* Compare work RAM: check every word that EITHER side changed */
    {
        int ram_diffs = 0;
        for (i = 0; i < 0x8000; i++) {
            cc_u16l native_val  = native->ram[i];
            cc_u16l interp_val  = s_emu->state.m68k.ram[i];
            cc_u16l precall_val = s_snap_emu.state.m68k.ram[i];

            /* Skip words neither side changed */
            if (native_val == precall_val && interp_val == precall_val)
                continue;

            /* Both changed to the same value — OK */
            if (native_val == interp_val)
                continue;

            /* Divergence in RAM */
            if (!diverged) {
                fprintf(stderr, "\n[DIV] func=$%06X frame=%"PRIu64"\n",
                        func_addr, native->frame);
                /* Print input registers for debugging */
                fprintf(stderr, "  INPUT: D0=$%08X D1=$%08X D2=$%08X D3=$%08X D4=$%08X D5=$%08X D6=$%08X D7=$%08X\n",
                        s_snap_emu.m68k.data_registers[0],
                        s_snap_emu.m68k.data_registers[1],
                        s_snap_emu.m68k.data_registers[2],
                        s_snap_emu.m68k.data_registers[3],
                        s_snap_emu.m68k.data_registers[4],
                        s_snap_emu.m68k.data_registers[5],
                        s_snap_emu.m68k.data_registers[6],
                        s_snap_emu.m68k.data_registers[7]);
                fprintf(stderr, "  INPUT: A0=$%08X A1=$%08X A2=$%08X A3=$%08X A4=$%08X A5=$%08X A6=$%08X SR=$%04X\n",
                        s_snap_emu.m68k.address_registers[0],
                        s_snap_emu.m68k.address_registers[1],
                        s_snap_emu.m68k.address_registers[2],
                        s_snap_emu.m68k.address_registers[3],
                        s_snap_emu.m68k.address_registers[4],
                        s_snap_emu.m68k.address_registers[5],
                        s_snap_emu.m68k.address_registers[6],
                        (unsigned)s_snap_emu.m68k.status_register);
                /* Also log register OUTPUT differences */
                fprintf(stderr, "  OUTPUT regs:\n");
                { int j;
                  for (j = 0; j < 8; j++) {
                    uint32_t nv = native->D[j], iv = (uint32_t)interp_m68k->data_registers[j];
                    if (nv != iv) fprintf(stderr, "    D%d: native=$%08X interp=$%08X\n", j, nv, iv);
                  }
                  for (j = 0; j < 7; j++) {
                    uint32_t nv = native->A[j], iv = (uint32_t)interp_m68k->address_registers[j];
                    if (nv != iv) fprintf(stderr, "    A%d: native=$%08X interp=$%08X\n", j, nv, iv);
                  }
                }
                diverged = 1;
            }
            if (ram_diffs < 20) {
                uint32_t byte_addr = 0xFF0000u + (uint32_t)i * 2;
                fprintf(stderr, "  RAM $%06X: native=$%04X interp=$%04X (was $%04X)\n",
                        byte_addr, native_val, interp_val, precall_val);
            }
            ram_diffs++;
        }
        if (ram_diffs > 20 && diverged) {
            fprintf(stderr, "  ... and %d more RAM differences\n", ram_diffs - 20);
        }
        if (ram_diffs > 0)
            s_total_ram_diffs += ram_diffs;
    }

    if (diverged) {
        s_total_divergences++;
        fprintf(stderr, "  (test #%u, total divergences: %u, ram_diffs_total: %u)\n",
                s_total_tests, s_total_divergences, s_total_ram_diffs);
    }

    s_total_tests++;
}

/* =========================================================================
 * Pre-instruction hook — the core of sandbox verification
 * ========================================================================= */

static void hybrid_pre_insn(cc_u32l pc)
{
    int i;
    Clown68000_State *m68k;

    if (s_emu == NULL)
        return;

    /* ---- Step 7: Check for pending comparison (function returned) ---- */
    if (s_native_result.valid && pc == s_native_result.ret_addr) {
        /* Interpreter just returned from the function.
         * Compare interpreter's result with saved native result. */
        m68k = &s_emu->m68k;
        compare_and_log(s_native_result.func_addr, &s_native_result, m68k);
        s_native_result.valid = 0;
    }

    /* ---- Skip if we're already waiting for a comparison ---- */
    if (s_native_result.valid)
        return;   /* nested call — skip, wait for outer to return */

    /* ---- Check: does this PC have a native function? ---- */
    /* call_by_address checks the full dispatch table.  If it can't find
     * the address, that's a missing function we need to add to game.cfg.
     * We only check PCs that look like function entries (called via JSR). */

    /* ---- Step 1-6: Function entry — run native in sandbox ---- */
    /* Skip sandbox during early init — false positives from uninitialized state */
    if (g_frame_count < 1000)
        return;
    /* Trace: log when stack-level functions are hit */
    if (pc == 0x012E18u && g_frame_count >= 1100 && g_frame_count <= 1210) {
        fprintf(stderr, "[STACK-HIT] PC=$%06X frame=%"PRIu64" native_pending=%d\n",
                pc, g_frame_count, s_native_result.valid);
    }
    for (i = 0; i < g_hybrid_table_size; i++) {
        if (g_hybrid_table[i].addr == pc) {
            m68k = &s_emu->m68k;

            /* Read return address from stack */
            uint32_t sp = m68k->address_registers[7];
            uint32_t ret_addr;
            {
                extern uint32_t m68k_read32(uint32_t byte_addr);
                ret_addr = m68k_read32(sp);
            }

            /* Step 1: Snapshot FULL emulator state */
            snapshot_full();

            /* Step 2-3: Run native in sandbox */
            sync_to_native(m68k);
            /* Verify sync worked */
            if (s_total_tests < 3 && pc == 0x0133EA) {
                fprintf(stderr, "[SYNC-CHECK] interp A5=$%08X A6=$%08X D0=$%08X\n",
                        (uint32_t)m68k->address_registers[5],
                        (uint32_t)m68k->address_registers[6],
                        (uint32_t)m68k->data_registers[0]);
                fprintf(stderr, "[SYNC-CHECK] g_cpu A5=$%08X A6=$%08X D0=$%08X\n",
                        g_cpu.A[5], g_cpu.A[6], g_cpu.D[0]);
                fprintf(stderr, "[SYNC-CHECK] F603=$%02X F602=$%02X A0=$%08X\n",
                        (uint8_t)(s_emu->state.m68k.ram[(0xF603 & 0xFFFF)/2] >> 0),
                        (uint8_t)(s_emu->state.m68k.ram[(0xF602 & 0xFFFF)/2] >> 8),
                        g_cpu.A[0]);
            }
            /* Redirect g_rte_pending to a dummy during sandbox execution.
             * The native function's internal RTE codegen sets g_rte_pending=1
             * which propagates up through every JSR check, causing early returns.
             * By pointing to a dummy, these writes have no effect. */
            { extern int *g_rte_pending_ptr;
              static int s_sandbox_rte_dummy = 0;
              int *saved_ptr = g_rte_pending_ptr;
              g_rte_pending_ptr = &s_sandbox_rte_dummy;
              s_sandbox_rte_dummy = 0;
              g_hybrid_table[i].fn();
              g_rte_pending_ptr = saved_ptr; }

            /* Step 4: Save native result (registers + RAM) */
            {
                int j;
                for (j = 0; j < 8; j++) s_native_result.D[j] = g_cpu.D[j];
                for (j = 0; j < 8; j++) s_native_result.A[j] = g_cpu.A[j];
                s_native_result.SR = g_cpu.SR;
                s_native_result.func_addr = pc;
                s_native_result.ret_addr  = ret_addr;
                s_native_result.frame     = g_frame_count;
                memcpy(s_native_result.ram, s_emu->state.m68k.ram,
                       sizeof(s_native_result.ram));
                s_native_result.valid     = 1;
            }

            /* Step 5: Restore FULL emulator state — DISCARD native result */
            restore_full();

            /* Step 6: Return — interpreter runs function naturally.
             * When PC reaches ret_addr, we'll compare in the check above. */
            return;
        }
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

extern void (*g_hybrid_pre_insn_fn)(cc_u32l pc);

#if SONIC_REVERSE_DEBUG && defined(SONIC_ORACLE_BUILD)
#include "oracle_trace.h"
#endif

void HybridInit(ClownMDEmu *emu)
{
    s_emu = emu;
    s_native_result.valid = 0;
    s_total_tests = 0;
    s_total_divergences = 0;
    g_hybrid_pre_insn_fn = hybrid_pre_insn;
#if SONIC_REVERSE_DEBUG && defined(SONIC_ORACLE_BUILD)
    /* Tier 3: chain a per-instruction capture in front of hybrid_pre_insn.
     * t3_pre_insn records (pc, D, A, SR) into its ring when a range
     * filter matches, then forwards to hybrid_pre_insn. Must install
     * AFTER the line above so we capture the right prev_fn. */
    t3_install_hook();
    fprintf(stderr, "[TIER3] oracle per-instruction trace armed (t3_range to record)\n");
#endif
    fprintf(stderr, "[SANDBOX] Verification active: interpreter oracle + native comparison\n");
}

/* Legacy API stubs — not used in sandbox mode */
int  VerifyBeforeDispatch(cc_u32l func_addr, cc_u32l ret_addr)
    { (void)func_addr; (void)ret_addr; return 1; }
void VerifyTakeSnapshot(void) { }
void VerifyAfterNative(cc_u32l func_addr, cc_u32l ret_addr)
    { (void)func_addr; (void)ret_addr; }
int  VerifyTogglePhase(void) { return 0; }
int  VerifyGetPhase(void) { return 1; }
void VerifyInit(ClownMDEmu *e, CPUCallbackUserData *c)
    { (void)e; (void)c; }
