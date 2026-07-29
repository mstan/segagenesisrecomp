/*
 * m68k_interp.c — Tier-3 clean-room 68000 interpreter (the floor).
 *
 * See m68k_interp.h for the design. In one line: it REUSES the recompiler's
 * decoder (m68k_decoder.c) and MIRRORS code_generator.c's per-mnemonic
 * semantics exactly, executing on the g_cpu / m68k_read*-write* runtime ABI.
 * Every EA-resolution and flag formula here is a direct "emit C" -> "execute"
 * port of the corresponding code_generator.c helper, so the interpreter and
 * the static path are parity-by-construction; clown68000 is the runtime oracle
 * that proves it (see runner/tests/m68k_interp_diff.c).
 *
 * Precision over recall: anything not implemented HALTS LOUDLY (returns
 * M68KI_HALT_UNIMPL with g_m68ki_bad_pc/op set) — it never silently
 * mis-executes. Widening coverage is purely additive and always safe.
 */
#include "m68k_interp.h"
#include "m68k_decoder.h"   /* recompiler/src — added to the runner include path */
#include "rom_parser.h"     /* GenesisRom, rom_read* (decoder's fetch source)    */
#ifdef GENESIS_COSIM
#include "game_cycles.h"    /* game_insn_cost — clown-measured baked costs        */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- per-run diagnostics ---- */
uint32_t g_m68ki_bad_pc = 0;
uint16_t g_m68ki_bad_op = 0;
uint64_t g_m68ki_insn_count = 0;

/* Coverage discovery (see header): distinct call/jump targets this run. */
uint32_t g_m68ki_discover[M68KI_MAX_DISCOVER];
int      g_m68ki_discover_count = 0;
static void discover(uint32_t t) {
    t &= 0xFFFFFFu;
    for (int i = 0; i < g_m68ki_discover_count; i++)
        if (g_m68ki_discover[i] == t) return;
    if (g_m68ki_discover_count < M68KI_MAX_DISCOVER)
        g_m68ki_discover[g_m68ki_discover_count++] = t;
}

/* Set when an instruction tries to write An through the general EA path. No
 * legal instruction does this (MOVEA/ADDA/LEA/ADDQ write An directly), so it
 * means an illegal encoding — only reachable when a fuzzed/garbage computed
 * jump lands on non-code bytes. The step wrapper turns it into a loud halt. */
static int s_illegal_ea = 0;

/* =========================================================================
 * Instruction fetch — ALL interpreter entry points decode through the live
 * bus view below (ROM + work RAM). A ROM-only view is NOT sufficient even
 * for the ROM-miss capsule: a missed ROM function's call tree can reach a
 * RAM-resident helper mid-body (RKA's end-of-stage sequencer at $023430
 * calls the $FFB1F2 shift stub), and a ROM-only fetch had to decline the
 * whole miss there — silent no-op, stage-end softlock. A PC outside ROM/RAM
 * halts loudly rather than decoding garbage.
 * ========================================================================= */
/* Bus-backed fetch view: decodes ROM from g_rom and RAM-RESIDENT code from
 * LIVE work RAM (own backend: g_ram is the authoritative WRAM). This is what
 * makes self-modifying copied handlers correct — RKA's H-int raster handler
 * patches its own source operand each HBlank, so its instruction bytes MUST
 * be fetched from RAM at execution time, never from a static ROM master. */
static uint8_t busview_fetch8(uint32_t addr, void *user) {
    (void)user;
    addr &= 0xFFFFFFu;
    if (addr >= 0xFF0000u) return g_ram[addr & 0xFFFFu];
    if (addr < ROM_SIZE)   return g_rom[addr];
    return 0xFFu;
}
static GenesisRom s_busview;
static int        s_busview_init = 0;
static const GenesisRom *busview(void) {
    if (!s_busview_init) {
        s_busview.rom_data       = g_rom;
        s_busview.rom_size       = 0x1000000u;   /* full 24-bit space */
        s_busview.read8_override = busview_fetch8;
        s_busview_init = 1;
    }
    return &s_busview;
}

/* A PC is fetchable when it points at ROM or work RAM. */
static int pc_fetchable(uint32_t pc) {
    pc &= 0xFFFFFFu;
    return pc < ROM_SIZE || pc >= 0xFF0000u;
}

/* =========================================================================
 * Size helpers (mirror code_generator.c)
 * ========================================================================= */
static uint32_t szmask(M68KSize sz) {
    switch (sz) { case M68K_SIZE_B: return 0xFFu;
                  case M68K_SIZE_L: return 0xFFFFFFFFu;
                  default:          return 0xFFFFu; }
}
static int szbits(M68KSize sz) {
    switch (sz) { case M68K_SIZE_B: return 8;
                  case M68K_SIZE_L: return 32;
                  default:          return 16; }
}
static int szbytes(M68KSize sz) {
    switch (sz) { case M68K_SIZE_B: return 1;
                  case M68K_SIZE_L: return 4;
                  default:          return 2; }
}
/* "keep" mask for size-preserving Dn writes (.B/.W keep upper bits). */
static uint32_t keepmask(M68KSize sz) {
    return (sz == M68K_SIZE_B) ? 0xFFFFFF00u
         : (sz == M68K_SIZE_W) ? 0xFFFF0000u : 0x00000000u;
}
static void store_dn(int reg, uint32_t val, M68KSize sz) {
    if (sz == M68K_SIZE_L) g_cpu.D[reg] = val;
    else g_cpu.D[reg] = (g_cpu.D[reg] & keepmask(sz)) | (val & szmask(sz));
}

/* =========================================================================
 * ExtReader — sequential walker over instruction extension words.
 * Direct port of code_generator.c's ExtReader (wi starts at 1, bp at 2).
 * ========================================================================= */
typedef struct { const M68KInstr *ins; int wi; uint32_t bp; } ExtR;
static void er_init(ExtR *e, const M68KInstr *ins)        { e->ins = ins; e->wi = 1;       e->bp = 2; }
static void er_init_at(ExtR *e, const M68KInstr *ins, int wi){ e->ins = ins; e->wi = wi;   e->bp = (uint32_t)(wi * 2); }
static uint16_t er_next(ExtR *e)        { uint16_t v = e->ins->words[e->wi]; e->wi++; e->bp += 2; return v; }
static uint32_t er_next_dword(ExtR *e)  { uint16_t hi = er_next(e); uint16_t lo = er_next(e); return ((uint32_t)hi << 16) | lo; }
static uint32_t er_next_imm(ExtR *e, M68KSize sz) {
    if (sz == M68K_SIZE_L) return er_next_dword(e);
    uint16_t w = er_next(e);
    return (sz == M68K_SIZE_B) ? (uint32_t)(w & 0xFFu) : (uint32_t)w;
}

/* =========================================================================
 * Effective-address resolution. read_ea_ex / addr_ea / write_ea_ex mirror
 * emit_ea_load_ex / emit_ea_addr_ex / emit_ea_store_ex exactly, including the
 * (An)+/-(An) side effects and the rmw suppression of the increment.
 *
 * read_ea_ex returns the operand "wide": Dn/An return the full 32-bit
 * register, memory returns the size-width read, immediates return the imm.
 * Callers size-mask at the point of use (matching the generated `(ct)expr`).
 * ========================================================================= */
static uint32_t mem_read(M68KSize sz, uint32_t a) {
    switch (sz) { case M68K_SIZE_B: return m68k_read8(a);
                  case M68K_SIZE_L: return m68k_read32(a);
                  default:          return m68k_read16(a); }
}
static void mem_write(M68KSize sz, uint32_t a, uint32_t v) {
    switch (sz) { case M68K_SIZE_B: m68k_write8(a, (uint8_t)v); break;
                  case M68K_SIZE_L: m68k_write32(a, v);        break;
                  default:          m68k_write16(a, (uint16_t)v); break; }
}

static uint32_t read_ea_ex(const M68KInstr *ins, int ea, M68KSize sz, ExtR *er, int rmw) {
    int mode = (ea >> 3) & 7, reg = ea & 7, sb = szbytes(sz);
    switch (mode) {
    case 0: return g_cpu.D[reg];
    case 1: return g_cpu.A[reg];
    case 2: return mem_read(sz, g_cpu.A[reg]);
    case 3: { uint32_t v = mem_read(sz, g_cpu.A[reg]); if (!rmw) g_cpu.A[reg] += sb; return v; }
    case 4: { g_cpu.A[reg] -= sb; return mem_read(sz, g_cpu.A[reg]); }
    case 5: { int16_t d16 = (int16_t)er_next(er);
              return mem_read(sz, (uint32_t)(g_cpu.A[reg] + (int32_t)d16)); }
    case 6: { uint16_t ext = er_next(er);
              int xreg = (ext >> 12) & 7; int8_t d8 = (int8_t)(ext & 0xFF);
              uint32_t xv = m68k_brief_index_value(ext, g_cpu.D[xreg], g_cpu.A[xreg]);
              return mem_read(sz, (uint32_t)(g_cpu.A[reg] + xv + (int32_t)d8)); }
    case 7:
        switch (reg) {
        case 0: { int16_t ext = (int16_t)er_next(er); return mem_read(sz, (uint32_t)(int32_t)ext); }
        case 1: { uint32_t a = er_next_dword(er); return mem_read(sz, a); }
        case 2: { uint32_t pc = ins->addr + er->bp; int16_t d16 = (int16_t)er_next(er);
                  return mem_read(sz, (uint32_t)((int32_t)pc + (int32_t)d16)); }
        case 3: { uint32_t pc = ins->addr + er->bp; uint16_t ext = er_next(er);
                  int xreg = (ext >> 12) & 7; int8_t d8 = (int8_t)(ext & 0xFF);
                  uint32_t xv = m68k_brief_index_value(ext, g_cpu.D[xreg], g_cpu.A[xreg]);
                  return mem_read(sz, (uint32_t)(pc + xv + (int32_t)d8)); }
        case 4: return er_next_imm(er, sz);
        default: return 0;
        }
    default: return 0;
    }
}
static uint32_t read_ea(const M68KInstr *ins, int ea, M68KSize sz, ExtR *er) {
    return read_ea_ex(ins, ea, sz, er, 0);
}

/* addr_ea — the address of a control/alterable EA (LEA/PEA/JMP/JSR). */
static uint32_t addr_ea(const M68KInstr *ins, int ea, ExtR *er) {
    int mode = (ea >> 3) & 7, reg = ea & 7;
    switch (mode) {
    case 2: return g_cpu.A[reg];
    case 5: { int16_t d16 = (int16_t)er_next(er); return (uint32_t)(g_cpu.A[reg] + (int32_t)d16); }
    case 6: { uint16_t ext = er_next(er);
              int xreg = (ext >> 12) & 7; int8_t d8 = (int8_t)(ext & 0xFF);
              uint32_t xv = m68k_brief_index_value(ext, g_cpu.D[xreg], g_cpu.A[xreg]);
              return (uint32_t)(g_cpu.A[reg] + xv + (int32_t)d8); }
    case 7:
        switch (reg) {
        case 0: { int16_t ext = (int16_t)er_next(er); return (uint32_t)(int32_t)ext; }
        case 1: return er_next_dword(er);
        case 2: { uint32_t pc = ins->addr + er->bp; int16_t d16 = (int16_t)er_next(er);
                  return (uint32_t)((int32_t)pc + (int32_t)d16); }
        case 3: { uint32_t pc = ins->addr + er->bp; uint16_t ext = er_next(er);
                  int xreg = (ext >> 12) & 7; int8_t d8 = (int8_t)(ext & 0xFF);
                  uint32_t xv = m68k_brief_index_value(ext, g_cpu.D[xreg], g_cpu.A[xreg]);
                  return (uint32_t)(pc + xv + (int32_t)d8); }
        default: return 0;
        }
    default: return 0;
    }
}

static void write_ea_ex(const M68KInstr *ins, int ea, M68KSize sz, ExtR *er, uint32_t val, int rmw) {
    int mode = (ea >> 3) & 7, reg = ea & 7, sb = szbytes(sz);
    (void)ins;
    switch (mode) {
    case 0: store_dn(reg, val, sz); break;
    case 1: s_illegal_ea = 1; break;          /* An is never a legal write_ea target */
    case 2: mem_write(sz, g_cpu.A[reg], val); break;
    case 3: mem_write(sz, g_cpu.A[reg], val); g_cpu.A[reg] += sb; break;
    case 4: if (!rmw) g_cpu.A[reg] -= sb; mem_write(sz, g_cpu.A[reg], val); break;
    case 5: { int16_t d16 = (int16_t)er_next(er);
              mem_write(sz, (uint32_t)(g_cpu.A[reg] + (int32_t)d16), val); break; }
    case 6: { uint16_t ext = er_next(er);
              int xreg = (ext >> 12) & 7; int8_t d8 = (int8_t)(ext & 0xFF);
              uint32_t xv = m68k_brief_index_value(ext, g_cpu.D[xreg], g_cpu.A[xreg]);
              mem_write(sz, (uint32_t)(g_cpu.A[reg] + xv + (int32_t)d8), val); break; }
    case 7:
        switch (reg) {
        case 0: { int16_t ext = (int16_t)er_next(er); mem_write(sz, (uint32_t)(int32_t)ext, val); break; }
        case 1: { uint32_t a = er_next_dword(er); mem_write(sz, a, val); break; }
        default: break; /* PC-relative/imm are not alterable destinations */
        }
        break;
    default: break;
    }
}
static void write_ea(const M68KInstr *ins, int ea, M68KSize sz, ExtR *er, uint32_t val) {
    write_ea_ex(ins, ea, sz, er, val, 0);
}

/* =========================================================================
 * Flag helpers — exact ports of emit_flags_{logic,add,sub,cmp}.
 * (X=bit4 N=bit3 Z=bit2 V=bit1 C=bit0.)
 * ========================================================================= */
static void flags_logic(uint32_t v, M68KSize sz) {
    int bits = szbits(sz); uint32_t fv = v & szmask(sz);
    g_cpu.SR &= ~0x0Fu;
    if (!fv)                 g_cpu.SR |= SR_Z;
    if (fv >> (bits - 1))    g_cpu.SR |= SR_N;
}
static void flags_add(uint32_t a, uint32_t b, uint32_t r, M68KSize sz) {
    int bits = szbits(sz); uint32_t m = szmask(sz), sb = (uint32_t)(1u << (bits - 1));
    uint32_t fa = a & m, fb = b & m, fr = r & m;
    g_cpu.SR &= ~0x1Fu;
    if (!fr)                                   g_cpu.SR |= SR_Z;
    if (fr >> (bits - 1))                      g_cpu.SR |= SR_N;
    if ((uint64_t)fa + (uint64_t)fb > m)     { g_cpu.SR |= SR_C; g_cpu.SR |= SR_X; }
    if (!((fa ^ fb) & sb) && ((fa ^ fr) & sb)) g_cpu.SR |= SR_V;
}
static void flags_sub(uint32_t a, uint32_t b, uint32_t r, M68KSize sz) {
    int bits = szbits(sz); uint32_t m = szmask(sz), sb = (uint32_t)(1u << (bits - 1));
    uint32_t fa = a & m, fb = b & m, fr = r & m;
    g_cpu.SR &= ~0x1Fu;
    if (!fr)                                   g_cpu.SR |= SR_Z;
    if (fr >> (bits - 1))                      g_cpu.SR |= SR_N;
    if (fb > fa)                             { g_cpu.SR |= SR_C; g_cpu.SR |= SR_X; }
    if (((fa ^ fb) & sb) && ((fa ^ fr) & sb))  g_cpu.SR |= SR_V;
}
static void flags_cmp(uint32_t a, uint32_t b, uint32_t r, M68KSize sz) {
    int bits = szbits(sz); uint32_t m = szmask(sz), sb = (uint32_t)(1u << (bits - 1));
    uint32_t fa = a & m, fb = b & m, fr = r & m;
    g_cpu.SR &= ~0x0Fu;
    if (!fr)                                   g_cpu.SR |= SR_Z;
    if (fr >> (bits - 1))                      g_cpu.SR |= SR_N;
    if (fb > fa)                               g_cpu.SR |= SR_C;
    if (((fa ^ fb) & sb) && ((fa ^ fr) & sb))  g_cpu.SR |= SR_V;
}

/* Condition-code evaluator — mirrors bcc_cond_expr. */
static int eval_cond(int cc) {
    uint16_t s = g_cpu.SR;
    int C = (s >> 0) & 1, V = (s >> 1) & 1, Z = (s >> 2) & 1, N = (s >> 3) & 1;
    switch (cc & 0xF) {
    case 0x0: return 1;                 /* T  */
    case 0x1: return 0;                 /* F  */
    case 0x2: return !C && !Z;          /* HI */
    case 0x3: return C || Z;            /* LS */
    case 0x4: return !C;                /* CC/HS */
    case 0x5: return C;                 /* CS/LO */
    case 0x6: return !Z;                /* NE */
    case 0x7: return Z;                 /* EQ */
    case 0x8: return !V;                /* VC */
    case 0x9: return V;                 /* VS */
    case 0xA: return !N;                /* PL */
    case 0xB: return N;                 /* MI */
    case 0xC: return N == V;            /* GE */
    case 0xD: return N != V;            /* LT */
    case 0xE: return !Z && (N == V);    /* GT */
    case 0xF: return Z || (N != V);     /* LE */
    default:  return 0;
    }
}

/* ---- stack helpers (active SP = A[7]) ---- */
static void push32(uint32_t v) { g_cpu.A[7] -= 4; m68k_write32(g_cpu.A[7], v); }
static uint32_t pop32(void)    { uint32_t v = m68k_read32(g_cpu.A[7]); g_cpu.A[7] += 4; return v; }

/* sign-extend a size-width value to 32 bits */
static uint32_t sext(uint32_t v, M68KSize sz) {
    switch (sz) { case M68K_SIZE_B: return (uint32_t)(int32_t)(int8_t)v;
                  case M68K_SIZE_W: return (uint32_t)(int32_t)(int16_t)v;
                  default:          return v; }
}
static void push16(uint16_t v) { g_cpu.A[7] -= 2; m68k_write16(g_cpu.A[7], v); }
static uint16_t pop16(void)     { uint16_t v = m68k_read16(g_cpu.A[7]); g_cpu.A[7] += 2; return v; }

/* =========================================================================
 * Shift/rotate engine — direct port of code_generator.c's MN_LSL..MN_ROXR
 * (register Dn target). Computes the result + C/V, then applies the SR tail
 * (emit_shift_sr_update / the rotate tails). Returns 0 on success, -1 if it
 * declines (ROXL/ROXR .L: the generated C's `X << 32` is undefined — halt
 * loudly rather than guess). reg_count: count from D[creg]; else imm_count.
 * ========================================================================= */
static int do_shift(M68KMnemonic mn, int dreg, M68KSize sz,
                    int reg_count, int creg, int imm_count) {
    int bits = szbits(sz);
    uint32_t m = szmask(sz);
    int cnt = reg_count ? (int)(g_cpu.D[creg] & 63u) : imm_count;
    uint32_t sv = g_cpu.D[dreg] & m;
    uint32_t sbit = (uint32_t)(1u << (bits - 1));
    uint32_t res = sv, c = 0, v = 0;
    int with_v = 0, touches_x = 1;

    switch (mn) {
    case MN_LSL:
        res = (cnt > 0 && cnt < bits) ? ((sv << cnt) & m) : (cnt == 0 ? sv : 0u);
        c   = (cnt > 0 && cnt <= bits) ? ((sv >> (bits - cnt)) & 1u) : 0u;
        break;
    case MN_LSR:
        res = (cnt > 0 && cnt < bits) ? (sv >> cnt) : (cnt == 0 ? sv : 0u);
        c   = (cnt > 0 && cnt <= bits) ? ((sv >> (cnt - 1)) & 1u) : 0u;
        break;
    case MN_ASL:
        res = (cnt > 0 && cnt < bits) ? ((sv << cnt) & m) : (cnt == 0 ? sv : 0u);
        c   = (cnt > 0 && cnt <= bits) ? ((sv >> (bits - cnt)) & 1u) : 0u;
        with_v = 1;
        /* V = sign bit changed at ANY point during the shift (HW-accurate).
         * Equivalent: the top (cnt+1) bits of the source are neither all-0 nor
         * all-1. (The generated C approximates this as initial-vs-final MSB,
         * which differs when the sign oscillates an even number of times —
         * a latent recompiler imperfection; the floor matches real hardware.) */
        if (cnt > 0) {
            if (cnt >= bits) v = (sv != 0) ? 1u : 0u;
            else {
                uint32_t top = sv >> (bits - 1 - cnt);
                uint32_t allm = (uint32_t)((1u << (cnt + 1)) - 1u);
                v = (top != 0 && top != allm) ? 1u : 0u;
            }
        }
        break;
    case MN_ASR: {
        int32_t ssv = (int32_t)sext(sv, sz);
        if (cnt > 0 && cnt < bits)      res = (uint32_t)(ssv >> cnt) & m;
        else if (cnt == 0)              res = sv;
        else                            res = (ssv < 0) ? m : 0u;
        /* C = last bit shifted out: for cnt >= bits that is the SIGN bit (the
         * value has gone all-sign), not 0 — the generated C approximates this
         * as 0; the floor matches hardware. */
        if (cnt == 0)        c = 0u;
        else if (cnt < bits) c = (sv >> (cnt - 1)) & 1u;
        else                 c = (sv >> (bits - 1)) & 1u;
        with_v = 1;                                            /* ASR never sets V */
        break;
    }
    case MN_ROL: { touches_x = 0; int cc = cnt % bits;
        res = cc ? (((sv << cc) | (sv >> (bits - cc))) & m) : sv;
        c = cnt ? (res & 1u) : 0u; break; }
    case MN_ROR: { touches_x = 0; int cc = cnt % bits;
        res = cc ? (((sv >> cc) | (sv << (bits - cc))) & m) : sv;
        c = cnt ? ((res >> (bits - 1)) & 1u) : 0u; break; }
    case MN_ROXL: case MN_ROXR: {
        /* rotate through X over a (bits+1)-wide field — use 64-bit so .L works */
        uint64_t x = (g_cpu.SR >> 4) & 1u;
        int period = bits + 1, cc = cnt % period;
        uint64_t wide = (uint64_t)sv | (x << bits);
        uint64_t widemask = (period >= 64) ? ~0ull : ((1ull << period) - 1ull);
        uint64_t rot = (mn == MN_ROXL)
            ? ((wide << cc) | (cc ? (wide >> (period - cc)) : 0)) & widemask
            : ((wide >> cc) | (cc ? (wide << (period - cc)) : 0)) & widemask;
        res = (uint32_t)(rot & m);
        c = (uint32_t)((rot >> bits) & 1u);
        break;
    }
    default: return -1;
    }

    store_dn(dreg, res, sz);

    if (!touches_x) {                                          /* ROL/ROR: preserve X */
        g_cpu.SR &= ~0x0Fu;
        if (!res) g_cpu.SR |= SR_Z;
        if ((res >> (bits - 1)) & 1u) g_cpu.SR |= SR_N;
        if (c) g_cpu.SR |= SR_C;
    } else if (reg_count && cnt == 0) {                        /* count 0: preserve X, C=0 */
        g_cpu.SR &= ~0x0Fu;
        if (!res) g_cpu.SR |= SR_Z;
        if ((res >> (bits - 1)) & 1u) g_cpu.SR |= SR_N;
    } else {
        g_cpu.SR &= ~0x1Fu;
        if (!res) g_cpu.SR |= SR_Z;
        if ((res >> (bits - 1)) & 1u) g_cpu.SR |= SR_N;
        if (c) { g_cpu.SR |= SR_C; g_cpu.SR |= SR_X; }
        if (with_v && v) g_cpu.SR |= SR_V;
    }
    return 0;
}

/* DIVU/DIVS — exact port of clown68000's Action_DIVCommon (the HW-accurate
 * overflow path the generated C omits: on quotient overflow, set V+N, clear Z,
 * and leave Dn UNCHANGED). On real (non-overflow) inputs this matches the
 * generated C; the fuzzer only exercises the overflow edge. */
static void do_div(int dr, uint32_t src16, int is_signed) {
    uint32_t dest = g_cpu.D[dr];
    g_cpu.SR &= ~SR_C;                                   /* carry always cleared */
    if ((uint16_t)src16 == 0) {                          /* div by zero: clown traps (vec 5) */
        g_cpu.SR &= ~(SR_N | SR_Z | SR_V);               /* (harness skips the trap excursion) */
        return;
    }
    int src_neg  = is_signed && ((src16 & 0x8000u) != 0);
    int dest_neg = is_signed && ((dest & 0x80000000u) != 0);
    int res_neg  = (src_neg != dest_neg);
    uint32_t abs_src  = src_neg  ? (uint32_t)(0u - (uint32_t)(int32_t)(int16_t)(uint16_t)src16)
                                 : (uint32_t)(uint16_t)src16;
    uint32_t abs_dest = dest_neg ? (0u - dest) : dest;

    if (abs_src >= (abs_dest >> 16)) {                   /* unsigned overflow pre-check */
        uint32_t abs_quo = abs_dest / abs_src;
        if (!is_signed || abs_quo <= (res_neg ? 0x8000u : 0x7FFFu)) {
            uint32_t abs_rem = abs_dest % abs_src;
            uint32_t quo = res_neg  ? (0u - abs_quo) : abs_quo;
            uint32_t rem = dest_neg ? (0u - abs_rem) : abs_rem;
            g_cpu.D[dr] = (quo & 0xFFFFu) | ((rem & 0xFFFFu) << 16);
            g_cpu.SR &= ~(SR_N | SR_Z | SR_V);
            if (quo & 0x8000u) g_cpu.SR |= SR_N;
            if (quo == 0)      g_cpu.SR |= SR_Z;
            return;
        }
    }
    /* overflow */
    g_cpu.SR |= SR_V; g_cpu.SR |= SR_N; g_cpu.SR &= ~SR_Z;   /* Dn left unchanged */
}

/* ADDX/SUBX — port of code_generator.c (Dn,Dn and -(Ay),-(Ax) forms).
 * X added/subtracted; Z is sticky (cleared only when result!=0). */
static void do_addx_subx(const M68KInstr *ins, M68KSize sz, int is_add) {
    uint16_t w0 = ins->words[0];
    int dst = (w0 >> 9) & 7, src = w0 & 7;
    int bits = szbits(sz), sb = szbytes(sz);
    uint32_t m = szmask(sz), sign = (uint32_t)(1u << (bits - 1));
    uint32_t a, b;
    if (ins->predec_mem_form) {
        int ay_dec = (src == 7 && sz == M68K_SIZE_B) ? 2 : sb;
        int ax_dec = (dst == 7 && sz == M68K_SIZE_B) ? 2 : sb;
        g_cpu.A[src] -= ay_dec; b = mem_read(sz, g_cpu.A[src]) & m;
        g_cpu.A[dst] -= ax_dec; a = mem_read(sz, g_cpu.A[dst]) & m;
    } else { a = g_cpu.D[dst] & m; b = g_cpu.D[src] & m; }
    uint32_t x = (g_cpu.SR >> 4) & 1u;
    uint64_t full = is_add ? ((uint64_t)a + b + x) : ((uint64_t)a - b - x);
    uint32_t r = (uint32_t)full & m;
    int zold = (g_cpu.SR >> 2) & 1;
    g_cpu.SR &= ~0x1Fu;
    if (!r && zold) g_cpu.SR |= SR_Z;
    if (r >> (bits - 1)) g_cpu.SR |= SR_N;
    int carry = is_add ? ((full >> bits) & 1u) : ((full >> 63) & 1u);
    if (carry) { g_cpu.SR |= SR_C; g_cpu.SR |= SR_X; }
    int v = is_add ? (!((a ^ b) & sign) && ((a ^ r) & sign))
                   : ( ((a ^ b) & sign) && ((a ^ r) & sign));
    if (v) g_cpu.SR |= SR_V;
    if (ins->predec_mem_form) mem_write(sz, g_cpu.A[dst], r);
    else                      store_dn(dst, r, sz);
}

/* ABCD/SBCD/NBCD — packed-BCD, port of code_generator.c. op: 0=ABCD 1=SBCD
 * 2=NBCD. Z is sticky; X=C. Operates byte-wide. */
static void do_bcd(const M68KInstr *ins, int op) {
    int x = (g_cpu.SR >> 4) & 1;
    uint8_t a, b = 0, r; int carry; uint32_t a_addr = 0; int to_mem = 0;
    if (op == 2) {                               /* NBCD <ea> (RMW) */
        ExtR er; er_init(&er, ins);
        if (((ins->src_ea >> 3) & 7) <= 1) {     /* Dn/An direct handled via read/store */
            a = (uint8_t)(read_ea_ex(ins, ins->src_ea, M68K_SIZE_B, &er, 1) & 0xFFu);
        } else { a = (uint8_t)(read_ea_ex(ins, ins->src_ea, M68K_SIZE_B, &er, 1) & 0xFFu); }
        int lo = -(int)(a & 0xF) - x;
        int adj_lo = (lo < 0) ? 6 : 0;
        int hi = -(int)(a >> 4) - (lo < 0 ? 1 : 0);
        int adj_hi = (hi < 0) ? 6 : 0;
        int full = (hi & 0xFF) * 16 - adj_hi * 16 + ((lo - adj_lo) & 0xF);
        r = (uint8_t)full; carry = (hi < 0);
        ExtR er2; er_init(&er2, ins);
        write_ea_ex(ins, ins->src_ea, M68K_SIZE_B, &er2, r, 1);
        goto flags;
    }
    int dst = ins->reg, src = ins->src_ea & 7;
    if (ins->predec_mem_form) {
        int ay_dec = (src == 7) ? 2 : 1, ax_dec = (dst == 7) ? 2 : 1;
        g_cpu.A[src] -= ay_dec; b = (uint8_t)m68k_read8(g_cpu.A[src]);
        g_cpu.A[dst] -= ax_dec; a = (uint8_t)m68k_read8(g_cpu.A[dst]);
        a_addr = g_cpu.A[dst]; to_mem = 1;
    } else { a = (uint8_t)g_cpu.D[dst]; b = (uint8_t)g_cpu.D[src]; }
    if (op == 0) {                               /* ABCD */
        unsigned lo = (a & 0xF) + (b & 0xF) + x;
        unsigned adj_lo = (lo > 9) ? 6 : 0;
        unsigned hi = (a >> 4) + (b >> 4) + ((lo + adj_lo) >> 4);
        unsigned adj_hi = (hi > 9) ? 6 : 0;
        unsigned full = (hi << 4) + adj_hi * 16 + ((lo + adj_lo) & 0xF);
        r = (uint8_t)full; carry = (full & 0x100) != 0;
    } else {                                     /* SBCD */
        int lo = (int)(a & 0xF) - (int)(b & 0xF) - x;
        int adj_lo = (lo < 0) ? 6 : 0;
        int hi = (int)(a >> 4) - (int)(b >> 4) - (lo < 0 ? 1 : 0);
        int adj_hi = (hi < 0) ? 6 : 0;
        int full = (hi & 0xFF) * 16 - adj_hi * 16 + ((lo - adj_lo) & 0xF);
        r = (uint8_t)full; carry = (hi < 0);
    }
    if (to_mem) m68k_write8(a_addr, r);
    else        g_cpu.D[dst] = (g_cpu.D[dst] & 0xFFFFFF00u) | r;
flags: {
        int zold = (g_cpu.SR >> 2) & 1;
        g_cpu.SR &= ~SR_V;
        if (r) g_cpu.SR &= ~SR_Z; else if (zold) g_cpu.SR |= SR_Z;
        g_cpu.SR &= ~(SR_C | SR_X | SR_N);
        if (r & 0x80u) g_cpu.SR |= SR_N;
        if (carry) { g_cpu.SR |= SR_C; g_cpu.SR |= SR_X; }
    }
}

/* MOVEM — port of code_generator.c MN_MOVEM. base address resolved from a
 * fresh ExtReader at words[2]; predecrement uses the reversed register mask;
 * mem->reg sign-extends .W to 32 for both An and Dn. */
static void do_movem(const M68KInstr *ins, M68KSize sz) {
    int dir = (ins->words[0] >> 10) & 1;       /* 0 = reg->mem, 1 = mem->reg */
    uint16_t mask = ins->words[1];
    int ea = ins->src_ea, mode = (ea >> 3) & 7, reg = ea & 7;
    int sb = (sz == M68K_SIZE_L) ? 4 : 2;

    ExtR er2; er_init_at(&er2, ins, 2);
    uint32_t base = 0;
    switch (mode) {
    case 2: case 3: case 4: base = g_cpu.A[reg]; break;
    case 5: { int16_t d16 = (int16_t)er_next(&er2); base = (uint32_t)(g_cpu.A[reg] + (int32_t)d16); break; }
    case 6: { uint16_t ext = er_next(&er2); int xreg=(ext>>12)&7; int8_t d8=(int8_t)(ext&0xFF);
              uint32_t xv=m68k_brief_index_value(ext,g_cpu.D[xreg],g_cpu.A[xreg]);
              base = (uint32_t)(g_cpu.A[reg] + xv + (int32_t)d8); break; }
    case 7:
        switch (reg) {
        case 0: { int16_t ext=(int16_t)er_next(&er2); base=(uint32_t)(int32_t)ext; break; }
        case 1: base = er_next_dword(&er2); break;
        case 2: { uint32_t pc = ins->addr + er2.bp; int16_t d16=(int16_t)er_next(&er2);
                  base = (uint32_t)((int32_t)pc + d16); break; }
        case 3: { uint32_t pc = ins->addr + er2.bp; uint16_t ext = er_next(&er2);   /* (d8,PC,Xn) */
                  int xreg=(ext>>12)&7; int8_t d8=(int8_t)(ext&0xFF);
                  uint32_t xv=m68k_brief_index_value(ext,g_cpu.D[xreg],g_cpu.A[xreg]);
                  base = (uint32_t)(pc + xv + (int32_t)d8); break; }
        default: break;
        }
        break;
    default: break;
    }

    if (dir == 0 && mode == 4) {                /* reg->mem, predecrement: reversed mask */
        uint32_t mb = base;
        for (int bit = 0; bit < 16; bit++) {
            if (!(mask & (1u << bit))) continue;
            int is_an = (bit < 8); int ridx = is_an ? (7 - bit) : (15 - bit);
            uint32_t rv = is_an ? g_cpu.A[ridx] : g_cpu.D[ridx];
            mb -= sb;
            if (sz == M68K_SIZE_L) m68k_write32(mb, rv); else m68k_write16(mb, (uint16_t)rv);
        }
        g_cpu.A[reg] = mb;
    } else if (dir == 0) {                       /* reg->mem, standard */
        uint32_t mb = base;
        for (int bit = 0; bit < 16; bit++) {
            if (!(mask & (1u << bit))) continue;
            int is_an = (bit >= 8); int ridx = is_an ? (bit - 8) : bit;
            uint32_t rv = is_an ? g_cpu.A[ridx] : g_cpu.D[ridx];
            if (sz == M68K_SIZE_L) { m68k_write32(mb, rv); mb += 4; }
            else                   { m68k_write16(mb, (uint16_t)rv); mb += 2; }
        }
        if (mode == 3) g_cpu.A[reg] = mb;
    } else {                                     /* mem->reg, standard */
        uint32_t mb = base;
        for (int bit = 0; bit < 16; bit++) {
            if (!(mask & (1u << bit))) continue;
            int is_an = (bit >= 8); int ridx = is_an ? (bit - 8) : bit;
            if (sz == M68K_SIZE_L) {
                uint32_t v = m68k_read32(mb); mb += 4;
                if (is_an) g_cpu.A[ridx] = v; else g_cpu.D[ridx] = v;
            } else {
                uint32_t v = (uint32_t)(int32_t)(int16_t)m68k_read16(mb); mb += 2;  /* .W sign-extends to 32 */
                if (is_an) g_cpu.A[ridx] = v; else g_cpu.D[ridx] = v;
            }
        }
        if (mode == 3) g_cpu.A[reg] = mb;
    }
}

/* =========================================================================
 * Single-instruction execution.
 *
 * Returns M68KI_OK and writes *next_pc on success; returns M68KI_HALT_UNIMPL
 * (with bad_pc/op recorded) for anything not yet implemented.
 * ========================================================================= */
/* ---- Executed-PC coverage (always-on ring; queried, never armed) ---------
 * Replaces the coverage the deleted clown68000 oracle used to provide. Every
 * instruction the interpreter retires marks a bit here, so the record is
 * complete from process start rather than from the moment a probe attached —
 * whether the interpreter is driving the whole program (GENESIS_FORCE_INTERP)
 * or only running Tier-3 floor capsules.
 *
 * One bit per word-aligned address over the 4 MB cart space = 256 KB, lazily
 * allocated on first mark so a build that never interprets pays nothing. The
 * hot path is a shift and an OR, not a printf. */
#define M68KI_COV_WORDS  (0x400000u >> 1)          /* word-aligned PCs */
#define M68KI_COV_BYTES  ((M68KI_COV_WORDS + 7u) >> 3)
static uint8_t *s_exec_cov = NULL;

static void cov_mark(uint32_t pc) {
    if (pc >= 0x400000u) return;                   /* RAM-resident code: not cart */
    if (!s_exec_cov) {
        s_exec_cov = (uint8_t *)calloc(M68KI_COV_BYTES, 1);
        if (!s_exec_cov) return;
    }
    uint32_t i = pc >> 1;
    s_exec_cov[i >> 3] |= (uint8_t)(1u << (i & 7u));
}

int m68k_interp_cov_active(void) { return s_exec_cov != NULL; }

/* Append the executed set, one hex address per line, ascending. Returns the
 * count written, or -1 if nothing was ever interpreted. */
long m68k_interp_cov_dump(FILE *f) {
    if (!s_exec_cov) return -1;
    long n = 0;
    for (uint32_t i = 0; i < M68KI_COV_WORDS; i++) {
        if (s_exec_cov[i >> 3] & (1u << (i & 7u))) {
            fprintf(f, "%06X\n", i << 1);
            n++;
        }
    }
    return n;
}

static M68kiStatus exec_one(const M68KInstr *ins, uint32_t *next_pc) {
    cov_mark(ins->addr);   /* every interpreted instruction, all entry paths */
    ExtR er; er_init(&er, ins);
    uint32_t fall = ins->addr + ins->byte_length;
    *next_pc = fall;
    M68KSize sz = ins->size;
    int dir = (ins->words[0] >> 8) & 1;
    int cc  = (ins->words[0] >> 8) & 0xF;

    switch (ins->mnemonic) {
    case MN_NOP:
        return M68KI_OK;

    case MN_MOVE: {
        /* MOVE to An (illegal — would be MOVEA) is caught generically by the
         * write_ea An guard + the step wrapper's s_illegal_ea check. */
        uint32_t v = read_ea(ins, ins->src_ea, sz, &er);
        write_ea(ins, ins->dst_ea, sz, &er, v);
        flags_logic(v, sz);
        return M68KI_OK;
    }
    case MN_MOVEA: {
        uint32_t v = read_ea(ins, ins->src_ea, sz, &er);
        g_cpu.A[ins->reg] = sext(v, sz);   /* .W sign-extends to 32; no flags */
        return M68KI_OK;
    }
    case MN_MOVEQ:
        g_cpu.D[ins->reg] = (uint32_t)(int32_t)(int8_t)ins->imm32;
        flags_logic(g_cpu.D[ins->reg], M68K_SIZE_L);
        return M68KI_OK;

    case MN_LEA:
        g_cpu.A[ins->reg] = addr_ea(ins, ins->src_ea, &er);
        return M68KI_OK;
    case MN_PEA: {
        uint32_t a = addr_ea(ins, ins->src_ea, &er);
        push32(a);
        return M68KI_OK;
    }

    case MN_TST: {
        uint32_t v = read_ea(ins, ins->src_ea, sz, &er);
        flags_logic(v, sz);
        return M68KI_OK;
    }
    case MN_CLR: {
        /* 68000 quirk: CLR does a (discarded) read before writing 0. */
        write_ea(ins, ins->src_ea, sz, &er, 0);
        g_cpu.SR = (uint16_t)((g_cpu.SR & ~0x0Fu) | SR_Z);
        return M68KI_OK;
    }

    case MN_EXT:
        if (sz == M68K_SIZE_W) { uint32_t v = (uint32_t)(int32_t)(int8_t)g_cpu.D[ins->reg];
                                 store_dn(ins->reg, v, M68K_SIZE_W); flags_logic(v, M68K_SIZE_W); }
        else                   { uint32_t v = (uint32_t)(int32_t)(int16_t)g_cpu.D[ins->reg];
                                 g_cpu.D[ins->reg] = v;             flags_logic(v, M68K_SIZE_L); }
        return M68KI_OK;
    case MN_SWAP: {
        uint32_t v = g_cpu.D[ins->reg];
        v = (v >> 16) | (v << 16);
        g_cpu.D[ins->reg] = v;
        flags_logic(v, M68K_SIZE_L);
        return M68KI_OK;
    }
    case MN_EXG: {
        /* Exchange two 32-bit registers. Encoding's opmode picks the pairing;
         * the decoder gives the two register indices in reg (Rx) and src_ea&7
         * (Ry) with the bank chosen by opmode bits. We reconstruct from words[0]. */
        uint16_t w = ins->words[0];
        int opmode = (w >> 3) & 0x1F, rx = (w >> 9) & 7, ry = w & 7;
        uint32_t *px, *py;
        if      (opmode == 0x08) { px = &g_cpu.D[rx]; py = &g_cpu.D[ry]; } /* D,D */
        else if (opmode == 0x09) { px = &g_cpu.A[rx]; py = &g_cpu.A[ry]; } /* A,A */
        else                     { px = &g_cpu.D[rx]; py = &g_cpu.A[ry]; } /* D,A (0x11) */
        uint32_t t = *px; *px = *py; *py = t;
        return M68KI_OK;
    }

    /* ---- ADD/SUB (arith, reg<->EA, both directions) ---- */
    case MN_ADD: case MN_SUB: {
        int isadd = (ins->mnemonic == MN_ADD);
        int dreg = ins->reg; uint32_t m = szmask(sz);
        if (dir == 0) {
            uint32_t b = read_ea(ins, ins->src_ea, sz, &er) & m;
            uint32_t a = g_cpu.D[dreg] & m;
            uint32_t r = isadd ? (a + b) : (a - b);
            if (isadd) flags_add(a, b, r, sz); else flags_sub(a, b, r, sz);
            store_dn(dreg, r, sz);
        } else {
            ExtR save = er;
            uint32_t a = read_ea_ex(ins, ins->src_ea, sz, &er, 1) & m;
            uint32_t b = g_cpu.D[dreg] & m;
            uint32_t r = isadd ? (a + b) : (a - b);
            if (isadd) flags_add(a, b, r, sz); else flags_sub(a, b, r, sz);
            er = save;
            write_ea_ex(ins, ins->src_ea, sz, &er, r, 1);
        }
        return M68KI_OK;
    }
    case MN_ADDA: case MN_SUBA: {
        uint32_t v = sext(read_ea(ins, ins->src_ea, sz, &er), sz);
        if (ins->mnemonic == MN_ADDA) g_cpu.A[ins->reg] += v;
        else                          g_cpu.A[ins->reg] -= v;
        return M68KI_OK;   /* address ops set no flags */
    }
    case MN_CMP: {
        uint32_t m = szmask(sz);
        uint32_t b = read_ea(ins, ins->src_ea, sz, &er) & m;
        uint32_t a = g_cpu.D[ins->reg] & m;
        flags_cmp(a, b, a - b, sz);
        return M68KI_OK;
    }
    case MN_CMPA: {
        uint32_t b = sext(read_ea(ins, ins->src_ea, sz, &er), sz);
        uint32_t a = g_cpu.A[ins->reg];
        flags_cmp(a, b, a - b, M68K_SIZE_L);
        return M68KI_OK;
    }
    case MN_CMPM: {
        /* CMPM (Ay)+,(Ax)+ : Ax is dest (ins->reg), Ay is src (src_ea&7). */
        int ay = ins->src_ea & 7, ax = ins->reg, sb = szbytes(sz); uint32_t m = szmask(sz);
        uint32_t b = mem_read(sz, g_cpu.A[ay]) & m; g_cpu.A[ay] += sb;
        uint32_t a = mem_read(sz, g_cpu.A[ax]) & m; g_cpu.A[ax] += sb;
        flags_cmp(a, b, a - b, sz);
        return M68KI_OK;
    }

    /* ---- AND/OR/EOR (logic, reg<->EA) ---- */
    case MN_AND: case MN_OR: {
        int isand = (ins->mnemonic == MN_AND);
        int dreg = ins->reg; uint32_t m = szmask(sz);
        if (dir == 0) {
            uint32_t b = read_ea(ins, ins->src_ea, sz, &er) & m;
            uint32_t a = g_cpu.D[dreg] & m;
            uint32_t r = isand ? (a & b) : (a | b);
            store_dn(dreg, r, sz); flags_logic(r, sz);
        } else {
            ExtR save = er;
            uint32_t a = read_ea_ex(ins, ins->src_ea, sz, &er, 1) & m;
            uint32_t b = g_cpu.D[dreg] & m;
            uint32_t r = isand ? (a & b) : (a | b);
            flags_logic(r, sz);
            er = save;
            write_ea_ex(ins, ins->src_ea, sz, &er, r, 1);
        }
        return M68KI_OK;
    }
    case MN_EOR: {   /* EOR has only the Dn->EA (dir=1 style) form */
        int dreg = ins->reg; uint32_t m = szmask(sz);
        ExtR save = er;
        uint32_t a = read_ea_ex(ins, ins->src_ea, sz, &er, 1) & m;
        uint32_t r = (a ^ (g_cpu.D[dreg] & m));
        flags_logic(r, sz);
        er = save;
        write_ea_ex(ins, ins->src_ea, sz, &er, r, 1);
        return M68KI_OK;
    }

    /* ---- ADDQ/SUBQ ---- */
    case MN_ADDQ: case MN_SUBQ: {
        int isadd = (ins->mnemonic == MN_ADDQ);
        uint32_t q = ins->imm32 ? ins->imm32 : 8u;
        int mode = (ins->src_ea >> 3) & 7;
        if (mode == 1) {                 /* to An: full 32-bit, no flags */
            int r = ins->src_ea & 7;
            g_cpu.A[r] += isadd ? (int32_t)q : -(int32_t)q;
            return M68KI_OK;
        }
        uint32_t m = szmask(sz);
        ExtR save = er;
        uint32_t a = read_ea_ex(ins, ins->src_ea, sz, &er, 1) & m;
        uint32_t r = isadd ? (a + q) : (a - q);
        if (isadd) flags_add(a, q, r, sz); else flags_sub(a, q, r, sz);
        er = save;
        write_ea_ex(ins, ins->src_ea, sz, &er, r, 1);
        return M68KI_OK;
    }

    /* ---- immediate-RMW (ADDI/SUBI/CMPI/ANDI/ORI/EORI) ---- */
    case MN_ADDI: case MN_SUBI: case MN_CMPI:
    case MN_ANDI: case MN_ORI:  case MN_EORI: {
        uint32_t imm = er_next_imm(&er, sz);   /* immediate first in the stream */
        uint32_t m = szmask(sz);
        if (ins->mnemonic == MN_CMPI) {
            /* CMPI is read-only: for (An)+ it MUST post-increment (rmw=0). */
            uint32_t a = read_ea(ins, ins->src_ea, sz, &er) & m;
            flags_cmp(a, imm & m, a - (imm & m), sz);
            return M68KI_OK;
        }
        ExtR after_imm = er;                   /* EA ext words start here */
        uint32_t a = read_ea_ex(ins, ins->src_ea, sz, &er, 1) & m;
        uint32_t r, im = imm & m;
        int arith = 0;
        switch (ins->mnemonic) {
        case MN_ADDI: r = a + im;  arith = 1; break;
        case MN_SUBI: r = a - im;  arith = 2; break;
        case MN_ANDI: r = a & im;            break;
        case MN_ORI:  r = a | im;            break;
        default:      r = a ^ im;            break;  /* EORI */
        }
        if (arith == 1)      flags_add(a, im, r, sz);
        else if (arith == 2) flags_sub(a, im, r, sz);
        else                 flags_logic(r, sz);
        er = after_imm;
        write_ea_ex(ins, ins->src_ea, sz, &er, r, 1);
        return M68KI_OK;
    }

    /* ---- control flow ---- */
    case MN_BRA:
        *next_pc = ins->target_addr;
        return M68KI_OK;
    case MN_Bcc:
        if (eval_cond(cc)) *next_pc = ins->target_addr;
        return M68KI_OK;
    case MN_BSR:
        discover(ins->target_addr);
        push32(fall);
        *next_pc = ins->target_addr;
        return M68KI_OK;
    case MN_DBcc: {
        if (eval_cond(cc)) return M68KI_OK;          /* condition true -> fall through */
        uint32_t dn = (g_cpu.D[ins->reg] & 0xFFFFu);
        dn = (dn - 1) & 0xFFFFu;
        store_dn(ins->reg, dn, M68K_SIZE_W);
        if ((int16_t)dn != -1) *next_pc = ins->target_addr;
        return M68KI_OK;
    }
    case MN_JMP:
        *next_pc = addr_ea(ins, ins->src_ea, &er);
        discover(*next_pc);
        return M68KI_OK;
    case MN_JSR: {
        uint32_t tgt = addr_ea(ins, ins->src_ea, &er);
        discover(tgt);
        push32(fall);
        *next_pc = tgt;
        return M68KI_OK;
    }
    case MN_RTS:
        *next_pc = pop32();
        return M68KI_OK;
    case MN_RTR: {                       /* pop CCR (word), then PC (long) */
        uint16_t ccr = pop16();
        g_cpu.SR = (uint16_t)((g_cpu.SR & 0xFF00u) | (ccr & 0xFFu));
        *next_pc = pop32();
        return M68KI_OK;
    }
    case MN_RTE: {                       /* pop SR (word), then PC (long) */
        g_cpu.SR = pop16() & 0xA71Fu;     /* mask to valid SR bits (T,S,I,CCR) */
        *next_pc = pop32();
        return M68KI_OK;
    }

    /* ---- shifts / rotates (register Dn target) ---- */
    case MN_LSL: case MN_LSR: case MN_ASL: case MN_ASR:
    case MN_ROL: case MN_ROR: case MN_ROXL: case MN_ROXR: {
        if (ins->mem_shift) {            /* 1-bit shift of a word in memory */
            ExtR save = er;
            uint32_t sv = read_ea_ex(ins, ins->src_ea, M68K_SIZE_W, &er, 1) & 0xFFFFu;
            /* reuse do_shift by faking a Dn: load into a scratch via D-less path */
            uint32_t res, c = 0, v = 0; int with_v = 0, touches_x = 1;
            switch (ins->mnemonic) {
            case MN_LSL: res = (sv << 1) & 0xFFFFu; c = (sv >> 15) & 1u; break;
            case MN_LSR: res = sv >> 1;             c = sv & 1u;        break;
            case MN_ASL: res = (sv << 1) & 0xFFFFu; c = (sv >> 15) & 1u; with_v = 1;
                         if ((sv & 0x8000u) != (res & 0x8000u)) v = 1;  break;
            case MN_ASR: res = (uint32_t)(((int16_t)sv) >> 1) & 0xFFFFu; c = sv & 1u; with_v = 1; break;
            case MN_ROL: touches_x = 0; res = ((sv << 1) | (sv >> 15)) & 0xFFFFu; c = (sv >> 15) & 1u; break;
            case MN_ROR: touches_x = 0; res = ((sv >> 1) | (sv << 15)) & 0xFFFFu; c = sv & 1u; break;
            case MN_ROXL: { uint32_t x=(g_cpu.SR>>4)&1u; res=((sv<<1)|x)&0xFFFFu; c=(sv>>15)&1u; break; }
            default:      { uint32_t x=(g_cpu.SR>>4)&1u; res=((sv>>1)|(x<<15))&0xFFFFu; c=sv&1u; break; } /* ROXR */
            }
            er = save;
            write_ea_ex(ins, ins->src_ea, M68K_SIZE_W, &er, res, 1);
            if (!touches_x) { g_cpu.SR &= ~0x0Fu; if(!res)g_cpu.SR|=SR_Z; if(res&0x8000u)g_cpu.SR|=SR_N; if(c)g_cpu.SR|=SR_C; }
            else { g_cpu.SR &= ~0x1Fu; if(!res)g_cpu.SR|=SR_Z; if(res&0x8000u)g_cpu.SR|=SR_N;
                   if(c){g_cpu.SR|=SR_C;g_cpu.SR|=SR_X;} if(with_v&&v)g_cpu.SR|=SR_V; }
            return M68KI_OK;
        }
        int reg_count = (ins->src_ea >= 0);
        if (do_shift(ins->mnemonic, ins->reg, sz, reg_count, ins->src_ea, (int)(ins->imm32 & 63u)) != 0) {
            g_m68ki_bad_pc = ins->addr; g_m68ki_bad_op = ins->words[0];
            return M68KI_HALT_UNIMPL;
        }
        return M68KI_OK;
    }

    /* ---- bit ops ---- */
    case MN_BTST: case MN_BCHG: case MN_BCLR: case MN_BSET: {
        int is_imm = (ins->reg < 0);
        int ea = ins->src_ea, ea_mode = (ea >> 3) & 7;
        int bit_mask = (ea_mode == 0) ? 31 : 7;
        M68KSize bsz = (ea_mode == 0) ? M68K_SIZE_L : M68K_SIZE_B;
        uint32_t bn;
        if (is_imm) { bn = ins->imm32 & (uint32_t)bit_mask; er_init_at(&er, ins, 2); }
        else        { bn = (uint32_t)g_cpu.D[ins->reg] & (uint32_t)bit_mask; }
        if (ins->mnemonic == MN_BTST) {
            /* BTST is read-only: for (An)+ it MUST post-increment (rmw=0). */
            uint32_t src = read_ea(ins, ea, bsz, &er) & szmask(bsz);
            g_cpu.SR = (uint16_t)((g_cpu.SR & ~SR_Z) | ((!(src & (1u << bn))) ? SR_Z : 0u));
            return M68KI_OK;
        }
        ExtR save = er;                                  /* BCHG/BCLR/BSET: RMW */
        uint32_t src = read_ea_ex(ins, ea, bsz, &er, 1) & szmask(bsz);
        g_cpu.SR = (uint16_t)((g_cpu.SR & ~SR_Z) | ((!(src & (1u << bn))) ? SR_Z : 0u));
        uint32_t res = (ins->mnemonic == MN_BCHG) ? (src ^ (1u << bn))
                     : (ins->mnemonic == MN_BCLR) ? (src & ~(1u << bn))
                                                  : (src | (1u << bn));
        er = save;
        write_ea_ex(ins, ea, bsz, &er, res, 1);
        return M68KI_OK;
    }

    /* ---- NEG / NEGX / NOT ---- */
    case MN_NEG: case MN_NEGX: case MN_NOT: {
        uint32_t m = szmask(sz);
        ExtR save = er;
        uint32_t a = read_ea_ex(ins, ins->src_ea, sz, &er, 1) & m;
        uint32_t r;
        if (ins->mnemonic == MN_NOT) { r = (~a) & m; flags_logic(r, sz); }
        else {
            uint32_t x = (ins->mnemonic == MN_NEGX) ? ((g_cpu.SR >> 4) & 1u) : 0u;
            r = (0u - a - x) & m;
            flags_sub(0u, a, r, sz);
        }
        er = save;
        write_ea_ex(ins, ins->src_ea, sz, &er, r, 1);
        return M68KI_OK;
    }

    /* ---- TAS ---- */
    case MN_TAS: {
        ExtR save = er;
        uint32_t v = read_ea_ex(ins, ins->src_ea, M68K_SIZE_B, &er, 1) & 0xFFu;
        g_cpu.SR &= ~0x0Fu;
        if (!v) g_cpu.SR |= SR_Z;
        if (v >> 7) g_cpu.SR |= SR_N;
        er = save;
        write_ea_ex(ins, ins->src_ea, M68K_SIZE_B, &er, v | 0x80u, 1);
        return M68KI_OK;
    }

    /* ---- MUL / DIV (.W source, .L Dn result) ---- */
    case MN_MULU: {
        uint32_t s = read_ea(ins, ins->src_ea, M68K_SIZE_W, &er) & 0xFFFFu;
        g_cpu.D[ins->reg] = (uint32_t)(uint16_t)g_cpu.D[ins->reg] * s;
        flags_logic(g_cpu.D[ins->reg], M68K_SIZE_L);
        return M68KI_OK;
    }
    case MN_MULS: {
        int32_t s = (int32_t)(int16_t)(read_ea(ins, ins->src_ea, M68K_SIZE_W, &er) & 0xFFFFu);
        g_cpu.D[ins->reg] = (uint32_t)((int32_t)(int16_t)g_cpu.D[ins->reg] * s);
        flags_logic(g_cpu.D[ins->reg], M68K_SIZE_L);
        return M68KI_OK;
    }
    case MN_DIVU:
        do_div(ins->reg, read_ea(ins, ins->src_ea, M68K_SIZE_W, &er) & 0xFFFFu, 0);
        return M68KI_OK;
    case MN_DIVS:
        do_div(ins->reg, read_ea(ins, ins->src_ea, M68K_SIZE_W, &er) & 0xFFFFu, 1);
        return M68KI_OK;

    /* ---- MOVEM ---- */
    case MN_MOVEM:
        do_movem(ins, sz);
        return M68KI_OK;

    /* ---- LINK / UNLK ---- */
    case MN_LINK: {
        int ar = ins->reg; int16_t disp = (int16_t)ins->words[1];
        g_cpu.A[7] -= 4; m68k_write32(g_cpu.A[7], g_cpu.A[ar]);
        g_cpu.A[ar] = g_cpu.A[7];
        g_cpu.A[7] += (int32_t)disp;
        return M68KI_OK;
    }
    case MN_UNLK: {
        int ar = ins->reg;
        g_cpu.A[7] = g_cpu.A[ar];
        g_cpu.A[ar] = m68k_read32(g_cpu.A[7]);
        g_cpu.A[7] += 4;
        return M68KI_OK;
    }

    /* ---- Scc ---- */
    case MN_Scc: {
        uint8_t val = eval_cond(cc) ? 0xFFu : 0x00u;
        write_ea(ins, ins->src_ea, M68K_SIZE_B, &er, val);
        return M68KI_OK;
    }

    /* ---- MOVE to/from SR / CCR / USP ---- */
    case MN_MOVE_SR:
        if (!ins->dst_is_ea) g_cpu.SR = (uint16_t)(read_ea(ins, ins->src_ea, M68K_SIZE_W, &er) & 0xA71Fu);
        else                 write_ea(ins, ins->src_ea, M68K_SIZE_W, &er, g_cpu.SR);
        return M68KI_OK;
    case MN_MOVE_CCR:        /* CCR is 5 bits (0x1F) — clown masks; matches HW */
        if (!ins->dst_is_ea)
            g_cpu.SR = (uint16_t)((g_cpu.SR & 0xFF00u) | (read_ea(ins, ins->src_ea, M68K_SIZE_W, &er) & 0x1Fu));
        else
            write_ea(ins, ins->src_ea, M68K_SIZE_W, &er, (uint16_t)(g_cpu.SR & 0x001Fu));
        return M68KI_OK;
    case MN_MOVE_USP:
        if ((ins->words[0] >> 3) & 1) g_cpu.A[ins->reg] = g_cpu.USP;   /* USP->An */
        else                          g_cpu.USP = g_cpu.A[ins->reg];   /* An->USP */
        return M68KI_OK;

    /* ---- ADDX / SUBX ---- */
    case MN_ADDX: do_addx_subx(ins, sz, 1); return M68KI_OK;
    case MN_SUBX: do_addx_subx(ins, sz, 0); return M68KI_OK;

    /* ---- packed BCD ---- */
    case MN_ABCD: do_bcd(ins, 0); return M68KI_OK;
    case MN_SBCD: do_bcd(ins, 1); return M68KI_OK;
    case MN_NBCD: do_bcd(ins, 2); return M68KI_OK;

    /* ---- immediate to SR / CCR (then mask to valid SR bits, like HW) ---- */
    case MN_ORI_TO_CCR:  g_cpu.SR |= (uint16_t)(ins->imm32 & 0xFFu);              g_cpu.SR &= 0xA71Fu; return M68KI_OK;
    case MN_ORI_TO_SR:   g_cpu.SR |= (uint16_t)(ins->imm32 & 0xFFFFu);            g_cpu.SR &= 0xA71Fu; return M68KI_OK;
    case MN_ANDI_TO_CCR: g_cpu.SR &= (uint16_t)(0xFF00u | (ins->imm32 & 0xFFu));  g_cpu.SR &= 0xA71Fu; return M68KI_OK;
    case MN_ANDI_TO_SR:  g_cpu.SR &= (uint16_t)(ins->imm32 & 0xFFFFu);            g_cpu.SR &= 0xA71Fu; return M68KI_OK;
    case MN_EORI_TO_CCR: g_cpu.SR ^= (uint16_t)(ins->imm32 & 0xFFu);              g_cpu.SR &= 0xA71Fu; return M68KI_OK;
    case MN_EORI_TO_SR:  g_cpu.SR ^= (uint16_t)(ins->imm32 & 0xFFFFu);            g_cpu.SR &= 0xA71Fu; return M68KI_OK;

    default:
        g_m68ki_bad_pc = ins->addr;
        g_m68ki_bad_op = ins->words[0];
        return M68KI_HALT_UNIMPL;
    }
}

/* =========================================================================
 * Per-instruction cycle accounting + VBlank servicing.
 *
 * This is the runtime tail code_generator.c's emit_cycle_accounting() appends
 * after EVERY generated instruction:
 *
 *     g_native_insn_count++;
 *     g_cycle_accumulator   += N;
 *     g_audio_cycle_counter += N;
 *     if (g_cycle_accumulator >= g_vblank_threshold) glue_check_vblank();
 *
 * Without it, a floor run executes a whole missed function (and its inlined
 * subtree) with the 68K cycle clock FROZEN: g_cycle_accumulator never advances,
 * so glue_check_vblank() never fires the VBla handler nor yields the game fiber
 * for the duration of the run, and g_audio_cycle_counter never moves so any
 * FM/PSG writes the function makes get stale cycle stamps. That desyncs
 * timing-sensitive code — Sonic 3&K froze ~2000 frames after a single boot-time
 * floor run. Mirroring the generated accounting makes a floored function behave
 * identically to its statically-recompiled twin (parity-by-construction).
 *
 * N is the PRM structural estimate — a verbatim port of code_generator.c's
 * estimate_cycles_prm(). The recompiler bakes clown-measured literals into the
 * generated C when its cycle_probe is available, but that probe links clown
 * (AGPL, dev-only) and is irrelevant here: the floor only needs the accumulator
 * to advance at a realistic rate so VBlank is serviced at the right cadence, and
 * estimate_cycles_prm() is exactly the fallback the recompiler itself uses.
 * Gated on ENABLE_RECOMPILED_CODE so the standalone differential harness (which
 * does not link glue.c) needs none of these symbols.
 * ========================================================================= */

static int interp_popcount16(uint16_t v) {
    int c = 0;
    while (v) { c += v & 1; v >>= 1; }
    return c;
}

/* PRM Table B-1 EA fetch cost (longword adds one memory round-trip). */
static int interp_ea_read_cost(int ea, M68KSize sz) {
    int is_long = (sz == M68K_SIZE_L);
    int mode    = (ea >> 3) & 7;
    int reg     =  ea       & 7;
    switch (mode) {
    case 0: return 0;                              /* Dn        */
    case 1: return 0;                              /* An        */
    case 2: return is_long ? 8  : 4;               /* (An)      */
    case 3: return is_long ? 8  : 4;               /* (An)+     */
    case 4: return is_long ? 10 : 6;               /* -(An)     */
    case 5: return is_long ? 12 : 8;               /* d16(An)   */
    case 6: return is_long ? 14 : 10;              /* d8(An,Xn) */
    case 7:
        switch (reg) {
        case 0: return is_long ? 12 : 8;           /* (xxx).W   */
        case 1: return is_long ? 16 : 12;          /* (xxx).L   */
        case 2: return is_long ? 12 : 8;           /* d16(PC)   */
        case 3: return is_long ? 14 : 10;          /* d8(PC,Xn) */
        case 4: return is_long ? 8  : 4;           /* #imm      */
        }
    }
    return 0;
}
static int interp_ea_write_cost(int ea, M68KSize sz) { return interp_ea_read_cost(ea, sz); }

/* Verbatim port of code_generator.c estimate_cycles_prm(). */
static int interp_estimate_cycles(const M68KInstr *instr) {
    M68KSize sz      = instr->size;
    int      is_long = (sz == M68K_SIZE_L);
    int      ea_src  = interp_ea_read_cost (instr->src_ea, sz);
    int      ea_dst  = interp_ea_write_cost(instr->dst_ea, sz);
    int      dst_mode = (instr->dst_ea >> 3) & 7;
    int      dst_is_reg = (dst_mode <= 1);

    switch (instr->mnemonic) {
    case MN_NOP:   return 4;
    case MN_RTS:   return 16;
    case MN_RTE:   return 20;
    case MN_STOP:  return 4;
    case MN_JSR:   return 18 + ea_src;
    case MN_BSR:   return 18;
    case MN_JMP:   return 10 + ea_src;
    case MN_BRA:   return 10;
    case MN_Bcc:   return 10;
    case MN_DBcc:  return 12;

    case MN_MOVE:  return 4 + ea_src + ea_dst;
    case MN_MOVEA: return 4 + ea_src;
    case MN_MOVEQ: return 4;
    case MN_LEA:   return ea_src ? ea_src : 4;
    case MN_PEA:   return 12 + ea_src;

    case MN_MOVEM: {
        int nregs = (instr->word_count >= 2) ? interp_popcount16(instr->words[1]) : 4;
        return (is_long ? 12 + 8*nregs : 8 + 4*nregs) + ea_src;
    }

    case MN_ADD: case MN_SUB: case MN_AND: case MN_OR:
    case MN_EOR: case MN_CMP:
        if (dst_is_reg) return (is_long ? 6 : 4) + ea_src;
        else            return (is_long ? 12 : 8) + ea_src + ea_dst;

    case MN_ADDA: case MN_SUBA: case MN_CMPA:
        return (is_long ? 6 : 8) + ea_src;

    case MN_ADDQ: case MN_SUBQ:
        if (dst_is_reg) return is_long ? 8 : 4;
        else            return (is_long ? 12 : 8) + ea_dst;

    case MN_ADDX: case MN_SUBX:
        return is_long ? 8 : 4;

    case MN_ADDI: case MN_SUBI: case MN_ANDI: case MN_ORI:
    case MN_EORI:
        if (dst_is_reg) return is_long ? 16 : 8;
        else            return (is_long ? 20 : 12) + ea_dst;

    case MN_CMPI:
        if (dst_is_reg) return is_long ? 14 : 8;
        else            return (is_long ? 12 : 8) + ea_dst;

    case MN_LSL: case MN_LSR:
    case MN_ASL: case MN_ASR:
    case MN_ROL: case MN_ROR:
    case MN_ROXL: case MN_ROXR: {
        if (!dst_is_reg) return 8 + ea_dst;
        uint16_t w0 = instr->words[0];
        int reg_count_mode = (w0 >> 5) & 1;
        int n;
        if (reg_count_mode) { n = 4; }
        else { n = (w0 >> 9) & 7; if (n == 0) n = 8; }
        return (is_long ? 8 : 6) + 2 * n;
    }

    case MN_MULS: return 70;
    case MN_MULU: return 70;
    case MN_DIVS: return 150;
    case MN_DIVU: return 140;

    case MN_TST:
    case MN_CLR:
    case MN_NEG: case MN_NEGX: case MN_NOT:
        if (dst_is_reg) return is_long ? 6 : 4;
        else            return (is_long ? 12 : 8) + ea_dst;

    case MN_NBCD: return dst_is_reg ? 6  : 8  + ea_dst;
    case MN_TAS:  return dst_is_reg ? 4  : 14 + ea_dst;

    case MN_EXT:  return 4;
    case MN_SWAP: return 4;

    case MN_BTST: return dst_is_reg ? 6 : (4 + ea_dst);
    case MN_BCHG: case MN_BCLR: return dst_is_reg ? 8 : (8 + ea_dst);
    case MN_BSET: return dst_is_reg ? 8 : (8 + ea_dst);

    case MN_LINK: return 16;
    case MN_UNLK: return 12;

    case MN_Scc:  return dst_is_reg ? 6 : (8 + ea_dst);

    case MN_TRAP:    return 34;
    case MN_TRAPV:   return 4;
    case MN_CHK:     return 10 + ea_src;
    case MN_RTR:     return 20;
    case MN_RESET:   return 132;
    case MN_ILLEGAL: return 34;

    case MN_ABCD: case MN_SBCD: return 6;

    case MN_EXG:       return 6;
    case MN_MOVE_USP:  return 4;
    case MN_MOVE_SR:   return dst_is_reg ? 6 : (8 + ea_dst);
    case MN_MOVE_CCR:  return 12 + ea_src;
    case MN_MOVEP:     return is_long ? 24 : 16;

    case MN_OTHER:
    default:
        return 4 + ea_src + ea_dst;
    }
}

/* The emit_cycle_accounting() tail, executed after one interpreted instruction. */
static void interp_account_cycles(const M68KInstr *ins) {
    int cyc = -1;
#ifdef GENESIS_COSIM
    /* Charge the SAME clown-measured cost the recompiled code was stamped with,
     * so g_audio_cycle_counter converges with the recomp instead of drifting on
     * a PRM estimate. Falls back to the estimate for addresses not in the table
     * (RAM-resident code). Gated to the cosim build for now — enabling it in the
     * shipping floor is a validated pacing change (see game_cycles.h). */
    cyc = game_insn_cost(ins->addr);
#endif
    if (cyc < 0) cyc = interp_estimate_cycles(ins);
    if (cyc < 0) cyc = 4;                          /* never stall the clock */
    g_native_insn_count++;
    g_cycle_accumulator   += (uint32_t)cyc;
    g_audio_cycle_counter += (uint32_t)cyc;
    if (g_cycle_accumulator >= g_vblank_threshold)
        glue_check_vblank();
    /* Differential co-sim: advance the same monotonic checkpoint axis the
     * recompiled backend advances via emit_cycle_accounting's GEN_COSIM_TICK,
     * with the identical per-instruction cost — so both backends park at the
     * same axis value. No-op unless GENESIS_COSIM. */
    GEN_COSIM_TICK(cyc);
}

/* =========================================================================
 * Public entries.
 * ========================================================================= */

/* Execute one instruction at g_cpu.PC; advance g_cpu.PC. */
M68kiStatus m68k_interp_step(void) {
    uint32_t pc = g_cpu.PC & 0xFFFFFFu;
    if (!pc_fetchable(pc)) { g_m68ki_bad_pc = pc; return M68KI_HALT_BADADDR; }

    M68KInstr ins;
    if (!m68k_decode(busview(), pc, &ins)) {
        g_m68ki_bad_pc = pc; g_m68ki_bad_op = m68k_read16(pc);
        return M68KI_HALT_UNIMPL;
    }
    uint32_t next;
    s_illegal_ea = 0;
    M68kiStatus st = exec_one(&ins, &next);
    if (st != M68KI_OK) return st;
    if (s_illegal_ea) {                       /* illegal An-destination encoding */
        g_m68ki_bad_pc = ins.addr; g_m68ki_bad_op = ins.words[0];
        return M68KI_HALT_UNIMPL;
    }
    g_cpu.PC = next & 0xFFFFFFu;
    /* Mirror the generated code's per-instruction cycle accounting + VBlank
     * servicing so a floor run keeps the 68K clock advancing and lets the VBla
     * handler fire / the game fiber yield between interpreted instructions,
     * exactly as it would between statically-recompiled ones. */
    interp_account_cycles(&ins);
    return M68KI_OK;
}

/* Run until PC == stop_pc (or halt). */
M68kiStatus m68k_interp_run(uint32_t entry_pc, uint32_t stop_pc) {
    g_cpu.PC = entry_pc & 0xFFFFFFu;
    g_m68ki_insn_count = 0;
    g_m68ki_discover_count = 0;

    while (g_cpu.PC != (stop_pc & 0xFFFFFFu)) {
        M68kiStatus st = m68k_interp_step();
        if (st != M68KI_OK) return st;
        if (++g_m68ki_insn_count > M68KI_INSN_GUARD) {
            g_m68ki_bad_pc = g_cpu.PC; return M68KI_HALT_GUARD;
        }
    }
    return M68KI_OK;
}

/* ---------------------------------------------------------------------------
 * Framed capsule run — the edge-aware tier-3 fallback primitive.
 *
 * Runs from entry_pc tracking NET CALL DEPTH (JSR/BSR = +1, the matching
 * RTS/RTR = -1).  The run stops at the *depth-0* return — the point where the
 * capsule's own frame returns to whoever invoked the missed computed transfer.
 * Crucially, that final return is PEEKED, NOT POPPED: we read the return target
 * off A7 and exit with A7 UNCHANGED.  This makes the capsule A7-NEUTRAL, which
 * is what the native loose-A7 model requires — the caller (the generated JSR
 * site's `A7 += 4`, or the enclosing JSR for a JMP-tail/interior miss) performs
 * the single pop, exactly as if the missed code had been a native function
 * whose RTS compiles to `return;`.  Letting the interpreter pop here is the old
 * desync bug the charter guard worked around (double pop: capsule + native).
 *
 * Because the stop condition is "the frame returns" rather than "PC == a
 * specific address", this single capsule is correct for all three miss shapes:
 *   - computed JSR  (target is a fresh function entry; its top-level RTS),
 *   - computed JMP-tail (target shares the caller's frame; the shared RTS),
 *   - interior-label (a JMP landing inside a known function; that function's
 *     RTS) — the case the charter guard had to SKIP under the old model.
 *
 * On success: returns M68KI_OK and writes *out_exit_pc = the (peeked) return
 * target.  The caller validates that target is a plausible return; an
 * implausible one is an UNSAFE_EXIT (signal, never silent corruption).
 * On failure: HALT_* (target wasn't runnable code / runaway / bad fetch).
 *
 * RTE is deliberately NOT treated as a frame return: it unwinds an exception
 * frame, not a subroutine call, and must not appear at the top of a computed
 * dispatch frame — if one shows up the loop will run past it and surface as a
 * halt/guard rather than a silent mis-return.
 * ------------------------------------------------------------------------- */
M68kiStatus m68k_interp_run_framed(uint32_t entry_pc, uint32_t *out_exit_pc) {
    g_cpu.PC = entry_pc & 0xFFFFFFu;
    g_m68ki_insn_count = 0;
    g_m68ki_discover_count = 0;
    if (out_exit_pc) *out_exit_pc = 0;

    int depth = 0;
    for (;;) {
        uint32_t pc = g_cpu.PC & 0xFFFFFFu;
        if (!pc_fetchable(pc)) { g_m68ki_bad_pc = pc; return M68KI_HALT_BADADDR; }

        M68KInstr ins;
        if (!m68k_decode(busview(), pc, &ins)) {
            g_m68ki_bad_pc = pc; g_m68ki_bad_op = m68k_read16(pc);
            return M68KI_HALT_UNIMPL;
        }

        /* The capsule's own return: peek, don't pop (A7-neutral). RTS pops PC;
         * RTR pops a CCR word first, so its PC is one word deeper. */
        if (depth == 0 && (ins.mnemonic == MN_RTS || ins.mnemonic == MN_RTR)) {
            uint32_t ret = (ins.mnemonic == MN_RTR)
                               ? m68k_read32(g_cpu.A[7] + 2u)
                               : m68k_read32(g_cpu.A[7]);
            if (out_exit_pc) *out_exit_pc = ret & 0xFFFFFFu;
            return M68KI_OK;
        }

        uint32_t next;
        s_illegal_ea = 0;
        M68kiStatus st = exec_one(&ins, &next);
        if (st != M68KI_OK) return st;
        if (s_illegal_ea) {
            g_m68ki_bad_pc = ins.addr; g_m68ki_bad_op = ins.words[0];
            return M68KI_HALT_UNIMPL;
        }
        g_cpu.PC = next & 0xFFFFFFu;
        interp_account_cycles(&ins);
        /* Net-depth bookkeeping (after exec). A nested RTS/RTR balances a prior
         * JSR/BSR within the capsule; depth never goes below 0 because a
         * depth-0 return is intercepted above. */
        if (ins.mnemonic == MN_JSR || ins.mnemonic == MN_BSR)
            depth++;
        else if (ins.mnemonic == MN_RTS || ins.mnemonic == MN_RTR)
            depth--;

        if (++g_m68ki_insn_count > M68KI_INSN_GUARD) {
            g_m68ki_bad_pc = g_cpu.PC; return M68KI_HALT_GUARD;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Interrupt-handler run (FORCE_INTERP; also used by co-sim pairing #1).
 *
 * Interprets an interrupt HANDLER BODY from its autovector entry, stopping at
 * the handler's own depth-0 RTE — the exact analog of what the recompiled
 * g_game_spec.call_vblank()/call_hblank() do inside glue's own_deliver_vint /
 * glue_own_interrupt: run the handler for its SIDE EFFECTS (RAM / chip / Z80
 * mailbox writes) with the caller's save/restore around it. The RTE is PEEKED,
 * not executed: glue set A7 = intr_stack with no real exception frame pushed
 * (matching the recomp model), and discards g_cpu afterward, so popping would
 * read garbage. Net call depth (JSR/BSR +1, RTS/RTR -1) keeps a nested
 * subroutine's RTE-free returns from ending the run early.
 * ------------------------------------------------------------------------- */
M68kiStatus m68k_interp_run_handler(uint32_t entry_pc) {
    g_cpu.PC = entry_pc & 0xFFFFFFu;
    g_m68ki_insn_count = 0;
    g_m68ki_discover_count = 0;

    int depth = 0;
    for (;;) {
        uint32_t pc = g_cpu.PC & 0xFFFFFFu;
        if (pc >= ROM_SIZE) { g_m68ki_bad_pc = pc; return M68KI_HALT_BADADDR; }

        M68KInstr ins;
        /* ROM-only handler body (guarded by the ROM_SIZE check above); busview()
         * returns identical bytes for pc < ROM_SIZE. (romview() was removed when
         * the interpreter moved to a unified live bus view.) */
        if (!m68k_decode(busview(), pc, &ins)) {
            g_m68ki_bad_pc = pc; g_m68ki_bad_op = m68k_read16(pc);
            return M68KI_HALT_UNIMPL;
        }

        if (depth == 0 && ins.mnemonic == MN_RTE)
            return M68KI_OK;                 /* handler's own return — stop, don't pop */

        uint32_t next;
        s_illegal_ea = 0;
        M68kiStatus st = exec_one(&ins, &next);
        if (st != M68KI_OK) return st;
        if (s_illegal_ea) {
            g_m68ki_bad_pc = ins.addr; g_m68ki_bad_op = ins.words[0];
            return M68KI_HALT_UNIMPL;
        }
        g_cpu.PC = next & 0xFFFFFFu;
        interp_account_cycles(&ins);
        if (ins.mnemonic == MN_JSR || ins.mnemonic == MN_BSR)
            depth++;
        else if (ins.mnemonic == MN_RTS || ins.mnemonic == MN_RTR)
            depth--;

        if (++g_m68ki_insn_count > M68KI_INSN_GUARD) {
            g_m68ki_bad_pc = g_cpu.PC; return M68KI_HALT_GUARD;
        }
    }
}

/* ---------------------------------------------------------------------------
 * RAM-handler capsule — execute RAM-RESIDENT code from LIVE memory.
 *
 * Games install interrupt handlers and helper stubs by copying (and then
 * PATCHING) code in work RAM: RKA's H-int raster handler advances its own
 * MOVE source operand each HBlank, and its per-scene installer parameterizes
 * that operand at copy time. Statically redirecting such a body to its ROM
 * master executes FROZEN operands — the class of bug behind RKA's dead
 * per-line VSRAM raster. This capsule decodes every instruction from the
 * live bus view (busview: RAM + ROM), so self-modifications take effect on
 * the very next fetch, exactly like hardware.
 *
 * Exit contract (mirrors the generated code's ABI):
 *   - depth-0 RTS/RTR: PEEK the return target without popping (A7-neutral);
 *     the generated caller performs the single pop — identical to the framed
 *     capsule and to recomp_dispatch_ram_stub's old hand-decoded RTS path.
 *   - RTE at any depth: the generated equivalent is `g_rte_pending = 1;
 *     return;` with each JSR level unwinding its own +4 — so set
 *     g_rte_pending, unwind the guest stack by 4*depth, and exit. This is
 *     how interrupt-handler bodies return to glue's delivery wrapper.
 * Cycle accounting runs per instruction (interp_account_cycles), the same
 * tail the generated code emits.
 * ------------------------------------------------------------------------- */
M68kiStatus m68k_interp_run_ram_handler(uint32_t entry_pc, uint32_t *out_exit_pc)
{
    g_cpu.PC = entry_pc & 0xFFFFFFu;
    g_m68ki_insn_count = 0;
    g_m68ki_discover_count = 0;
    if (out_exit_pc) *out_exit_pc = 0;

    int depth = 0;
    for (;;) {
        uint32_t pc = g_cpu.PC & 0xFFFFFFu;
        if (!pc_fetchable(pc)) { g_m68ki_bad_pc = pc; return M68KI_HALT_BADADDR; }

        M68KInstr ins;
        if (!m68k_decode(busview(), pc, &ins)) {
            g_m68ki_bad_pc = pc; g_m68ki_bad_op = m68k_read16(pc);
            return M68KI_HALT_UNIMPL;
        }

        /* Depth-0 subroutine return: peek, don't pop (A7-neutral). */
        if (depth == 0 && (ins.mnemonic == MN_RTS || ins.mnemonic == MN_RTR)) {
            uint32_t ret = (ins.mnemonic == MN_RTR)
                               ? m68k_read32(g_cpu.A[7] + 2u)
                               : m68k_read32(g_cpu.A[7]);
            if (out_exit_pc) *out_exit_pc = ret & 0xFFFFFFu;
            return M68KI_OK;
        }

        /* RTE: generated-code semantics — no exception frame was pushed by
         * glue's delivery wrapper, so nothing is popped. Unwind any nested
         * JSR return addresses this capsule pushed, flag the skip-return,
         * and hand control back to the C caller. */
        if (ins.mnemonic == MN_RTE) {
            g_cpu.A[7] += 4u * (uint32_t)depth;
            g_rte_pending = 1;
            interp_account_cycles(&ins);
            return M68KI_OK;
        }

        uint32_t next;
        s_illegal_ea = 0;
        M68kiStatus st = exec_one(&ins, &next);
        if (st != M68KI_OK) return st;
        if (s_illegal_ea) {
            g_m68ki_bad_pc = ins.addr; g_m68ki_bad_op = ins.words[0];
            return M68KI_HALT_UNIMPL;
        }
        g_cpu.PC = next & 0xFFFFFFu;
        interp_account_cycles(&ins);

        if (ins.mnemonic == MN_JSR || ins.mnemonic == MN_BSR)
            depth++;
        else if (ins.mnemonic == MN_RTS || ins.mnemonic == MN_RTR)
            depth--;

        if (++g_m68ki_insn_count > M68KI_INSN_GUARD) {
            g_m68ki_bad_pc = g_cpu.PC; return M68KI_HALT_GUARD;
        }
    }
}
