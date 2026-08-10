/* game_cycles.h — per-instruction cycle-cost table (single source of truth).
 *
 * The recompiler stamps each translated instruction with a cycle cost from the
 * clean-room MC68000 timing model in m68k-recomp-core — that's what
 * drives native's audio-stamp pacing. The interpreter (Tier-3 floor / co-sim
 * B-side) must charge the IDENTICAL cost, or its g_audio_cycle_counter drifts
 * from the recompiled code's. The recompiler emits the exact costs it used as
 * <prefix>_cycles.c (the g_game_insn_costs table); game_insn_cost() looks them
 * up by ROM address. Returns -1 for addresses not in the table (e.g. RAM-
 * resident code), where the caller falls back to a static estimate.
 */
#ifndef GAME_CYCLES_H
#define GAME_CYCLES_H

#include <stdint.h>
#include <stddef.h>

typedef struct { uint32_t addr; uint16_t cost; } GameInsnCost;

extern const GameInsnCost g_game_insn_costs[];   /* sorted by addr, per-game TU */
extern const size_t       g_game_insn_cost_count;

/* Generated cost for the instruction at ROM `addr`, or -1 if not present. */
int game_insn_cost(uint32_t addr);

#endif /* GAME_CYCLES_H */
