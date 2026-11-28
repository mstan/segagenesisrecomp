/* cosim_cycles.c — cross-backend 68K WORK-cycle ruler (pairing #2).
 *
 * Gap #1/#4 in COSIM.md: the own backend fast-forwards the WaitForVBla idle
 * spin, so it shares no per-instruction cycle axis with the (literal) clownmdemu
 * oracle, and pairing #2 had only the frame-ordinal clock. This module gives
 * both backends a directly-comparable number: the 68K cycles of REAL WORK a
 * logical frame does before it parks at WaitForVBla — the "apples-to-apples
 * timing" ruler.
 *
 * Why it is comparable. Both sides measure in the SAME unit: the clown68000-
 * measured per-instruction cost table (recompiler/src/cycle_probe.h ->
 * g_game_insn_costs). The own backend already sums it into g_cycle_accumulator
 * (emit_cycle_accounting); the oracle sums game_insn_cost(pc) in a per-
 * instruction hook. clownmdemu's 68K IS clown68000, so game_insn_cost(pc) is
 * exactly the cost it charges. The idle spin is EXCLUDED on both sides (own
 * fast-forwards it and never charges; the oracle skips charging while pc is in
 * the WaitForVBla window), so this is honest WORK, not the fixed frame budget
 * (~127856 cyc) that the spin would otherwise pad every frame up to.
 *
 * Window. The comparable window is PARK-to-PARK — the game's logical frame,
 * which is phase-shifted from clownmdemu's raster frame. Both sides capture the
 * work done between two consecutive WaitForVBla entries, so the per-frame
 * VALUE is comparable even though the raster phase differs.
 *
 * NOT psxrecomp's mechanism. psxrecomp never fast-forwards spins, so its
 * backends converge on absolute cycles for free. We deliberately keep the
 * fast-forward (it is the shipping runner's frame-pacing model), so the cosim
 * measures the machine that actually ships, and we recover the axis by charging
 * work cycles on both sides against the same cost table.
 *
 * Compiled only under GENESIS_COSIM (both the own-backend `_cosim` and the
 * clownmdemu `_oracle_cosim` targets). Zero effect on the shipping build.
 */
#include "cosim.h"
#include "game_cycles.h"

#include <stdint.h>
#include <stdlib.h>

/* Reported by the `cyclefields` TCP command; same names/units on both backends
 * so the coordinator diffs them directly. */
uint64_t g_cosim_work_cycles = 0;   /* work cycles of the most recent logical frame */
uint64_t g_cosim_cum_cycles  = 0;   /* monotonic sum of per-frame work (spin excl.) */
uint64_t g_cosim_park_count  = 0;   /* WaitForVBla parks so far (post-priming) — lets
                                     * the coordinator tell a comparable frame (both
                                     * sides parked once) from a STALE one (own didn't
                                     * park; oracle's cum advances every instruction). */
uint64_t g_cosim_work_insns  = 0;   /* instructions executed in the most recent frame
                                     * (spin excluded) — distinguishes a same-stream
                                     * cost-lookup delta from a control-flow divergence */
uint64_t g_cosim_fb_count    = 0;   /* oracle: instructions charged the FALLBACK cost
                                     * this frame (pc not in g_game_insn_costs); 0 own  */

/* -------- Own backend: park-to-park delta of the monotonic cosim axis. -----
 * glue_yield_for_vblank() calls this at the WaitForVBla park. We take the delta
 * of g_cosim_cycle (bumped per-instruction by GEN_COSIM_TICK for ALL executed
 * code — the V-int handler AND the main loop) between consecutive parks. The
 * fast-forwarded idle spin is never executed, so it never advances g_cosim_cycle
 * and is excluded — exactly matching the oracle, which excludes the spin window.
 *
 * NOTE we deliberately do NOT use g_cycle_accumulator here: it is reset after
 * each resume (glue_yield_for_vblank FIBER_FULL path), so it excludes the V-int
 * handler that ran on the main fiber before resume — which would systematically
 * undercount vs the oracle (whose handler runs inside the frame and is counted). */
extern uint64_t g_cosim_cycle;        /* monotonic per-instruction cosim axis (cosim.c) */
extern uint64_t g_native_insn_count;  /* recompiled instructions retired (glue.c)        */
static uint64_t s_last_park = 0;
static uint64_t s_last_insn = 0;
static int      s_primed    = 0;   /* first park only primes the baseline */

void cosim_cycles_note_park(void)
{
    if (!s_primed) {
        s_last_park = g_cosim_cycle; s_last_insn = g_native_insn_count; s_primed = 1;
        return;
    }
    g_cosim_work_cycles = g_cosim_cycle - s_last_park;
    g_cosim_work_insns  = g_native_insn_count - s_last_insn;
    g_cosim_cum_cycles += g_cosim_work_cycles;
    s_last_park = g_cosim_cycle;
    s_last_insn = g_native_insn_count;
    ++g_cosim_park_count;
}

