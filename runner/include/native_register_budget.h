#ifndef GENESIS_NATIVE_REGISTER_BUDGET_H
#define GENESIS_NATIVE_REGISTER_BUDGET_H
#include <stdint.h>

/* Read-only qualification for an unobservable, fixed-charge register interval.
 * The caller publishes its counters and invokes its ordinary retirement check
 * once on exit. No interval may hide a watchdog, wrap, yield or clock rescale. */
static inline int genesis_native_register_budget(
    uint32_t cycle, uint32_t audio, uint64_t instructions, uint64_t watchdog_base,
    uint64_t watchdog_limit, uint32_t budget_check_at, int interleave, int service,
    unsigned divisor, int32_t budget, uint32_t seen, unsigned cycles, unsigned count)
{
    uint64_t used=instructions-watchdog_base;
    if(!count || !cycles || used>=watchdog_limit ||
       (uint64_t)count>=watchdog_limit-used || cycles>=UINT32_MAX-cycle ||
       cycles>=UINT32_MAX-audio)return 0;
    if(!interleave || service)return 1;
    if(cycle<budget_check_at)return cycles<budget_check_at-cycle;
    /* A scaled clock is normalized by every ordinary check. Keep that exact
     * stream, rather than assuming repeated rounded conversions compose. */
    if(divisor>1 || budget<=0 || audio<seen)return 0;
    uint32_t elapsed=audio-seen;
    return elapsed<(uint32_t)budget && cycles<(uint32_t)budget-elapsed;
}
#endif
