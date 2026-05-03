/*
 * game_config.h — .cfg file parser interface.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MAX_JUMP_TABLES      256
#define MAX_EXTRA_FUNCS      8192
#define MAX_BLACKLIST        1024
#define MAX_PROTECTED_RANGES 16

typedef struct {
    uint32_t start_addr;
    uint32_t end_addr;
} JumpTableEntry;

typedef struct {
    char           output_prefix[64];
    char           annotations_path[256];
    char           symbols_path[256];       /* TOML symbols file (replaces extra_func) */
    JumpTableEntry jump_tables[MAX_JUMP_TABLES];
    int            jump_table_count;
    uint32_t       extra_funcs[MAX_EXTRA_FUNCS];
    int            extra_func_count;
    uint32_t       blacklist[MAX_BLACKLIST];
    int            blacklist_count;
    uint32_t       vblank_yield_addr;   /* 0 = not set; emit glue_yield_for_vblank() for this function */
    struct { uint32_t lo; uint32_t hi; } protected_ranges[MAX_PROTECTED_RANGES];
    int            protected_range_count;
    /* When true, the validator tolerates Bcc/BSR/BRA forms that use
     * a 32-bit displacement (d8 == 0xFF). Those are 68020+ extensions
     * and out of scope for MC68000-only Genesis ROMs unless a game
     * specifically opts in. Default: false. */
    bool           allow_68020_branch;
} GameConfig;

/* Returns true if addr falls in a protected range (no boundary splitting) */
bool game_config_is_protected(const GameConfig *cfg, uint32_t addr);

/* Returns true if addr is in the blacklist */
bool game_config_is_blacklisted(const GameConfig *cfg, uint32_t addr);

void game_config_init_empty(GameConfig *cfg);
bool game_config_load(GameConfig *cfg, const char *path);
