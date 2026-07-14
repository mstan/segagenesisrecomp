/*
 * main_genesis.c — GenesisRecomp entry point
 * Usage: GenesisRecomp.exe <rom.md|rom.bin> [--game <path/to/game.toml>]
 *        [--output-dir <directory>]
 * Output: <output-dir>/<prefix>_full.c + <output-dir>/<prefix>_dispatch.c
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "rom_parser.h"
#include "function_finder.h"
#include "code_generator.h"
#include "codegen_diag.h"
#include "annotations.h"
#include "game_config.h"
#include "cycle_probe.h"

static bool ensure_output_directory(const char *path) {
    struct stat info;
    if (!path || !path[0]) return false;
    if (stat(path, &info) == 0)
        return (info.st_mode & S_IFDIR) != 0;
#ifdef _WIN32
    if (_mkdir(path) == 0) return true;
#else
    if (mkdir(path, 0777) == 0) return true;
#endif
    return errno == EEXIST && stat(path, &info) == 0 &&
           (info.st_mode & S_IFDIR) != 0;
}

static bool make_output_path(char *dst, size_t dst_size,
                             const char *directory, const char *prefix,
                             const char *suffix) {
    int written = snprintf(dst, dst_size, "%s/%s_%s.c",
                           directory, prefix, suffix);
    return written >= 0 && (size_t)written < dst_size;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,
            "Usage: GenesisRecomp <rom.md|rom.bin> [--game <path/to/game.toml>] "
            "[--output-dir <directory>] [--reverse-debug] "
            "[--fail-on-unsupported]\n");
        return 1;
    }

    const char *rom_path  = argv[1];
    const char *game_path = NULL;
    const char *output_dir = "generated";
    bool reverse_debug = false;
    bool fail_on_unsupported = false;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--game") == 0 && i+1 < argc) game_path = argv[++i];
        else if (strcmp(argv[i], "--output-dir") == 0 && i+1 < argc)
            output_dir = argv[++i];
        else if (strcmp(argv[i], "--reverse-debug") == 0) reverse_debug = true;
        else if (strcmp(argv[i], "--fail-on-unsupported") == 0) fail_on_unsupported = true;
        else if (strcmp(argv[i], "--dump-functions") == 0 && i+1 < argc)
            codegen_set_dump_functions_path(argv[++i]);
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

    /* Initialise the clown68000 cycle probe so code_generator can measure
     * per-instruction cycle costs directly from the oracle. */
    if (cycle_probe_init(&rom) == 0)
        printf("[GenesisRecomp] Cycle probe armed (clown68000 linked)\n");
    else
        fprintf(stderr, "[GenesisRecomp] WARNING: cycle probe init failed; "
                        "falling back to PRM estimates\n");
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

    if (!ensure_output_directory(output_dir)) {
        fprintf(stderr, "[GenesisRecomp] Could not create output directory '%s'\n",
                output_dir);
        rom_free(&rom);
        annotations_free(&at);
        return 1;
    }

    /* Find all functions via BSR/JSR/RTS graph walk */
    static FunctionList funcs = {0};
    function_finder_run(&rom, &funcs, &cfg, output_dir);
    printf("[GenesisRecomp] Found %d functions\n", funcs.count);

    /* Emit C */
    char out_full[1024], out_dispatch[1024], out_layout[1024], out_cycles[1024];
    if (!make_output_path(out_full, sizeof(out_full), output_dir, output_prefix, "full") ||
        !make_output_path(out_dispatch, sizeof(out_dispatch), output_dir, output_prefix, "dispatch") ||
        !make_output_path(out_layout, sizeof(out_layout), output_dir, output_prefix, "layout") ||
        !make_output_path(out_cycles, sizeof(out_cycles), output_dir, output_prefix, "cycles")) {
        fprintf(stderr, "[GenesisRecomp] Output path is too long\n");
        rom_free(&rom);
        function_list_free(&funcs);
        return 1;
    }

    if (!codegen_emit(&rom, &funcs, out_full, out_dispatch, &at, &cfg, reverse_debug)) {
        fprintf(stderr, "[GenesisRecomp] Code generation failed\n");
        rom_free(&rom);
        function_list_free(&funcs);
        return 1;
    }

    /* Emit the per-instruction cycle-cost table (populated during codegen) so
     * the runtime interpreter / Tier-3 floor charge the SAME costs the
     * recompiled code does. See emit_insn_cost_table in code_generator.c. */
    { extern void emit_insn_cost_table(FILE *);
      FILE *cf = fopen(out_cycles, "w");
      if (cf) { emit_insn_cost_table(cf); fclose(cf);
                printf("[GenesisRecomp] Emitted %s\n", out_cycles); }
      else fprintf(stderr, "[GenesisRecomp] WARN: could not write %s\n", out_cycles); }

    /* Emit per-game RAM layout TU. Hard build failure if the game.toml
     * has no [ram_layout] — shared runner code now reads g_game_layout
     * unconditionally and would crash without it. */
    if (!game_config_emit_layout(&cfg, out_layout)) {
        rom_free(&rom);
        function_list_free(&funcs);
        return 1;
    }

    printf("[GenesisRecomp] Done. Output:\n  %s\n  %s\n  %s\n",
           out_full, out_dispatch, out_layout);

    /* Always print the unsupported summary so coverage misses are visible
     * without grepping the generated C. --fail-on-unsupported turns any
     * non-zero count into a hard build failure. */
    codegen_diag_print_summary(stderr);
    int diag_total = codegen_diag_total();

    cycle_probe_shutdown();
    rom_free(&rom);
    function_list_free(&funcs);
    annotations_free(&at);

    if (fail_on_unsupported && diag_total > 0) {
        fprintf(stderr, "[GenesisRecomp] --fail-on-unsupported: %d unsupported "
                        "events; failing build.\n", diag_total);
        return 2;
    }
    return 0;
}
