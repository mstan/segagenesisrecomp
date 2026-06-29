/* m68k_cycle_cost.h — operand-keyed (data-dependent) instruction cycle costs.
 *
 * PARKED / EXPERIMENTAL. This branch (accuracy/operand-keyed-cycle-costs) holds
 * the reverted operand-keyed cycle-cost work. It is numerically correct but
 * destabilises the own-backend's timing-sensitive execution (broke Sonic 3 —
 * pacing shift → control-flow divergence → interior-label $00043A dispatch
 * miss). See GENESIS_ACCURACY_BURNDOWN.md "revisit someday" item 7. Do NOT merge
 * until the own-backend frame pacing is robust to per-instruction cost changes
 * and the $00043A-class interior JMP target has a landing pad.
 *
 * The static recompiler probes clown68000 at codegen time for each instruction's
 * exact cycle cost (cycle_probe.c). For most instructions the cost is fixed and
 * the probe is exact. A handful are *data-dependent*: their cost varies with the
 * runtime operand value, which codegen cannot know (the probe substitutes a fixed
 * synthetic operand, so the baked-in constant over- or under-counts):
 *
 *   - MULU.W  cost = 38 + 2 * (number of 1 bits in the source)
 *   - MULS.W  cost = 38 + 2 * (number of 10/01 bit-pairs in the source)
 *   - DIVU.W / DIVS.W  cost depends on dividend and divisor (a per-bit algorithm)
 *   - register-counted shifts/rotates  cost = base + 2 * count
 *
 * These inline helpers reproduce clown68000's *exact* cycle model (the project's
 * validated cost oracle; see Action_MULCommon / Action_DIVCommon / the
 * ShiftRotate execution-time helpers in clown68000.c). The recompiler emits a
 * runtime cycle add for the data-dependent instructions using these, so the
 * recompiled CPU charges the real operand-dependent cost instead of a synthetic
 * guess. Verified numerically against the clown68000 probe in
 * tools/cycle_compare/insn_cost (MULU 38/54/70, MULS 38/40/70, DIVU 136/134,
 * DIVS 150/154, ASL.W reg-count 8/14/48).
 *
 * Shared between the runner (runtime) and the recompiler (which calls the same
 * functions at the synthetic operand to recover the operand-independent base:
 * fixed = probe_total - action_at_synthetic_operand).
 */
#ifndef M68K_CYCLE_COST_H
#define M68K_CYCLE_COST_H

#include <stdint.h>

static inline int m68k_cyc_popcount(uint32_t v) {
    int c = 0;
    while (v) { c += (int)(v & 1u); v >>= 1; }
    return c;
}

/* MULU.W: variable part = 2 * popcount(source). (clown Action_MULU: 34 + 2*ones;
 * the 34 + EA + decode base is the operand-independent part recovered separately.) */
static inline int m68k_mulu_var_cycles(uint16_t src) {
    return 2 * m68k_cyc_popcount((uint32_t)src);
}

/* MULS.W: variable part = 2 * (number of 10 and 01 bit-pairs in source<<1).
 * Mirrors clown Action_MULS exactly. */
static inline int m68k_muls_var_cycles(uint16_t src) {
    uint32_t s   = (uint32_t)src << 1;
    uint32_t p10 = (s ^ (s << 1)) & ((uint32_t)0xAAAA << 1);
    uint32_t p01 = (s ^ (s >> 1)) & ((uint32_t)0xAAAA >> 1);
    return 2 * (m68k_cyc_popcount(p10) + m68k_cyc_popcount(p01));
}

/* DIVU.W: the cycle count clown's Action_DIVCommon adds (is_signed = false),
 * i.e. everything from the divide algorithm; the caller adds the instruction
 * base (4) + source EA-read cost. `src16` is the raw 16-bit divisor. */
static inline int m68k_divu_action_cycles(uint32_t dest, uint16_t src16) {
    if (src16 == 0) return 30;             /* divide-by-zero trap: DoInterrupt(5) = +30 */
    {
        uint32_t asrc = src16;
        uint32_t adst = dest;
        int cyc = 6;
        if (asrc >= (adst >> 16)) {
            uint32_t shifted_divisor = (asrc & 0xFFFFu) << 16;
            uint32_t wd = adst;
            int i;
            cyc += 66;
            for (i = 0; i < 15; ++i) {
                int high = (wd & 0x80000000u) != 0;
                wd <<= 1;
                if (!high) {
                    cyc += 2;
                    if (wd < shifted_divisor) { cyc += 2; continue; }
                }
                wd -= shifted_divisor;
            }
        }
        /* Overflow (asrc < adst>>16) adds nothing beyond the base 6. */
        return cyc;
    }
}

/* DIVS.W: the cycle count clown's Action_DIVCommon adds (is_signed = true).
 * `src16` is the raw 16-bit divisor (sign-extended internally). */
static inline int m68k_divs_action_cycles(uint32_t dest, uint16_t src16) {
    if (src16 == 0) return 30;
    {
        int src_neg = (src16 & 0x8000u) != 0;
        int dst_neg = (dest & 0x80000000u) != 0;
        uint32_t asrc = src_neg ? (uint32_t)(0u - (int32_t)(int16_t)src16) : (uint32_t)src16;
        uint32_t adst = dst_neg ? (0u - dest) : dest;
        int cyc = 6;
        cyc += dst_neg ? 8 : 6;
        if (asrc >= (adst >> 16)) {
            uint32_t aq = adst / asrc;
            cyc += 104;
            if (src_neg)      cyc += 2;
            else if (dst_neg) cyc += 4;
            cyc += (15 - m68k_cyc_popcount(aq >> 1)) * 2;
        }
        return cyc;
    }
}

#endif /* M68K_CYCLE_COST_H */
