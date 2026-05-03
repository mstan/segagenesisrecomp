/*
 * m68k_validator.c — post-decode legality checks for MC68000.
 *
 * The decoder is intentionally permissive so it never aborts mid-scan,
 * and so generated unit tests can probe corner cases. This validator
 * is the gate that callers consult to decide whether an instruction
 * should drive scanning / codegen as if it were real executable code.
 */
#include "m68k_validator.h"

/* Set of EA modes that count as "alterable memory" for store
 * destinations (per 68K Programmer's Reference, "Memory Alterable").
 * Excludes immediate, PC-relative, and An direct. */
static bool is_data_alterable_dst(int ea) {
    int mode = (ea >> 3) & 7;
    if (mode == 0) return true;             /* Dn  */
    if (mode == 1) return false;            /* An direct — caller decides */
    if (mode >= 2 && mode <= 6) return true;
    /* mode 7: only abs.W (0) and abs.L (1) are alterable. */
    int reg = ea & 7;
    return (reg == 0 || reg == 1);
}

static bool ea_is_immediate(int ea) {
    return ((ea >> 3) & 7) == 7 && (ea & 7) == 4;
}
static bool ea_is_pc_relative(int ea) {
    return ((ea >> 3) & 7) == 7 && ((ea & 7) == 2 || (ea & 7) == 3);
}

M68KValidity m68k_validate(const M68KInstr *instr,
                           const M68KValidatorOptions *opts) {
    if (!instr) return M68K_LEGAL;

    /* Phase 7A retired the blanket "0x4AFC / A-line / F-line are illegal-
     * encoding" rule. Those opcodes now decode to MN_ILLEGAL with real
     * codegen (m68k_illegal_trap → vectored trap), so they are valid
     * executable instructions when reached deliberately. They remain
     * dangerous when reached *speculatively* during function discovery
     * (data masquerading as code), but the function_finder doesn't need
     * the validator to flag them — m68k_is_terminator now treats
     * MN_ILLEGAL as a terminator, ending the linear scan cleanly. */

    /* Branches with the 32-bit displacement form (d8==0xFF) are 68020+.
     * The decoder tolerates them; the validator flags them unless the
     * caller has opted into 68020 compatibility. */
    bool allow_68020 = opts && opts->allow_68020_branch;
    if ((instr->mnemonic == MN_BRA  || instr->mnemonic == MN_BSR ||
         instr->mnemonic == MN_Bcc) &&
        !allow_68020) {
        uint16_t w0 = instr->words[0];
        if ((w0 & 0xFF) == 0xFF) return M68K_ILLEGAL_NON_MC68000;
    }

    switch (instr->mnemonic) {
    /* MOVEA is W or L only. The decoder produces MOVEA when MOVE.B
     * has An destination, which is itself illegal — surface it. */
    case MN_MOVEA:
        if (instr->size == M68K_SIZE_B) return M68K_ILLEGAL_SIZE;
        break;

    /* MOVE.B cannot have an address-register destination (illegal
     * encoding on MC68000; should have been MOVEA but only W/L). */
    case MN_MOVE:
        if (instr->size == M68K_SIZE_B &&
            ((instr->dst_ea >> 3) & 7) == 1) return M68K_ILLEGAL_DST_EA;
        /* immediate / PC-relative cannot be a MOVE destination */
        if (ea_is_immediate(instr->dst_ea) ||
            ea_is_pc_relative(instr->dst_ea))
            return M68K_ILLEGAL_DST_EA;
        break;

    /* Reserved size encoding (ss==3) for non-A variants of arithmetic
     * and logical ops. The decoder routes ss==3 to the *_A address-
     * register variants, but a residual ss==3 reaching the plain
     * mnemonic means the decoder fell into a pattern that would have
     * been illegal on hardware. */
    case MN_ADD: case MN_SUB: case MN_AND: case MN_OR: case MN_EOR:
    case MN_CMP:
        if ((int)instr->size > (int)M68K_SIZE_L)
            return M68K_ILLEGAL_RESERVED_SIZE;
        break;

    /* Stores cannot use immediate or PC-relative destinations.
     * (CLR/NEG/NOT/NEGX/TST/Scc/TAS/NBCD/MOVEM mem-write and friends
     * all share emit_ea_store; the validator gives them one rule.) */
    case MN_CLR: case MN_NEG: case MN_NEGX: case MN_NOT: case MN_TAS:
    case MN_NBCD: case MN_Scc:
        if (ea_is_immediate(instr->src_ea) ||
            ea_is_pc_relative(instr->src_ea))
            return M68K_ILLEGAL_DST_EA;
        if (!is_data_alterable_dst(instr->src_ea) &&
            ((instr->src_ea >> 3) & 7) != 1)   /* allow An only where the op spec does */
            return M68K_ILLEGAL_DST_EA;
        break;

    /* MN_OTHER is the residual decode-miss class: any byte sequence
     * the decoder couldn't classify after Phase 7A's coverage. Treat as
     * illegal so speculative scans stop on unknown encodings instead of
     * extending function bodies into ambiguous data. ILLEGAL / A-line /
     * F-line are no longer folded here — they decode to MN_ILLEGAL
     * directly and are treated as legitimate (trap-emitting) code. */
    case MN_OTHER:
        return M68K_ILLEGAL_OPCODE;

    default:
        break;
    }

    return M68K_LEGAL;
}

const char *m68k_validity_str(M68KValidity v) {
    switch (v) {
    case M68K_LEGAL:                return "legal";
    case M68K_ILLEGAL_SIZE:         return "illegal-size";
    case M68K_ILLEGAL_SRC_EA:       return "illegal-src-ea";
    case M68K_ILLEGAL_DST_EA:       return "illegal-dst-ea";
    case M68K_ILLEGAL_RESERVED_SIZE:return "illegal-reserved-size";
    case M68K_ILLEGAL_NON_MC68000:  return "illegal-non-mc68000";
    case M68K_ILLEGAL_OPCODE:       return "illegal-opcode";
    }
    return "?";
}
