/*
 * codegen_diag.h — centralized diagnostics for codegen coverage gaps.
 *
 * Every place in code_generator.c that emits a comment-only / TODO /
 * unsupported path records an event here so that:
 *   - a summary is printed at end-of-run
 *   - --fail-on-unsupported can turn any such event into a hard exit
 *
 * This makes 68K coverage misses machine-checkable instead of silent.
 */
#pragma once
#include <stdio.h>
#include <stdint.h>
#include "m68k_decoder.h"

typedef enum {
    CGD_MN_OTHER,
    CGD_TODO_MOVEP,
    CGD_TODO_CHK,
    /* Retired in Phase 7A: CGD_TODO_TRAP/TRAPV/RTR/RESET/ILLEGAL —
     * those mnemonics now route through m68k_trap_vector /
     * m68k_illegal_trap / genesis_reset_devices.
     * Retired in Phase 7B: CGD_TODO_ABCD/SBCD/NBCD — real packed-BCD
     * codegen with sticky-Z semantics now lives in code_generator.c. */
    CGD_TODO_ADDX_MEM_PREDEC,
    CGD_TODO_SUBX_MEM_PREDEC,
    CGD_TODO_DYNAMIC_JSR_UNSUPPORTED,
    CGD_TODO_DYNAMIC_JMP_UNSUPPORTED,
    CGD_BRANCH_WITHOUT_TARGET,
    CGD_INVALID_STORE_EA,
    CGD_MOVE_CCR_DIRECTION_AMBIGUOUS,
    CGD_KIND_COUNT
} CodegenDiagKind;

typedef struct {
    CodegenDiagKind kind;
    uint32_t        addr;
    uint16_t        opcode;
    M68KMnemonic    mnemonic;
    const char     *func_name;   /* annotation name of containing function, or NULL */
    uint32_t        func_addr;   /* entry of containing function */
} CodegenDiagEvent;

void codegen_diag_reset(void);
void codegen_diag_record(CodegenDiagKind kind, uint32_t addr, uint16_t opcode,
                         M68KMnemonic mn, const char *func_name, uint32_t func_addr);
int  codegen_diag_total(void);
int  codegen_diag_count(CodegenDiagKind kind);
int  codegen_diag_event_count(void);
const CodegenDiagEvent *codegen_diag_get(int i);

const char *codegen_diag_kind_str(CodegenDiagKind kind);
void codegen_diag_print_summary(FILE *out);
