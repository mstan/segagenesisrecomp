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

    /* Seeds from game.cfg extra_func entries (skip blacklisted) */
    for (int i = 0; i < cfg->extra_func_count; i++) {
        if (!game_config_is_blacklisted(cfg, cfg->extra_funcs[i]))
            add_function(list, cfg->extra_funcs[i]);
    }

    /* Walk loop */
    while (s_work_top > 0) {
        uint32_t func_start = s_work_stack[--s_work_top];
        uint32_t pc = func_start;

        while (pc < rom->rom_size) {
            M68KInstr instr;
            if (!m68k_decode(rom, pc, &instr)) break;

            /* Follow calls */
            if (m68k_is_call(&instr) && instr.has_target) {
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

            /* JMP with indexed EA — consult game.cfg jump tables */
            if (instr.mnemonic == MN_JMP && !instr.has_target) {
                /* TODO: detect JMP table(PC,Dn.W) pattern and enumerate jump_table entries */
                /* For now, just terminate the path */
                break;
            }

            /* Terminator */
            if (m68k_is_terminator(&instr)) break;

            pc += instr.byte_length;
        }
    }

    printf("[FunctionFinder] %d functions found\n", list->count);
}

void function_list_free(FunctionList *list) {
    free(list->entries);
    list->entries  = NULL;
    list->count    = 0;
    list->capacity = 0;
}
