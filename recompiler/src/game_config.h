/*
 * game_config.h — game.toml parser interface.
 *
 * The legacy whitespace `.cfg` format has been retired; see
 * game_config.c for the TOML schema and how `discovery_files`
 * recursively merge auto-generated address tables into this struct.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Encoding of each entry in a static jump table.
 *
 * JT_FMT_ABS_L   : 32-bit absolute target per entry (stride defaults 4).
 * JT_FMT_PCREL_W : 16-bit signed offset added to base_addr (stride 2);
 *                  this is the common Sega pattern emitted by
 *                  `JMP <table>(PC,Dn.W)` where the assembler assembles
 *                  `dc.w  (target - <table>)` rows.
 * JT_FMT_BRA_W   : 4-byte bra.w trampolines per entry (stride 4). The
 *                  JMP lands ON the trampoline body itself; each entry
 *                  IS a function entry at base + i*stride.
 * JT_FMT_BRA_S   : 2-byte bra.s trampolines per entry (stride 2). Same
 *                  semantics as BRA_W, narrower instruction.
 */
typedef enum {
    JT_FMT_ABS_L  = 0,
    JT_FMT_PCREL_W = 1,
    JT_FMT_BRA_W  = 2,
    JT_FMT_BRA_S  = 3,
} JumpTableFormat;

typedef struct {
    uint32_t        start_addr;     /* table base                          */
    uint32_t        end_addr;       /* exclusive end (start + stride*N)    */
    uint32_t        stride_bytes;   /* 2 or 4 typically                    */
    JumpTableFormat format;
} JumpTableEntry;

typedef struct { uint32_t lo; uint32_t hi; } ProtectedRange;

/*
 * Per-game RAM layout, parsed from the [ram_layout] table of game.toml.
 * The recompiler reads these here, then emits <prefix>_layout.c that
 * declares the runtime struct (GameRamLayout in runner/game_layout.h)
 * and instantiates `g_game_layout` with the same values.
 *
 * `present` is true when [ram_layout] was found in the source TOML.
 * The recompiler refuses to emit a layout TU for a config without it
 * — partial migration is not allowed; once shared runner code reads
 * g_game_layout, every game must populate the table.
 */
#define GAMECFG_LEVEL_MODES_MAX 16

typedef struct {
    bool     present;
    uint32_t game_mode_addr;
    uint32_t vint_runcount_addr;
    uint32_t vint_routine_addr;
    uint32_t plc_pending_addr;     /* 0 = no PLC system */
    uint32_t initial_ssp;
    uint32_t vbla_stack;
    uint32_t intr_stack;
    uint32_t player_object_addr;
    uint8_t  level_modes[GAMECFG_LEVEL_MODES_MAX];
    int      level_mode_count;
} GameRamLayoutCfg;

/*
 * GameConfig — every list grows on demand. After game_config_load,
 * the arrays are owned by the cfg; call game_config_free to release
 * them (or just leak them at process exit, which is what the CLI
 * does today). Counts are how many entries are populated; capacities
 * are the current allocation size and are an internal detail.
 */
typedef struct {
    char           output_prefix[64];
    char           annotations_path[256];
    char           symbols_path[256];       /* TOML symbols file (replaces extra_func) */
    JumpTableEntry *jump_tables;
    int            jump_table_count;
    int            jump_table_cap;
    uint32_t       *extra_funcs;
    int            extra_func_count;
    int            extra_func_cap;
    /* Additional INTERIOR PCs to seed scan_function's CFG-walk worklist.
     * Sourced from disasm local labels (asm68k `.foo` scoped under a
     * parent global label). NOT promoted to function entries — they're
     * just hints so the CFG walker discovers PCs that are reached only
     * by `JMP (PC,Dn.W)` (e.g. the Sonic 2 CPZ Duff's-device pattern). */
    uint32_t       *extra_seeds;
    int            extra_seed_count;
    int            extra_seed_cap;
    uint32_t       *blacklist;
    int            blacklist_count;
    int            blacklist_cap;
    /* Disasm "is this address code?" oracle (instruction-start set), loaded
     * from game.toml `code_addrs_file` (one hex addr per line, e.g. produced
     * by tests/tools/gen_code_addrs.py). When non-empty, boundary-split /
     * dispatch-seed promotion is gated on membership: an extern target that
     * lands on a known DATA address is never promoted to a function entry,
     * killing the data-as-code false-positive class. Empty => no gating
     * (default; preserves prior behavior for games without the file).
     * Kept SORTED so game_config_is_known_code can binary-search. */
    uint32_t       *code_addrs;
    int            code_addr_count;
    int            code_addr_cap;
    uint32_t       vblank_yield_addr;   /* 0 = not set; emit glue_yield_for_vblank() for this function */
    ProtectedRange *protected_ranges;
    int            protected_range_count;
    int            protected_range_cap;
    /* When true, the validator tolerates Bcc/BSR/BRA forms that use
     * a 32-bit displacement (d8 == 0xFF). Those are 68020+ extensions
     * and out of scope for MC68000-only Genesis ROMs unless a game
     * specifically opts in. Default: false. */
    bool           allow_68020_branch;
    /* When true, function_finder auto-walks PC-indexed JMP tables
     * with no matching jump_table directive in this config. The
     * walk is conservative (validator gate per entry, max 256
     * entries) but can still mis-identify random ROM data as code
     * for some games — keep off unless you've audited the ROM.
     * Manual jump_table directives are always honored regardless of
     * this flag. Default: false. */
    bool           jump_table_autodiscovery;
    GameRamLayoutCfg ram_layout;
} GameConfig;

/* Returns true if addr falls in a protected range (no boundary splitting) */
bool game_config_is_protected(const GameConfig *cfg, uint32_t addr);

/* Returns true if addr is in the blacklist */
bool game_config_is_blacklisted(const GameConfig *cfg, uint32_t addr);

/* Returns true if addr is a known instruction-start per the disasm code-addr
 * oracle. If no code_addrs_file was loaded (code_addr_count == 0) this returns
 * true for every address (gating disabled — prior behavior). */
bool game_config_is_known_code(const GameConfig *cfg, uint32_t addr);

void game_config_init_empty(GameConfig *cfg);
void game_config_free(GameConfig *cfg);
bool game_config_load(GameConfig *cfg, const char *path);

/* Emit <prefix>_layout.c — defines `const GameRamLayout g_game_layout`
 * populated from cfg->ram_layout. Returns false if [ram_layout] was
 * absent in the source TOML; the caller decides whether to treat that
 * as a build failure. The output_path argument is the full file path
 * (typically generated/<prefix>_layout.c). */
bool game_config_emit_layout(const GameConfig *cfg, const char *output_path);
