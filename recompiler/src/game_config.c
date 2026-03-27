/*
 * game_config.c — .cfg file parser for per-game recompiler configuration.
 *
 * Parses the game.cfg file that tells the recompiler:
 *   output_prefix <name>        — prefix for generated file names
 *   jump_table <start> <end>    — address range of an indexed jump table
 *   extra_func <hex_addr>       — additional function entry point seeds
 *   extra_func_file <path>      — file of extra_func lines (relative to cfg)
 *   annotations <path>          — path to the annotations CSV file
 *
 * Lines beginning with '#' are comments.
 * All addresses are in hex without 0x prefix.
 */
#include "game_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void game_config_init_empty(GameConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
}

bool game_config_load(GameConfig *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    game_config_init_empty(cfg);

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing newline */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (len == 0 || line[0] == '#') continue;

        /* Split into key and value(s) */
        char key[64] = {0};
        char val1[64] = {0}, val2[64] = {0};
        int n = sscanf(line, "%63s %63s %63s", key, val1, val2);
        if (n < 1) continue;

        if (strcmp(key, "output_prefix") == 0 && n >= 2) {
            strncpy(cfg->output_prefix, val1, sizeof(cfg->output_prefix) - 1);
        }
        else if (strcmp(key, "annotations") == 0 && n >= 2) {
            strncpy(cfg->annotations_path, val1, sizeof(cfg->annotations_path) - 1);
        }
        else if (strcmp(key, "jump_table") == 0 && n >= 3) {
            if (cfg->jump_table_count < MAX_JUMP_TABLES) {
                JumpTableEntry *e = &cfg->jump_tables[cfg->jump_table_count++];
                e->start_addr = (uint32_t)strtoul(val1, NULL, 16);
                e->end_addr   = (uint32_t)strtoul(val2, NULL, 16);
            }
        }
        else if (strcmp(key, "extra_func") == 0 && n >= 2) {
            if (cfg->extra_func_count < MAX_EXTRA_FUNCS)
                cfg->extra_funcs[cfg->extra_func_count++] =
                    (uint32_t)strtoul(val1, NULL, 16);
        }
        else if (strcmp(key, "extra_func_file") == 0 && n >= 2) {
            /* Resolve path relative to the cfg file's directory */
            char ef_path[512] = {0};
            const char *slash = strrchr(path, '/');
            const char *bslash = strrchr(path, '\\');
            const char *sep = slash > bslash ? slash : bslash;
            if (sep) {
                int dir_len = (int)(sep - path) + 1;
                snprintf(ef_path, sizeof(ef_path), "%.*s%s", dir_len, path, val1);
            } else {
                strncpy(ef_path, val1, sizeof(ef_path) - 1);
            }
            FILE *ef = fopen(ef_path, "r");
            if (!ef) {
                fprintf(stderr, "game_config: cannot open extra_func_file '%s'\n", ef_path);
            } else {
                char ef_line[256];
                while (fgets(ef_line, sizeof(ef_line), ef)) {
                    char ekey[32] = {0}, eaddr[32] = {0};
                    if (ef_line[0] == '#' || ef_line[0] == '\n') continue;
                    if (sscanf(ef_line, "%31s %31s", ekey, eaddr) == 2 &&
                        strcmp(ekey, "extra_func") == 0) {
                        if (cfg->extra_func_count < MAX_EXTRA_FUNCS)
                            cfg->extra_funcs[cfg->extra_func_count++] =
                                (uint32_t)strtoul(eaddr, NULL, 16);
                    }
                }
                fclose(ef);
            }
        }
        else if (strcmp(key, "blacklist") == 0 && n >= 2) {
            if (cfg->blacklist_count < MAX_BLACKLIST)
                cfg->blacklist[cfg->blacklist_count++] =
                    (uint32_t)strtoul(val1, NULL, 16);
        }
        else if (strcmp(key, "blacklist_file") == 0 && n >= 2) {
            /* Load blacklist from external file (one hex addr per line, # comments) */
            char bl_path[512] = {0};
            const char *slash2 = strrchr(path, '/');
            const char *bslash2 = strrchr(path, '\\');
            const char *sep2 = slash2 > bslash2 ? slash2 : bslash2;
            if (sep2) {
                int dir_len = (int)(sep2 - path) + 1;
                snprintf(bl_path, sizeof(bl_path), "%.*s%s", dir_len, path, val1);
            } else {
                strncpy(bl_path, val1, sizeof(bl_path) - 1);
            }
            FILE *bf = fopen(bl_path, "r");
            if (!bf) {
                fprintf(stderr, "game_config: cannot open blacklist_file '%s'\n", bl_path);
            } else {
                char bf_line[256];
                while (fgets(bf_line, sizeof(bf_line), bf)) {
                    if (bf_line[0] == '#' || bf_line[0] == '\n' || bf_line[0] == '\r') continue;
                    char baddr[32] = {0};
                    if (sscanf(bf_line, "%31s", baddr) == 1 && baddr[0] != '#') {
                        if (cfg->blacklist_count < MAX_BLACKLIST)
                            cfg->blacklist[cfg->blacklist_count++] =
                                (uint32_t)strtoul(baddr, NULL, 16);
                    }
                }
                fclose(bf);
                fprintf(stderr, "game_config: loaded %d blacklist entries from '%s'\n",
                        cfg->blacklist_count, bl_path);
            }
        }
        else if (strcmp(key, "vblank_yield") == 0 && n >= 2) {
            cfg->vblank_yield_addr = (uint32_t)strtoul(val1, NULL, 16);
        }
        else if (line[0] != '#') {
            /* Unknown directive — warn but continue */
            fprintf(stderr, "game_config: unknown directive '%s'\n", key);
        }
    }

    fclose(f);

    if (cfg->blacklist_count > 0)
        printf("[GameConfig] %d blacklisted addresses loaded\n", cfg->blacklist_count);

    return true;
}

bool game_config_is_blacklisted(const GameConfig *cfg, uint32_t addr) {
    for (int i = 0; i < cfg->blacklist_count; i++)
        if (cfg->blacklist[i] == addr)
            return true;
    return false;
}
