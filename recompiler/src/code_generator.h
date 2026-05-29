/*
 * code_generator.h — 68K → C code emitter interface.
 */
#pragma once
#include <stdbool.h>
#include "rom_parser.h"
#include "function_finder.h"
#include "annotations.h"
#include "game_config.h"

/* When reverse_debug is true, emitted C routes every m68k_write* through
 * sgrdb_write* (Tier-1 reverse debugger) and writes g_rdb_current_func =
 * 0xXXXXXXu; at the entry of every func_XXXXXX. The runner must be built
 * with -DSONIC_REVERSE_DEBUG=ON for the symbols to resolve. When false,
 * emission is byte-for-byte identical to the pre-Tier-1 baseline. */
bool codegen_emit(const GenesisRom *rom, const FunctionList *funcs,
                  const char *out_full_path, const char *out_dispatch_path,
                  const AnnotationTable *at, const GameConfig *cfg,
                  bool reverse_debug);

/* Diagnostic: if set before codegen_emit, the final post-boundary-split
 * function-entry set is written (one hex addr per line) to `path`. Used by
 * the heuristic-coverage exercise (diff disasm-seeded vs pure-heuristic runs
 * vs the disasm label set). NULL (default) disables the dump. */
void codegen_set_dump_functions_path(const char *path);
