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

static void push_addr(uint32_t addr) {
    if (addr >= 0x400000 || addr_seen[addr]) return;
    if (s_work_top >= WORK_STACK_SIZE) {
        fprintf(stderr, "function_finder: work stack overflow at $%06X\n", addr);
        return;
    }
    addr_seen[addr] = true;
    s_work_stack[s_work_top++] = addr;
}

static void add_function(FunctionList *list, uint32_t addr) {
    if (addr >= 0x400000) return;
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

void function_finder_run(const GenesisRom *rom, FunctionList *list, const GameConfig *cfg) {
    memset(addr_seen, 0, sizeof(addr_seen));
    s_work_top = 0;
    s_invalid_terminations = 0;
    s_jt_pc_indexed_sites = 0;
    s_jt_auto_enumerated  = 0;
    s_jt_manual_enumerated = 0;
    s_jt_targets_pushed   = 0;
    s_jt_targets_rejected = 0;
    s_jt_unresolved       = 0;
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
