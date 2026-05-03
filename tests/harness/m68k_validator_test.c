/*
 * m68k_validator_test.c — synthetic validator fixtures.
 *
 * Drives m68k_decode then m68k_validate to lock down which encodings
 * the validator accepts vs. rejects. Sonic exercises only the LEGAL
 * path; this harness covers the rejection cases that don't appear in
 * a real ROM.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k_decoder.h"
#include "m68k_validator.h"
#include "rom_parser.h"

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, msg); \
        g_failures++; \
    } \
} while (0)

static void make_rom(GenesisRom *rom, const uint8_t *bytes, uint32_t nbytes) {
    static uint8_t buffer[0x1000];
    memset(buffer, 0, sizeof(buffer));
    if (nbytes > sizeof(buffer)) nbytes = sizeof(buffer);
    memcpy(buffer, bytes, nbytes);
    rom->rom_data = buffer;
    rom->rom_size = sizeof(buffer);
    rom->initial_sp = 0; rom->initial_pc = 0;
    rom->header_checksum = 0; rom->computed_checksum = 0;
    rom->domestic_name[0] = 0; rom->overseas_name[0] = 0; rom->region[0] = 0;
}

static M68KValidity validate_one(const uint8_t *bytes, size_t n,
                                 bool allow_68020) {
    GenesisRom rom; make_rom(&rom, bytes, (uint32_t)n);
    M68KInstr d = {0};
    if (!m68k_decode(&rom, 0, &d)) return M68K_LEGAL;
    M68KValidatorOptions opts = { .allow_68020_branch = allow_68020 };
    return m68k_validate(&d, &opts);
}

int main(void) {
    /* MOVEA.B is illegal — MOVEA only takes word/long.
     * 0x1040 = MOVE.B D0,A0 → decoded as MOVEA.B (per current decoder).
     * Validator must flag illegal-size. */
    {
        uint8_t b[] = { 0x10, 0x40 };
        CHECK(validate_one(b, sizeof(b), false) == M68K_ILLEGAL_SIZE,
              "MOVEA.B → ILLEGAL_SIZE");
    }

    /* MOVE.W D0,D1 — perfectly legal. */
    {
        uint8_t b[] = { 0x32, 0x00 };
        CHECK(validate_one(b, sizeof(b), false) == M68K_LEGAL,
              "MOVE.W D0,D1 → LEGAL");
    }

    /* Bcc / BSR / BRA d8==0xFF (32-bit displacement form) is 68020+
     * and should be flagged unless the caller allows 68020 branches. */
    {
        /* BRA.L: 0x60FF then 32-bit displacement. */
        uint8_t b[] = { 0x60, 0xFF, 0x00, 0x00, 0x00, 0x10 };
        CHECK(validate_one(b, sizeof(b), false) == M68K_ILLEGAL_NON_MC68000,
              "BRA d8==FF → ILLEGAL_NON_MC68000");
        CHECK(validate_one(b, sizeof(b), true)  == M68K_LEGAL,
              "BRA d8==FF (with allow_68020_branch) → LEGAL");
    }

    /* ILLEGAL opcode 0x4AFC. */
    {
        uint8_t b[] = { 0x4A, 0xFC };
        CHECK(validate_one(b, sizeof(b), false) == M68K_ILLEGAL_OPCODE,
              "0x4AFC ILLEGAL → ILLEGAL_OPCODE");
    }

    /* A-line trap (top nibble 0xA). */
    {
        uint8_t b[] = { 0xA0, 0x00 };
        CHECK(validate_one(b, sizeof(b), false) == M68K_ILLEGAL_OPCODE,
              "A-line → ILLEGAL_OPCODE");
    }

    /* F-line trap (top nibble 0xF). */
    {
        uint8_t b[] = { 0xF0, 0x00 };
        CHECK(validate_one(b, sizeof(b), false) == M68K_ILLEGAL_OPCODE,
              "F-line → ILLEGAL_OPCODE");
    }

    /* MOVE D0,#imm is illegal (immediate as destination).
     * dst_mode 7 / dst_reg 4 = #imm. Encoding: MOVE.W src=D0 → dst=#imm.
     * In MOVE encoding, dst_mode is bits 8-6 and dst_reg is bits 11-9.
     * So we want dst_reg=100 (4) in bits 11-9 and dst_mode=111 (7) in
     * bits 8-6. That's 1001 11_ _xxxxx → 0x91Cx.
     * top4=0x3 (MOVE.W) | dst_reg<<9 (100<<9=0x800) | dst_mode<<6 (111<<6=0x1C0)
     * | src_mode<<3 (000) | src_reg (000) → 0x39C0. */
    {
        uint8_t b[] = { 0x39, 0xC0 };
        M68KValidity v = validate_one(b, sizeof(b), false);
        CHECK(v == M68K_ILLEGAL_DST_EA,
              "MOVE.W D0,#imm → ILLEGAL_DST_EA");
    }

    if (g_failures == 0) {
        printf("m68k_validator: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "m68k_validator: %d check(s) failed\n", g_failures);
    return 1;
}
