/*
 * m68k_validator.h — post-decode opcode legality checks.
 *
 * The decoder is permissive (returns true for nearly every byte
 * sequence). Speculative scans through data regions can therefore
 * extend false function bodies, and silently invalid encodings can
 * reach codegen and emit "cannot store" comments instead of failing
 * loudly. This module classifies each decoded instruction so that:
 *   - function_finder can terminate paths that contain illegal
 *     encodings (so scan stops at data masquerading as code)
 *   - code_generator can route illegal encodings to a vectored
 *     ILLEGAL trap and record a diagnostic
 *
 * Scope is strictly MC68000. Branch displacements via byte 0xFF (the
 * 32-bit displacement form) are 68020+ and are reported as
 * M68K_ILLEGAL_NON_MC68000 unless the caller opts in via
 * cfg.allow_68020_branch.
 */
#pragma once
#include <stdbool.h>
#include "m68k_decoder.h"

typedef enum {
    M68K_LEGAL = 0,
    M68K_ILLEGAL_SIZE,           /* operand size invalid for mnemonic */
    M68K_ILLEGAL_SRC_EA,         /* source EA mode not allowed        */
    M68K_ILLEGAL_DST_EA,         /* destination EA mode not allowed   */
    M68K_ILLEGAL_RESERVED_SIZE,  /* size encoding 11 reserved         */
    M68K_ILLEGAL_NON_MC68000,    /* 68020+ form on MC68000 target     */
    M68K_ILLEGAL_OPCODE,         /* ILLEGAL / A-line / F-line trap    */
} M68KValidity;

typedef struct {
    bool allow_68020_branch;     /* tolerate Bcc/BSR/BRA d8==0xFF      */
} M68KValidatorOptions;

M68KValidity m68k_validate(const M68KInstr *instr,
                           const M68KValidatorOptions *opts);
const char *m68k_validity_str(M68KValidity v);
