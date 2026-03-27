/*
 * game_config.c — .cfg file parser for per-game recompiler configuration.
 *
 * Parses the game.cfg file that tells the recompiler:
 *   output_prefix <name>        — prefix for generated file names
 *   jump_table <start> <end>    — address range of an indexed jump table
 *   extra_func <hex_addr>       — additional function entry point seeds
 *   extra_func_file <path>      — file of extra_func lines (relative to cfg)
 *   symbols_file <path>         — TOML symbols file with function definitions
 *   annotations <path>          — path to the annotations CSV file
 *
 * Lines beginning with '#' are comments.
 * All addresses are in hex without 0x prefix.
 */
#include "game_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Resolve a relative path against a base file's directory */
static void resolve_path(const char *base, const char *rel, char *out, int out_size) {
    const char *slash  = strrchr(base, '/');
    const char *bslash = strrchr(base, '\\');
    const char *sep    = slash > bslash ? slash : bslash;
    if (sep) {
        int dir_len = (int)(sep - base) + 1;
        snprintf(out, out_size, "%.*s%s", dir_len, base, rel);
    } else {
        strncpy(out, rel, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

/*
 * Load function addresses from a TOML symbols file.
 *
 * We parse a minimal subset of TOML — just enough for our format:
 *   { name = "FuncName", addr = 0xNNNNNN },
 *
 * Lines starting with # are comments. We look for "addr = 0x" patterns.
 */
static int load_symbols_toml(GameConfig *cfg, const char *sym_path) {
    FILE *sf = fopen(sym_path, "r");
    if (!sf) {
        fprintf(stderr, "game_config: cannot open symbols_file '%s'\n", sym_path);
        return 0;
    }

    int loaded = 0;
    char line[512];
    while (fgets(line, sizeof(line), sf)) {
        /* Skip comments and BLACKLISTED entries */
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == '#' || *trimmed == '\n' || *trimmed == '\r') continue;

        /* Look for addr = 0xNNNNNN pattern */
        char *addr_key = strstr(trimmed, "addr");
        if (!addr_key) continue;

        /* Find the '=' after addr */
        char *eq = strchr(addr_key, '=');
        if (!eq) continue;

        /* Find the hex value */
        char *hex = strstr(eq, "0x");
        if (!hex) hex = strstr(eq, "0X");
        if (!hex) continue;

        uint32_t addr = (uint32_t)strtoul(hex, NULL, 16);
        if (addr == 0) continue;  /* skip addr 0 */

        if (cfg->extra_func_count < MAX_EXTRA_FUNCS) {
            cfg->extra_funcs[cfg->extra_func_count++] = addr;
            loaded++;
        }
    }

    fclose(sf);
    return loaded;
}

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
        else if (strcmp(key, "symbols_file") == 0 && n >= 2) {
            char sym_path[512] = {0};
            resolve_path(path, val1, sym_path, sizeof(sym_path));
            strncpy(cfg->symbols_path, sym_path, sizeof(cfg->symbols_path) - 1);
            int sym_count = load_symbols_toml(cfg, sym_path);
            printf("[GameConfig] Loaded %d function addresses from symbols_file '%s'\n",
                   sym_count, sym_path);
        }
        else if (strcmp(key, "extra_func_file") == 0 && n >= 2) {
            char ef_path[512] = {0};
            resolve_path(path, val1, ef_path, sizeof(ef_path));
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
            resolve_path(path, val1, bl_path, sizeof(bl_path));
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
