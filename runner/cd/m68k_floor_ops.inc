/* Explicit-PC semantics floor, mechanically ported from m68k_interp.c.
 * The caller has already validated instruction encoding and owns PC, cycles,
 * exceptions, privilege checks, and module dispatch. Every stateful helper
 * receives ctx; m68k_step_bind.h must be included before this file. */

enum { M68KF_SR_C = 1u << 0, M68KF_SR_V = 1u << 1, M68KF_SR_Z = 1u << 2,
       M68KF_SR_N = 1u << 3, M68KF_SR_X = 1u << 4 };

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
static void store_dn(M68kStepContext *ctx, int reg, uint32_t val, M68KSize sz) {
    if (sz == M68K_SIZE_L) g_cpu.D[reg] = val;
    else g_cpu.D[reg] = (g_cpu.D[reg] & keepmask(sz)) | (val & szmask(sz));
}

/* =========================================================================
 * ExtReader - sequential walker over instruction extension words.
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
static uint32_t mem_read(M68kStepContext *ctx, M68KSize sz, uint32_t a) {
    switch (sz) { case M68K_SIZE_B: return m68k_read8(a);
                  case M68K_SIZE_L: return m68k_read32(a);
                  default:          return m68k_read16(a); }
}
static void mem_write(M68kStepContext *ctx, M68KSize sz, uint32_t a, uint32_t v) {
    switch (sz) { case M68K_SIZE_B: m68k_write8(a, (uint8_t)v); break;
                  case M68K_SIZE_L: m68k_write32(a, v);        break;
                  default:          m68k_write16(a, (uint16_t)v); break; }
}

static uint32_t read_ea_ex(M68kStepContext *ctx, const M68KInstr *ins, int ea, M68KSize sz, ExtR *er, int rmw) {
    int mode = (ea >> 3) & 7, reg = ea & 7, sb = szbytes(sz);
    if(reg==7 && sz==M68K_SIZE_B)sb=2;
    switch (mode) {
    case 0: return g_cpu.D[reg];
    case 1: return g_cpu.A[reg];
    case 2: return mem_read(ctx, sz, g_cpu.A[reg]);
    case 3: { uint32_t v = mem_read(ctx, sz, g_cpu.A[reg]); if (!rmw) g_cpu.A[reg] += sb; return v; }
    case 4: { g_cpu.A[reg] -= sb; return mem_read(ctx, sz, g_cpu.A[reg]); }
    case 5: { int16_t d16 = (int16_t)er_next(er);
              return mem_read(ctx, sz, (uint32_t)(g_cpu.A[reg] + (int32_t)d16)); }
    case 6: { uint16_t ext = er_next(er);
              int xreg = (ext >> 12) & 7; int8_t d8 = (int8_t)(ext & 0xFF);
              uint32_t xv = m68k_brief_index_value(ext, g_cpu.D[xreg], g_cpu.A[xreg]);
              return mem_read(ctx, sz, (uint32_t)(g_cpu.A[reg] + xv + (int32_t)d8)); }
    case 7:
        switch (reg) {
        case 0: { int16_t ext = (int16_t)er_next(er); return mem_read(ctx, sz, (uint32_t)(int32_t)ext); }
        case 1: { uint32_t a = er_next_dword(er); return mem_read(ctx, sz, a); }
        case 2: { uint32_t pc = ins->addr + er->bp; int16_t d16 = (int16_t)er_next(er);
                  return mem_read(ctx, sz, (uint32_t)((int32_t)pc + (int32_t)d16)); }
        case 3: { uint32_t pc = ins->addr + er->bp; uint16_t ext = er_next(er);
                  int xreg = (ext >> 12) & 7; int8_t d8 = (int8_t)(ext & 0xFF);
                  uint32_t xv = m68k_brief_index_value(ext, g_cpu.D[xreg], g_cpu.A[xreg]);
                  return mem_read(ctx, sz, (uint32_t)(pc + xv + (int32_t)d8)); }
        case 4: return er_next_imm(er, sz);
        default: return 0;
        }
    default: return 0;
    }
}
static uint32_t read_ea(M68kStepContext *ctx, const M68KInstr *ins, int ea, M68KSize sz, ExtR *er) {
    return read_ea_ex(ctx, ins, ea, sz, er, 0);
}

/* addr_ea - the address of a control/alterable EA (LEA/PEA/JMP/JSR). */
static uint32_t addr_ea(M68kStepContext *ctx, const M68KInstr *ins, int ea, ExtR *er) {
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

static int write_ea_ex(M68kStepContext *ctx, const M68KInstr *ins, int ea, M68KSize sz, ExtR *er, uint32_t val, int rmw) {
    int mode = (ea >> 3) & 7, reg = ea & 7, sb = szbytes(sz);
    if(reg==7 && sz==M68K_SIZE_B)sb=2;
    (void)ins;
    switch (mode) {
    case 0: store_dn(ctx, reg, val, sz); break;
    case 1: return 0;                         /* An is never an alterable destination */
    case 2: mem_write(ctx, sz, g_cpu.A[reg], val); break;
    case 3: mem_write(ctx, sz, g_cpu.A[reg], val); g_cpu.A[reg] += sb; break;
    case 4: if (!rmw) g_cpu.A[reg] -= sb; mem_write(ctx, sz, g_cpu.A[reg], val); break;
    case 5: { int16_t d16 = (int16_t)er_next(er);
              mem_write(ctx, sz, (uint32_t)(g_cpu.A[reg] + (int32_t)d16), val); break; }
    case 6: { uint16_t ext = er_next(er);
              int xreg = (ext >> 12) & 7; int8_t d8 = (int8_t)(ext & 0xFF);
              uint32_t xv = m68k_brief_index_value(ext, g_cpu.D[xreg], g_cpu.A[xreg]);
              mem_write(ctx, sz, (uint32_t)(g_cpu.A[reg] + xv + (int32_t)d8), val); break; }
    case 7:
        switch (reg) {
        case 0: { int16_t ext = (int16_t)er_next(er); mem_write(ctx, sz, (uint32_t)(int32_t)ext, val); break; }
        case 1: { uint32_t a = er_next_dword(er); mem_write(ctx, sz, a, val); break; }
        default: return 0; /* PC-relative/imm are not alterable destinations */
        }
        break;
    default: return 0;
    }
    return 1;
}
static int write_ea(M68kStepContext *ctx, const M68KInstr *ins, int ea, M68KSize sz, ExtR *er, uint32_t val) {
    return write_ea_ex(ctx, ins, ea, sz, er, val, 0);
}

/* =========================================================================
 * Flag helpers - exact ports of emit_flags_{logic,add,sub,cmp}.
 * (X=bit4 N=bit3 Z=bit2 V=bit1 C=bit0.)
 * ========================================================================= */
static void flags_logic(M68kStepContext *ctx, uint32_t v, M68KSize sz) {
    int bits = szbits(sz); uint32_t fv = v & szmask(sz);
    g_cpu.SR &= ~0x0Fu;
    if (!fv)                 g_cpu.SR |= M68KF_SR_Z;
    if (fv >> (bits - 1))    g_cpu.SR |= M68KF_SR_N;
}
static void flags_add(M68kStepContext *ctx, uint32_t a, uint32_t b, uint32_t r, M68KSize sz) {
    int bits = szbits(sz); uint32_t m = szmask(sz), sb = (uint32_t)(1u << (bits - 1));
    uint32_t fa = a & m, fb = b & m, fr = r & m;
    g_cpu.SR &= ~0x1Fu;
    if (!fr)                                   g_cpu.SR |= M68KF_SR_Z;
    if (fr >> (bits - 1))                      g_cpu.SR |= M68KF_SR_N;
    if ((uint64_t)fa + (uint64_t)fb > m)     { g_cpu.SR |= M68KF_SR_C; g_cpu.SR |= M68KF_SR_X; }
    if (!((fa ^ fb) & sb) && ((fa ^ fr) & sb)) g_cpu.SR |= M68KF_SR_V;
}
static void flags_sub(M68kStepContext *ctx, uint32_t a, uint32_t b, uint32_t r, M68KSize sz) {
    int bits = szbits(sz); uint32_t m = szmask(sz), sb = (uint32_t)(1u << (bits - 1));
    uint32_t fa = a & m, fb = b & m, fr = r & m;
    g_cpu.SR &= ~0x1Fu;
    if (!fr)                                   g_cpu.SR |= M68KF_SR_Z;
    if (fr >> (bits - 1))                      g_cpu.SR |= M68KF_SR_N;
    if (fb > fa)                             { g_cpu.SR |= M68KF_SR_C; g_cpu.SR |= M68KF_SR_X; }
    if (((fa ^ fb) & sb) && ((fa ^ fr) & sb))  g_cpu.SR |= M68KF_SR_V;
}
static void flags_cmp(M68kStepContext *ctx, uint32_t a, uint32_t b, uint32_t r, M68KSize sz) {
    int bits = szbits(sz); uint32_t m = szmask(sz), sb = (uint32_t)(1u << (bits - 1));
    uint32_t fa = a & m, fb = b & m, fr = r & m;
    g_cpu.SR &= ~0x0Fu;
    if (!fr)                                   g_cpu.SR |= M68KF_SR_Z;
    if (fr >> (bits - 1))                      g_cpu.SR |= M68KF_SR_N;
    if (fb > fa)                               g_cpu.SR |= M68KF_SR_C;
    if (((fa ^ fb) & sb) && ((fa ^ fr) & sb))  g_cpu.SR |= M68KF_SR_V;
}

/* Condition-code evaluator - mirrors bcc_cond_expr. */
static int eval_cond(M68kStepContext *ctx, int cc) {
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


static uint32_t sext(uint32_t v, M68KSize sz) {
    switch (sz) { case M68K_SIZE_B: return (uint32_t)(int32_t)(int8_t)v;
                  case M68K_SIZE_W: return (uint32_t)(int32_t)(int16_t)v;
                  default:          return v; }
}

static void push32(M68kStepContext *ctx, uint32_t value) {
    g_cpu.A[7] -= 4; m68k_write32(g_cpu.A[7], value);
}


static int do_shift(M68kStepContext *ctx, M68KMnemonic mn, int dreg, M68KSize sz,
                    int reg_count, int creg, int imm_count) {
    int bits = szbits(sz);
    uint32_t m = szmask(sz);
    int cnt = reg_count ? (int)(g_cpu.D[creg] & 63u) : imm_count;
    uint32_t sv = g_cpu.D[dreg] & m;
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
         * all-1. This also detects intermediate sign changes. */
        if (cnt > 0) {
            if (cnt >= bits) v = (sv != 0) ? 1u : 0u;
            else {
                uint32_t top = sv >> (bits - 1 - cnt);
                uint32_t allm = (uint32_t)((UINT64_C(1) << (cnt + 1)) - 1u);
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
         * value has gone all-sign), not 0 - the generated C approximates this
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
        /* rotate through X over a (bits+1)-wide field - use 64-bit so .L works */
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

    store_dn(ctx, dreg, res, sz);

    if (!touches_x) {                                          /* ROL/ROR: preserve X */
        g_cpu.SR &= ~0x0Fu;
        if (!res) g_cpu.SR |= M68KF_SR_Z;
        if ((res >> (bits - 1)) & 1u) g_cpu.SR |= M68KF_SR_N;
        if (c) g_cpu.SR |= M68KF_SR_C;
    } else if (reg_count && cnt == 0) { /* Preserve X; ROX copies X to C. */
        g_cpu.SR &= ~0x0Fu;
        if (!res) g_cpu.SR |= M68KF_SR_Z;
        if ((res >> (bits - 1)) & 1u) g_cpu.SR |= M68KF_SR_N;
        if ((mn == MN_ROXL || mn == MN_ROXR) && (g_cpu.SR & M68KF_SR_X)) g_cpu.SR |= M68KF_SR_C;
    } else {
        g_cpu.SR &= ~0x1Fu;
        if (!res) g_cpu.SR |= M68KF_SR_Z;
        if ((res >> (bits - 1)) & 1u) g_cpu.SR |= M68KF_SR_N;
        if (c) { g_cpu.SR |= M68KF_SR_C; g_cpu.SR |= M68KF_SR_X; }
        if (with_v && v) g_cpu.SR |= M68KF_SR_V;
    }
    return 0;
}


static void do_addx_subx(M68kStepContext *ctx, const M68KInstr *ins, M68KSize sz, int is_add) {
    uint16_t w0 = ins->words[0];
    int dst = (w0 >> 9) & 7, src = w0 & 7;
    int bits = szbits(sz), sb = szbytes(sz);
    uint32_t m = szmask(sz), sign = (uint32_t)(1u << (bits - 1));
    uint32_t a, b;
    if (ins->predec_mem_form) {
        int ay_dec = (src == 7 && sz == M68K_SIZE_B) ? 2 : sb;
        int ax_dec = (dst == 7 && sz == M68K_SIZE_B) ? 2 : sb;
        g_cpu.A[src] -= ay_dec; b = mem_read(ctx, sz, g_cpu.A[src]) & m;
        g_cpu.A[dst] -= ax_dec; a = mem_read(ctx, sz, g_cpu.A[dst]) & m;
    } else { a = g_cpu.D[dst] & m; b = g_cpu.D[src] & m; }
    uint32_t x = (g_cpu.SR >> 4) & 1u;
    uint64_t full = is_add ? ((uint64_t)a + b + x) : ((uint64_t)a - b - x);
    uint32_t r = (uint32_t)full & m;
    int zold = (g_cpu.SR >> 2) & 1;
    g_cpu.SR &= ~0x1Fu;
    if (!r && zold) g_cpu.SR |= M68KF_SR_Z;
    if (r >> (bits - 1)) g_cpu.SR |= M68KF_SR_N;
    int carry = is_add ? ((full >> bits) & 1u) : ((full >> 63) & 1u);
    if (carry) { g_cpu.SR |= M68KF_SR_C; g_cpu.SR |= M68KF_SR_X; }
    int v = is_add ? (!((a ^ b) & sign) && ((a ^ r) & sign))
                   : ( ((a ^ b) & sign) && ((a ^ r) & sign));
    if (v) g_cpu.SR |= M68KF_SR_V;
    if (ins->predec_mem_form) mem_write(ctx, sz, g_cpu.A[dst], r);
    else                      store_dn(ctx, dst, r, sz);
}

/* ABCD/SBCD/NBCD - packed-BCD, port of code_generator.c. op: 0=ABCD 1=SBCD
 * 2=NBCD. Z is sticky; X=C. Operates byte-wide. */
static void do_bcd(M68kStepContext *ctx, const M68KInstr *ins, int op) {
    int x = (g_cpu.SR >> 4) & 1;
    uint8_t a, b = 0, r; int carry; uint32_t a_addr = 0; int to_mem = 0;
    if (op == 2) {                               /* NBCD <ea> (RMW) */
        ExtR er; er_init(&er, ins);
        if (((ins->src_ea >> 3) & 7) <= 1) {     /* Dn/An direct handled via read/store */
            a = (uint8_t)(read_ea_ex(ctx, ins, ins->src_ea, M68K_SIZE_B, &er, 1) & 0xFFu);
        } else { a = (uint8_t)(read_ea_ex(ctx, ins, ins->src_ea, M68K_SIZE_B, &er, 1) & 0xFFu); }
        int lo = -(int)(a & 0xF) - x;
        int adj_lo = (lo < 0) ? 6 : 0;
        int hi = -(int)(a >> 4) - (lo < 0 ? 1 : 0);
        int adj_hi = (hi < 0) ? 6 : 0;
        int full = (hi & 0xFF) * 16 - adj_hi * 16 + ((lo - adj_lo) & 0xF);
        r = (uint8_t)full; carry = (hi < 0);
        ExtR er2; er_init(&er2, ins);
        write_ea_ex(ctx, ins, ins->src_ea, M68K_SIZE_B, &er2, r, 1);
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
        g_cpu.SR &= ~M68KF_SR_V;
        if (r) g_cpu.SR &= ~M68KF_SR_Z; else if (zold) g_cpu.SR |= M68KF_SR_Z;
        g_cpu.SR &= ~(M68KF_SR_C | M68KF_SR_X | M68KF_SR_N);
        if (r & 0x80u) g_cpu.SR |= M68KF_SR_N;
        if (carry) { g_cpu.SR |= M68KF_SR_C; g_cpu.SR |= M68KF_SR_X; }
    }
}

/* MOVEM - port of code_generator.c MN_MOVEM. base address resolved from a
 * fresh ExtReader at words[2]; predecrement uses the reversed register mask;
 * mem->reg sign-extends .W to 32 for both An and Dn. */
static void do_movem(M68kStepContext *ctx, const M68KInstr *ins, M68KSize sz) {
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


static int floor_semantics(M68kStepContext *ctx, const M68KInstr *ins, uint32_t *next_pc)
{
    /* These instructions belong to the explicit-PC executor. Decline before
     * changing next_pc, registers, or the bus. */
    switch (ins->mnemonic) {
    case MN_JSR: case MN_BSR: case MN_JMP: case MN_BRA: case MN_Bcc: case MN_DBcc:
    case MN_RTS: case MN_RTE: case MN_RTR: case MN_STOP: case MN_RESET:
    case MN_TRAP: case MN_TRAPV: case MN_ILLEGAL: case MN_CHK:
    case MN_MOVE_SR: case MN_MOVE_CCR: case MN_MOVE_USP: case MN_MOVEC:
    case MN_ORI_TO_SR: case MN_ORI_TO_CCR:
    case MN_ANDI_TO_SR: case MN_ANDI_TO_CCR:
    case MN_EORI_TO_SR: case MN_EORI_TO_CCR:
    case MN_DIVU: case MN_DIVS:
        return 0;
    default: break;
    }

    ExtR er; er_init(&er, ins);
    uint32_t fall = ins->addr + ins->byte_length;
    *next_pc = fall;
    M68KSize sz = ins->size;
    int dir = (ins->words[0] >> 8) & 1;
    int cc = (ins->words[0] >> 8) & 0xF;
    switch (ins->mnemonic) {
    case MN_NOP:
        return 1;

    case MN_MOVE: {
        /* The upstream decoder/validator rejects illegal MOVE-to-An forms. */
        uint32_t v = read_ea(ctx, ins, ins->src_ea, sz, &er);
        write_ea(ctx, ins, ins->dst_ea, sz, &er, v);
        flags_logic(ctx, v, sz);
        return 1;
    }
    case MN_MOVEA: {
        uint32_t v = read_ea(ctx, ins, ins->src_ea, sz, &er);
        g_cpu.A[ins->reg] = sext(v, sz);   /* .W sign-extends to 32; no flags */
        return 1;
    }
    case MN_MOVEQ:
        g_cpu.D[ins->reg] = (uint32_t)(int32_t)(int8_t)ins->imm32;
        flags_logic(ctx, g_cpu.D[ins->reg], M68K_SIZE_L);
        return 1;

    case MN_LEA:
        g_cpu.A[ins->reg] = addr_ea(ctx, ins, ins->src_ea, &er);
        return 1;
    case MN_PEA: {
        uint32_t a = addr_ea(ctx, ins, ins->src_ea, &er);
        push32(ctx, a);
        return 1;
    }

    case MN_TST: {
        uint32_t v = read_ea(ctx, ins, ins->src_ea, sz, &er);
        flags_logic(ctx, v, sz);
        return 1;
    }
    case MN_CLR: {
        /* MC68000 performs a discarded operand read before the zero write.
         * Keep this as one RMW EA transaction: postincrement happens at the
         * write, predecrement happens at the read, and extension words are
         * consumed once by each phase from the same instruction stream. */
        (void)read_ea_ex(ctx, ins, ins->src_ea, sz, &er, 1);
        ExtR write_er; er_init(&write_er, ins);
        write_ea_ex(ctx, ins, ins->src_ea, sz, &write_er, 0, 1);
        g_cpu.SR = (uint16_t)((g_cpu.SR & ~0x0Fu) | M68KF_SR_Z);
        return 1;
    }

    case MN_EXT:
        if (sz == M68K_SIZE_W) { uint32_t v = (uint32_t)(int32_t)(int8_t)g_cpu.D[ins->reg];
                                 store_dn(ctx, ins->reg, v, M68K_SIZE_W); flags_logic(ctx, v, M68K_SIZE_W); }
        else                   { uint32_t v = (uint32_t)(int32_t)(int16_t)g_cpu.D[ins->reg];
                                 g_cpu.D[ins->reg] = v;             flags_logic(ctx, v, M68K_SIZE_L); }
        return 1;
    case MN_SWAP: {
        uint32_t v = g_cpu.D[ins->reg];
        v = (v >> 16) | (v << 16);
        g_cpu.D[ins->reg] = v;
        flags_logic(ctx, v, M68K_SIZE_L);
        return 1;
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
        return 1;
    }

    /* ---- ADD/SUB (arith, reg<->EA, both directions) ---- */
    case MN_ADD: case MN_SUB: {
        int isadd = (ins->mnemonic == MN_ADD);
        int dreg = ins->reg; uint32_t m = szmask(sz);
        if (dir == 0) {
            uint32_t b = read_ea(ctx, ins, ins->src_ea, sz, &er) & m;
            uint32_t a = g_cpu.D[dreg] & m;
            uint32_t r = isadd ? (a + b) : (a - b);
            if (isadd) flags_add(ctx, a, b, r, sz); else flags_sub(ctx, a, b, r, sz);
            store_dn(ctx, dreg, r, sz);
        } else {
            ExtR save = er;
            uint32_t a = read_ea_ex(ctx, ins, ins->src_ea, sz, &er, 1) & m;
            uint32_t b = g_cpu.D[dreg] & m;
            uint32_t r = isadd ? (a + b) : (a - b);
            if (isadd) flags_add(ctx, a, b, r, sz); else flags_sub(ctx, a, b, r, sz);
            er = save;
            write_ea_ex(ctx, ins, ins->src_ea, sz, &er, r, 1);
        }
        return 1;
    }
    case MN_ADDA: case MN_SUBA: {
        uint32_t v = sext(read_ea(ctx, ins, ins->src_ea, sz, &er), sz);
        if (ins->mnemonic == MN_ADDA) g_cpu.A[ins->reg] += v;
        else                          g_cpu.A[ins->reg] -= v;
        return 1;   /* address ops set no flags */
    }
    case MN_CMP: {
        uint32_t m = szmask(sz);
        uint32_t b = read_ea(ctx, ins, ins->src_ea, sz, &er) & m;
        uint32_t a = g_cpu.D[ins->reg] & m;
        flags_cmp(ctx, a, b, a - b, sz);
        return 1;
    }
    case MN_CMPA: {
        uint32_t b = sext(read_ea(ctx, ins, ins->src_ea, sz, &er), sz);
        uint32_t a = g_cpu.A[ins->reg];
        flags_cmp(ctx, a, b, a - b, M68K_SIZE_L);
        return 1;
    }
    case MN_CMPM: {
        /* CMPM (Ay)+,(Ax)+ : Ax is dest (ins->reg), Ay is src (src_ea&7). */
        int ay = ins->src_ea & 7, ax = ins->reg, sb = szbytes(sz); uint32_t m = szmask(sz);
        uint32_t b = mem_read(ctx, sz, g_cpu.A[ay]) & m; g_cpu.A[ay] += ay==7 && sz==M68K_SIZE_B?2:sb;
        uint32_t a = mem_read(ctx, sz, g_cpu.A[ax]) & m; g_cpu.A[ax] += ax==7 && sz==M68K_SIZE_B?2:sb;
        flags_cmp(ctx, a, b, a - b, sz);
        return 1;
    }

    /* ---- AND/OR/EOR (logic, reg<->EA) ---- */
    case MN_AND: case MN_OR: {
        int isand = (ins->mnemonic == MN_AND);
        int dreg = ins->reg; uint32_t m = szmask(sz);
        if (dir == 0) {
            uint32_t b = read_ea(ctx, ins, ins->src_ea, sz, &er) & m;
            uint32_t a = g_cpu.D[dreg] & m;
            uint32_t r = isand ? (a & b) : (a | b);
            store_dn(ctx, dreg, r, sz); flags_logic(ctx, r, sz);
        } else {
            ExtR save = er;
            uint32_t a = read_ea_ex(ctx, ins, ins->src_ea, sz, &er, 1) & m;
            uint32_t b = g_cpu.D[dreg] & m;
            uint32_t r = isand ? (a & b) : (a | b);
            flags_logic(ctx, r, sz);
            er = save;
            write_ea_ex(ctx, ins, ins->src_ea, sz, &er, r, 1);
        }
        return 1;
    }
    case MN_EOR: {   /* EOR has only the Dn->EA (dir=1 style) form */
        int dreg = ins->reg; uint32_t m = szmask(sz);
        ExtR save = er;
        uint32_t a = read_ea_ex(ctx, ins, ins->src_ea, sz, &er, 1) & m;
        uint32_t r = (a ^ (g_cpu.D[dreg] & m));
        flags_logic(ctx, r, sz);
        er = save;
        write_ea_ex(ctx, ins, ins->src_ea, sz, &er, r, 1);
        return 1;
    }

    /* ---- ADDQ/SUBQ ---- */
    case MN_ADDQ: case MN_SUBQ: {
        int isadd = (ins->mnemonic == MN_ADDQ);
        uint32_t q = ins->imm32 ? ins->imm32 : 8u;
        int mode = (ins->src_ea >> 3) & 7;
        if (mode == 1) {                 /* to An: full 32-bit, no flags */
            int r = ins->src_ea & 7;
            g_cpu.A[r] += isadd ? (int32_t)q : -(int32_t)q;
            return 1;
        }
        uint32_t m = szmask(sz);
        ExtR save = er;
        uint32_t a = read_ea_ex(ctx, ins, ins->src_ea, sz, &er, 1) & m;
        uint32_t r = isadd ? (a + q) : (a - q);
        if (isadd) flags_add(ctx, a, q, r, sz); else flags_sub(ctx, a, q, r, sz);
        er = save;
        write_ea_ex(ctx, ins, ins->src_ea, sz, &er, r, 1);
        return 1;
    }

    /* ---- immediate-RMW (ADDI/SUBI/CMPI/ANDI/ORI/EORI) ---- */
    case MN_ADDI: case MN_SUBI: case MN_CMPI:
    case MN_ANDI: case MN_ORI:  case MN_EORI: {
        uint32_t imm = er_next_imm(&er, sz);   /* immediate first in the stream */
        uint32_t m = szmask(sz);
        if (ins->mnemonic == MN_CMPI) {
            /* CMPI is read-only: for (An)+ it MUST post-increment (rmw=0). */
            uint32_t a = read_ea(ctx, ins, ins->src_ea, sz, &er) & m;
            flags_cmp(ctx, a, imm & m, a - (imm & m), sz);
            return 1;
        }
        ExtR after_imm = er;                   /* EA ext words start here */
        uint32_t a = read_ea_ex(ctx, ins, ins->src_ea, sz, &er, 1) & m;
        uint32_t r, im = imm & m;
        int arith = 0;
        switch (ins->mnemonic) {
        case MN_ADDI: r = a + im;  arith = 1; break;
        case MN_SUBI: r = a - im;  arith = 2; break;
        case MN_ANDI: r = a & im;            break;
        case MN_ORI:  r = a | im;            break;
        default:      r = a ^ im;            break;  /* EORI */
        }
        if (arith == 1)      flags_add(ctx, a, im, r, sz);
        else if (arith == 2) flags_sub(ctx, a, im, r, sz);
        else                 flags_logic(ctx, r, sz);
        er = after_imm;
        write_ea_ex(ctx, ins, ins->src_ea, sz, &er, r, 1);
        return 1;
    }

    /* ---- shifts / rotates (register Dn target) ---- */
    case MN_LSL: case MN_LSR: case MN_ASL: case MN_ASR:
    case MN_ROL: case MN_ROR: case MN_ROXL: case MN_ROXR: {
        if (ins->mem_shift) {            /* 1-bit shift of a word in memory */
            ExtR save = er;
            uint32_t sv = read_ea_ex(ctx, ins, ins->src_ea, M68K_SIZE_W, &er, 1) & 0xFFFFu;
            /* reuse do_shift by faking a Dn: load into a scratch via D-less path */
            uint32_t res, c = 0, v = 0; int with_v = 0, touches_x = 1;
            switch (ins->mnemonic) {
            case MN_LSL: res = (sv << 1) & 0xFFFFu; c = (sv >> 15) & 1u; break;
            case MN_LSR: res = sv >> 1;             c = sv & 1u;        break;
            case MN_ASL: res = (sv << 1) & 0xFFFFu; c = (sv >> 15) & 1u; with_v = 1;
                         if ((sv & 0x8000u) != (res & 0x8000u)) v = 1;
                         break;
            case MN_ASR: res = (uint32_t)(((int16_t)sv) >> 1) & 0xFFFFu; c = sv & 1u; with_v = 1; break;
            case MN_ROL: touches_x = 0; res = ((sv << 1) | (sv >> 15)) & 0xFFFFu; c = (sv >> 15) & 1u; break;
            case MN_ROR: touches_x = 0; res = ((sv >> 1) | (sv << 15)) & 0xFFFFu; c = sv & 1u; break;
            case MN_ROXL: { uint32_t x=(g_cpu.SR>>4)&1u; res=((sv<<1)|x)&0xFFFFu; c=(sv>>15)&1u; break; }
            default:      { uint32_t x=(g_cpu.SR>>4)&1u; res=((sv>>1)|(x<<15))&0xFFFFu; c=sv&1u; break; } /* ROXR */
            }
            er = save;
            write_ea_ex(ctx, ins, ins->src_ea, M68K_SIZE_W, &er, res, 1);
            if (!touches_x) { g_cpu.SR &= ~0x0Fu; if(!res)g_cpu.SR|=M68KF_SR_Z; if(res&0x8000u)g_cpu.SR|=M68KF_SR_N; if(c)g_cpu.SR|=M68KF_SR_C; }
            else { g_cpu.SR &= ~0x1Fu; if(!res)g_cpu.SR|=M68KF_SR_Z; if(res&0x8000u)g_cpu.SR|=M68KF_SR_N;
                   if(c){g_cpu.SR|=M68KF_SR_C;g_cpu.SR|=M68KF_SR_X;} if(with_v&&v)g_cpu.SR|=M68KF_SR_V; }
            return 1;
        }
        int reg_count = (ins->src_ea >= 0);
        if (do_shift(ctx, ins->mnemonic, ins->reg, sz, reg_count, ins->src_ea, (int)(ins->imm32 & 63u)) != 0) {
            return 0;
        }
        return 1;
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
            uint32_t src = read_ea(ctx, ins, ea, bsz, &er) & szmask(bsz);
            g_cpu.SR = (uint16_t)((g_cpu.SR & ~M68KF_SR_Z) | ((!(src & (1u << bn))) ? M68KF_SR_Z : 0u));
            return 1;
        }
        ExtR save = er;                                  /* BCHG/BCLR/BSET: RMW */
        uint32_t src = read_ea_ex(ctx, ins, ea, bsz, &er, 1) & szmask(bsz);
        g_cpu.SR = (uint16_t)((g_cpu.SR & ~M68KF_SR_Z) | ((!(src & (1u << bn))) ? M68KF_SR_Z : 0u));
        uint32_t res = (ins->mnemonic == MN_BCHG) ? (src ^ (1u << bn))
                     : (ins->mnemonic == MN_BCLR) ? (src & ~(1u << bn))
                                                  : (src | (1u << bn));
        er = save;
        write_ea_ex(ctx, ins, ea, bsz, &er, res, 1);
        return 1;
    }

    /* ---- NEG / NEGX / NOT ---- */
    case MN_NEG: case MN_NEGX: case MN_NOT: {
        uint32_t m = szmask(sz);
        ExtR save = er;
        uint32_t a = read_ea_ex(ctx, ins, ins->src_ea, sz, &er, 1) & m;
        uint32_t r;
        if (ins->mnemonic == MN_NOT) { r = (~a) & m; flags_logic(ctx, r, sz); }
        else {
            uint32_t x = (ins->mnemonic == MN_NEGX) ? ((g_cpu.SR >> 4) & 1u) : 0u;
            r = (0u - a - x) & m;
            flags_sub(ctx, 0u, a, r, sz);
        }
        er = save;
        write_ea_ex(ctx, ins, ins->src_ea, sz, &er, r, 1);
        return 1;
    }

    /* ---- TAS ---- */
    case MN_TAS: {
        ExtR save = er;
        uint32_t v = read_ea_ex(ctx, ins, ins->src_ea, M68K_SIZE_B, &er, 1) & 0xFFu;
        g_cpu.SR &= ~0x0Fu;
        if (!v) g_cpu.SR |= M68KF_SR_Z;
        if (v >> 7) g_cpu.SR |= M68KF_SR_N;
        er = save;
        write_ea_ex(ctx, ins, ins->src_ea, M68K_SIZE_B, &er, v | 0x80u, 1);
        return 1;
    }

    /* ---- MUL (.W source, .L Dn result) ---- */
    case MN_MULU: {
        uint32_t source = read_ea(ctx, ins, ins->src_ea, M68K_SIZE_W, &er) & 0xFFFFu;
        g_cpu.D[ins->reg] = (uint32_t)(uint16_t)g_cpu.D[ins->reg] * source;
        flags_logic(ctx, g_cpu.D[ins->reg], M68K_SIZE_L);
        return 1;
    }
    case MN_MULS: {
        int32_t source = (int32_t)(int16_t)(read_ea(ctx, ins, ins->src_ea,
                                                   M68K_SIZE_W, &er) & 0xFFFFu);
        g_cpu.D[ins->reg] = (uint32_t)((int32_t)(int16_t)g_cpu.D[ins->reg] * source);
        flags_logic(ctx, g_cpu.D[ins->reg], M68K_SIZE_L);
        return 1;
    }

    /* ---- MOVEM ---- */
    case MN_MOVEM:
        do_movem(ctx, ins, sz);
        return 1;

    /* ---- LINK / UNLK ---- */
    case MN_LINK: {
        int ar = ins->reg; int16_t disp = (int16_t)ins->words[1];
        g_cpu.A[7] -= 4; m68k_write32(g_cpu.A[7], g_cpu.A[ar]);
        g_cpu.A[ar] = g_cpu.A[7];
        g_cpu.A[7] += (int32_t)disp;
        return 1;
    }
    case MN_UNLK: {
        int ar = ins->reg;
        g_cpu.A[7] = g_cpu.A[ar];
        g_cpu.A[ar] = m68k_read32(g_cpu.A[7]);
        g_cpu.A[7] += 4;
        return 1;
    }

    /* ---- Scc ---- */
    case MN_Scc: {
        uint8_t val = eval_cond(ctx, cc) ? 0xFFu : 0x00u;
        write_ea(ctx, ins, ins->src_ea, M68K_SIZE_B, &er, val);
        return 1;
    }

    /* ---- ADDX / SUBX ---- */
    case MN_ADDX: do_addx_subx(ctx, ins, sz, 1); return 1;
    case MN_SUBX: do_addx_subx(ctx, ins, sz, 0); return 1;

    /* ---- packed BCD ---- */
    case MN_ABCD: do_bcd(ctx, ins, 0); return 1;
    case MN_SBCD: do_bcd(ctx, ins, 1); return 1;
    case MN_NBCD: do_bcd(ctx, ins, 2); return 1;

    default:
        return 0;
    }
}
