/*
 * game_config.h — .cfg file parser interface.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MAX_JUMP_TABLES  256
#define MAX_EXTRA_FUNCS  8192

typedef struct {
    uint32_t start_addr;
    uint32_t end_addr;
} JumpTableEntry;

typedef struct {
    char           output_prefix[64];
    char           annotations_path[256];
    JumpTableEntry jump_tables[MAX_JUMP_TABLES];
    int            jump_table_count;
    uint32_t       extra_funcs[MAX_EXTRA_FUNCS];
    int            extra_func_count;
    uint32_t       vblank_yield_addr;   /* 0 = not set; emit glue_yield_for_vblank() for this function */
} GameConfig;

void game_config_init_empty(GameConfig *cfg);
bool game_config_load(GameConfig *cfg, const char *path);
