/*
 * m68k_decoder.c — Motorola 68000 instruction decoder.
 *
 * Decodes one 68K instruction at a time from word-aligned ROM bytes.
 * Returns an M68KInstr struct describing the opcode, size qualifier
 * (.B/.W/.L), and effective address operand modes.
 *
 * 68K instruction encoding overview:
 *   - All instructions are word-aligned (16-bit minimum)
 *   - Extension words follow the first word for EA, displacement, immediate
 *   - Size encoded in bits 6-7 of word 0 for most instructions:
 *       00 = .B (byte), 01 = .W (word), 10 = .L (long)
 *
 * Reference: Motorola M68000 Family Programmer's Reference Manual
 */
#include "m68k_decoder.h"
#include "rom_parser.h"
#include <stdio.h>
#include <string.h>

/* ---- Effective Address mode encoding ---- */
/* EA field = (mode[5:3] << 0) | reg[2:0] */
#define EA_MODE(ea)  (((ea) >> 3) & 7)
#define EA_REG(ea)   ((ea) & 7)

/* ---- Low-level fetch helpers ---- */

static uint16_t fetch16(const GenesisRom *rom, uint32_t *pc) {
    uint16_t v = rom_read16(rom, *pc);
    *pc += 2;
    return v;
}

static uint32_t fetch32(const GenesisRom *rom, uint32_t *pc) {
    uint32_t v = rom_read32(rom, *pc);
    *pc += 4;
    return v;
}

/* ---- EA extension word count ---- */

static int ea_ext_words(int mode, int reg, M68KSize size) {
    switch (mode) {
    case EA_Dn:
    case EA_An:
    case EA_An_IND:
    case EA_An_POST:
    case EA_An_PRE:
        return 0;
    case EA_An_DISP:
        return 1;   /* 16-bit displacement */
    case EA_An_IDX:
        return 1;   /* brief or full extension word */
    case EA_PCR:
        switch (reg) {
        case PCR_ABS_W:   return 1;
        case PCR_ABS_L:   return 2;
        case PCR_PC_DISP: return 1;
        case PCR_PC_IDX:  return 1;
        case PCR_IMM:
            /* Size NONE treated as word (1 word) for safety */
            if (size == M68K_SIZE_NONE) return 1;
            return (size == M68K_SIZE_L) ? 2 : 1;
        default:
            return 0;
        }
    default:
        return 0;
    }
}

/* ---- Consume EA extension words into out->words ---- */

static void consume_ea_ext(const GenesisRom *rom, uint32_t *pc,
                            M68KInstr *out, int mode, int reg, M68KSize size)
{
    int n = ea_ext_words(mode, reg, size);
    for (int i = 0; i < n; i++) {
        out->words[out->word_count++] = fetch16(rom, pc);
    }
}

/* ---- Main decode function ---- */

bool m68k_decode(const GenesisRom *rom, uint32_t addr, M68KInstr *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->addr = addr;
    out->reg  = -1;

    uint32_t pc = addr;
    uint16_t w0 = fetch16(rom, &pc);
    out->words[0] = w0;
    out->word_count = 1;

    int top4 = (w0 >> 12) & 0xF;

    switch (top4) {

    /* ------------------------------------------------------------------ */
    /* GROUP 0x0 — Bit manipulation / Immediate / MOVEP                    */
    /* ------------------------------------------------------------------ */
    case 0x0: {
        int ea_mode = (w0 >> 3) & 7;
        int ea_reg  = w0 & 7;
        int src_ea  = (ea_mode << 3) | ea_reg;

        /* MOVEP: bit 8 = 1, bits 5-3 = 001 (An indirect, ea_mode==1) */
        if ((w0 & 0x0138) == 0x0108) {
            out->mnemonic = MN_MOVEP;
            out->size     = (w0 & 0x0040) ? M68K_SIZE_L : M68K_SIZE_W;
            out->reg      = (w0 >> 9) & 7;
            out->src_ea   = (EA_An_DISP << 3) | ea_reg;
            /* displacement word */
            out->words[out->word_count++] = fetch16(rom, &pc);
            break;
        }

        /* Dynamic bit ops: bit 8 = 1, not MOVEP */
        if (w0 & 0x0100) {
            int subop = (w0 >> 6) & 3;
            static const M68KMnemonic dyn_bit[4] = {
                MN_BTST, MN_BCHG, MN_BCLR, MN_BSET
            };
            out->mnemonic = dyn_bit[subop];
            out->size     = M68K_SIZE_NONE;
            out->reg      = (w0 >> 9) & 7;
            out->src_ea   = src_ea;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_B);
            break;
        }

        /* Bit 8 = 0: static bit ops or immediate ops */
        int op_bits = (w0 >> 9) & 7;
        int ss      = (w0 >> 6) & 3;

        if (op_bits == 4) {
            /* Static BTST/BCHG/BCLR/BSET #n, EA */
            int subop = ss; /* bits 7-6 give which op */
            static const M68KMnemonic sta_bit[4] = {
                MN_BTST, MN_BCHG, MN_BCLR, MN_BSET
            };
            out->mnemonic = sta_bit[subop];
            out->size     = M68K_SIZE_NONE;
            out->src_ea   = src_ea;
            /* consume bit-number immediate word */
            uint16_t bit_imm = fetch16(rom, &pc);
            out->words[out->word_count++] = bit_imm;
            out->imm32 = bit_imm & 0xFF;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_B);
            break;
        }

        /* Immediate arithmetic: ORI/ANDI/SUBI/ADDI/EORI/CMPI */
        static const M68KMnemonic imm_ops[8] = {
            MN_ORI, MN_ANDI, MN_SUBI, MN_ADDI,
            MN_OTHER, MN_EORI, MN_CMPI, MN_OTHER
        };

        if (ss == 3) {
            /* Special: CCR/SR form (byte already encoded) */
            out->mnemonic = MN_OTHER;
            out->size     = M68K_SIZE_W;
            out->words[out->word_count++] = fetch16(rom, &pc);
            break;
        }

        out->mnemonic = imm_ops[op_bits];
        out->size     = (M68KSize)ss;   /* 0=B, 1=W, 2=L */
        out->src_ea   = src_ea;

        /* Consume immediate: 1 word for B/W, 2 words for L */
        if (ss == M68K_SIZE_L) {
            uint16_t hi = fetch16(rom, &pc);
            uint16_t lo = fetch16(rom, &pc);
            out->words[out->word_count++] = hi;
            out->words[out->word_count++] = lo;
            out->imm32 = ((uint32_t)hi << 16) | lo;
        } else {
            uint16_t imm_w = fetch16(rom, &pc);
            out->words[out->word_count++] = imm_w;
            if (ss == M68K_SIZE_B) {
                out->imm32 = (uint32_t)(int32_t)(int8_t)(imm_w & 0xFF);
            } else {
                out->imm32 = imm_w;
            }
        }
        consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, (M68KSize)ss);
        break;
    }

    /* ------------------------------------------------------------------ */
    /* GROUPS 0x1, 0x2, 0x3 — MOVE                                         */
    /* ------------------------------------------------------------------ */
    case 0x1:
    case 0x2:
    case 0x3: {
        M68KSize size;
        if (top4 == 0x1)      size = M68K_SIZE_B;
        else if (top4 == 0x2) size = M68K_SIZE_L;
        else                  size = M68K_SIZE_W;

        /* In MOVE encoding bits are reversed for destination:
         * bits 11-9 = dst_REG, bits 8-6 = dst_MODE
         * bits  5-3 = src_MODE, bits 2-0 = src_REG
         */
        int src_mode = (w0 >> 3) & 7;
        int src_reg  = w0 & 7;
        int dst_mode = (w0 >> 6) & 7;
        int dst_reg  = (w0 >> 9) & 7;

        out->src_ea = (src_mode << 3) | src_reg;
        out->dst_ea = (dst_mode << 3) | dst_reg;
        out->size   = size;

        if (dst_mode == EA_An) {
            out->mnemonic = MN_MOVEA;
            out->reg = dst_reg;
        } else {
            out->mnemonic = MN_MOVE;
        }

        consume_ea_ext(rom, &pc, out, src_mode, src_reg, size);
        consume_ea_ext(rom, &pc, out, dst_mode, dst_reg, size);
        break;
    }

    /* ------------------------------------------------------------------ */
    /* GROUP 0x4 — Miscellaneous                                            */
    /* ------------------------------------------------------------------ */
    case 0x4: {
        int ea_mode = (w0 >> 3) & 7;
        int ea_reg  = w0 & 7;

        /* Exact single-word matches first */
        if (w0 == 0x4E75) {
            out->mnemonic = MN_RTS;
            out->size     = M68K_SIZE_NONE;
            break;
        }
        if (w0 == 0x4E71) {
            out->mnemonic = MN_NOP;
            out->size     = M68K_SIZE_NONE;
            break;
        }
        if (w0 == 0x4E72) {
            out->mnemonic = MN_STOP;
            out->size     = M68K_SIZE_NONE;
            out->words[out->word_count++] = fetch16(rom, &pc);
            break;
        }
        if (w0 == 0x4E73) {
            out->mnemonic = MN_RTE;
            out->size     = M68K_SIZE_NONE;
            break;
        }
        if (w0 == 0x4E70) {
            /* RESET */
            out->mnemonic = MN_OTHER;
            out->size     = M68K_SIZE_NONE;
            break;
        }
        if (w0 == 0x4E76) {
            /* TRAPV */
            out->mnemonic = MN_OTHER;
            out->size     = M68K_SIZE_NONE;
            break;
        }
        if (w0 == 0x4E77) {
            /* RTR */
            out->mnemonic = MN_OTHER;
            out->size     = M68K_SIZE_NONE;
            break;
        }

        /* JMP <ea> */
        if ((w0 & 0xFFC0) == 0x4EC0) {
            out->mnemonic = MN_JMP;
            out->size     = M68K_SIZE_NONE;
            out->src_ea   = w0 & 0x3F;
            if (ea_mode == EA_PCR) {
                if (ea_reg == PCR_ABS_L) {
                    uint32_t target = fetch32(rom, &pc);
                    out->words[out->word_count++] = (uint16_t)(target >> 16);
                    out->words[out->word_count++] = (uint16_t)(target & 0xFFFF);
                    out->target_addr = target;
                    out->has_target  = true;
                } else if (ea_reg == PCR_PC_DISP) {
                    uint16_t dw = fetch16(rom, &pc);
                    out->words[out->word_count++] = dw;
                    int16_t disp = (int16_t)dw;
                    out->target_addr = pc - 2 + (int32_t)disp;
                    out->has_target  = true;
                } else if (ea_reg == PCR_PC_IDX) {
                    out->words[out->word_count++] = fetch16(rom, &pc);
                    /* dynamic, no static target */
                } else if (ea_reg == PCR_ABS_W) {
                    uint16_t aw = fetch16(rom, &pc);
                    out->words[out->word_count++] = aw;
                    out->target_addr = (uint32_t)(int32_t)(int16_t)aw;
                    out->has_target  = true;
                } else {
                    /* PCR_IMM or other: shouldn't be used with JMP but handle */
                    consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_L);
                }
            } else if (ea_mode == EA_An_IND) {
                /* (An) — no ext words, no static target */
            } else if (ea_mode == EA_An_DISP) {
                out->words[out->word_count++] = fetch16(rom, &pc);
            } else if (ea_mode == EA_An_IDX) {
                out->words[out->word_count++] = fetch16(rom, &pc);
            } else {
                consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_L);
            }
            break;
        }

        /* JSR <ea> */
        if ((w0 & 0xFFC0) == 0x4E80) {
            out->mnemonic = MN_JSR;
            out->size     = M68K_SIZE_NONE;
            out->src_ea   = w0 & 0x3F;
            if (ea_mode == EA_PCR) {
                if (ea_reg == PCR_ABS_L) {
                    uint32_t target = fetch32(rom, &pc);
                    out->words[out->word_count++] = (uint16_t)(target >> 16);
                    out->words[out->word_count++] = (uint16_t)(target & 0xFFFF);
                    out->target_addr = target;
                    out->has_target  = true;
                } else if (ea_reg == PCR_PC_DISP) {
                    uint16_t dw = fetch16(rom, &pc);
                    out->words[out->word_count++] = dw;
                    int16_t disp = (int16_t)dw;
                    out->target_addr = pc - 2 + (int32_t)disp;
                    out->has_target  = true;
                } else if (ea_reg == PCR_PC_IDX) {
                    out->words[out->word_count++] = fetch16(rom, &pc);
                    /* dynamic */
                } else if (ea_reg == PCR_ABS_W) {
                    uint16_t aw = fetch16(rom, &pc);
                    out->words[out->word_count++] = aw;
                    out->target_addr = (uint32_t)(int32_t)(int16_t)aw;
                    out->has_target  = true;
                } else {
                    consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_L);
                }
            } else if (ea_mode == EA_An_IND) {
                /* no ext words */
            } else if (ea_mode == EA_An_DISP) {
                out->words[out->word_count++] = fetch16(rom, &pc);
            } else if (ea_mode == EA_An_IDX) {
                out->words[out->word_count++] = fetch16(rom, &pc);
            } else {
                consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_L);
            }
            break;
        }

        /* LEA EA, An: 0100 DDD1 11 mmm rrr */
        if ((w0 & 0xF1C0) == 0x41C0) {
            out->mnemonic = MN_LEA;
            out->size     = M68K_SIZE_L;
            out->reg      = (w0 >> 9) & 7;
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_L);
            break;
        }

        /* CHK EA, Dn: 0100 DDD1 10 mmm rrr */
        if ((w0 & 0xF1C0) == 0x4180) {
            out->mnemonic = MN_CHK;
            out->size     = M68K_SIZE_W;
            out->reg      = (w0 >> 9) & 7;
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_W);
            break;
        }

        /* TRAP #n: 0100 1110 0100 vvvv */
        if ((w0 & 0xFFF0) == 0x4E40) {
            out->mnemonic = MN_TRAP;
            out->size     = M68K_SIZE_NONE;
            out->imm32    = w0 & 0xF;
            break;
        }

        /* LINK An, #d16: 0100 1110 0101 0rrr */
        if ((w0 & 0xFFF8) == 0x4E50) {
            out->mnemonic = MN_LINK;
            out->size     = M68K_SIZE_W;
            out->reg      = w0 & 7;
            out->words[out->word_count++] = fetch16(rom, &pc);
            break;
        }

        /* UNLK An: 0100 1110 0101 1rrr */
        if ((w0 & 0xFFF8) == 0x4E58) {
            out->mnemonic = MN_UNLK;
            out->size     = M68K_SIZE_NONE;
            out->reg      = w0 & 7;
            break;
        }

        /* MOVE An, USP: 0100 1110 0110 0rrr */
        if ((w0 & 0xFFF8) == 0x4E60) {
            out->mnemonic = MN_MOVE_USP;
            out->size     = M68K_SIZE_L;
            out->reg      = w0 & 7;
            break;
        }

        /* MOVE USP, An: 0100 1110 0110 1rrr */
        if ((w0 & 0xFFF8) == 0x4E68) {
            out->mnemonic = MN_MOVE_USP;
            out->size     = M68K_SIZE_L;
            out->reg      = w0 & 7;
            break;
        }

        /* MOVE <ea>, CCR: 0100 0100 11 mmm rrr — EA is source, CCR is dest */
        if ((w0 & 0xFFC0) == 0x44C0) {
            out->mnemonic  = MN_MOVE_CCR;
            out->size      = M68K_SIZE_W;
            out->src_ea    = w0 & 0x3F;
            out->dst_is_ea = false;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_W);
            break;
        }

        /* MOVE <ea>, SR: 0100 0110 11 mmm rrr — EA is source, SR is dest */
        if ((w0 & 0xFFC0) == 0x46C0) {
            out->mnemonic  = MN_MOVE_SR;
            out->size      = M68K_SIZE_W;
            out->src_ea    = w0 & 0x3F;
            out->dst_is_ea = false;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_W);
            break;
        }

        /* MOVE SR, <ea>: 0100 0000 11 mmm rrr — SR is source, EA is dest */
        if ((w0 & 0xFFC0) == 0x40C0) {
            out->mnemonic  = MN_MOVE_SR;
            out->size      = M68K_SIZE_W;
            out->src_ea    = w0 & 0x3F;
            out->dst_is_ea = true;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_W);
            break;
        }

        /* MOVE CCR, <ea>: 0100 0010 11 mmm rrr — CCR is source, EA is dest */
        if ((w0 & 0xFFC0) == 0x42C0) {
            out->mnemonic  = MN_MOVE_CCR;
            out->size      = M68K_SIZE_W;
            out->src_ea    = w0 & 0x3F;
            out->dst_is_ea = true;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_W);
            break;
        }

        /* MOVEM register list, <ea>: 0100 1000 1S mmm rrr
         * But if ea_mode==0 (Dn direct): this is EXT, not MOVEM */
        if ((w0 & 0xFF80) == 0x4880) {
            if (ea_mode == 0) {
                /* EXT Dn */
                out->mnemonic = MN_EXT;
                out->size     = (w0 & 0x0040) ? M68K_SIZE_L : M68K_SIZE_W;
                out->reg      = ea_reg;
            } else {
                out->mnemonic = MN_MOVEM;
                out->size     = (w0 & 0x0040) ? M68K_SIZE_L : M68K_SIZE_W;
                out->src_ea   = w0 & 0x3F;
                /* register mask word */
                out->words[out->word_count++] = fetch16(rom, &pc);
                consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, out->size);
            }
            break;
        }

        /* MOVEM <ea>, register list: 0100 1100 1S mmm rrr */
        if ((w0 & 0xFF80) == 0x4C80) {
            if (ea_mode == 0) {
                /* Technically invalid but treat as EXT for robustness */
                out->mnemonic = MN_EXT;
                out->size     = (w0 & 0x0040) ? M68K_SIZE_L : M68K_SIZE_W;
                out->reg      = ea_reg;
            } else {
                out->mnemonic = MN_MOVEM;
                out->size     = (w0 & 0x0040) ? M68K_SIZE_L : M68K_SIZE_W;
                out->src_ea   = w0 & 0x3F;
                /* register mask word */
                out->words[out->word_count++] = fetch16(rom, &pc);
                consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, out->size);
            }
            break;
        }

        /* TAS <ea>: 0100 1010 11 mmm rrr */
        if ((w0 & 0xFFC0) == 0x4AC0) {
            out->mnemonic = MN_TAS;
            out->size     = M68K_SIZE_B;
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_B);
            break;
        }

        /* TST <ea>: 0100 1010 SS mmm rrr, SS != 11 */
        if ((w0 & 0xFF00) == 0x4A00 && (w0 & 0x00C0) != 0x00C0) {
            out->mnemonic = MN_TST;
            out->size     = (M68KSize)((w0 >> 6) & 3);
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, out->size);
            break;
        }

        /* NBCD <ea>: 0100 1000 00 mmm rrr */
        if ((w0 & 0xFFC0) == 0x4800) {
            out->mnemonic = MN_NBCD;
            out->size     = M68K_SIZE_B;
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_B);
            break;
        }

        /* SWAP Dn: 0100 1000 0100 0rrr */
        if ((w0 & 0xFFF8) == 0x4840) {
            out->mnemonic = MN_SWAP;
            out->size     = M68K_SIZE_W;
            out->reg      = w0 & 7;
            break;
        }

        /* PEA <ea>: 0100 1000 01 mmm rrr, ea_mode != 0 */
        if ((w0 & 0xFFC0) == 0x4840 && ea_mode != 0) {
            out->mnemonic = MN_PEA;
            out->size     = M68K_SIZE_L;
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_L);
            break;
        }

        /* NEG/NEGX/CLR/NOT: 0100 0XY0 SS mmm rrr */
        {
            int ss = (w0 >> 6) & 3;
            if ((w0 & 0xF700) == 0x4000 && ss != 3) {
                out->mnemonic = MN_NEGX;
                out->size     = (M68KSize)ss;
                out->src_ea   = w0 & 0x3F;
                consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, out->size);
                break;
            }
            if ((w0 & 0xF700) == 0x4200 && ss != 3) {
                out->mnemonic = MN_CLR;
                out->size     = (M68KSize)ss;
                out->src_ea   = w0 & 0x3F;
                consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, out->size);
                break;
            }
            if ((w0 & 0xF700) == 0x4400 && ss != 3) {
                out->mnemonic = MN_NEG;
                out->size     = (M68KSize)ss;
                out->src_ea   = w0 & 0x3F;
                consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, out->size);
                break;
            }
            if ((w0 & 0xF700) == 0x4600 && ss != 3) {
                out->mnemonic = MN_NOT;
                out->size     = (M68KSize)ss;
                out->src_ea   = w0 & 0x3F;
                consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, out->size);
                break;
            }
        }

        /* Fallthrough: unknown group 4 encoding */
        out->mnemonic = MN_OTHER;
        out->size     = M68K_SIZE_NONE;
        break;
    }

    /* ------------------------------------------------------------------ */
    /* GROUP 0x5 — ADDQ / SUBQ / Scc / DBcc                               */
    /* ------------------------------------------------------------------ */
    case 0x5: {
        int ea_mode = (w0 >> 3) & 7;
        int ea_reg  = w0 & 7;
        int ss      = (w0 >> 6) & 3;

        if (ss == 3) {
            /* Scc or DBcc.
             * DBcc: 0101 cccc 1100 1rrr → bits 7-3 = 11001, so (w0 & 0x00F8) == 0x00C8 */
            if ((w0 & 0x00F8) == 0x00C8) {
                out->mnemonic = MN_DBcc;
                out->size     = M68K_SIZE_W;
                out->reg      = ea_reg;
                uint16_t dw   = fetch16(rom, &pc);
                out->words[out->word_count++] = dw;
                int16_t disp  = (int16_t)dw;
                out->target_addr = pc - 2 + (int32_t)disp;
                out->has_target  = true;
            } else {
                /* Scc */
                out->mnemonic = MN_Scc;
                out->size     = M68K_SIZE_B;
                out->src_ea   = w0 & 0x3F;
                consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_B);
            }
            break;
        }

        /* ADDQ or SUBQ */
        M68KSize sz = (M68KSize)ss;
        /* If destination is An, 68K promotes to long for the operation */
        if (ea_mode == EA_An) {
            sz = M68K_SIZE_L;
        }
        uint32_t imm = (uint32_t)((w0 >> 9) & 7);
        if (imm == 0) imm = 8;
        out->imm32  = imm;
        out->src_ea = w0 & 0x3F;
        out->size   = sz;

        if (w0 & 0x0100) {
            out->mnemonic = MN_SUBQ;
        } else {
            out->mnemonic = MN_ADDQ;
        }
        consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, sz);
        break;
    }

    /* ------------------------------------------------------------------ */
    /* GROUP 0x6 — BRA / BSR / Bcc                                         */
    /* ------------------------------------------------------------------ */
    case 0x6: {
        int cond   = (w0 >> 8) & 0xF;
        int8_t d8  = (int8_t)(w0 & 0xFF);
        out->size  = M68K_SIZE_NONE;

        if (cond == 0x1) {
            out->mnemonic = MN_BSR;
        } else if (cond == 0x0) {
            out->mnemonic = MN_BRA;
        } else {
            out->mnemonic = MN_Bcc;
        }

        if (d8 == 0) {
            /* 16-bit displacement follows */
            uint16_t dw    = fetch16(rom, &pc);
            int16_t disp16 = (int16_t)dw;
            out->words[out->word_count++] = dw;
            out->target_addr = pc - 2 + (int32_t)disp16;
        } else if (d8 == -1) {
            /* 32-bit displacement (68020+) */
            uint32_t d32 = fetch32(rom, &pc);
            out->words[out->word_count++] = (uint16_t)(d32 >> 16);
            out->words[out->word_count++] = (uint16_t)(d32 & 0xFFFF);
            out->target_addr = pc - 4 + (int32_t)d32;
        } else {
            /* 8-bit displacement in opcode word */
            out->target_addr = pc + (int32_t)d8;
        }
        out->has_target = true;
        break;
    }

    /* ------------------------------------------------------------------ */
    /* GROUP 0x7 — MOVEQ                                                   */
    /* ------------------------------------------------------------------ */
    case 0x7: {
        out->mnemonic = MN_MOVEQ;
        out->size     = M68K_SIZE_L;
        out->reg      = (w0 >> 9) & 7;
        out->imm32    = (uint32_t)(int32_t)(int8_t)(w0 & 0xFF);
        /* Note: bit 8 should be 0 for valid MOVEQ; if not, treat as OTHER */
        if (w0 & 0x0100) {
            out->mnemonic = MN_OTHER;
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    /* GROUP 0x8 — OR / DIV / SBCD                                         */
    /* ------------------------------------------------------------------ */
    case 0x8: {
        int ea_mode = (w0 >> 3) & 7;
        int ea_reg  = w0 & 7;
        int ss      = (w0 >> 6) & 3;
        int dir     = (w0 >> 8) & 1;

        /* DIVU: 1000 DDD0 11 mmm rrr */
        if ((w0 & 0xF1C0) == 0x80C0) {
            out->mnemonic = MN_DIVU;
            out->size     = M68K_SIZE_W;
            out->reg      = (w0 >> 9) & 7;
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_W);
            break;
        }

        /* DIVS: 1000 DDD1 11 mmm rrr */
        if ((w0 & 0xF1C0) == 0x81C0) {
            out->mnemonic = MN_DIVS;
            out->size     = M68K_SIZE_W;
            out->reg      = (w0 >> 9) & 7;
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_W);
            break;
        }

        /* SBCD: 1000 DDD1 0000 0rrr or 1000 DDD1 0000 1rrr */
        if ((w0 & 0xF1F0) == 0x8100) {
            out->mnemonic = MN_SBCD;
            out->size     = M68K_SIZE_B;
            out->reg      = (w0 >> 9) & 7;
            break;
        }

        /* OR: 1000 DDD M SS mmm rrr */
        out->mnemonic = MN_OR;
        out->size     = (M68KSize)ss;
        out->reg      = (w0 >> 9) & 7;
        out->src_ea   = w0 & 0x3F;
        consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, (M68KSize)ss);
        (void)dir;
        break;
    }

    /* ------------------------------------------------------------------ */
    /* GROUP 0x9 — SUB / SUBX / SUBA                                       */
    /* ------------------------------------------------------------------ */
    case 0x9: {
        int ea_mode = (w0 >> 3) & 7;
        int ea_reg  = w0 & 7;
        int ss      = (w0 >> 6) & 3;
        int dir     = (w0 >> 8) & 1;

        if (ss == 3) {
            /* SUBA: size = dir ? L : W */
            out->mnemonic = MN_SUBA;
            out->size     = dir ? M68K_SIZE_L : M68K_SIZE_W;
            out->reg      = (w0 >> 9) & 7;
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, out->size);
            break;
        }

        if (dir && (ea_mode == EA_Dn || ea_mode == EA_An)) {
            /* SUBX */
            out->mnemonic = MN_SUBX;
            out->size     = (M68KSize)ss;
            out->reg      = (w0 >> 9) & 7;
            break;
        }

        /* SUB */
        out->mnemonic = MN_SUB;
        out->size     = (M68KSize)ss;
        out->reg      = (w0 >> 9) & 7;
        out->src_ea   = w0 & 0x3F;
        consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, (M68KSize)ss);
        break;
    }

    /* ------------------------------------------------------------------ */
    /* GROUP 0xA — A-line (illegal)                                        */
    /* ------------------------------------------------------------------ */
    case 0xA:
        out->mnemonic = MN_OTHER;
        out->size     = M68K_SIZE_NONE;
        break;

    /* ------------------------------------------------------------------ */
    /* GROUP 0xB — CMP / EOR                                               */
    /* ------------------------------------------------------------------ */
    case 0xB: {
        int ea_mode = (w0 >> 3) & 7;
        int ea_reg  = w0 & 7;
        int ss      = (w0 >> 6) & 3;
        int dir     = (w0 >> 8) & 1;

        if (ss == 3) {
            /* CMPA: dir=0 → .W, dir=1 → .L */
            out->mnemonic = MN_CMPA;
            out->size     = dir ? M68K_SIZE_L : M68K_SIZE_W;
            out->reg      = (w0 >> 9) & 7;
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, out->size);
            break;
        }

        if (dir) {
            /* bit 8 = 1, ss < 3 */
            if (ea_mode == EA_An_POST) {
                /* CMPM (An)+,(An)+ */
                out->mnemonic = MN_OTHER;
                out->size     = (M68KSize)ss;
            } else {
                /* EOR Dn,<ea> */
                out->mnemonic = MN_EOR;
                out->size     = (M68KSize)ss;
                out->reg      = (w0 >> 9) & 7;
                out->src_ea   = w0 & 0x3F;
                consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, (M68KSize)ss);
            }
            break;
        }

        /* CMP EA,Dn */
        out->mnemonic = MN_CMP;
        out->size     = (M68KSize)ss;
        out->reg      = (w0 >> 9) & 7;
        out->src_ea   = w0 & 0x3F;
        consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, (M68KSize)ss);
        break;
    }

    /* ------------------------------------------------------------------ */
    /* GROUP 0xC — AND / MUL / ABCD / EXG                                  */
    /* ------------------------------------------------------------------ */
    case 0xC: {
        int ea_mode = (w0 >> 3) & 7;
        int ea_reg  = w0 & 7;
        int ss      = (w0 >> 6) & 3;

        /* MULU: 1100 DDD0 11 mmm rrr */
        if ((w0 & 0xF1C0) == 0xC0C0) {
            out->mnemonic = MN_MULU;
            out->size     = M68K_SIZE_W;
            out->reg      = (w0 >> 9) & 7;
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_W);
            break;
        }

        /* MULS: 1100 DDD1 11 mmm rrr */
        if ((w0 & 0xF1C0) == 0xC1C0) {
            out->mnemonic = MN_MULS;
            out->size     = M68K_SIZE_W;
            out->reg      = (w0 >> 9) & 7;
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_W);
            break;
        }

        /* ABCD: 1100 DDD1 0000 0rrr or -(Ax),-(Ay) form */
        if ((w0 & 0xF1F0) == 0xC100) {
            out->mnemonic = MN_ABCD;
            out->size     = M68K_SIZE_B;
            out->reg      = (w0 >> 9) & 7;
            break;
        }

        /* EXG: various forms, bit 8 = 1 with specific opmode */
        if ((w0 & 0xF1F8) == 0xC140) {
            /* EXG Dx,Dy */
            out->mnemonic = MN_EXG;
            out->size     = M68K_SIZE_L;
            break;
        }
        if ((w0 & 0xF1F8) == 0xC148) {
            /* EXG Ax,Ay */
            out->mnemonic = MN_EXG;
            out->size     = M68K_SIZE_L;
            break;
        }
        if ((w0 & 0xF1F8) == 0xC188) {
            /* EXG Dx,Ay */
            out->mnemonic = MN_EXG;
            out->size     = M68K_SIZE_L;
            break;
        }

        /* AND: 1100 DDD M SS mmm rrr */
        out->mnemonic = MN_AND;
        out->size     = (M68KSize)ss;
        out->reg      = (w0 >> 9) & 7;
        out->src_ea   = w0 & 0x3F;
        consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, (M68KSize)ss);
        break;
    }

    /* ------------------------------------------------------------------ */
    /* GROUP 0xD — ADD / ADDX / ADDA                                       */
    /* ------------------------------------------------------------------ */
    case 0xD: {
        int ea_mode = (w0 >> 3) & 7;
        int ea_reg  = w0 & 7;
        int ss      = (w0 >> 6) & 3;
        int dir     = (w0 >> 8) & 1;

        if (ss == 3) {
            /* ADDA: dir=0 → .W, dir=1 → .L */
            out->mnemonic = MN_ADDA;
            out->size     = dir ? M68K_SIZE_L : M68K_SIZE_W;
            out->reg      = (w0 >> 9) & 7;
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, out->size);
            break;
        }

        if (dir && (ea_mode == EA_Dn || ea_mode == EA_An)) {
            /* ADDX */
            out->mnemonic = MN_ADDX;
            out->size     = (M68KSize)ss;
            out->reg      = (w0 >> 9) & 7;
            break;
        }

        /* ADD */
        out->mnemonic = MN_ADD;
        out->size     = (M68KSize)ss;
        out->reg      = (w0 >> 9) & 7;
        out->src_ea   = w0 & 0x3F;
        consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, (M68KSize)ss);
        break;
    }

    /* ------------------------------------------------------------------ */
    /* GROUP 0xE — Shift / Rotate                                          */
    /* ------------------------------------------------------------------ */
    case 0xE: {
        int ea_mode = (w0 >> 3) & 7;
        int ea_reg  = w0 & 7;
        int bits76  = (w0 >> 6) & 3;

        /* Memory shifts: bits 7-6 == 11 */
        if (bits76 == 3) {
            int type = (w0 >> 9) & 3;
            int dir  = (w0 >> 8) & 1;
            static const M68KMnemonic shift_r[4] = {
                MN_ASR, MN_LSR, MN_ROXR, MN_ROR
            };
            static const M68KMnemonic shift_l[4] = {
                MN_ASL, MN_LSL, MN_ROXL, MN_ROL
            };
            out->mnemonic = dir ? shift_l[type] : shift_r[type];
            out->size     = M68K_SIZE_W;
            out->src_ea   = w0 & 0x3F;
            consume_ea_ext(rom, &pc, out, ea_mode, ea_reg, M68K_SIZE_W);
            break;
        }

        /* Register shifts: bits 7-6 != 11 */
        {
            int type = (w0 >> 3) & 3;   /* bits 4-3 */
            int dir  = (w0 >> 8) & 1;
            int use_reg = (w0 >> 5) & 1; /* bit 5: 1=count from Dn, 0=immediate */
            int ss   = bits76;

            static const M68KMnemonic shift_r[4] = {
                MN_ASR, MN_LSR, MN_ROXR, MN_ROR
            };
            static const M68KMnemonic shift_l[4] = {
                MN_ASL, MN_LSL, MN_ROXL, MN_ROL
            };
            out->mnemonic = dir ? shift_l[type] : shift_r[type];
            out->size     = (M68KSize)ss;
            out->reg      = ea_reg;  /* destination Dn */

            if (!use_reg) {
                /* Immediate count in bits 11-9 */
                uint32_t cnt = (uint32_t)((w0 >> 9) & 7);
                if (cnt == 0) cnt = 8;
                out->imm32 = cnt;
                out->src_ea = -1;  /* immediate count */
            } else {
                /* Count from Dn: store Dn number in src_ea */
                out->imm32 = 0;    /* not used for register-count */
                out->src_ea = (int)((w0 >> 9) & 7); /* source Dn */
            }
            /* 2 bytes only for register shifts */
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    /* GROUP 0xF — F-line (illegal / coprocessor)                          */
    /* ------------------------------------------------------------------ */
    case 0xF:
        out->mnemonic = MN_OTHER;
        out->size     = M68K_SIZE_NONE;
        break;

    default:
        out->mnemonic = MN_OTHER;
        out->size     = M68K_SIZE_NONE;
        break;
    }

    out->byte_length = (uint32_t)(pc - addr);
    return true;
}

bool m68k_is_terminator(const M68KInstr *instr) {
    return instr->mnemonic == MN_RTS
        || instr->mnemonic == MN_RTE
        || instr->mnemonic == MN_JMP
        || instr->mnemonic == MN_BRA;  /* unconditional branch, no fall-through */
}

bool m68k_is_call(const M68KInstr *instr) {
    return instr->mnemonic == MN_BSR
        || instr->mnemonic == MN_JSR;
}
