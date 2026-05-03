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

int main(void) {
    test_move_ccr_directions();
    test_move_sr_directions();

    if (g_failures == 0) {
        printf("m68k_decoder_synth: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "m68k_decoder_synth: %d check(s) failed\n", g_failures);
    return 1;
}
