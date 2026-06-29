/*
 * m68k_interp_diff.c — same-state differential: Tier-3 interpreter vs clown68000.
 *
 * Links ONLY the interpreter (m68k_interp.c) + the recompiler decoder
 * (m68k_decoder.c, rom_parser.c) + clown68000 (the oracle) + a flat memory
 * model. No SDL, no full runner — a fast parity gate, the Genesis analogue of
 * psxrecomp's sljit_offline_diff.
 *
 * For each function entry, for each fuzz seed: set identical input CPU+RAM
 * state, run clown68000 to the sentinel return (capturing a per-instruction
 * register trace + final RAM), restore, run the interpreter likewise, and
 * compare. Any post-instruction register divergence or final-RAM mismatch is
 * a codegen-parity bug. clown's group-0 fault path is caught via setjmp so a
 * fuzzed odd-address input is SKIPPED, never miscounted as a divergence.
 *
 * Usage:  m68k_interp_diff <rom.bin> [entries.txt] [seeds_per_entry]
 *         entries.txt: one hex address per line (e.g. 0x000B64). Lines that
 *         aren't a hex address are ignored, so a raw dispatch table works.
 *
 * Build:  see runner/tests/build_interp_diff.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <setjmp.h>

#include "genesis_runtime.h"   /* M68KState, ROM_SIZE, RAM_BASE, SR_* */
#include "m68k_interp.h"
#include "m68k_decoder.h"      /* to identify BCD ops (undefined V/N flags)   */
#include "rom_parser.h"
#include "clown68000.h"

/* ---- runtime ABI the interpreter links against ---- */
M68KState g_cpu;
uint8_t   g_rom[0x400000];
uint8_t   g_ram[0x010000];
int       g_ws_margin = 0;

static uint8_t mem8(uint32_t a) {
    a &= 0xFFFFFFu;
    if (a < ROM_SIZE)      return g_rom[a];
    if (a >= RAM_BASE)     return g_ram[a - RAM_BASE];
    return 0xFF;                       /* VDP/IO/Z80 scratch: both engines see this */
}
static void mem8w(uint32_t a, uint8_t v) {
    a &= 0xFFFFFFu;
    if (a >= RAM_BASE) g_ram[a - RAM_BASE] = v;   /* ROM + hardware writes ignored */
}
uint8_t  m68k_read8 (uint32_t a) { return mem8(a); }
uint16_t m68k_read16(uint32_t a) { return (uint16_t)((mem8(a) << 8) | mem8(a + 1)); }
uint32_t m68k_read32(uint32_t a) { return ((uint32_t)m68k_read16(a) << 16) | m68k_read16(a + 2); }
void m68k_write8 (uint32_t a, uint8_t  v) { mem8w(a, v); }
void m68k_write16(uint32_t a, uint16_t v) { mem8w(a, (uint8_t)(v >> 8)); mem8w(a + 1, (uint8_t)v); }
void m68k_write32(uint32_t a, uint32_t v) { m68k_write16(a, (uint16_t)(v >> 16)); m68k_write16(a + 2, (uint16_t)v); }

/* ---- clown68000 bus callbacks over the SAME flat memory ----
 * clown passes a WORD address (byte_addr >> 1, masked to 23 bits) with
 * do_high_byte/do_low_byte selecting the even/odd byte of that word
 * (clown68000.c:175,186). Convert back to a byte address: high byte at
 * word*2, low byte at word*2+1. */
static cc_u16f clown_read(const void *u, cc_u32f word_addr, cc_bool dh, cc_bool dl, cc_u32f cyc) {
    (void)u; (void)cyc;
    uint32_t a = ((uint32_t)word_addr << 1) & 0xFFFFFFu;
    cc_u16f hi = dh ? mem8(a)     : 0;
    cc_u16f lo = dl ? mem8(a + 1) : 0;
    return (cc_u16f)((hi << 8) | lo);
}
static void clown_write(const void *u, cc_u32f word_addr, cc_bool dh, cc_bool dl, cc_u32f cyc, cc_u16f value) {
    (void)u; (void)cyc;
    uint32_t a = ((uint32_t)word_addr << 1) & 0xFFFFFFu;
    if (dh) mem8w(a,     (uint8_t)(value >> 8));
    if (dl) mem8w(a + 1, (uint8_t)value);
}

/* clown's group-0/error path -> recover instead of aborting the whole run. */
static jmp_buf s_fault;
static void clown_error(void *u, const char *fmt, va_list ap) { (void)u; (void)fmt; (void)ap; longjmp(s_fault, 1); }

/* The recompiler's clown68000.c carries two per-instruction hooks used by the
 * oracle build (a cycle stamp + a pre-insn callback). Inert here — define them
 * so the standalone harness links. */
cc_u32f g_hybrid_cycle_counter = 0;
void (*g_hybrid_pre_insn_fn)(cc_u32l pc) = NULL;

/* ---- config ---- */
#define SENTINEL 0x00FFFFFEu      /* return address that ends a function run     */
#define STACKTOP 0x00FFF000u      /* SP for the fuzzed call (even, mid-RAM)       */
/* Bounded lockstep window: run each function for at most STEP_CAP instructions
 * (or until it returns to the sentinel), comparing the per-instruction trace.
 * A non-terminating function (boot code, a hardware spin) simply validates its
 * first STEP_CAP instructions instead of being skipped — both engines execute
 * the identical instruction stream over the same flat memory, so they stay in
 * lockstep unless the interpreter is wrong. */
#define STEP_CAP 8000

typedef struct { uint32_t pc, D[8], A[8]; uint16_t sr; } StepRec;

static StepRec s_trace_c[STEP_CAP];
static StepRec s_trace_i[STEP_CAP];

/* Exception handler addresses (from the ROM vector table). If clown vectors
 * into one of these mid-run it took a CPU exception — almost always an ADDRESS
 * ERROR from a fuzzed odd-address access. The generated C (and this interpreter)
 * deliberately don't model address/bus errors, so such a case is a fuzzing
 * artifact, not a parity divergence: skip it. */
static uint32_t s_exc[64]; static int s_nexc = 0;
static int is_exc_handler(uint32_t pc) {
    for (int i = 0; i < s_nexc; i++) if (s_exc[i] == pc) return 1;
    return 0;
}

static uint32_t xs_state;
static uint32_t xs(void) { uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; xs_state = x; return x; }

/* Seed input regs: D fully random; A point into an even RAM scratch window so
 * pointer dereferences land in RAM (and stay even -> no spurious address fault). */
static void seed_input(uint32_t entry, uint32_t seed) {
    xs_state = (entry * 2654435761u) ^ (seed + 0x9E3779B9u);
    if (!xs_state) xs_state = 1;
    memset(&g_cpu, 0, sizeof(g_cpu));
    for (int i = 0; i < 8; i++) g_cpu.D[i] = xs();
    for (int i = 0; i < 7; i++) g_cpu.A[i] = 0xFF8000u + ((xs() & 0x3FF) << 1); /* even, RAM */
    g_cpu.A[7] = STACKTOP;
    g_cpu.SR  = 0x2700;             /* supervisor, ints masked */
    g_cpu.PC  = entry;
    /* Zero RAM. Register inputs are fuzzed (above) for operand diversity, but
     * RAM stays zero so that state bytes a function reads to index a jump table
     * stay in-range — random RAM drives those indices out of bounds, sending a
     * computed JMP onto non-code bytes (a fuzz artifact, not an opcode bug). */
    memset(g_ram, 0, RAM_SIZE);
    /* install the sentinel return address at SP */
    m68k_write32(STACKTOP - 4, SENTINEL);
    g_cpu.A[7] = STACKTOP - 4;
}

static void record(StepRec *r) {
    r->pc = g_cpu.PC & 0xFFFFFFu;
    for (int i = 0; i < 8; i++) { r->D[i] = g_cpu.D[i]; r->A[i] = g_cpu.A[i]; }
    r->sr = g_cpu.SR;
}

/* Run clown from g_cpu for at most STEP_CAP instructions or until PC==SENTINEL.
 * Records each pre-instruction state into trace. Returns recorded step count,
 * or -1 if clown faulted (group-0 exception -> skip this fuzz case).
 * *ended==1 iff it returned to the sentinel (terminated) within the window. */
static int run_clown(StepRec *trace, int *ended, uint8_t *ram_out) {
    Clown68000_State cs;
    Clown68000_ReadWriteCallbacks cbs;
    cbs.read_callback = clown_read; cbs.write_callback = clown_write; cbs.user_data = NULL;

    memset(&cs, 0, sizeof(cs));
    for (int i = 0; i < 8; i++) { cs.data_registers[i] = g_cpu.D[i]; cs.address_registers[i] = g_cpu.A[i]; }
    cs.supervisor_stack_pointer = g_cpu.A[7];
    cs.user_stack_pointer = g_cpu.A[7];
    cs.program_counter = g_cpu.PC;
    cs.status_register = g_cpu.SR;

    *ended = 0;
    if (setjmp(s_fault)) return -1;

    int n = 0;
    while (n < STEP_CAP) {
        uint32_t pc = cs.program_counter & 0xFFFFFFu;
        if (pc == SENTINEL) { *ended = 1; break; }
        if (n > 0 && is_exc_handler(pc)) return -1;   /* clown took a CPU exception -> skip */
        StepRec *r = &trace[n];
        r->pc = cs.program_counter & 0xFFFFFFu;
        for (int i = 0; i < 8; i++) { r->D[i] = cs.data_registers[i]; r->A[i] = cs.address_registers[i]; }
        r->sr = (uint16_t)cs.status_register;
        /* Force exactly ONE instruction per call: clown's DoCycles runs
         * "instructions that fit in the budget", and leftover_cycles from the
         * previous (>=4-cycle) instruction would otherwise make a 1-cycle
         * budget run ZERO instructions on alternate calls. Zeroing it makes
         * each call retire exactly one instruction (timing-only field; does
         * not affect architectural state). */
        cs.leftover_cycles = 0;
        Clown68000_DoCycles(&cs, &cbs, 1);
        n++;
    }
    memcpy(ram_out, g_ram, RAM_SIZE);
    return n;
}

/* Run interpreter from g_cpu for at most STEP_CAP instructions or until
 * PC==SENTINEL. Same convention; returns -3 on M68KI_HALT_UNIMPL/BADADDR. */
static int run_interp(StepRec *trace, int *ended, uint8_t *ram_out) {
    *ended = 0;
    int n = 0;
    while (n < STEP_CAP) {
        if ((g_cpu.PC & 0xFFFFFFu) == SENTINEL) { *ended = 1; break; }
        record(&trace[n]);
        M68kiStatus st = m68k_interp_step();
        if (st == M68KI_HALT_UNIMPL || st == M68KI_HALT_BADADDR) return -3;
        n++;
    }
    memcpy(ram_out, g_ram, RAM_SIZE);
    return n;
}

/* BCD ops (ABCD/SBCD/NBCD) leave N and V OFFICIALLY UNDEFINED. The generated C
 * (and this interpreter, which mirrors it) use a deterministic model; clown
 * reproduces real-silicon undefined values. A divergence that is ONLY in N/V
 * right after a BCD instruction is therefore an expected gen-C-vs-silicon delta
 * on undefined flags, not an interpreter bug — recognise and exclude it. */
static GenesisRom h_rom;
static int is_bcd_at(uint32_t pc) {
    M68KInstr ins;
    if (pc >= ROM_SIZE || !m68k_decode(&h_rom, pc, &ins)) return 0;
    return ins.mnemonic == MN_ABCD || ins.mnemonic == MN_SBCD || ins.mnemonic == MN_NBCD;
}

static int rec_eq(const StepRec *a, const StepRec *b) {
    if (a->pc != b->pc) return 0;
    for (int i = 0; i < 8; i++) if (a->D[i] != b->D[i]) return 0;
    for (int i = 0; i < 7; i++) if (a->A[i] != b->A[i]) return 0;   /* skip A7 */
    if ((a->sr & 0x1Fu) != (b->sr & 0x1Fu)) return 0;               /* CCR only */
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <rom.bin> [entries.txt] [seeds]\n", argv[0]); return 2; }

    FILE *rf = fopen(argv[1], "rb");
    if (!rf) { fprintf(stderr, "cannot open ROM %s\n", argv[1]); return 2; }
    size_t romlen = fread(g_rom, 1, sizeof(g_rom), rf); fclose(rf);
    fprintf(stderr, "[diff] ROM %s: %zu bytes\n", argv[1], romlen);
    h_rom.rom_data = g_rom; h_rom.rom_size = ROM_SIZE;

    Clown68000_SetErrorCallback(clown_error, NULL);

    /* collect exception-handler addresses from the ROM vector table (vectors
     * 2..47: bus/address/illegal/div0/CHK/TRAPV/priv/trace/A-line/F-line +
     * autovectors + TRAP #0..15) so we can skip clown fault excursions. */
    for (int v = 2; v < 48 && s_nexc < 64; v++) {
        uint32_t h = m68k_read32((uint32_t)v * 4) & 0xFFFFFFu;
        if (h && h < ROM_SIZE) {
            int dup = 0; for (int j = 0; j < s_nexc; j++) if (s_exc[j] == h) dup = 1;
            if (!dup) s_exc[s_nexc++] = h;
        }
    }

    int seeds = (argc >= 4) ? atoi(argv[3]) : 8;

    /* gather entries */
    uint32_t *entries = NULL; int nent = 0, cap = 0;
    if (argc >= 3) {
        FILE *ef = fopen(argv[2], "r");
        if (!ef) { fprintf(stderr, "cannot open entries %s\n", argv[2]); return 2; }
        char line[256];
        while (fgets(line, sizeof(line), ef)) {
            char *p = strstr(line, "0x"); if (!p) p = strstr(line, "0X");
            unsigned long a;
            if (p && sscanf(p, "%lx", &a) == 1) {
                if (nent == cap) { cap = cap ? cap * 2 : 256; entries = realloc(entries, cap * sizeof(uint32_t)); }
                entries[nent++] = (uint32_t)a & 0xFFFFFFu;
            }
        }
        fclose(ef);
    }
    if (nent == 0) { fprintf(stderr, "[diff] no entries given\n"); return 2; }
    fprintf(stderr, "[diff] %d entries x %d seeds\n", nent, seeds);

    static uint8_t ram_c[RAM_SIZE], ram_i[RAM_SIZE], ram_in[RAM_SIZE];
    int pass = 0, diverge = 0, skip_fault = 0, unimpl = 0, terminated = 0, bcd_undef = 0;
    uint16_t unimpl_op = 0; uint32_t unimpl_pc = 0;

    for (int e = 0; e < nent; e++) {
        for (int s = 0; s < seeds; s++) {
            seed_input(entries[e], (uint32_t)s);
            memcpy(ram_in, g_ram, RAM_SIZE);
            M68KState cpu_in = g_cpu;

            int ec, ei;
            int nc = run_clown(s_trace_c, &ec, ram_c);
            if (nc == -1) { skip_fault++; continue; }   /* clown faulted -> skip */

            /* restore identical input for the interpreter */
            g_cpu = cpu_in; memcpy(g_ram, ram_in, RAM_SIZE);
            int ni = run_interp(s_trace_i, &ei, ram_i);
            if (ni == -3) {
                unimpl++; unimpl_op = g_m68ki_bad_op; unimpl_pc = g_m68ki_bad_pc;
                fprintf(stderr, "[UNIMPL] entry $%06X seed %d: opcode $%04X at $%06X\n",
                        entries[e], s, g_m68ki_bad_op, g_m68ki_bad_pc);
                continue;
            }

            int ok = 1;
            int lim = nc < ni ? nc : ni;
            if (nc != ni) ok = 0;                       /* control-flow divergence */
            int firstbad = -1;
            for (int k = 0; k < lim; k++) {
                if (!rec_eq(&s_trace_c[k], &s_trace_i[k])) { ok = 0; firstbad = k; break; }
            }
            /* RAM compare only when both ran the identical-length window cleanly */
            if (ok && memcmp(ram_c, ram_i, RAM_SIZE) != 0) { ok = 0; }

            if (ok) { pass++; if (ec && ei) terminated++; continue; }

            /* Exclude BCD undefined-flag (N/V) deltas vs clown's silicon values. */
            if (firstbad >= 1) {
                StepRec *c = &s_trace_c[firstbad], *i = &s_trace_i[firstbad];
                int regs = (c->pc == i->pc);
                for (int r = 0; r < 8 && regs; r++) if (c->D[r] != i->D[r]) regs = 0;
                for (int r = 0; r < 7 && regs; r++) if (c->A[r] != i->A[r]) regs = 0;
                uint16_t sd = (uint16_t)((c->sr ^ i->sr) & 0x1Fu);
                if (regs && (sd & ~0x0Au) == 0 && is_bcd_at(s_trace_c[firstbad - 1].pc)) {
                    bcd_undef++; continue;
                }
            }

            diverge++;
            fprintf(stderr, "[DIVERGE] entry $%06X seed %d: clown_steps=%d(end=%d) interp_steps=%d(end=%d)\n",
                    entries[e], s, nc, ec, ni, ei);
            if (firstbad >= 0) {
                StepRec *c = &s_trace_c[firstbad], *i = &s_trace_i[firstbad];
                fprintf(stderr, "  first diff at step %d: pc c=$%06X i=$%06X  sr c=$%04X i=$%04X\n",
                        firstbad, c->pc, i->pc, c->sr, i->sr);
                for (int r = 0; r < 8; r++) if (c->D[r] != i->D[r])
                    fprintf(stderr, "    D%d c=$%08X i=$%08X\n", r, c->D[r], i->D[r]);
                for (int r = 0; r < 7; r++) if (c->A[r] != i->A[r])
                    fprintf(stderr, "    A%d c=$%08X i=$%08X\n", r, c->A[r], i->A[r]);
            }
            if (diverge >= 40) { fprintf(stderr, "[diff] stopping after 40 divergences\n"); goto done; }
        }
    }
done:
    fprintf(stderr,
        "\n[diff] RESULT  pass=%d (terminated=%d) diverge=%d unimpl=%d skip_fault=%d bcd_undef=%d\n",
        pass, terminated, diverge, unimpl, skip_fault, bcd_undef);
    if (unimpl) fprintf(stderr, "[diff] last unimpl opcode $%04X at $%06X\n", unimpl_op, unimpl_pc);
    free(entries);
    return (diverge || unimpl) ? 1 : 0;
}
