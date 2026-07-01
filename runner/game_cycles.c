/* game_cycles.c — binary-search lookup over the recompiler-emitted per-
 * instruction cycle-cost table (see game_cycles.h). */
#include "game_cycles.h"

int game_insn_cost(uint32_t addr) {
    size_t lo = 0, hi = g_game_insn_cost_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g_game_insn_costs[mid].addr < addr) lo = mid + 1;
        else                                    hi = mid;
    }
    if (lo < g_game_insn_cost_count && g_game_insn_costs[lo].addr == addr)
        return (int)g_game_insn_costs[lo].cost;
    return -1;
}
