/*
 * m68k_decoder.h — Motorola 68000 instruction decoder interface.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "rom_parser.h"

/* Instruction size qualifier */
typedef enum {
    M68K_SIZE_NONE = -1,
    M68K_SIZE_B    =  0,   /* .B — byte */
    M68K_SIZE_W    =  1,   /* .W — word */
    M68K_SIZE_L    =  2,   /* .L — long */
} M68KSize;

/* Mnemonic classes relevant to the recompiler */
typedef enum {
    MN_OTHER,    /* Anything not specially handled */
    MN_MOVE,
    MN_MOVEQ,
    MN_JSR,
    MN_BSR,
    MN_JMP,
    MN_BRA,
    MN_Bcc,
    MN_DBcc,
    MN_RTS,
    MN_RTE,
    MN_NOP,
    MN_STOP,
    MN_TRAP,
    /* Extended mnemonics */
    MN_MOVEA,
    MN_MOVEM,
    MN_LEA,
    MN_PEA,
    MN_TST,
    MN_CLR,
    MN_NEG,
    MN_NEGX,
    MN_NOT,
    MN_EXT,
    MN_SWAP,
    MN_ORI,
    MN_ANDI,
    MN_SUBI,
    MN_ADDI,
    MN_EORI,
    MN_CMPI,
    MN_ADD,
    MN_ADDA,
    MN_ADDQ,
    MN_SUB,
    MN_SUBA,
    MN_SUBQ,
    MN_AND,
    MN_OR,
    MN_EOR,
    MN_CMP,
    MN_CMPA,
    MN_LSL,
    MN_LSR,
    MN_ASL,
    MN_ASR,
    MN_ROL,
    MN_ROR,
    MN_ROXL,
    MN_ROXR,
    MN_Scc,
    MN_LINK,
    MN_UNLK,
    MN_MULS,
    MN_MULU,
    MN_DIVS,
    MN_DIVU,
    MN_ABCD,
    MN_SBCD,
    MN_BTST,
    MN_BCHG,
    MN_BCLR,
    MN_BSET,
    MN_MOVEP,
    MN_CHK,
    MN_NBCD,
    MN_TAS,
    MN_MOVE_USP,
    MN_MOVE_SR,
    MN_MOVE_CCR,
    MN_EXG,
    MN_ADDX,
    MN_SUBX,
} M68KMnemonic;

#define M68K_MAX_WORDS 8   /* Maximum instruction length in 16-bit words */

typedef struct {
    uint32_t     addr;                    /* ROM address of this instruction */
    M68KMnemonic mnemonic;
    M68KSize     size;
    uint16_t     words[M68K_MAX_WORDS];   /* Raw instruction words */
    int          word_count;
    uint32_t     byte_length;             /* Total bytes consumed */
    int          src_ea;                  /* Source effective address field (6 bits) */
    int          dst_ea;                  /* Destination effective address field */
    int          reg;                     /* Primary register number (Dn or An index, -1 if unused) */
    uint32_t     imm32;                   /* Decoded immediate value */
    uint32_t     target_addr;             /* Static branch/call target (if has_target) */
    bool         has_target;
    /* For MN_MOVE_CCR and MN_MOVE_SR: true when the EA is the destination
     * (e.g. MOVE CCR,<ea>) and false when EA is the source (MOVE <ea>,CCR).
     * Resolves the direction ambiguity without forcing codegen to
     * re-inspect the raw opcode word. Unused for other mnemonics. */
    bool         dst_is_ea;
} M68KInstr;

/* EA mode constants */
#define EA_Dn      0
#define EA_An      1
#define EA_An_IND  2
#define EA_An_POST 3
#define EA_An_PRE  4
#define EA_An_DISP 5
#define EA_An_IDX  6
#define EA_PCR     7

/* PCR sub-modes (reg field when mode=7) */
#define PCR_ABS_W   0
#define PCR_ABS_L   1
#define PCR_PC_DISP 2
#define PCR_PC_IDX  3
#define PCR_IMM     4

bool m68k_decode(const GenesisRom *rom, uint32_t addr, M68KInstr *out);
bool m68k_is_terminator(const M68KInstr *instr);
bool m68k_is_call(const M68KInstr *instr);
