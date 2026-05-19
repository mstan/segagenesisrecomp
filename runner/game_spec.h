/*
 * game_spec.h — single source of truth for everything the runner
 * needs to know about a specific game ROM.
 *
 * The runner is game-agnostic. Each game project provides exactly one
 * translation unit that defines `const GameSpec g_game_spec`, and the
 * runner reads from that struct rather than hard-coding func_NNNNNN
 * calls, expected CRCs, RAM offsets, or per-game native overrides.
 *
 * Design notes:
 *
 *   - This struct supersedes the legacy free-function hooks in
 *     game_extras.h (game_fill_frame_record, game_handle_debug_cmd,
 *     game_extras_name) and the per-call externs in glue.c
 *     (func_000206, func_000B10, func_001126, etc.). Existing code
 *     continues to use the free-function form during the migration;
 *     once all call sites read g_game_spec, the legacy header can
 *     be removed.
 *
 *   - Function-pointer fields are NULL when the game doesn't need
 *     that hook. The runner checks before dispatching so a minimal
 *     spec can boot with just entry/vblank/hblank populated.
 *
 *   - Debug-cmd handlers receive raw JSON strings, mirroring the
 *     existing game_handle_debug_cmd contract — no cJSON dependency.
 *
 *   - The hybrid table pointer is consulted only by the oracle
 *     build; native builds typically run with size=0. Keeping it in
 *     the spec means each game's verified-clean function list is
 *     declared next to the rest of its identity, not in a separate
 *     hybrid_table.c that has to be rebuilt per game.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Forward decl from hybrid.h to avoid pulling in clownmdemu headers. */
typedef struct HybridEntry HybridEntry;

/*
 * One per-game TCP debug command. The handler receives the request id
 * (echo it back in responses) and the raw JSON line. Send replies via
 * cmd_send_response / cmd_send_err declared in cmd_server.h.
 */
typedef struct {
    const char *name;       /* matched against the request "cmd" field */
    void      (*handler)(int id, const char *json_line);
} GameDebugCommand;

typedef struct GameSpec {
    /* ---- Identity ---- */
    const char *display_name;        /* "Sonic the Hedgehog" — window title */
    const char *short_name;          /* "Sonic1" — shows up in info / ping */

    /* Expected CRC32 of the raw ROM (0 = skip verification). The
     * launcher checks this before starting the game and re-prompts
     * the user if the file doesn't match. */
    uint32_t    expected_rom_crc32;

    /* Expected ROM size in bytes (0 = skip the check). Sonic 1 is
     * 0x80000, Sonic 2 is 0x100000, Sonic 3K is 0x400000. */
    uint32_t    expected_rom_size;

    /* ---- Entry points (recompiled C) ---- */
    /* Called once on the game thread. Must contain the game's main
     * loop and not return — typically wraps func_NNNNNN(). NULL means
     * the runner should refuse to start a native build (oracle builds
     * don't need this). */
    void      (*call_entry_point)(void);

    /* IRQ6 / IRQ4 handlers, called from glue.c during VBlank/HBlank
     * service. NULL = no handler (the runner just sets the flag). */
    void      (*call_vblank)(void);
    void      (*call_hblank)(void);

    /* Save-state resume loop PC. Native save-state loads restart the
     * host game fiber here after restoring RAM/CPU state, because the
     * host C stack itself is not portable save-state data. This should
     * be a stable post-init per-frame loop for the state shape normally
     * saved by the game. 0 preserves the old fiber continuation
     * behavior. */
    uint32_t    resume_main_loop_pc;

    /* Outer game-mode dispatcher PC. If the save-state resume loop
     * returns because the game changed modes, the restarted host fiber
     * continues here so normal mode dispatch takes over. */
    uint32_t    dispatch_main_loop_pc;

    /* Optional per-yield periodic. Sonic 1 calls a UpdateMusic-like
     * routine (func_001642) while the game thread is parked at
     * WaitForVBlank. NULL = nothing to do. */
    void      (*call_periodic)(void);

    /* ---- Lifecycle hooks ---- */
    /* Called once after runtime_init() and ROM load, before the game
     * thread starts. Use for RAM seeding (e.g., Sonic 1 sets
     * g_ram[0xF009] = 0x80 so UpdateMusic exits cleanly given the
     * Z80 stub never runs the SMPS init). NULL = nothing. */
    void      (*on_post_reset)(void);

    /* Called every VBlank, before/after the VBlank handler. The
     * frame counter is the runner's internal frame number, not the
     * game's. NULL = nothing. */
    void      (*on_frame_pre)(uint64_t frame_count);
    void      (*on_frame_post)(uint64_t frame_count);

    /* Called every HBlank with the line number. Almost always NULL —
     * games that need per-line state should poke their own state
     * inside call_hblank instead. */
    void      (*on_hblank)(uint32_t line);

    /* ---- CLI args ---- */
    /* Called once per unrecognized argv pair during startup. Return 1
     * if the key was consumed (val too if applicable), 0 to let the
     * runner report it as unknown. NULL = no game-specific args. */
    int       (*handle_arg)(const char *key, const char *val);

    /* One-line help text appended to --help output, or NULL. */
    const char *arg_usage;

    /* ---- Dispatch override ---- */
    /* Called when call_by_address has no entry for an addr. Return 1
     * if the game handled it, 0 to fall through to the dispatch-miss
     * log. NULL = always fall through. */
    int       (*dispatch_override)(uint32_t addr);

    /* ---- Frame-record packing (debug ring buffer) ---- */
    /* Pack game-specific telemetry into the 256-byte tail of each
     * FrameRecord. Called from cmd_server_record_frame() once per
     * frame. Must not block, must not allocate. NULL = leave it
     * zeroed (frame_range will return empty game_data views). */
    void      (*fill_frame_record)(uint8_t game_data[256]);

    /* Version stamp for the layout fill_frame_record writes. Bump
     * when the per-game struct shape changes so consumers can detect
     * stale captures. */
    uint16_t    frame_record_version;

    /* ---- Debug-server commands ---- */
    /* Per-game TCP commands. The runner first checks built-in
     * commands (read_mem, frame_range, etc.); if no built-in matches
     * it walks this array. Set commands=NULL / command_count=0 if
     * the game has no extras. */
    const GameDebugCommand *commands;
    int                     command_count;

    /* ---- Hybrid dispatch (oracle build only) ---- */
    /* Verified-clean native overrides. Native builds typically run
     * with size=0 (everything stays as recompiled C); oracle builds
     * use this to swap interpreter execution for native execution
     * one function at a time during divergence hunting. NULL+0 is
     * legal and means "no overrides". */
    const HybridEntry *hybrid_table;
    int                hybrid_table_size;
} GameSpec;

/*
 * The single per-game spec instance. Each game project defines this
 * exactly once (sonicthehedgehog/sonic1_spec.c, sonicthehedgehog2/
 * sonic2_spec.c, etc.). The runner picks it up at link time.
 */
extern const GameSpec g_game_spec;
