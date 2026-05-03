/*
 * m68k_decoder_synth_test.c — synthetic decoder fixtures.
 *
 * Drives m68k_decode() against in-memory ROM buffers to cover instruction
 * forms Sonic's disasm doesn't exercise. Grows phase-by-phase as new
 * forms are added to the decoder. Each Phase 3+ introduction documents
 * its own block here so regressions surface immediately.
 *
 * Non-zero exit on first failed assertion.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k_decoder.h"
#include "rom_parser.h"

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, msg); \
        g_failures++; \
    } \
} while (0)

/* Stand up a fake ROM around a 16-byte little-payload so we can
 * sequence-decode at PC=0. Bytes are big-endian per 68K convention. */
static void make_rom(GenesisRom *rom, const uint8_t *bytes, uint32_t nbytes) {
    static uint8_t buffer[0x1000];
    memset(buffer, 0, sizeof(buffer));
    if (nbytes > sizeof(buffer)) nbytes = sizeof(buffer);
    memcpy(buffer, bytes, nbytes);
    rom->rom_data = buffer;
    rom->rom_size = sizeof(buffer);
    rom->initial_sp = 0; rom->initial_pc = 0;
    rom->header_checksum = 0; rom->computed_checksum = 0;
    rom->domestic_name[0] = 0; rom->overseas_name[0] = 0;
    rom->region[0] = 0;
}

/* ---------------------------------------------------------------------
 * Phase 3A — MOVE CCR direction
 * --------------------------------------------------------------------- */
static void test_move_ccr_directions(void) {
    /* MOVE <ea>,CCR : 0x44C0 | ea  — load EA into CCR.
     * MOVE CCR,<ea> : 0x42C0 | ea  — store CCR to EA.
     *
     * EA = D6 = mode 0, reg 6  ⇒  ea field = 0x06.
     * Byte sequence here is sequential so two decodes share a buffer. */
    uint8_t bytes[] = {
        0x44, 0xC6,   /* MOVE D6,CCR    — load form */
        0x42, 0xC6,   /* MOVE CCR,D6    — store form */
    };
    GenesisRom rom; make_rom(&rom, bytes, sizeof(bytes));

    M68KInstr a = {0};
    CHECK(m68k_decode(&rom, 0, &a), "decode MOVE D6,CCR");
    CHECK(a.mnemonic == MN_MOVE_CCR, "MOVE D6,CCR → MN_MOVE_CCR");
    CHECK(a.size == M68K_SIZE_W, "MOVE D6,CCR size=W");
    CHECK(a.byte_length == 2, "MOVE D6,CCR length=2");
    CHECK(a.dst_is_ea == false, "MOVE D6,CCR dst_is_ea=false (load)");
    CHECK(a.src_ea == 0x06, "MOVE D6,CCR src_ea=D6");

    M68KInstr b = {0};
    CHECK(m68k_decode(&rom, 2, &b), "decode MOVE CCR,D6");
    CHECK(b.mnemonic == MN_MOVE_CCR, "MOVE CCR,D6 → MN_MOVE_CCR");
    CHECK(b.size == M68K_SIZE_W, "MOVE CCR,D6 size=W");
    CHECK(b.byte_length == 2, "MOVE CCR,D6 length=2");
    CHECK(b.dst_is_ea == true, "MOVE CCR,D6 dst_is_ea=true (store)");
    CHECK(b.src_ea == 0x06, "MOVE CCR,D6 dst-as-EA preserved in src_ea");
}

static void test_move_sr_directions(void) {
    /* MOVE <ea>,SR : 0x46C0 | ea  — load.
     * MOVE SR,<ea> : 0x40C0 | ea  — store.  Both use the same dst_is_ea
     * flag the Phase 3A decoder change introduced. */
    uint8_t bytes[] = {
        0x46, 0xC0,   /* MOVE D0,SR  — load */
        0x40, 0xC0,   /* MOVE SR,D0  — store */
    };
    GenesisRom rom; make_rom(&rom, bytes, sizeof(bytes));

    M68KInstr a = {0};
    CHECK(m68k_decode(&rom, 0, &a), "decode MOVE D0,SR");
    CHECK(a.mnemonic == MN_MOVE_SR, "MN_MOVE_SR (load)");
    CHECK(a.dst_is_ea == false, "MOVE D0,SR dst_is_ea=false");

    M68KInstr b = {0};
    CHECK(m68k_decode(&rom, 2, &b), "decode MOVE SR,D0");
    CHECK(b.mnemonic == MN_MOVE_SR, "MN_MOVE_SR (store)");
    CHECK(b.dst_is_ea == true, "MOVE SR,D0 dst_is_ea=true");
}

/* ---------------------------------------------------------------------
 * Phase 3B — immediate-to-CCR / immediate-to-SR
 * --------------------------------------------------------------------- */
static void test_imm_to_ccr_sr(void) {
    /* All six forms are 4 bytes: opcode word + immediate word.
     * Per 68K spec the immediate is .B for CCR (held in low byte of
     * the immediate word) and .W for SR. */
    uint8_t bytes[] = {
        0x00, 0x3C, 0x00, 0x05,  /* ORI  #$05,CCR     */
        0x00, 0x7C, 0x07, 0x00,  /* ORI  #$0700,SR    */
        0x02, 0x3C, 0x00, 0xEF,  /* ANDI #$EF,CCR     */
        0x02, 0x7C, 0xF8, 0xFF,  /* ANDI #$F8FF,SR    */
        0x0A, 0x3C, 0x00, 0x10,  /* EORI #$10,CCR     */
        0x0A, 0x7C, 0x20, 0x00,  /* EORI #$2000,SR    */
    };
    GenesisRom rom; make_rom(&rom, bytes, sizeof(bytes));

    struct {
        uint32_t pc;
        M68KMnemonic expect_mn;
        M68KSize     expect_size;
        uint32_t     expect_imm;
    } cases[] = {
        { 0,  MN_ORI_TO_CCR,  M68K_SIZE_B, 0x05   },
        { 4,  MN_ORI_TO_SR,   M68K_SIZE_W, 0x0700 },
        { 8,  MN_ANDI_TO_CCR, M68K_SIZE_B, 0xEF   },
        { 12, MN_ANDI_TO_SR,  M68K_SIZE_W, 0xF8FF },
        { 16, MN_EORI_TO_CCR, M68K_SIZE_B, 0x10   },
        { 20, MN_EORI_TO_SR,  M68K_SIZE_W, 0x2000 },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        M68KInstr d = {0};
        char msg[128];
        snprintf(msg, sizeof(msg), "decode @ %u (case %zu)", cases[i].pc, i);
        CHECK(m68k_decode(&rom, cases[i].pc, &d), msg);

        snprintf(msg, sizeof(msg), "case %zu mnemonic", i);
        CHECK(d.mnemonic == cases[i].expect_mn, msg);

        snprintf(msg, sizeof(msg), "case %zu size", i);
        CHECK(d.size == cases[i].expect_size, msg);

        /* CRITICAL: byte_length must be 4 — the prior decoder
         * over-consumed by an extra word for these forms because the
         * pseudo-EA mode 7 reg 4 made consume_ea_ext re-fetch the
         * immediate. The fix gates consume_ea_ext on src_ea != 0x3C. */
        snprintf(msg, sizeof(msg), "case %zu byte_length=4", i);
        CHECK(d.byte_length == 4, msg);

        snprintf(msg, sizeof(msg), "case %zu imm32", i);
        CHECK(d.imm32 == cases[i].expect_imm, msg);
    }
}

int main(void) {
    test_move_ccr_directions();
    test_move_sr_directions();
    test_imm_to_ccr_sr();

    if (g_failures == 0) {
        printf("m68k_decoder_synth: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "m68k_decoder_synth: %d check(s) failed\n", g_failures);
    return 1;
}
