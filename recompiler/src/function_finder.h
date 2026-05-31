/*
 * function_finder.h — 68K function boundary detection interface.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "rom_parser.h"
#include "game_config.h"

typedef struct {
    uint32_t addr;
} FunctionEntry;

typedef struct {
    FunctionEntry *entries;
    int            count;
    int            capacity;
} FunctionList;

void function_finder_run(const GenesisRom *rom, FunctionList *list, const GameConfig *cfg);
void function_list_free(FunctionList *list);

/* True if the routine at `start` unconditionally pops its own return address
 * off the stack at entry (Obj_WaitOffscreen idiom). The code generator uses
 * this to emit `jsr`/`bsr` to such a routine as a non-returning tail transfer
 * instead of falling through to the instruction after the call. */
bool function_finder_pops_return_unconditionally(const GenesisRom *rom, uint32_t start);
