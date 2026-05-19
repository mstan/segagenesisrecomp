/*
 * crash_report.h — structured runtime-crash diagnostic dump.
 *
 * Inspired by the n64recomp / Pokemon Stadium project's crash trace.
 * When the watchdog fires (or any other unrecoverable-stuck condition
 * is detected) we want more than just "registers + stack hex":
 *
 *   1. Symbol-resolved current function name
 *   2. Stack walk: every plausible return address on the 68K stack,
 *      annotated with its function name from the disasm-derived
 *      symbol table (annotations_from_disasm.csv).
 *   3. Recent function-entry ring: last N func_NNNNNN bodies executed,
 *      populated by the rdb_on_block hook the codegen already emits.
 *   4. Recent instruction-block ring: last N intra-function block
 *      transitions, populated by rdb_on_insn.
 *
 * All compiled in unconditionally — the rings are tiny and the
 * hooks already exist in generated code under SONIC_REVERSE_DEBUG.
 * When reverse-debug is OFF the rings just stay empty and the
 * report omits those sections. Symbol loading is also optional:
 * call crash_report_load_symbols(path) at startup if the per-game
 * annotations CSV is available, otherwise the report falls back to
 * raw $XXXXXX addresses.
 */
#pragma once

#include <stdint.h>
#include <stdio.h>

/*
 * Push the most recent block transition into the ring. Called from
 * the rdb_on_block inline so every recompiled function entry and
 * every label transition is recorded. The recompiler emits
 * `rdb_on_block(func_addr)` as the first call inside every
 * func_NNNNNN body, so the ring captures function entries naturally
 * — distinguishing them from intra-function labels is done via the
 * symbol table at report-print time.
 *
 * Fast path is a single write+increment, no branches.
 */
void crash_report_record_block(uint32_t block_addr);

/*
 * Load addr→name mappings from a CSV with `addr,name` rows (the
 * gen_annotations_csv.py output). Returns the number of symbols
 * loaded, or 0 on failure. Safe to call multiple times — the
 * second call replaces the first table. NULL path tolerated.
 */
int crash_report_load_symbols(const char *csv_path);

/*
 * Look up a symbol name for `addr`. Returns NULL if not found or
 * symbols haven't been loaded. Pointer is valid until the next
 * crash_report_load_symbols call (or program exit).
 */
const char *crash_report_lookup(uint32_t addr);

/*
 * Print a structured crash report to `out`. Includes register dump,
 * stack walk with symbol resolution, and the recent-func/block
 * rings. `reason` is a short string describing why we're crashing
 * (e.g. "watchdog: 10M bus accesses without yield"). Safe to call
 * even when nothing has been recorded yet.
 */
struct M68KState;
void crash_report_dump(FILE *out, const char *reason,
                       const struct M68KState *cpu,
                       uint32_t last_access_addr,
                       int last_access_is_write,
                       uint64_t frame_count);

/*
 * Same payload as crash_report_dump, but fans out to BOTH stderr AND
 * a persistent log file (build/Release/last_error.log by default,
 * appended). Pattern lifted from PokemonStadiumRecomp's diagnostic
 * surface — critical when the runner is launched headless or with
 * stderr piped to a tool that closes early (head -N, etc.). The
 * persistent log survives any pipe-closing exit and lets you
 * post-mortem the most-recent crash by just opening the file.
 */
void crash_report_dump_persistent(const char *reason,
                                  const struct M68KState *cpu,
                                  uint32_t last_access_addr,
                                  int last_access_is_write,
                                  uint64_t frame_count);

/*
 * Optional: override the persistent-log path. Default is
 * "last_error.log" in the runner's CWD. Pass NULL to disable. */
void crash_report_set_log_path(const char *path);
