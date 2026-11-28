/*
 * m68k_interp.h — Tier-3 clean-room 68000 interpreter (the floor).
 *
 * This is the runtime correctness floor for the static recompiler: when an
 * indirect dispatch reaches an address that was not statically discovered
 * (a "dispatch miss"), this interpreter runs the supported target, then the
 * coverage-manifest layer records it so a future regen folds it into the
 * statically-recompiled Tier-1 set.
 *
 * Design (mirrors psxrecomp's "interpreter parallels the codegen" insight,
 * minus the JIT Genesis doesn't need):
 *   - It REUSES the recompiler's own decoder (recompiler/src/m68k_decoder.c),
 *     so operand/EA/immediate parsing is parity-by-construction with the
 *     static C emitter.
 *   - Its per-mnemonic semantics MIRROR recompiler/src/code_generator.c
 *     exactly (same flag formulas, same EA math, same size masking).
 *   - It operates on the SAME runtime ABI the generated C uses: the global
 *     g_cpu (M68KState) and the m68k_read/write{8,16,32} bus.
 *   - Generated and interpreted execution are cross-checked by the supported
 *     recomp-vs-interpreter cosim workflow.
 *
 * Safety contract (precision over recall): the interpreter is the floor, so
 * it cannot "decline" like a JIT. Any instruction it cannot execute must HALT
 * LOUDLY (M68KI_HALT_UNIMPL) rather than silently mis-execute. A partial
 * interpreter is therefore safe — it stops and reports, never corrupts state.
 */
#pragma once
#include <stdint.h>
#include "genesis_runtime.h"   /* M68KState, g_cpu, m68k_read/write, SR_* */

typedef enum {
    M68KI_OK = 0,          /* reached stop_pc cleanly */
    M68KI_HALT_UNIMPL,     /* hit an instruction the executor doesn't implement */
    M68KI_HALT_GUARD,      /* exceeded the instruction guard (runaway / spin) */
    M68KI_HALT_BADADDR,    /* tried to fetch an instruction from an un-fetchable PC */
} M68kiStatus;

/*
 * Run the interpreter starting at entry_pc until PC == stop_pc, operating on
 * the global g_cpu and the m68k_read/write bus. Callees reached via BSR/JSR
 * are interpreted through the real 68K stack (pure-interpret mode), so the
 * function's whole subtree runs on the interpreter; the outermost RTS pops the
 * caller-installed return address and lands on stop_pc.
 *
 * Returns M68KI_OK on a clean return, or a HALT_* code on failure. On
 * M68KI_HALT_UNIMPL, g_m68ki_bad_pc / g_m68ki_bad_op identify the instruction.
 */
M68kiStatus m68k_interp_run(uint32_t entry_pc, uint32_t stop_pc);

/*
 * Framed capsule run — the edge-aware tier-3 fallback primitive (see the long
 * comment in m68k_interp.c). Runs from entry_pc tracking net call depth and
 * stops at the depth-0 return, PEEKING the return target without popping A7
 * (A7-neutral) so the native loose-A7 caller performs the single pop. Correct
 * for computed-JSR, computed-JMP-tail, and interior-label misses alike.
 *
 * On M68KI_OK, *out_exit_pc is the peeked return target; the caller must
 * validate it is a plausible return (an implausible one is an UNSAFE_EXIT).
 * On HALT_*, the target was not runnable code (or ran away / bad fetch).
 */
M68kiStatus m68k_interp_run_framed(uint32_t entry_pc, uint32_t *out_exit_pc);

/*
 * RAM-handler capsule — executes RAM-RESIDENT code decoding every instruction
 * from LIVE memory (self-modifying copied handlers stay correct). A7-neutral
 * at the depth-0 RTS/RTR (peeks *out_exit_pc); an RTE at any depth mirrors
 * generated-code semantics (set g_rte_pending, unwind capsule-pushed frames,
 * return to the C caller). Per-instruction cycle accounting included.
 */
M68kiStatus m68k_interp_run_ram_handler(uint32_t entry_pc, uint32_t *out_exit_pc);

/*
 * Execute exactly one instruction at g_cpu.PC and advance g_cpu.PC. Used by
 * interpreter driving and diagnostic lockstep paths. Returns M68KI_OK or a
 * HALT_* status.
 */
M68kiStatus m68k_interp_step(void);

/* Interpret an interrupt HANDLER BODY from its autovector entry, stopping at the
 * handler's own depth-0 RTE (peeked, not executed). In FORCE_INTERP mode this
 * is the interpreted twin of g_game_spec.call_vblank()/call_hblank(). */
M68kiStatus m68k_interp_run_handler(uint32_t entry_pc);

/* --- Executed-PC coverage -------------------------------------------------
 * Always-on: every instruction the interpreter retires is recorded, from
 * process start. Replaces the coverage the deleted clown68000 oracle gave.
 * With GENESIS_FORCE_INTERP=1 the interpreter drives the whole program, so the
 * dump is a complete executed-PC set for the run; otherwise it covers the
 * Tier-3 floor capsules. Probes QUERY this — there is nothing to arm. */
#include <stdio.h>
int  m68k_interp_cov_active(void);      /* non-zero once anything was interpreted */
long m68k_interp_cov_dump(FILE *f);     /* ascending hex addrs, one per line; -1 if empty */

/* Per-run diagnostics (the manifest/floor layers read these). */
extern uint32_t g_m68ki_bad_pc;      /* PC of the offending instruction (UNIMPL) */
extern uint16_t g_m68ki_bad_op;      /* opcode word of the offending instruction */
extern uint64_t g_m68ki_insn_count;  /* instructions retired by the last run     */

/* Coverage discovery: the distinct JSR/BSR/JMP targets traversed during the
 * last m68k_interp_run() — i.e. the missed function's call/jump subtree. The
 * coverage-manifest layer records these as new leads: because the interpreter
 * runs callees INLINE (it does not re-dispatch them), an undiscovered callee
 * never logs as its own dispatch miss, so one floor run is the only place its
 * whole subtree surfaces. Reset at the start of each run. */
#define M68KI_MAX_DISCOVER 512
extern uint32_t g_m68ki_discover[M68KI_MAX_DISCOVER];
extern int      g_m68ki_discover_count;

/* Guard ceiling on a single m68k_interp_run() (anti-runaway). Tunable. */
#ifndef M68KI_INSN_GUARD
#define M68KI_INSN_GUARD 50000000ull
#endif
