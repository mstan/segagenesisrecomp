/* cosim.h — Genesis differential co-simulation: full-state hashing + lockstep.
 *
 * Compiled ONLY into the genesis-cosim build target (GENESIS_COSIM). Zero
 * effect on the shipping native / oracle builds. Mirrors psxrecomp's
 * cosim.c / cosim_state.c (see F:\Projects\recomp-template\
 * DIFFERENTIAL-COSIMULATION.md and .../GENESIS/DIFFERENTIAL-COSIM-PROPOSAL.md).
 *
 * Method: two deterministic instances of the SAME machine (A = recompiled
 * 68K, B = the clean-room m68k_interp, both driving the same ymfm/sn76489/
 * Z80/VDP) are advanced in lockstep on the master clock. At each checkpoint we
 * FNV-1a hash the ENTIRE architectural state, per subsystem, and a coordinator
 * halts at the first mismatch. The per-subsystem sub-hash names WHICH subsystem
 * split first; a field dump names WHICH field.
 *
 * Discipline (from the proposal): hash the FULL state — including the ymfm and
 * sn76489 internals that frame_snapshots.c deliberately ZEROES. If the boop is
 * chip-internal synthesis, only a full-state hash can see it. Do NOT trim the
 * surface on a hypothesis about where the bug is.
 */
#ifndef GENESIS_COSIM_H
#define GENESIS_COSIM_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ FNV-1a */
/* 64-bit FNV-1a, matching psxrecomp's constants so the two ecosystems'
 * tooling reads the same. */
#define COSIM_FNV_OFFSET 1469598103934665603ULL
#define COSIM_FNV_PRIME  1099511628211ULL

static inline uint64_t cosim_fnv_init(void) { return COSIM_FNV_OFFSET; }

static inline uint64_t cosim_fnv_bytes(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= COSIM_FNV_PRIME; }
    return h;
}
/* Explicit little-endian scalar folds — never hash struct memory directly
 * (padding bytes are a classic false-divergence source). */
static inline uint64_t cosim_fnv_u8 (uint64_t h, uint8_t  v){ return cosim_fnv_bytes(h,&v,1); }
static inline uint64_t cosim_fnv_u16(uint64_t h, uint16_t v){ uint8_t b[2]={(uint8_t)v,(uint8_t)(v>>8)}; return cosim_fnv_bytes(h,b,2); }
static inline uint64_t cosim_fnv_u32(uint64_t h, uint32_t v){ uint8_t b[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)}; return cosim_fnv_bytes(h,b,4); }
static inline uint64_t cosim_fnv_u64(uint64_t h, uint64_t v){ h=cosim_fnv_u32(h,(uint32_t)v); return cosim_fnv_u32(h,(uint32_t)(v>>32)); }

/* Fold a value into a rolling chain hash (order-sensitive). */
static inline uint64_t cosim_fold(uint64_t h, uint64_t v){ return cosim_fnv_u64(h, v); }

/* -------------------------------------------------- per-subsystem sub-hashes */
/* One 64-bit hash per guest subsystem. The coordinator diffs these on a chain
 * mismatch to localize WHICH subsystem split first. Add fields here and in
 * cosim_state.c together; keep the `sub` wire order in cosim.c in sync. */
typedef struct CosimSubHashes {
    uint64_t cpu68k;    /* g_cpu D/A/SR/USP  (PC excluded — currency caveat)   */
    uint64_t timing;    /* g_audio_cycle_counter, g_68k_stamp_rebase, vint     */
                        /* latch, master_cycle, z80_cycle_debt                 */
    uint64_t ram;       /* 64 KB 68K work RAM (g_ram)                          */
    uint64_t z80;       /* superzazu register file (funcptrs excluded)         */
    uint64_t z80ram;    /* 8 KB Z80 RAM (g_machine.bus.z80_ram)                */
    uint64_t handshake; /* 68K<->Z80 bus latches + I/O/pad + SRAM overlay      */
    uint64_t vdp;       /* VRAM/CRAM/VSRAM/regs/FSM/status/HV/DMA (no funcptrs) */
    uint64_t fm;        /* ymfm YM2612 full internal state (un-zeroed)         */
    uint64_t psg;       /* sn76489 SN76489 full state (LFSR, counters, ...)    */
    uint64_t evq;       /* pending (undrained) cycle-stamped audio events      */
} CosimSubHashes;

/* Compute all sub-hashes of the CURRENT live guest state. If `sub` is non-NULL
 * it is filled with the per-subsystem hashes. Returns the combined full-state
 * hash (fold of all sub-hashes, in struct order). Side-effect-free. */
uint64_t cosim_state_hash(CosimSubHashes *sub);

/* Pairing #2 (recomp own-backend vs clownmdemu oracle): hash the guest-VISIBLE
 * surface (normalized identically for both backends via frame_snapshots.c), NOT
 * the bit-exact full state (doomed across independent implementations). Fills
 * cpu68k/ram/z80/z80ram/vdp/evq; leaves timing/handshake/fm/psg = 0. Defined in
 * cosim_visible.c; compiles on BOTH the own-backend and oracle builds. Selected
 * at checkpoint time by env GENESIS_COSIM_VISIBLE=1. */
uint64_t cosim_state_hash_visible(CosimSubHashes *sub);

/* Gate-3 fault injection (applied to live state at the next checkpoint). */
void cosim_inject_ram(uint32_t addr /*offset into g_ram*/, uint8_t xor_val);
void cosim_inject_reg(int reg_index /*0..7 D, 8..15 A*/, uint32_t xor_val);
void cosim_state_apply_pending_injection(void);  /* called by cosim_tick */

/* Reset incremental/injection state (not the guest). */
void cosim_state_reset(void);

/* ---------------------------------------------------------- lockstep server */
/* Bring up the TCP command server + arm the checkpoint machinery. Reads env:
 *   GENESIS_COSIM_PORT   (default 4600)
 *   GENESIS_COSIM_STRIDE (cycles between checkpoints on the monotonic axis;
 *                         large => frame-sync grain, small => cycle-exact)
 * Call once, early, before the guest runs an instruction.
 *
 * cosim_checkpoint() (declared in genesis_runtime.h so the generated code and
 * the interpreter can call it via GEN_COSIM_TICK) is the per-instruction hook:
 * it hashes full architectural state, records a ring row, folds the chain, and
 * PARKS until the coordinator grants budget via `step N`. */
void cosim_init(void);

/* FRAME-mode checkpoint: call once per wall frame AFTER the audio drain +
 * g_audio_cycle_counter reset, passing g_machine.master_cycle (the backend-
 * independent ruler). No-op unless FRAME clock mode is active. */
void cosim_frame_checkpoint(uint64_t master_cycle);

/* Is the cosim active (env/port up)? Guards the FORCE_INTERP path. */
int  cosim_active(void);

/* ---------------------------------------------- cross-backend work-cycle ruler */
/* Per-frame 68K WORK cycles (executed before the WaitForVBla idle wait) and a
 * monotonic cumulative of same, in the SAME unit on both backends (clown-
 * measured g_game_insn_costs) — so directly comparable across own-backend and
 * oracle. The idle spin is EXCLUDED, so this is real work, not the fixed frame
 * budget. Served by the `cyclefields` command. See cosim_cycles.c. */
extern uint64_t g_cosim_work_cycles;   /* most recent logical frame's work cycles */
extern uint64_t g_cosim_cum_cycles;    /* monotonic sum of per-frame work          */
extern uint64_t g_cosim_park_count;    /* WaitForVBla parks so far (comparability) */
extern uint64_t g_cosim_work_insns;    /* instructions in the most recent frame     */
extern uint64_t g_cosim_fb_count;      /* oracle: fallback-costed insns this frame  */

#if OWN_BACKEND
/* Own backend: called at each WaitForVBla park; captures the park-to-park delta
 * of the monotonic cosim axis (g_cosim_cycle) as this frame's work cycles. */
void cosim_cycles_note_park(void);
#else
/* Oracle: install the per-instruction cycle-charging hook (chains onto the
 * existing g_hybrid_pre_insn_fn). Reads GENESIS_COSIM_WAITVBL_PC for the
 * spin-entry PC. Call once from cosim_init, AFTER glue_init/HybridInit. */
void cosim_cycles_oracle_install(void);
#endif

#endif /* GENESIS_COSIM_H */
