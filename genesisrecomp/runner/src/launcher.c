/*
 * launcher.c — ROM discovery, CRC32 verification, and main() entry helpers.
 *
 * Responsibilities:
 *   - launcher_pick_rom()  — open file dialog if ROM not on CLI (Windows only)
 *   - launcher_load_rom()  — read ROM file into g_rom[], record rom_size
 *   - launcher_verify_crc() — compute CRC32 and compare to game_get_expected_crc32()
 *
 * The Genesis ROM is a flat binary with no header offset (unlike iNES).
 * Maximum size is 4MB ($400000 bytes).
 *
 * TODO: implement file-dialog for non-Windows platforms.
 */
#include "launcher.h"
#include "genesis_runtime.h"
#include "crc32.h"
#include "game_extras.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <commdlg.h>
#endif

/* ROM size is stored here after launch_load_rom */
static uint32_t s_rom_size = 0;
uint32_t launcher_get_rom_size(void) { return s_rom_size; }

bool launcher_pick_rom(char *out_path, int max_len) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char filename[512] = {0};
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = NULL;
    ofn.lpstrFilter     = "Genesis ROM\0*.md;*.bin;*.gen\0All Files\0*.*\0";
    ofn.lpstrFile       = filename;
    ofn.nMaxFile        = sizeof(filename);
    ofn.lpstrTitle      = "Select Genesis ROM";
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) return false;
    strncpy(out_path, filename, max_len - 1);
    out_path[max_len - 1] = '\0';
    return true;
#else
    (void)out_path; (void)max_len;
    fprintf(stderr, "launcher: file dialog not supported on this platform.\n");
    fprintf(stderr, "          Pass ROM path as command-line argument.\n");
    return false;
#endif
}

bool launcher_load_rom(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "launcher: cannot open %s\n", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > (long)sizeof(g_rom)) {
        fprintf(stderr, "launcher: ROM size %ld out of range (max %zu bytes)\n",
                size, sizeof(g_rom));
        fclose(f);
        return false;
    }

    memset(g_rom, 0xFF, sizeof(g_rom));
    if (fread(g_rom, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "launcher: read failed\n");
        fclose(f);
        return false;
    }
    fclose(f);

    s_rom_size = (uint32_t)size;
    printf("[Launcher] ROM loaded: %s (%u bytes)\n", path, s_rom_size);
    return true;
}

bool launcher_verify_crc(void) {
    uint32_t expected = game_get_expected_crc32();
    if (expected == 0) return true;  /* Skip verification */

    /* CRC32 over entire ROM */
    uint32_t actual = crc32_compute(g_rom, s_rom_size);
    if (actual != expected) {
        fprintf(stderr, "[Launcher] CRC32 mismatch: got $%08X, expected $%08X\n",
                actual, expected);
        fprintf(stderr, "[Launcher] Wrong ROM revision? Check for the correct dump.\n");
        return false;
    }
    printf("[Launcher] CRC32 OK: $%08X\n", actual);
    return true;
}
