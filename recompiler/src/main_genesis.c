/*
 * main_genesis.c — GenesisRecomp entry point
 * Usage: GenesisRecomp.exe <rom.md|rom.bin> [--game <path/to/game.cfg>]
 * Output: generated/<prefix>_full.c + generated/<prefix>_dispatch.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "rom_parser.h"
#include "function_finder.h"
#include "code_generator.h"
#include "annotations.h"
#include "game_config.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,
            "Usage: GenesisRecomp <rom.md|rom.bin> [--game <path/to/game.cfg>] "
            "[--reverse-debug]\n");
        return 1;
    }

    const char *rom_path  = argv[1];
    const char *game_path = NULL;
    bool reverse_debug = false;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--game") == 0 && i+1 < argc) game_path = argv[++i];
        else if (strcmp(argv[i], "--reverse-debug") == 0) reverse_debug = true;
    }

    if (reverse_debug)
        printf("[GenesisRecomp] --reverse-debug ON: emitting sgrdb_write* "
               "hooks + g_rdb_current_func. Runner must be built with "
               "-DSONIC_REVERSE_DEBUG=ON.\n");

    printf("[GenesisRecomp] Loading ROM: %s\n", rom_path);

    /* Parse ROM */
    GenesisRom rom = {0};
    if (!rom_parse(rom_path, &rom)) {
        fprintf(stderr, "[GenesisRecomp] Failed to parse ROM\n");
        return 1;
    }
    printf("[GenesisRecomp] ROM: %u bytes, \"%s\"\n", rom.rom_size, rom.domestic_name);
    printf("[GenesisRecomp] Vectors: RESET_SSP=$%08X  RESET_PC=$%08X\n",
           rom.initial_sp, rom.initial_pc);
    printf("[GenesisRecomp] Checksum: $%04X (header), $%04X (computed)\n",
           rom.header_checksum, rom.computed_checksum);

    /* Load game config */
    GameConfig cfg = {0};
    if (game_path) {
        if (game_config_load(&cfg, game_path))
            printf("[GenesisRecomp] Game config: %s  (prefix='%s', %d jump tables, "
                   "%d extra funcs)\n",
                   game_path, cfg.output_prefix,
                   cfg.jump_table_count, cfg.extra_func_count);
        else
            fprintf(stderr, "[GenesisRecomp] Warning: could not load game config '%s'\n", game_path);
    } else {
        game_config_init_empty(&cfg);
        printf("[GenesisRecomp] No --game config; using empty dispatch tables\n");
    }

    /* Determine output prefix */
    char output_prefix[128];
    if (cfg.output_prefix[0]) {
        snprintf(output_prefix, sizeof(output_prefix), "%s", cfg.output_prefix);
    } else {
        const char *base = rom_path;
        const char *s = rom_path;
        while (*s) { if (*s == '/' || *s == '\\') base = s+1; s++; }
        size_t len = strlen(base);
        const char *dot = strrchr(base, '.');
        if (dot) len = (size_t)(dot - base);
        if (len >= sizeof(output_prefix)) len = sizeof(output_prefix) - 1;
        memcpy(output_prefix, base, len);
        output_prefix[len] = '\0';
        for (char *p = output_prefix; *p; p++) if (*p == ' ') *p = '_';
    }

    /* Load annotations */
    AnnotationTable at = {0};
    {
        char ann_path[512];
        if (cfg.annotations_path[0]) {
            snprintf(ann_path, sizeof(ann_path), "%s", cfg.annotations_path);
        } else {
            const char *dot = strrchr(rom_path, '.');
            if (dot) {
                size_t n = (size_t)(dot - rom_path);
                if (n >= sizeof(ann_path) - 20) n = sizeof(ann_path) - 20;
                memcpy(ann_path, rom_path, n);
                strcpy(ann_path + n, "_annotations.csv");
            } else {
                snprintf(ann_path, sizeof(ann_path), "%s_annotations.csv", rom_path);
            }
        }
        if (annotations_load(&at, ann_path))
            printf("[GenesisRecomp] Annotations: %d entries from %s\n", at.count, ann_path);
    }

    /* Find all functions via BSR/JSR/RTS graph walk */
    static FunctionList funcs = {0};
    function_finder_run(&rom, &funcs, &cfg);
    printf("[GenesisRecomp] Found %d functions\n", funcs.count);

    /* Emit C */
    char out_full[256], out_dispatch[256];
    snprintf(out_full,     sizeof(out_full),     "generated/%s_full.c",     output_prefix);
    snprintf(out_dispatch, sizeof(out_dispatch), "generated/%s_dispatch.c", output_prefix);

    if (!codegen_emit(&rom, &funcs, out_full, out_dispatch, &at, &cfg, reverse_debug)) {
        fprintf(stderr, "[GenesisRecomp] Code generation failed\n");
        rom_free(&rom);
        function_list_free(&funcs);
        return 1;
    }

    printf("[GenesisRecomp] Done. Output:\n  %s\n  %s\n", out_full, out_dispatch);

    rom_free(&rom);
    function_list_free(&funcs);
    annotations_free(&at);
    return 0;
}
