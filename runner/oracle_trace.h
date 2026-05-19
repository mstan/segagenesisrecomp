/*
 * oracle_trace.h — Tier-3 reverse debugger: per-instruction CPU state
 * capture on the oracle (clown68000 interpreter) side.
 *
 * Design: chain into the existing g_hybrid_pre_insn_fn hook. Before
 * each 68K instruction the interpreter executes, snapshot
 * (pc, D[8], A[8], SR, frame) into a ring buffer, filtered by a
 * TCP-configurable set of PC ranges. Combined with Tier 2's native
 * block snapshots, this lets a Python tool pair (pc, nth-hit) entries
 * across targets and find the first 68K instruction where native and
 * oracle diverge during live gameplay.
 *
 * Double-gated: built only when SONIC_REVERSE_DEBUG is on AND we're in
 * the oracle build (SONIC_ORACLE_BUILD). The native build has no
 * interpreter (stub_clown68000.c stubs DoCycles), so a pre-instruction
 * hook has nothing to hook. All t3_* TCP handlers still compile on
 * native but reply with "oracle only" errors for a uniform surface.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#if SONIC_REVERSE_DEBUG && defined(SONIC_ORACLE_BUILD)

/* Install / uninstall the pre-insn chain. install() must run AFTER the
 * existing hybrid_pre_insn installation in HybridInit — we save the
 * previous fn pointer and forward to it on every call. Idempotent. */
void t3_install_hook(void);
void t3_uninstall_hook(void);

/* Add a PC-range filter (inclusive). Activates capture on first call.
 * Returns 0 if the range table is full (max 8). */
int  t3_add_range(uint32_t lo, uint32_t hi);

/* Clear ring + ranges + deactivate. */
void t3_reset(void);

int      t3_range_count(void);
int      t3_range_get(int i, uint32_t *lo_out, uint32_t *hi_out);

/* Snapshot API for paged JSON dumps. During a snapshot, live captures
 * still enter the ring but aren't visible through format_entry, so
 * the paged dump is consistent across multiple TCP calls. */
void     t3_snapshot_begin(void);
void     t3_snapshot_end  (void);
uint32_t t3_snapshot_count(void);

/* Format entry at snapshot-index i as a JSON object into buf. Returns
 * bytes written or -1 on bad index / buffer overflow. Entry shape:
 *   {"f":N,"pc":"0xXXXXXX","sr":"0xXXXX",
 *    "D":[d0,...,d7],"A":[a0,...,a7]}
 * All register values are 32-bit unsigned, SR is 16-bit. */
int t3_format_entry(uint32_t i, char *buf, size_t buflen);

/* ---- Oracle break/step (Phase 3 of the Tier-4 vision) ----
 *
 * Set PC-breakpoints that fire when the interpreter reaches a given PC.
 * On hit, t3_pre_insn sets g_oracle_parked and enters a cmd_server_poll
 * spin loop. cmd_server is safe to call from here because it's
 * non-blocking and runs on the same (main) thread. User sends
 * rdb_oracle_step_insn or rdb_oracle_continue to advance/resume.
 *
 * step_insn is one-shot: the next instruction parks, then clears. */
int  oracle_break_add(uint32_t pc);
void oracle_break_clear_all(void);
int  oracle_break_count(void);
uint32_t oracle_break_get(int i);
void oracle_cmd_step_insn(void);
void oracle_cmd_continue(void);

int      oracle_is_parked(void);
uint32_t oracle_parked_pc(void);

/* ---- Phase 4: backward stepping via snapshot+replay ----
 *
 * Every ORACLE_SNAP_INTERVAL instructions, t3_pre_insn snapshots the
 * full ClownMDEmu struct (68K+Z80+VDP+FM state) into a ring. When the
 * user requests step-back, we rewind to the most recent snapshot with
 * insn_count < current, restoring all emulator state.
 *
 * Step-back granularity is thus one SNAPSHOT interval (~ORACLE_SNAP_
 * INTERVAL insns). To get finer resolution, step-back then step-insn
 * forward to the exact target.
 *
 * Snapshots are taken unconditionally (not gated on break arming) so
 * the user can go back in time from any parked moment. Cost: one
 * struct copy every ORACLE_SNAP_INTERVAL insns — tolerable.
 */
int      oracle_step_back(uint64_t *restored_insn_count_out);
int      oracle_snap_count(void);
uint64_t oracle_snap_insn_at(int i);  /* 0 if slot empty */

#endif /* SONIC_REVERSE_DEBUG && SONIC_ORACLE_BUILD */
