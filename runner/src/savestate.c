/*
 * savestate.c — Save/load state for the Genesis runner.
 *
 * A save state captures the complete emulation snapshot:
 *   - g_cpu (M68KState)
 *   - g_ram (64KB)
 *   - g_z80_ram (8KB)
 *   - VDP state (VRAM, CRAM, VSRAM, registers)
 *   - YM2612 + PSG register state (best-effort)
 *   - g_frame_count
 *   - g_controller1_buttons
 *
 * File format: flat binary, prefixed with a 8-byte magic + version header.
 * Magic: "GSSR" + uint32_t version (1).
 *
 * TODO: implement the full save/load. Start with CPU + RAM.
 */
#include "savestate.h"
#include "genesis_runtime.h"
#include "vdp.h"
#include "z80.h"
#include <stdio.h>
#include <string.h>

#define SAVESTATE_MAGIC   "GSSR"
#define SAVESTATE_VERSION 1

bool savestate_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;

    fwrite(SAVESTATE_MAGIC, 1, 4, f);
    uint32_t ver = SAVESTATE_VERSION;
    fwrite(&ver, 4, 1, f);

    fwrite(&g_cpu,        sizeof(g_cpu),        1, f);
    fwrite(g_ram,         sizeof(g_ram),         1, f);
    fwrite(g_z80_ram,     sizeof(g_z80_ram),     1, f);
    fwrite(g_vdp_vram,    sizeof(g_vdp_vram),    1, f);
    fwrite(g_vdp_cram,    sizeof(g_vdp_cram),    1, f);
    fwrite(g_vdp_vsram,   sizeof(g_vdp_vsram),   1, f);
    fwrite(g_vdp_regs,    sizeof(g_vdp_regs),    1, f);
    fwrite(&g_frame_count, sizeof(g_frame_count), 1, f);

    fclose(f);
    printf("[SaveState] Saved to %s\n", path);
    return true;
}

bool savestate_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    char magic[4];
    uint32_t ver;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, SAVESTATE_MAGIC, 4) != 0) {
        fprintf(stderr, "[SaveState] Bad magic in %s\n", path);
        fclose(f);
        return false;
    }
    if (fread(&ver, 4, 1, f) != 1 || ver != SAVESTATE_VERSION) {
        fprintf(stderr, "[SaveState] Unsupported version %u in %s\n", ver, path);
        fclose(f);
        return false;
    }

    fread(&g_cpu,         sizeof(g_cpu),         1, f);
    fread(g_ram,          sizeof(g_ram),          1, f);
    fread(g_z80_ram,      sizeof(g_z80_ram),      1, f);
    fread(g_vdp_vram,     sizeof(g_vdp_vram),     1, f);
    fread(g_vdp_cram,     sizeof(g_vdp_cram),     1, f);
    fread(g_vdp_vsram,    sizeof(g_vdp_vsram),    1, f);
    fread(g_vdp_regs,     sizeof(g_vdp_regs),     1, f);
    fread(&g_frame_count, sizeof(g_frame_count),  1, f);

    fclose(f);
    printf("[SaveState] Loaded from %s\n", path);
    return true;
}
