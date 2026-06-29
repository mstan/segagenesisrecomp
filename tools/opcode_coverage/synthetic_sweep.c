/*
 * synthetic_sweep.c - Axis-1 SYNTHETIC opcode-coverage sweep.
 *
 * Decodes ALL 65536 base opcode words (0x0000..0xFFFF) through the real
 * recompiler decoder (m68k_decoder.c) + legality validator (m68k_validator.c)
 * and tallies, for the whole 16-bit opcode space:
 *   - how many decode to a real mnemonic vs MN_OTHER (decoder coverage)
 *   - how many the validator deems LEGAL vs illegal (legality screen)
 *   - the cross-tab: real-mnemonic & legal / real-mnemonic & illegal /
 *     MN_OTHER (always illegal)
 *   - a per-mnemonic histogram of which base opcodes map where
 *
 * This is a STATIC measurement harness: it links three pure-C recompiler
 * files unmodified and feeds them synthetic 4-byte ROMs. It does NOT touch
 * the recompiler/runner/generated source.
 *
 * Build (from the worktree root, with mingw gcc on PATH):
 *   gcc -I recompiler/src \
 *       tools/opcode_coverage/synthetic_sweep.c \
 *       recompiler/src/m68k_decoder.c \
 *       recompiler/src/m68k_validator.c \
 *       recompiler/src/rom_parser.c \
 *       -o tools/opcode_coverage/synthetic_sweep
 *   ./tools/opcode_coverage/synthetic_sweep
 *
 * Note: extension words are synthesized as 0x0000, which is fine for
 * mnemonic/legality classification (it does not depend on operand values).
 */
#include <stdio.h>
#include <string.h>
#include "m68k_decoder.h"
#include "m68k_validator.h"
#include "rom_parser.h"

/* Keep this name table in sync with M68KMnemonic in m68k_decoder.h. */
static const char *MN_NAMES[] = {
    "MN_OTHER","MN_MOVE","MN_MOVEQ","MN_JSR","MN_BSR","MN_JMP","MN_BRA","MN_Bcc",
    "MN_DBcc","MN_RTS","MN_RTE","MN_NOP","MN_STOP","MN_TRAP","MN_MOVEA","MN_MOVEM",
    "MN_LEA","MN_PEA","MN_TST","MN_CLR","MN_NEG","MN_NEGX","MN_NOT","MN_EXT",
    "MN_SWAP","MN_ORI","MN_ANDI","MN_SUBI","MN_ADDI","MN_EORI","MN_CMPI","MN_ADD",
    "MN_ADDA","MN_ADDQ","MN_SUB","MN_SUBA","MN_SUBQ","MN_AND","MN_OR","MN_EOR",
    "MN_CMP","MN_CMPA","MN_LSL","MN_LSR","MN_ASL","MN_ASR","MN_ROL","MN_ROR",
    "MN_ROXL","MN_ROXR","MN_Scc","MN_LINK","MN_UNLK","MN_MULS","MN_MULU","MN_DIVS",
    "MN_DIVU","MN_ABCD","MN_SBCD","MN_BTST","MN_BCHG","MN_BCLR","MN_BSET","MN_MOVEP",
    "MN_CHK","MN_NBCD","MN_TAS","MN_MOVE_USP","MN_MOVE_SR","MN_MOVE_CCR","MN_EXG",
    "MN_ADDX","MN_SUBX","MN_ORI_TO_CCR","MN_ORI_TO_SR","MN_ANDI_TO_CCR",
    "MN_ANDI_TO_SR","MN_EORI_TO_CCR","MN_EORI_TO_SR","MN_CMPM","MN_RTR","MN_RESET",
    "MN_TRAPV","MN_ILLEGAL",
};
#define NMN ((int)(sizeof(MN_NAMES)/sizeof(MN_NAMES[0])))

int main(void) {
    uint8_t buf[32];
    GenesisRom rom;
    memset(&rom, 0, sizeof(rom));
    rom.rom_data = buf;
    rom.rom_size = sizeof(buf);

    M68KValidatorOptions vopts;
    vopts.allow_68020_branch = false;

    long total = 0;
    long real_mn = 0, mn_other = 0;
    long legal = 0, illegal = 0;
    long real_and_legal = 0, real_and_illegal = 0, other_and_legal = 0;
    long mn_count[256] = {0};
    long mn_legal[256] = {0};

    for (int w0 = 0; w0 <= 0xFFFF; w0++) {
        memset(buf, 0, sizeof(buf));
        buf[0] = (uint8_t)(w0 >> 8);
        buf[1] = (uint8_t)(w0 & 0xFF);
        M68KInstr ins;
        if (!m68k_decode(&rom, 0, &ins)) continue;
        total++;
        int mn = (int)ins.mnemonic;
        if (mn >= 0 && mn < 256) mn_count[mn]++;

        int is_other = (ins.mnemonic == MN_OTHER);
        if (is_other) mn_other++; else real_mn++;

        M68KValidity v = m68k_validate(&ins, &vopts);
        int is_legal = (v == M68K_LEGAL);
        if (is_legal) { legal++; if (mn >= 0 && mn < 256) mn_legal[mn]++; }
        else illegal++;

        if (!is_other && is_legal)  real_and_legal++;
        if (!is_other && !is_legal) real_and_illegal++;
        if (is_other && is_legal)   other_and_legal++;
    }

    printf("==== Synthetic 68000 opcode sweep (all %ld base opcode words) ====\n", total);
    printf("Decoder classification:\n");
    printf("  real mnemonic (non-MN_OTHER) : %6ld  (%.2f%%)\n",
           real_mn, 100.0 * real_mn / total);
    printf("  MN_OTHER (declined)          : %6ld  (%.2f%%)\n",
           mn_other, 100.0 * mn_other / total);
    printf("Validator (MC68000, no 68020 branch):\n");
    printf("  LEGAL                        : %6ld  (%.2f%%)\n",
           legal, 100.0 * legal / total);
    printf("  illegal/reserved/non-68000   : %6ld  (%.2f%%)\n",
           illegal, 100.0 * illegal / total);
    printf("Cross-tab:\n");
    printf("  real-mnemonic & LEGAL        : %6ld  <- exercised, real codegen, in-scope\n", real_and_legal);
    printf("  real-mnemonic & illegal      : %6ld  <- decoder over-accepts; validator screens at discovery\n", real_and_illegal);
    printf("  MN_OTHER & LEGAL             : %6ld  <- should be 0 (validator flags MN_OTHER illegal)\n", other_and_legal);
    printf("\nKey result: every LEGAL opcode maps to a real (non-MN_OTHER) mnemonic\n"
           "with a real codegen path => %s\n",
           (other_and_legal == 0) ? "CONFIRMED (0 legal opcodes decode to MN_OTHER)"
                                  : "VIOLATED (see MN_OTHER & LEGAL above)");

    printf("\nPer-mnemonic histogram (base-opcode encodings that map here):\n");
    printf("  %-16s %10s %10s\n", "mnemonic", "encodings", "of-which-legal");
    for (int i = 0; i < NMN; i++) {
        if (mn_count[i] == 0) continue;
        printf("  %-16s %10ld %10ld\n", MN_NAMES[i], mn_count[i], mn_legal[i]);
    }
    /* Any mnemonic with 0 encodings is unreachable from the decoder. */
    printf("\nMnemonics with ZERO reachable base-opcode encodings (decoder never emits):\n");
    int any_zero = 0;
    for (int i = 1; i < NMN; i++) {  /* skip MN_OTHER */
        if (mn_count[i] == 0) { printf("  %s\n", MN_NAMES[i]); any_zero = 1; }
    }
    if (!any_zero) printf("  (none - every defined mnemonic is reachable)\n");
    return 0;
}
