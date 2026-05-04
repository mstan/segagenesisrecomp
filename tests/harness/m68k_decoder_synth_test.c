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

/* ---------------------------------------------------------------------
 * Phase 3C — CMPM (Ay)+,(Ax)+
 * --------------------------------------------------------------------- */
static void test_cmpm(void) {
    /* Pattern: 1011 xxx 1 ss 011 yyy   (mode=011=(An)+)
     * Encodings here:
     *   B7 19 = CMPM.B (A1)+,(A3)+   xxx=011 (Ax=A3), ss=00, yyy=001
     *   B5 59 = CMPM.W (A1)+,(A2)+   xxx=010 (Ax=A2), ss=01, yyy=001
     *   B3 99 = CMPM.L (A1)+,(A1)+   xxx=001 (Ax=A1), ss=10, yyy=001  */
    uint8_t bytes[] = {
        0xB7, 0x19,    /* CMPM.B (A1)+,(A3)+ */
        0xB5, 0x59,    /* CMPM.W (A1)+,(A2)+ */
        0xB3, 0x99,    /* CMPM.L (A1)+,(A1)+ */
    };
    GenesisRom rom; make_rom(&rom, bytes, sizeof(bytes));

    M68KInstr a = {0};
    CHECK(m68k_decode(&rom, 0, &a), "decode CMPM.B");
    CHECK(a.mnemonic == MN_CMPM,           "CMPM.B → MN_CMPM");
    CHECK(a.size     == M68K_SIZE_B,       "CMPM.B size=B");
    CHECK(a.byte_length == 2,              "CMPM.B length=2");
    CHECK((a.src_ea & 7) == 1,             "CMPM.B Ay=A1 (src_ea low bits)");
    CHECK(((a.src_ea >> 3) & 7) == EA_An_POST, "CMPM.B src EA mode=(An)+");
    CHECK(a.reg == 3,                      "CMPM.B Ax=A3");

    M68KInstr b = {0};
    CHECK(m68k_decode(&rom, 2, &b), "decode CMPM.W");
    CHECK(b.mnemonic == MN_CMPM,           "CMPM.W → MN_CMPM");
    CHECK(b.size     == M68K_SIZE_W,       "CMPM.W size=W");
    CHECK(b.reg == 2 && (b.src_ea & 7) == 1, "CMPM.W Ax=A2, Ay=A1");

    M68KInstr c = {0};
    CHECK(m68k_decode(&rom, 4, &c), "decode CMPM.L");
    CHECK(c.mnemonic == MN_CMPM,           "CMPM.L → MN_CMPM");
    CHECK(c.size     == M68K_SIZE_L,       "CMPM.L size=L");
    CHECK(c.reg == 1 && (c.src_ea & 7) == 1, "CMPM.L Ax=A1, Ay=A1 (self)");
}

/* ---------------------------------------------------------------------
 * Phase 4 — ADDX / SUBX memory predecrement form
 * --------------------------------------------------------------------- */
static void test_addx_subx_forms(void) {
    /* ADDX register form: 1101 xxx 1 ss 00 0 yyy
     * ADDX memory form  : 1101 xxx 1 ss 00 1 yyy
     * SUBX register form: 1001 xxx 1 ss 00 0 yyy
     * SUBX memory form  : 1001 xxx 1 ss 00 1 yyy
     *
     * Encodings used:
     *   D7 01 = ADDX.B D1,D3      (xxx=011, ss=00, R/M=0, yyy=001)
     *   D7 09 = ADDX.B -(A1)-,-(A3)+ (xxx=011, ss=00, R/M=1, yyy=001)
     *   95 41 = SUBX.W D1,D2      (xxx=010, ss=01, R/M=0, yyy=001)
     *   93 89 = SUBX.L -(A1)-,-(A1)- (xxx=001, ss=10, R/M=1, yyy=001) */
    uint8_t bytes[] = {
        0xD7, 0x01,    /* ADDX.B D1,D3        */
        0xD7, 0x09,    /* ADDX.B -(A1),-(A3)  */
        0x95, 0x41,    /* SUBX.W D1,D2        */
        0x93, 0x89,    /* SUBX.L -(A1),-(A1)  */
    };
    GenesisRom rom; make_rom(&rom, bytes, sizeof(bytes));

    M68KInstr a = {0};
    CHECK(m68k_decode(&rom, 0, &a), "ADDX.B Dy,Dx decode");
    CHECK(a.mnemonic == MN_ADDX && a.size == M68K_SIZE_B,
          "ADDX.B reg-form mnemonic+size");
    CHECK(a.predec_mem_form == false, "ADDX.B reg-form: predec=false");
    CHECK(a.reg == 3 && (a.src_ea & 7) == 1, "ADDX.B Ax=D3, Ay=D1");

    M68KInstr b = {0};
    CHECK(m68k_decode(&rom, 2, &b), "ADDX.B mem decode");
    CHECK(b.mnemonic == MN_ADDX && b.size == M68K_SIZE_B,
          "ADDX.B mem-form mnemonic+size");
    CHECK(b.predec_mem_form == true,  "ADDX.B mem-form: predec=true");
    CHECK(b.reg == 3 && (b.src_ea & 7) == 1, "ADDX.B mem Ax=A3, Ay=A1");

    M68KInstr c = {0};
    CHECK(m68k_decode(&rom, 4, &c), "SUBX.W reg decode");
    CHECK(c.mnemonic == MN_SUBX && c.size == M68K_SIZE_W,
          "SUBX.W reg-form mnemonic+size");
    CHECK(c.predec_mem_form == false, "SUBX.W reg-form: predec=false");

    M68KInstr d = {0};
    CHECK(m68k_decode(&rom, 6, &d), "SUBX.L mem decode");
    CHECK(d.mnemonic == MN_SUBX && d.size == M68K_SIZE_L,
          "SUBX.L mem-form mnemonic+size");
    CHECK(d.predec_mem_form == true,  "SUBX.L mem-form: predec=true");
    CHECK(d.reg == 1 && (d.src_ea & 7) == 1,
          "SUBX.L mem Ax=A1, Ay=A1 (self)");
}

/* ---------------------------------------------------------------------
 * Phase 7A — vectored exception mnemonics
 * --------------------------------------------------------------------- */
static void test_phase7a_traps(void) {
    /* Single-word group 4 traps. Each must round-trip through the decoder
     * with byte_length=2 (no extension words) and the right mnemonic.
     * Before Phase 7A every one of these decoded to MN_OTHER, leaving
     * codegen to emit a comment-only stub. */
    uint8_t bytes[] = {
        0x4E, 0x70,    /* RESET           */
        0x4E, 0x76,    /* TRAPV           */
        0x4E, 0x77,    /* RTR             */
        0x4A, 0xFC,    /* ILLEGAL ($4AFC) */
        0xA0, 0x00,    /* A-line trap     */
        0xF0, 0x00,    /* F-line trap     */
        0x4E, 0x40,    /* TRAP #0         */
        0x4E, 0x4F,    /* TRAP #15        */
    };
    GenesisRom rom; make_rom(&rom, bytes, sizeof(bytes));

    struct {
        uint32_t pc;
        M68KMnemonic expect_mn;
        const char  *label;
    } cases[] = {
        { 0,  MN_RESET,   "RESET decodes to MN_RESET"     },
        { 2,  MN_TRAPV,   "TRAPV decodes to MN_TRAPV"     },
        { 4,  MN_RTR,     "RTR decodes to MN_RTR"         },
        { 6,  MN_ILLEGAL, "$4AFC decodes to MN_ILLEGAL"   },
        { 8,  MN_ILLEGAL, "A-line decodes to MN_ILLEGAL"  },
        { 10, MN_ILLEGAL, "F-line decodes to MN_ILLEGAL"  },
        { 12, MN_TRAP,    "TRAP #0 still decodes to MN_TRAP"  },
        { 14, MN_TRAP,    "TRAP #15 still decodes to MN_TRAP" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        M68KInstr d = {0};
        char msg[160];
        snprintf(msg, sizeof(msg), "decode @ %u (%s)", cases[i].pc, cases[i].label);
        CHECK(m68k_decode(&rom, cases[i].pc, &d), msg);
        snprintf(msg, sizeof(msg), "%s — mnemonic", cases[i].label);
        CHECK(d.mnemonic == cases[i].expect_mn, msg);
        snprintf(msg, sizeof(msg), "%s — byte_length=2", cases[i].label);
        CHECK(d.byte_length == 2, msg);
    }

    /* TRAP #N immediate carries the vector-low nibble. */
    M68KInstr t0 = {0}, tF = {0};
    CHECK(m68k_decode(&rom, 12, &t0), "decode TRAP #0 imm");
    CHECK(t0.imm32 == 0, "TRAP #0 imm32==0");
    CHECK(m68k_decode(&rom, 14, &tF), "decode TRAP #15 imm");
    CHECK(tF.imm32 == 0xF, "TRAP #15 imm32==15");

    /* RTR, ILLEGAL, A-line and F-line must be recognised as path
     * terminators by m68k_is_terminator (function_finder relies on
     * this to stop linear scans cleanly). */
    M68KInstr probe = {0};
    CHECK(m68k_decode(&rom, 4, &probe), "decode RTR for terminator check");
    CHECK(m68k_is_terminator(&probe), "RTR is a terminator");
    CHECK(m68k_decode(&rom, 6, &probe), "decode ILLEGAL for terminator check");
    CHECK(m68k_is_terminator(&probe), "ILLEGAL is a terminator");
}

/* ---------------------------------------------------------------------
 * Phase 7B — ABCD / SBCD / NBCD packed-BCD forms
 * --------------------------------------------------------------------- */
static void test_phase7b_bcd(void) {
    /* ABCD register form : 1100 xxx 1 0000 0 yyy   → 0xC1 00 | (xxx<<9) | yyy
     *      memory  form  : 1100 xxx 1 0000 1 yyy
     * SBCD register form : 1000 xxx 1 0000 0 yyy
     *      memory  form  : 1000 xxx 1 0000 1 yyy
     * NBCD <ea>          : 0100 1000 00 mmm rrr   → 0x4800 | ea
     *
     * Encodings:
     *   C5 01 = ABCD.B D1,D2          xxx=010, R/M=0, yyy=001
     *   C5 09 = ABCD.B -(A1),-(A2)    xxx=010, R/M=1, yyy=001
     *   85 01 = SBCD.B D1,D2
     *   85 09 = SBCD.B -(A1),-(A2)
     *   48 03 = NBCD.B D3             ea=D3 (mode=0,reg=3) */
    uint8_t bytes[] = {
        0xC5, 0x01,    /* ABCD.B D1,D2          */
        0xC5, 0x09,    /* ABCD.B -(A1),-(A2)    */
        0x85, 0x01,    /* SBCD.B D1,D2          */
        0x85, 0x09,    /* SBCD.B -(A1),-(A2)    */
        0x48, 0x03,    /* NBCD.B D3             */
    };
    GenesisRom rom; make_rom(&rom, bytes, sizeof(bytes));

    M68KInstr a = {0};
    CHECK(m68k_decode(&rom, 0, &a), "ABCD.B reg decode");
    CHECK(a.mnemonic == MN_ABCD && a.size == M68K_SIZE_B, "ABCD.B reg-form");
    CHECK(a.predec_mem_form == false, "ABCD.B reg: predec=false");
    CHECK(a.reg == 2 && (a.src_ea & 7) == 1, "ABCD.B reg: Dx=2 Dy=1");

    M68KInstr b = {0};
    CHECK(m68k_decode(&rom, 2, &b), "ABCD.B mem decode");
    CHECK(b.mnemonic == MN_ABCD, "ABCD.B mem-form");
    CHECK(b.predec_mem_form == true, "ABCD.B mem: predec=true");
    CHECK(b.reg == 2 && (b.src_ea & 7) == 1, "ABCD.B mem: Ax=2 Ay=1");

    M68KInstr c = {0};
    CHECK(m68k_decode(&rom, 4, &c), "SBCD.B reg decode");
    CHECK(c.mnemonic == MN_SBCD, "SBCD.B reg-form");
    CHECK(c.predec_mem_form == false, "SBCD.B reg: predec=false");
    CHECK(c.reg == 2 && (c.src_ea & 7) == 1, "SBCD.B reg: Dx=2 Dy=1");

    M68KInstr d = {0};
    CHECK(m68k_decode(&rom, 6, &d), "SBCD.B mem decode");
    CHECK(d.mnemonic == MN_SBCD, "SBCD.B mem-form");
    CHECK(d.predec_mem_form == true, "SBCD.B mem: predec=true");

    M68KInstr e = {0};
    CHECK(m68k_decode(&rom, 8, &e), "NBCD.B decode");
    CHECK(e.mnemonic == MN_NBCD && e.size == M68K_SIZE_B, "NBCD.B");
    CHECK(e.src_ea == 0x03, "NBCD.B EA=D3");
}

/* ---------------------------------------------------------------------
 * Phase 7C — MOVEP / CHK / STOP
 * --------------------------------------------------------------------- */
static void test_phase7c_movep_chk_stop(void) {
    /* MOVEP encodings: 0000 ddd 1MM 001 aaa  d16
     *   MM=00 .W mem→Dn, 01 .L mem→Dn, 10 .W Dn→mem, 11 .L Dn→mem
     * CHK: 0100 DDD 110 mmm rrr (size .W)
     * STOP: 0x4E72  imm
     *
     * Encodings:
     *   01 09 00 10 = MOVEP.W d16=0x0010 (A1)→D0    ddd=000, MM=00, aaa=001
     *   03 49 00 20 = MOVEP.L d16=0x0020 (A1)→D1    ddd=001, MM=01
     *   01 89 00 30 = MOVEP.W D0→d16=0x0030 (A1)    ddd=000, MM=10
     *   03 C9 00 40 = MOVEP.L D1→d16=0x0040 (A1)    ddd=001, MM=11
     *   41 81       = CHK.W D1,D0                   ddd=000, mmm=000, rrr=001
     *   4E 72 27 00 = STOP #$2700                                                */
    uint8_t bytes[] = {
        0x01, 0x09, 0x00, 0x10,   /* MOVEP.W (mem→D0)       */
        0x03, 0x49, 0x00, 0x20,   /* MOVEP.L (mem→D1)       */
        0x01, 0x89, 0x00, 0x30,   /* MOVEP.W (D0→mem)       */
        0x03, 0xC9, 0x00, 0x40,   /* MOVEP.L (D1→mem)       */
        0x41, 0x81,               /* CHK.W D1,D0            */
        0x4E, 0x72, 0x27, 0x00,   /* STOP #$2700            */
    };
    GenesisRom rom; make_rom(&rom, bytes, sizeof(bytes));

    M68KInstr m1 = {0};
    CHECK(m68k_decode(&rom, 0, &m1), "MOVEP.W mem→Dn decode");
    CHECK(m1.mnemonic == MN_MOVEP && m1.size == M68K_SIZE_W, "MOVEP.W mnemonic+size");
    CHECK(m1.byte_length == 4, "MOVEP.W length=4 (op+disp)");
    CHECK(m1.reg == 0, "MOVEP.W Dn=D0");
    CHECK((m1.src_ea & 7) == 1, "MOVEP.W An=A1 in src_ea low bits");
    CHECK((m1.words[0] & 0x80) == 0, "MOVEP.W direction bit=0 (mem→Dn)");
    CHECK(m1.words[1] == 0x0010, "MOVEP.W disp word preserved");

    M68KInstr m2 = {0};
    CHECK(m68k_decode(&rom, 4, &m2), "MOVEP.L mem→Dn decode");
    CHECK(m2.mnemonic == MN_MOVEP && m2.size == M68K_SIZE_L, "MOVEP.L mnemonic+size");
    CHECK(m2.reg == 1, "MOVEP.L Dn=D1");

    M68KInstr m3 = {0};
    CHECK(m68k_decode(&rom, 8, &m3), "MOVEP.W Dn→mem decode");
    CHECK(m3.mnemonic == MN_MOVEP && m3.size == M68K_SIZE_W, "MOVEP.W out mnemonic+size");
    CHECK((m3.words[0] & 0x80) != 0, "MOVEP.W direction bit=1 (Dn→mem)");

    M68KInstr m4 = {0};
    CHECK(m68k_decode(&rom, 12, &m4), "MOVEP.L Dn→mem decode");
    CHECK(m4.mnemonic == MN_MOVEP && m4.size == M68K_SIZE_L, "MOVEP.L out mnemonic+size");
    CHECK((m4.words[0] & 0x80) != 0, "MOVEP.L direction bit=1 (Dn→mem)");

    M68KInstr c = {0};
    CHECK(m68k_decode(&rom, 16, &c), "CHK.W decode");
    CHECK(c.mnemonic == MN_CHK && c.size == M68K_SIZE_W, "CHK.W mnemonic+size");
    CHECK(c.reg == 0 && (c.src_ea & 7) == 1, "CHK.W Dn=D0, EA=D1");

    M68KInstr s = {0};
    CHECK(m68k_decode(&rom, 18, &s), "STOP decode");
    CHECK(s.mnemonic == MN_STOP, "STOP mnemonic");
    CHECK(s.byte_length == 4, "STOP length=4 (op+imm)");
    CHECK(s.imm32 == 0x2700, "STOP imm preserved");
}

int main(void) {
    test_move_ccr_directions();
    test_move_sr_directions();
    test_imm_to_ccr_sr();
    test_cmpm();
    test_addx_subx_forms();
    test_phase7a_traps();
    test_phase7b_bcd();
    test_phase7c_movep_chk_stop();

    if (g_failures == 0) {
        printf("m68k_decoder_synth: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "m68k_decoder_synth: %d check(s) failed\n", g_failures);
    return 1;
}
