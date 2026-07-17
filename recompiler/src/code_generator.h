/*
 * code_generator.h — 68K → C code emitter interface.
 */
#pragma once
#include <stdbool.h>
#include "rom_parser.h"
#include "function_finder.h"
#include "annotations.h"
#include "game_config.h"

/* Function bodies are distributed across a FIXED number of balanced-size
 * translation units so the build can compile them in parallel. CMake must
 * know all shard filenames at configure time (generation is a build step),
 * so this is a compile-time constant, not a runtime option: the emitter
 * always writes exactly this many <prefix>_partNN.c files (part00..part{N-1},
 * zero-padded to 2 digits), even if some end up empty for a small game. */
#define GENESIS_SPLIT_PART_COUNT 32

/* When reverse_debug is true, emitted C routes every m68k_write* through
 * sgrdb_write* (Tier-1 reverse debugger) and writes g_rdb_current_func =
 * 0xXXXXXXu; at the entry of every func_XXXXXX. The runner must be built
 * with -DSONIC_REVERSE_DEBUG=ON for the symbols to resolve. When false,
 * emission is byte-for-byte identical to the pre-Tier-1 baseline (modulo
 * the split-TU restructuring: function bodies are unchanged, only their
 * distribution across files and g_split_sp_popped's static->extern linkage
 * differ).
 *
 * Output, written to out_dir/:
 *   <prefix>_decls.h        — shared includes + forward decls (#pragma once)
 *   <prefix>_part00.c ..    — GENESIS_SPLIT_PART_COUNT function-body TUs
 *   <prefix>_part{N-1}.c      (part00 also defines g_split_sp_popped)
 * plus out_dispatch_path (unchanged: the dispatch table TU). */
bool codegen_emit(const GenesisRom *rom, const FunctionList *funcs,
                  const char *out_dir, const char *prefix,
                  const char *out_dispatch_path,
                  const AnnotationTable *at, const GameConfig *cfg,
                  bool reverse_debug);

/* Diagnostic: if set before codegen_emit, the final post-boundary-split
 * function-entry set is written (one hex addr per line) to `path`. Used by
 * the heuristic-coverage exercise (diff disasm-seeded vs pure-heuristic runs
 * vs the disasm label set). NULL (default) disables the dump. */
void codegen_set_dump_functions_path(const char *path);
