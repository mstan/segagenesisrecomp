/*
 * runtime_evidence.h — persistent, build-stamped runtime evidence files.
 *
 * The runner records addresses the static recompile did not cover as it
 * meets them at runtime. Each kind lives in its own TOML file next to the
 * executable:
 *
 *   dispatch_misses.toml        [functions].extra  computed-dispatch targets
 *                                                 with no generated function
 *   floor_coverage.toml         [functions].extra  code the tier-3 floor ran
 *   interior_label_misses.toml  [interior_labels]  misses INSIDE a function —
 *                                                 codegen gaps, never seeds
 *   floor_unsafe.toml           [floor_unsafe]     misses the floor declined
 *
 * The two [functions].extra files stay directly loadable as GameConfig
 * discovery files; the other two use tables the recompiler ignores, so they
 * can never leak into the seed set.
 *
 * Lifecycle: a file accumulates across every launch of the SAME build (same
 * executable bytes, same game, same ROM image). At start-up the runner reads
 * the file back and seeds its in-memory set; a file written by a different
 * build is moved to <name>.prev.toml and a fresh one started, because another
 * build's addresses mislead a regen. When several game executables share one
 * directory, another game's file is parked as <name>.<game>.toml and restored
 * when that game runs again.
 *
 * Metadata lives in TOML comments (game, build id, sessions, frames emulated,
 * first/last update) so an empty list is still meaningful: "12 sessions,
 * 480000 frames, 0 entries". Every entry carries a trailing comment with the
 * session and frame it was first seen in plus whatever context the caller
 * had. Files are replaced atomically (temp file + rename), so killing the
 * game never leaves a torn file.
 *
 * Cost: lookups are hashed; a file is rewritten only when a NEW address is
 * recorded, and session counters are flushed at most once a wall-clock
 * minute (checked every 600 frames) and at exit.
 */
#ifndef GENESIS_RUNTIME_EVIDENCE_H
#define GENESIS_RUNTIME_EVIDENCE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RT_EVIDENCE_DISPATCH_MISS = 0,
    RT_EVIDENCE_FLOOR_COVERAGE,
    RT_EVIDENCE_INTERIOR_LABEL,
    RT_EVIDENCE_FLOOR_UNSAFE,
    RT_EVIDENCE_KIND_COUNT
} RuntimeEvidenceKind;

/* Start the next launch with empty evidence (the old files are rotated to
 * <name>.prev.toml). Call before runtime_evidence_init. Also enabled by the
 * environment variable GENESIS_EVIDENCE_FRESH=1. */
void runtime_evidence_request_fresh(void);

/* Load (or rotate) every evidence file, count this launch as a new session,
 * and write the files back with the updated header. Idempotent. */
void runtime_evidence_init(const char *game_short_name,
                           const char *game_display_name,
                           const uint8_t *rom, size_t rom_len);

/* Record an address. Returns 1 when it is new to this build's evidence (the
 * kind is then marked dirty), 0 when already known. note may be NULL; commas
 * and brackets in it are replaced so line-oriented readers stay simple. */
int runtime_evidence_add(RuntimeEvidenceKind kind, uint32_t addr,
                         uint64_t frame, const char *note);

/* 1 when addr is already recorded for this build (one hash probe). */
int runtime_evidence_has(RuntimeEvidenceKind kind, uint32_t addr);

/* Rewrite the kind's file if anything new was added since the last write. */
void runtime_evidence_sync(RuntimeEvidenceKind kind);

/* Number of addresses recorded for this build (all sessions). */
int runtime_evidence_count(RuntimeEvidenceKind kind);

/* Once per emulated frame: counts frames for the header and periodically
 * flushes the counters (bounded wall-clock rate, no per-frame I/O). */
void runtime_evidence_tick(void);

/* Rewrite every file with current counters. Registered with atexit() too. */
void runtime_evidence_flush(void);

/* FNV-1a 32 of the running executable module's bytes — the exact build
 * identity (also the netplay build fingerprint). Never 0. */
uint32_t genesis_build_exe_fingerprint(void);

/* Human-readable build description: game version + git, engine git, compile
 * time and configuration (from the generated genesis_build_info.h). */
const char *genesis_build_info_string(void);

#ifdef __cplusplus
}
#endif

#endif /* GENESIS_RUNTIME_EVIDENCE_H */
