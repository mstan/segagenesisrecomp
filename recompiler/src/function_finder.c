/*
 * function_finder.c — 68K function boundary detection.
 *
 * Walks the ROM starting from known entry points (initial PC, interrupt
 * vectors, extra_func entries from game.cfg). Follows BSR/JSR to discover
 * reachable functions. Marks RTS as terminators.
 *
 * Special cases to handle:
 *   JMP (An)         — register-indirect jump (dynamic dispatch, no static target)
 *   JMP table(PC,Dn) — indexed jump table (most common Genesis dispatch pattern)
 *   BRA              — unconditional branch (terminates current path)
 *   DBcc             — loop branch (both taken and fall-through paths explored)
 *
 * Jump table detection: when JMP with PC-relative indexed EA is seen,
 * we consult game.cfg's jump_table entries to enumerate all case targets.
 */
#include "function_finder.h"
#include "m68k_decoder.h"
#include "m68k_validator.h"
#include "rom_parser.h"
#include "game_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FUNCTIONS 65536
#define WORK_STACK_SIZE 65536

static uint32_t s_work_stack[WORK_STACK_SIZE];
static int      s_work_top = 0;

static bool addr_seen[0x400000];   /* one byte per ROM address — visited flags */

/* Validator-rejection counter — surfaces speculative-scan terminations
 * so misdecoded data regions show up at the end of the run. */
static int s_invalid_terminations = 0;

/* Phase 6: PC-indexed jump-table discovery counters. */
static int s_jt_pc_indexed_sites      = 0;  /* JMP (d8,PC,Xn.W) seen        */
static int s_jt_auto_enumerated       = 0;  /* tables auto-walked from rom  */
static int s_jt_manual_enumerated     = 0;  /* tables matched in game.cfg   */
static int s_jt_targets_pushed        = 0;  /* worklist additions           */
static int s_jt_targets_rejected      = 0;  /* failed validation            */
static int s_jt_unresolved            = 0;  /* path terminated, no table    */

/* Two-step long-pointer dispatch counters (movea.l <tbl>(pc,Xn),aN; jmp/jsr (aN)). */
static int s_jt_twostep_sites         = 0;  /* movea+indirect idioms matched */
static int s_jt_twostep_tables        = 0;  /* tables yielding >=1 entry      */

/* Per-site record for every dispatch we couldn't enumerate. Dumped to
 * generated/<prefix>.unresolved_jumptables.log so the user can grep
 * disasm for these PCs and either add manual jump_table directives or
 * confirm the dispatch is OK to leave dynamic. */
typedef struct {
    uint32_t pc;        /* PC of the JMP instruction itself */
    uint32_t base;      /* computed table base from (d8,PC,Xn.W) */
    uint16_t ext;       /* extension word (Xn.W register encoded here) */
    uint8_t  reason;    /* 0 = no manual + no auto-walk; 1 = auto-walk
                           found < MIN entries; 2 = non-PC-indexed JMP */
} UnresolvedSite;
static UnresolvedSite *s_jt_unresolved_sites = NULL;
static int             s_jt_unresolved_cap   = 0;

static void record_unresolved(uint32_t pc, uint32_t base, uint16_t ext, uint8_t reason) {
    if (s_jt_unresolved >= s_jt_unresolved_cap) {
        int new_cap = s_jt_unresolved_cap ? s_jt_unresolved_cap * 2 : 64;
        UnresolvedSite *p = realloc(s_jt_unresolved_sites,
                                    (size_t)new_cap * sizeof(UnresolvedSite));
        if (!p) return;
        s_jt_unresolved_sites = p;
        s_jt_unresolved_cap   = new_cap;
    }
    UnresolvedSite *e = &s_jt_unresolved_sites[s_jt_unresolved++];
    e->pc     = pc;
    e->base   = base;
    e->ext    = ext;
    e->reason = reason;
}

#define JT_AUTO_MAX_ENTRIES   256
#define JT_AUTO_MIN_ENTRIES     2

/* Two-step long-pointer table walk bounds (see jt_enumerate_long). */
#define JT_LONG_MAX_ENTRIES    64
#define JT_LONG_LEAD_SKIP       4

/* Forward decl — defined later, but jt_enumerate needs to call it. */
static void add_function(FunctionList *list, uint32_t addr);

static const JumpTableEntry *
jt_lookup_manual(const GameConfig *cfg, uint32_t base) {
    if (!cfg) return NULL;
    for (int i = 0; i < cfg->jump_table_count; i++) {
        if (cfg->jump_tables[i].start_addr == base)
            return &cfg->jump_tables[i];
    }
    return NULL;
}

/* Compute the absolute jump target for entry `i` of a table at `base`
 * with the supplied stride/format, reading from rom. Returns
 * 0xFFFFFFFFu on out-of-rom or unaligned target — signal to stop. */
static uint32_t
jt_entry_target(const GenesisRom *rom, uint32_t base,
                uint32_t stride, JumpTableFormat fmt, int i) {
    uint32_t entry_addr = base + (uint32_t)i * stride;
    if (entry_addr + (stride - 1) >= rom->rom_size) return 0xFFFFFFFFu;

    uint32_t target;
    if (fmt == JT_FMT_PCREL_W) {
        /* 16-bit signed offset relative to the table base. */
        int16_t off = (int16_t)rom_read16(rom, entry_addr);
        target = base + (int32_t)off;
    } else if (fmt == JT_FMT_BRA_W || fmt == JT_FMT_BRA_S) {
        /* bra-trampoline tables: the JMP at the dispatch site lands on
         * the trampoline body itself, so the function entry IS the
         * entry's address. (The bra.w / bra.s instruction inside that
         * trampoline branches on to the real handler — that target
         * comes in via the python tool's [functions].extra emission,
         * not via this enumerate step.) */
        target = entry_addr;
    } else {
        target = rom_read32(rom, entry_addr) & 0xFFFFFFu;
    }
    if (target & 1) return 0xFFFFFFFFu;        /* must be even-aligned */
    if (target >= rom->rom_size) return 0xFFFFFFFFu;
    return target;
}

/* Walk a jump table at `base` and push every legal target into the
 * worklist via add_function. Honors a hard cap so a misidentified
 * table can't run away through random data. Returns number of
 * targets pushed. */
static int
jt_enumerate(const GenesisRom *rom, const GameConfig *cfg,
             FunctionList *list,
             uint32_t base, uint32_t stride, JumpTableFormat fmt,
             int max_entries,
             const M68KValidatorOptions *vopts) {
    int pushed = 0;
    for (int i = 0; i < max_entries; i++) {
        uint32_t target = jt_entry_target(rom, base, stride, fmt, i);
        if (target == 0xFFFFFFFFu) break;

        /* Validator gate: the target's first instruction must be a
         * legal MC68000 encoding. If not, the table likely ran into
         * adjacent data; stop. */
        M68KInstr probe;
        if (!m68k_decode(rom, target, &probe)) { s_jt_targets_rejected++; break; }
        if (m68k_validate(&probe, vopts) != M68K_LEGAL) {
            s_jt_targets_rejected++;
            break;
        }

        if (cfg && game_config_is_blacklisted(cfg, target)) {
            s_jt_targets_rejected++;
            continue;
        }
        add_function(list, target);
        pushed++;
        s_jt_targets_pushed++;
    }
    return pushed;
}

/* Enumerate a branch-ladder jump table: a run of consecutive `bra.s`
 * (2-byte) or `bra.w` (4-byte) trampolines that a `JMP (d8,PC,Dn.W)`
 * dispatches into by computing base + index*stride. Each slot's ADDRESS
 * is the dispatch landing point (the bra inside it tail-jumps to the real
 * handler), so we add each slot as a function entry — exactly the
 * JT_FMT_BRA_S/W semantics jt_entry_target encodes, but discovered
 * automatically instead of via a manual game.toml [[jump_table]].
 *
 * Safe to run unconditionally (unlike the pcrel16 auto-walk): it only
 * matches when every slot literally decodes as a same-width BRA whose
 * target is even and in-ROM. A run of >=2 such slots is an extremely
 * strong table signal — arbitrary data does not look like consecutive
 * bra instructions with valid in-ROM targets. The stride is locked to the
 * first slot's width, so a ladder of bra.s won't accidentally absorb a
 * following bra.w (or vice-versa) or any non-branch code/data after it.
 *
 * Returns the number of slots enumerated. */
static int
jt_enumerate_bra_ladder(const GenesisRom *rom, const GameConfig *cfg,
                        FunctionList *list, uint32_t base,
                        const M68KValidatorOptions *vopts) {
    M68KInstr first;
    if (!m68k_decode(rom, base, &first)) return 0;
    if (m68k_validate(&first, vopts) != M68K_LEGAL) return 0;
    if (first.mnemonic != MN_BRA || !first.has_target) return 0;
    uint32_t stride = first.byte_length;     /* 2 = bra.s, 4 = bra.w */
    if (stride != 2 && stride != 4) return 0;

    /* Pass 1 — count consecutive same-width BRA slots WITHOUT mutating the
     * function list. A ladder ends at the first slot that isn't a same-width
     * BRA with an even, in-ROM target. We only commit (pass 2) if the run is
     * long enough to be a real table; this avoids turning a lone offset word
     * that happens to decode as a bra into a spurious data-as-code entry. */
    int count = 0;
    for (int i = 0; i < JT_AUTO_MAX_ENTRIES; i++) {
        uint32_t slot = base + (uint32_t)i * stride;
        if (slot + (stride - 1) >= rom->rom_size) break;
        M68KInstr e;
        if (!m68k_decode(rom, slot, &e)) break;
        if (m68k_validate(&e, vopts) != M68K_LEGAL) break;
        if (e.mnemonic != MN_BRA || !e.has_target) break;
        if (e.byte_length != stride) break;
        if ((e.target_addr & 1) || e.target_addr >= rom->rom_size) break;
        count++;
    }
    if (count < JT_AUTO_MIN_ENTRIES) return 0;

    /* Pass 2 — commit each slot as a dispatch landing point. Return the
     * recognized table length (not the post-blacklist add count) so the
     * caller treats a fully-blacklisted ladder as resolved, not unresolved. */
    for (int i = 0; i < count; i++) {
        uint32_t slot = base + (uint32_t)i * stride;
        if (cfg && game_config_is_blacklisted(cfg, slot)) continue;
        add_function(list, slot);
        s_jt_targets_pushed++;
    }
    return count;
}

/* True if `ins` writes address register `areg`. Used to detect when the
 * pointer a two-step dispatch loaded into aN is clobbered before the
 * indirect jmp/jsr (aN) consumes it — in which case the loaded table is
 * NOT the one the indirect transfer uses, so we must not attribute it.
 * Conservative: EXG / MOVEM are treated as clobbers (rare between a table
 * load and its use; a false abort only costs a conservative miss). */
static bool insn_writes_areg(const M68KInstr *ins, int areg) {
    switch (ins->mnemonic) {
    case MN_MOVEA: case MN_LEA: case MN_ADDA: case MN_SUBA:
    case MN_LINK:  case MN_UNLK: case MN_MOVE_USP:
        return ins->reg == areg;
    case MN_EXG:                 /* may swap an An; can't tell which cheaply */
    case MN_MOVEM:               /* (sp)+,<regs> can reload aN               */
        return true;
    default:
        break;
    }
    /* ADDQ/SUBQ #n,An — the An destination is encoded in src_ea. */
    if ((ins->mnemonic == MN_ADDQ || ins->mnemonic == MN_SUBQ)
            && ((ins->src_ea >> 3) & 7) == EA_An
            && (ins->src_ea & 7) == areg)
        return true;
    /* Any MOVE-group instruction whose DESTINATION EA is (An,areg).
     * (dst_ea is only populated for the MOVE groups; 0 elsewhere = D0,
     * so this is a no-op for non-MOVE mnemonics.) */
    if (((ins->dst_ea >> 3) & 7) == EA_An && (ins->dst_ea & 7) == areg)
        return true;
    return false;
}

/* Enumerate a two-step long-pointer dispatch table at `base`: a run of
 * 32-bit ROM code pointers that `movea.l <base>(pc,Xn),aN; jmp/jsr (aN)`
 * indexes. Each valid entry is a function ENTRY (the indirect transfer
 * lands directly on it).
 *
 * Unlike the pcrel16 auto-walk, the per-entry gate here is STRONG and so
 * this runs unconditionally for every game (no jump_table_autodiscovery
 * opt-in), like the bra-ladder detector: a genuine ROM code pointer has a
 * clear top byte and is < rom_size, so a random longword almost never
 * passes (top byte 0 is ~1/256, and < a <=4MB rom_size rarer still). That
 * makes over-walk self-terminating — the first longword past the table end
 * is overwhelmingly likely to be code/data with a set top byte.
 *
 * Tolerates up to JT_LONG_LEAD_SKIP leading invalid slots: the low command
 * indices of a 0-based dispatch table are frequently unused garbage (e.g.
 * RKA's $1944 dispatcher, whose cmd-0 slot at $1976 is $65D04E75 — odd, so
 * not a pointer). After the first valid entry, the first invalid one ends
 * the table. Caps at JT_LONG_MAX_ENTRIES to bound a misfire. Returns the
 * number of targets pushed (0 == not a recognizable table). */
static int
jt_enumerate_long(const GenesisRom *rom, const GameConfig *cfg,
                  FunctionList *list, uint32_t base,
                  const M68KValidatorOptions *vopts) {
    int pushed = 0;
    int valid_seen = 0;
    for (int i = 0; i < JT_LONG_MAX_ENTRIES; i++) {
        uint32_t entry_addr = base + (uint32_t)i * 4;
        if (entry_addr + 4 > rom->rom_size) break;
        uint32_t raw = rom_read32(rom, entry_addr);
        /* A real entry is a non-null, even, in-ROM pointer (top byte clear,
         * which `raw < rom_size` enforces for these <=4MB ROMs) whose first
         * instruction is a legal MC68000 encoding. */
        bool ok = (raw != 0) && !(raw & 1) && (raw < rom->rom_size);
        if (ok) {
            M68KInstr probe;
            if (!m68k_decode(rom, raw, &probe)
                    || m68k_validate(&probe, vopts) != M68K_LEGAL)
                ok = false;
        }
        if (!ok) {
            if (valid_seen) break;             /* table end after real entries */
            if (i >= JT_LONG_LEAD_SKIP) break; /* too many leading non-pointers */
            continue;                          /* tolerate a leading dead slot  */
        }
        valid_seen++;
        if (cfg && game_config_is_blacklisted(cfg, raw)) continue;
        add_function(list, raw);
        pushed++;
        s_jt_targets_pushed++;
    }
    return pushed;
}

static void push_addr(uint32_t addr) {
    if (addr >= 0x400000 || addr_seen[addr]) return;
    if (s_work_top >= WORK_STACK_SIZE) {
        fprintf(stderr, "function_finder: work stack overflow at $%06X\n", addr);
        return;
    }
    addr_seen[addr] = true;
    s_work_stack[s_work_top++] = addr;
}

/* Set at the top of function_finder_run so add_function can consult the
 * disasm code-address oracle without threading cfg through every call site
 * (jt_enumerate, bra-ladder, the CFG walk). NULL => no gating. */
static const GameConfig *s_ff_cfg = NULL;

static void add_function(FunctionList *list, uint32_t addr) {
    if (addr >= 0x400000) return;
    /* Data-gate: when a code-address oracle is loaded, never register a target
     * the disasm assembles as DATA (or a mid-instruction address). Explicit
     * seeds — vectors and game.toml extras/discovery labels — are instruction
     * starts and pass; this only rejects speculative jump-table / bra-ladder /
     * walk targets that fell into data. No-op when no code_addrs_file is set. */
    if (!game_config_is_known_code(s_ff_cfg, addr)) return;
    /* Check if already in list */
    for (int i = 0; i < list->count; i++)
        if (list->entries[i].addr == addr) return;

    if (list->count >= list->capacity) {
        int new_cap = list->capacity ? list->capacity * 2 : 256;
        FunctionEntry *tmp = realloc(list->entries, new_cap * sizeof(FunctionEntry));
        if (!tmp) return;
        list->entries   = tmp;
        list->capacity  = new_cap;
    }
    FunctionEntry *e = &list->entries[list->count++];
    e->addr = addr;
    push_addr(addr);
}

/* True if the routine at `start` captures its caller's return address off the
 * stack and repurposes it (the Sonic "Obj_WaitOffscreen" idiom): it pops the
 * return longword with `move(a).l (a7)+,<ea>` and stores it as the object's
 * code pointer, which is later reached via `jmp (An)`. For such callees the
 * caller's RETURN ADDRESS is a live dispatch entry that no static
 * branch/call/address-taken edge points at — so it must be registered or it
 * becomes a dispatch miss (the badnik's on-screen body never runs).
 *
 * Detection: scan the prologue for a longword pop from (a7)+ that occurs
 * BEFORE any push / link / call / branch (which would mean the pop is merely
 * rebalancing the callee's own frame, not consuming the return address).
 * Conservative — bails at the first such instruction or terminator. */
static bool function_captures_return_addr(const GenesisRom *rom, uint32_t start) {
    uint32_t pc = start;
    for (int n = 0; n < 12 && pc + 1 < rom->rom_size; n++) {
        M68KInstr ins;
        if (!m68k_decode(rom, pc, &ins)) return false;
        /* Longword pop from (a7)+ at net-zero stack depth == the return addr. */
        if ((ins.mnemonic == MN_MOVE || ins.mnemonic == MN_MOVEA)
                && ins.size == M68K_SIZE_L
                && ins.src_ea == ((3 << 3) | 7))            /* (a7)+ */
            return true;
        /* A push / link / multi-pop / call / branch means any later pop is
         * balancing the callee's own frame — stop looking. */
        if (ins.dst_ea == ((4 << 3) | 7)                    /* -(a7) dest */
                || ins.mnemonic == MN_PEA
                || ins.mnemonic == MN_LINK
                || ins.mnemonic == MN_MOVEM
                || m68k_is_call(&ins)
                || m68k_is_terminator(&ins))
            return false;
        pc += ins.byte_length;
    }
    return false;
}

/* Strict variant used by the code generator (NOT discovery).
 *
 * Returns true only if the routine UNCONDITIONALLY pops its own return
 * address off the stack at entry — the Obj_WaitOffscreen idiom
 * (`move.l (sp)+,$34(a0)`): the routine saves the return PC as an object
 * code pointer and redirects control, so its eventual rts returns to the
 * caller of the `jsr`, never to the instruction after it. The generator
 * turns `jsr <such routine>` into a non-returning tail transfer (no second
 * stack pop, no fall-through to the post-jsr address).
 *
 * Stricter than function_captures_return_addr(): it also bails on any
 * conditional branch (Bcc/DBcc) before the pop, so a routine that only
 * pops its return on SOME path is never mis-treated as always-redirecting.
 * Discovery keeps the looser heuristic; only emission uses this one. */
bool function_finder_pops_return_unconditionally(const GenesisRom *rom, uint32_t start) {
    uint32_t pc = start;
    for (int n = 0; n < 12 && pc + 1 < rom->rom_size; n++) {
        M68KInstr ins;
        if (!m68k_decode(rom, pc, &ins)) return false;
        /* Longword pop from (a7)+ reached with no intervening control flow
         * or stack growth == the return address being consumed. */
        if ((ins.mnemonic == MN_MOVE || ins.mnemonic == MN_MOVEA)
                && ins.size == M68K_SIZE_L
                && ins.src_ea == ((3 << 3) | 7))            /* (a7)+ */
            return true;
        /* Anything that pushes, calls, branches (conditional or not), or
         * terminates before the pop makes the entry pop non-guaranteed. */
        if (ins.dst_ea == ((4 << 3) | 7)                    /* -(a7) dest */
                || ins.mnemonic == MN_PEA
                || ins.mnemonic == MN_LINK
                || ins.mnemonic == MN_MOVEM
                || ins.mnemonic == MN_Bcc
                || ins.mnemonic == MN_DBcc
                || m68k_is_call(&ins)
                || m68k_is_terminator(&ins))
            return false;
        pc += ins.byte_length;
    }
    return false;
}

void function_finder_run(const GenesisRom *rom, FunctionList *list, const GameConfig *cfg) {
    s_ff_cfg = cfg;
    memset(addr_seen, 0, sizeof(addr_seen));
    s_work_top = 0;
    s_invalid_terminations = 0;
    s_jt_pc_indexed_sites = 0;
    s_jt_auto_enumerated  = 0;
    s_jt_manual_enumerated = 0;
    s_jt_targets_pushed   = 0;
    s_jt_targets_rejected = 0;
    s_jt_unresolved       = 0;
    s_jt_twostep_sites    = 0;
    s_jt_twostep_tables   = 0;
    /* Reuse the unresolved-site buffer across runs but reset its
     * logical length. Capacity is preserved so the next run avoids
     * re-allocating from scratch. */
    M68KValidatorOptions vopts = {0};
    vopts.allow_68020_branch = cfg ? cfg->allow_68020_branch : false;

    /* Seed from vector table at $000000 */
    /* Offset 4 = initial PC (RESET handler) */
    add_function(list, rom_read32(rom, 4) & 0xFFFFFF);

    /* Common interrupt vectors (68K vector table at $000000) */
    /* Bus error=$8, Address error=$C, Illegal=$10 ... H-blank=$70, V-blank=$78 */
    for (int vec = 2; vec < 64; vec++) {
        uint32_t handler = rom_read32(rom, (uint32_t)vec * 4) & 0xFFFFFF;
        if (handler != 0 && handler != 0xFFFFFF && handler < rom->rom_size)
            add_function(list, handler);
    }

    /* Seeds from game.toml [functions].extra entries — but skip any
     * blacklisted addresses, even if a discovery_files merge added them. */
    for (int i = 0; i < cfg->extra_func_count; i++) {
        if (game_config_is_blacklisted(cfg, cfg->extra_funcs[i])) continue;
        add_function(list, cfg->extra_funcs[i]);
    }

    /* Walk loop */
    while (s_work_top > 0) {
        uint32_t func_start = s_work_stack[--s_work_top];
        uint32_t pc = func_start;

        while (pc < rom->rom_size) {
            M68KInstr instr;
            if (!m68k_decode(rom, pc, &instr)) break;

            /* Post-decode legality check: if this byte sequence isn't
             * a valid MC68000 encoding we are most likely scanning
             * data, not code. Stop the path so we don't pollute the
             * function list with speculative entries. */
            M68KValidity v = m68k_validate(&instr, &vopts);
            if (v != M68K_LEGAL) {
                s_invalid_terminations++;
                break;
            }

            /* Follow calls — but skip blacklisted addresses (game.cfg
             * `blacklist` directive). Useful for JSR/BSR targets that
             * happen to land on non-code addresses (e.g., conditional
             * code paths the static walker can't prove dead). */
            if (m68k_is_call(&instr) && instr.has_target
                    && !game_config_is_blacklisted(cfg, instr.target_addr)) {
                add_function(list, instr.target_addr);
                /* If the callee captures its return address off the stack and
                 * repurposes it as a code pointer (Obj_WaitOffscreen idiom),
                 * the return address is a live dispatch entry — register it.
                 * add_function code-gates it, so data return addresses (e.g.
                 * inline-parameter callees) are rejected. */
                if (function_captures_return_addr(rom, instr.target_addr))
                    add_function(list, pc + instr.byte_length);
            }

            /* JSR (d8,PC,Dn.W) — a computed CALL into a bra-ladder (the Sonic
             * Animate_Raw command dispatch: `jsr table-N(pc,Dn.w)`). The
             * finder already enumerates bra-ladders for the JMP form; do it
             * for JSR too, so the unlabeled ladder slots become dispatch
             * entries (otherwise the animation command dispatch-misses and the
             * sprite's mapping frame is never updated). JSR returns — do NOT
             * break the scan path. */
            if (instr.mnemonic == MN_JSR && !instr.has_target) {
                int jea_mode = (instr.src_ea >> 3) & 7;
                int jea_reg  = instr.src_ea & 7;
                if (jea_mode == 7 && jea_reg == 3 && instr.word_count >= 2) {
                    int8_t   d8   = (int8_t)(instr.words[1] & 0xFF);
                    uint32_t base = pc + 2 + (int32_t)d8;
                    /* The `table-N(pc,Dn)` idiom places the first slot N bytes
                     * past the computed base (N = the slot stride). Probe
                     * base, base+2, base+4 for the ladder start; first run of
                     * >=2 same-width BRA slots wins. */
                    for (int off = 0; off <= 4; off += 2) {
                        if (jt_enumerate_bra_ladder(rom, cfg, list,
                                base + (uint32_t)off, &vopts) >= JT_AUTO_MIN_ENTRIES)
                            break;
                    }
                }
            }

            /* Two-step long-pointer dispatch table:
             *   movea.l <table>(pc,Xn.W),aN   ; load a 32-bit code pointer
             *   ...                           ; (optional arg setup)
             *   jmp/jsr (aN)                  ; transfer through it
             * The recompiler reaches most of a non-Sonic ROM's handlers
             * through this idiom (RKA's command queue at $1944, object state
             * machines, etc.). The `jmp/jsr (aN)` site alone has no static
             * target, so without enumerating the table at the LOAD the target
             * handlers dispatch-miss (graphics/VDP routines never run → empty
             * VRAM → black screen). Match on the movea.l load, confirm the
             * indirect transfer through the same aN follows in straight-line
             * code, then enumerate the long-pointer table. Strong per-entry
             * gate (jt_enumerate_long) — safe to run for every game. */
            if (instr.mnemonic == MN_MOVEA && instr.size == M68K_SIZE_L
                    && instr.src_ea == ((EA_PCR << 3) | PCR_PC_IDX)
                    && instr.word_count >= 2) {
                int      areg = instr.reg;                       /* destination aN  */
                int8_t   d8   = (int8_t)(instr.words[1] & 0xFF); /* brief-ext disp  */
                uint32_t base = pc + 2 + (int32_t)d8;            /* (pc) bias = +2  */
                /* Scan straight-line forward for `jmp/jsr (aN)` on the same
                 * aN. Abort if aN is reloaded/clobbered, or control leaves
                 * the region, before the indirect transfer. */
                uint32_t lpc = pc + instr.byte_length;
                for (int k = 0; k < 6 && lpc < rom->rom_size; k++) {
                    M68KInstr li;
                    if (!m68k_decode(rom, lpc, &li)) break;
                    if (m68k_validate(&li, &vopts) != M68K_LEGAL) break;
                    if ((li.mnemonic == MN_JMP || li.mnemonic == MN_JSR)
                            && !li.has_target
                            && li.src_ea == ((EA_An_IND << 3) | areg)) {
                        s_jt_twostep_sites++;
                        if (jt_enumerate_long(rom, cfg, list, base, &vopts) > 0)
                            s_jt_twostep_tables++;
                        break;
                    }
                    if (insn_writes_areg(&li, areg)) break;
                    if (m68k_is_call(&li) || m68k_is_terminator(&li)
                            || li.mnemonic == MN_Bcc || li.mnemonic == MN_DBcc)
                        break;
                    lpc += li.byte_length;
                }
            }

            /* Two-step long-pointer dispatch via a base REGISTER:
             *   lea     <table>, aN          ; abs.l or (d,pc) table base -> aN
             *   ...                          ; index scaling (no clobber of aN)
             *   movea.l (aN,Xn.W), aP        ; aP = table[index]
             *   jmp/jsr (aP)                 ; transfer through it
             * Distinct from the PC-indexed movea form above: here a `lea`
             * makes the table base static and a SEPARATE `movea.l (aN,Xn.W)`
             * does the indexed load (RKA's command dispatchers, e.g. $23B6:
             * `lea $23D8,a0; movea.l (a0,d0.w),a0; jmp (a0)`, whose slots
             * include the shared rts handler $23D6). The indirect jmp/jsr (aP)
             * site is non-PC-indexed, so without enumerating the lea's table
             * those handlers dispatch-miss. Same strong long-pointer gate. */
            if (instr.mnemonic == MN_LEA) {
                int      tbl_reg  = instr.reg;               /* aN = base reg   */
                int      lea_mode = (instr.src_ea >> 3) & 7;
                int      lea_reg  = instr.src_ea & 7;
                uint32_t tbl_base = 0;
                bool     have_base = false;
                if (lea_mode == EA_PCR && lea_reg == PCR_ABS_L
                        && instr.word_count >= 3) {
                    tbl_base = ((uint32_t)instr.words[1] << 16) | instr.words[2];
                    have_base = (tbl_base < rom->rom_size);
                } else if (lea_mode == EA_PCR && lea_reg == PCR_PC_DISP
                        && instr.word_count >= 2) {
                    tbl_base = pc + 2 + (int32_t)(int16_t)instr.words[1];
                    have_base = (tbl_base < rom->rom_size);
                }
                uint32_t lpc = pc + instr.byte_length;
                for (int k = 0; have_base && k < 8 && lpc < rom->rom_size; k++) {
                    M68KInstr li;
                    if (!m68k_decode(rom, lpc, &li)) break;
                    if (m68k_validate(&li, &vopts) != M68K_LEGAL) break;
                    /* the indexed long load through aN */
                    if (li.mnemonic == MN_MOVEA && li.size == M68K_SIZE_L
                            && ((li.src_ea >> 3) & 7) == EA_An_IDX
                            && (li.src_ea & 7) == tbl_reg
                            && li.word_count >= 2) {
                        int      ap   = li.reg;                       /* dest aP   */
                        int8_t   disp = (int8_t)(li.words[1] & 0xFF); /* base disp */
                        uint32_t base = tbl_base + (int32_t)disp;
                        uint32_t jpc  = lpc + li.byte_length;
                        for (int j = 0; j < 4 && jpc < rom->rom_size; j++) {
                            M68KInstr ji;
                            if (!m68k_decode(rom, jpc, &ji)) break;
                            if (m68k_validate(&ji, &vopts) != M68K_LEGAL) break;
                            if ((ji.mnemonic == MN_JMP || ji.mnemonic == MN_JSR)
                                    && !ji.has_target
                                    && ji.src_ea == ((EA_An_IND << 3) | ap)) {
                                s_jt_twostep_sites++;
                                if (jt_enumerate_long(rom, cfg, list, base, &vopts) > 0)
                                    s_jt_twostep_tables++;
                                break;
                            }
                            if (insn_writes_areg(&ji, ap)) break;
                            if (m68k_is_call(&ji) || m68k_is_terminator(&ji)
                                    || ji.mnemonic == MN_Bcc
                                    || ji.mnemonic == MN_DBcc) break;
                            jpc += ji.byte_length;
                        }
                        break;  /* consumed this lea's indexed load */
                    }
                    /* aN reloaded before the indexed load -> not a table base. */
                    if (insn_writes_areg(&li, tbl_reg)) break;
                    if (m68k_is_call(&li) || m68k_is_terminator(&li)
                            || li.mnemonic == MN_Bcc || li.mnemonic == MN_DBcc)
                        break;
                    lpc += li.byte_length;
                }
            }

            /* Follow conditional branches (both paths) */
            if ((instr.mnemonic == MN_Bcc || instr.mnemonic == MN_DBcc)
                    && instr.has_target) {
                push_addr(instr.target_addr);
            }

            /* Follow unconditional branch */
            if (instr.mnemonic == MN_BRA && instr.has_target) {
                push_addr(instr.target_addr);
                /* BRA: no fall-through */
                break;
            }

            /* JMP with static target (absolute long EA) */
            if (instr.mnemonic == MN_JMP && instr.has_target) {
                push_addr(instr.target_addr);
                break;  /* JMP: no fall-through */
            }

            /* JMP with indexed EA — handle the common Genesis pattern
             * `JMP (d8,PC,Xn.W)` (mode 7, reg 3) by enumerating its
             * jump table either from a matching game.cfg jump_table
             * directive or, failing that, by an auto-walk of pcrel16
             * entries until the validator says we've left the table. */
            if (instr.mnemonic == MN_JMP && !instr.has_target) {
                int ea_mode = (instr.src_ea >> 3) & 7;
                int ea_reg  = instr.src_ea & 7;
                if (ea_mode == 7 && ea_reg == 3 && instr.word_count >= 2) {
                    s_jt_pc_indexed_sites++;
                    uint16_t ext = instr.words[1];
                    int8_t   d8  = (int8_t)(ext & 0xFF);
                    /* Codegen uses (instr->addr + er.bp + d8); for the
                     * (d8,PC,Xn.W) form the PC bias is 2 (one ext word
                     * read) plus d8. */
                    uint32_t base = pc + 2 + (int32_t)d8;
                    const JumpTableEntry *m = jt_lookup_manual(cfg, base);
                    if (m) {
                        int max_entries = (int)((m->end_addr > m->start_addr)
                            ? (m->end_addr - m->start_addr) / m->stride_bytes
                            : JT_AUTO_MAX_ENTRIES);
                        if (max_entries < 1) max_entries = 1;
                        if (max_entries > JT_AUTO_MAX_ENTRIES)
                            max_entries = JT_AUTO_MAX_ENTRIES;
                        jt_enumerate(rom, cfg, list, base,
                                     m->stride_bytes, m->format,
                                     max_entries, &vopts);
                        s_jt_manual_enumerated++;
                    } else if (jt_enumerate_bra_ladder(rom, cfg, list, base,
                                                       &vopts)
                                   >= JT_AUTO_MIN_ENTRIES) {
                        /* base points at a run of bra.s/bra.w trampolines —
                         * enumerate the ladder automatically. Safe without
                         * the autodiscovery opt-in (see the function's
                         * comment): it only fires on actual branch runs. */
                        s_jt_auto_enumerated++;
                    } else if (cfg && cfg->jump_table_autodiscovery) {
                        /* Conservative auto-walk: pcrel16 only. The
                         * validator gate inside jt_enumerate stops
                         * at the first target that doesn't decode
                         * cleanly. Off by default — game-specific
                         * data layouts can produce data sequences
                         * whose first decoded instruction is legal
                         * MC68000 even though the address is data,
                         * so unsupervised auto-walk pollutes the
                         * function list. Sonic 1 measured 495
                         * spurious externs with auto-walk on. */
                        int n = jt_enumerate(rom, cfg, list, base,
                                             2, JT_FMT_PCREL_W,
                                             JT_AUTO_MAX_ENTRIES, &vopts);
                        if (n >= JT_AUTO_MIN_ENTRIES)
                            s_jt_auto_enumerated++;
                        else
                            record_unresolved(pc, base, ext, 1);
                    } else {
                        record_unresolved(pc, base, ext, 0);
                    }
                } else {
                    record_unresolved(pc, 0, 0, 2);
                }
                break;  /* JMP terminates the path either way */
            }

            /* Terminator */
            if (m68k_is_terminator(&instr)) break;

            pc += instr.byte_length;
        }
    }

    printf("[FunctionFinder] %d functions found\n", list->count);
    printf("[FunctionFinder] %d speculative paths terminated by validator\n",
           s_invalid_terminations);
    printf("[FunctionFinder] Jump-table discovery: pc_indexed=%d "
           "auto=%d manual=%d targets=%d rejected=%d unresolved=%d\n",
           s_jt_pc_indexed_sites, s_jt_auto_enumerated,
           s_jt_manual_enumerated, s_jt_targets_pushed,
           s_jt_targets_rejected, s_jt_unresolved);
    printf("[FunctionFinder] Two-step long-pointer dispatch: sites=%d "
           "tables_enumerated=%d\n",
           s_jt_twostep_sites, s_jt_twostep_tables);

    /* Dump unresolved dispatch sites so the user can investigate which
     * tables the static extractor missed. The recompiler still emits
     * a runtime dynamic-dispatch path for these — they don't break
     * correctness — but a non-zero count is a signal that gen_disasm_
     * jumptables.py left some tables on the table. */
    if (s_jt_unresolved > 0 && cfg && cfg->output_prefix[0]) {
        char log_path[256];
        snprintf(log_path, sizeof(log_path),
                 "generated/%s.unresolved_jumptables.log", cfg->output_prefix);
        FILE *lf = fopen(log_path, "w");
        if (lf) {
            fprintf(lf, "# %d PC-indexed JMP dispatch sites with no static "
                        "table coverage.\n", s_jt_unresolved);
            fprintf(lf, "# Format: <jmp_pc> <table_base> <ext_word> <reason>\n");
            fprintf(lf, "# Reason: 0=no manual jump_table directive, "
                        "1=auto-walk yielded too few entries, "
                        "2=non-PC-indexed JMP variant\n");
            fprintf(lf, "# Each line is one runtime dynamic dispatch — add a\n"
                        "#   jump_table <base> <end> <stride> <fmt>\n"
                        "# directive to game.cfg (or to the disasm_jumptables\n"
                        "# side file) to convert it to static enumeration.\n");
            for (int i = 0; i < s_jt_unresolved; i++) {
                const UnresolvedSite *u = &s_jt_unresolved_sites[i];
                fprintf(lf, "%06X %06X %04X %u\n",
                        u->pc, u->base, u->ext, u->reason);
            }
            fclose(lf);
            printf("[FunctionFinder] Wrote %d unresolved sites to %s\n",
                   s_jt_unresolved, log_path);
        } else {
            fprintf(stderr, "[FunctionFinder] could not open %s for writing\n",
                    log_path);
        }
    }
}

void function_list_free(FunctionList *list) {
    free(list->entries);
    list->entries  = NULL;
    list->count    = 0;
    list->capacity = 0;
}
