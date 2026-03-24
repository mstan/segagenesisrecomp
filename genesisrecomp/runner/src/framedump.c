/*
 * framedump.c — Per-frame state snapshot and regression detection.
 * See framedump.h for behavior description.
 */
#include "framedump.h"
#include "genesis_runtime.h"
#include "vdp.h"
#include "crc32.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
static void ensure_dir(void) { _mkdir(FRAMEDUMP_DIR); }
#else
#include <sys/stat.h>
static void ensure_dir(void) { mkdir(FRAMEDUMP_DIR, 0755); }
#endif

static int s_active       = 1;
static int s_dir_created  = 0;

int framedump_is_active(void) { return s_active; }

/* ---- Helpers ---- */

static void make_path(char *buf, size_t sz, uint64_t frame) {
    snprintf(buf, sz, "%s/frame_%04llu.json",
             FRAMEDUMP_DIR, (unsigned long long)frame);
}

/* Delete frame files from start_frame up to FRAMEDUMP_MAX_FRAMES. */
static void delete_from(uint64_t start_frame) {
    char path[128];
    for (uint64_t f = start_frame; f < FRAMEDUMP_MAX_FRAMES; f++) {
        make_path(path, sizeof(path), f);
        remove(path);
    }
}

/* ---- Hash bundle ---- */

typedef struct {
    uint32_t ram;
    uint32_t vram;
    uint32_t cram;
    uint32_t vsram;
} Hashes;

static Hashes compute_hashes(void) {
    Hashes h;
    h.ram   = crc32_compute(g_ram,                    sizeof(g_ram));
    h.vram  = crc32_compute(g_vdp_vram,               VDP_VRAM_SIZE);
    h.cram  = crc32_compute((const uint8_t*)g_vdp_cram,  VDP_CRAM_SIZE);
    h.vsram = crc32_compute((const uint8_t*)g_vdp_vsram, VDP_VSRAM_SIZE);
    return h;
}

/* Read the four CRC32 hashes from an existing JSON file.
 * Returns 1 on success (all four found), 0 on failure. */
static int read_hashes(const char *path, Hashes *out) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        uint32_t v;
        if (sscanf(line, " \"ram_crc32\": \"0x%X\"",   &v) == 1) { out->ram   = v; found |= 1; }
        if (sscanf(line, " \"vram_crc32\": \"0x%X\"",  &v) == 1) { out->vram  = v; found |= 2; }
        if (sscanf(line, " \"cram_crc32\": \"0x%X\"",  &v) == 1) { out->cram  = v; found |= 4; }
        if (sscanf(line, " \"vsram_crc32\": \"0x%X\"", &v) == 1) { out->vsram = v; found |= 8; }
    }
    fclose(f);
    return (found == 15);
}

/* Write a full frame JSON file. */
static void write_frame(const char *path, uint64_t frame, const Hashes *h) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[FRAMEDUMP] Cannot open %s for write\n", path);
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"frame\": %llu,\n", (unsigned long long)frame);
    fprintf(f, "  \"ram_crc32\":   \"0x%08X\",\n", h->ram);
    fprintf(f, "  \"vram_crc32\":  \"0x%08X\",\n", h->vram);
    fprintf(f, "  \"cram_crc32\":  \"0x%08X\",\n", h->cram);
    fprintf(f, "  \"vsram_crc32\": \"0x%08X\",\n", h->vsram);

    fprintf(f, "  \"vdp_regs\": [");
    for (int i = 0; i < VDP_REG_COUNT; i++)
        fprintf(f, "%d%s", g_vdp_regs[i], i < VDP_REG_COUNT - 1 ? ", " : "");
    fprintf(f, "],\n");

    fprintf(f, "  \"cpu_D\": [");
    for (int i = 0; i < 8; i++)
        fprintf(f, "\"0x%08X\"%s", g_cpu.D[i], i < 7 ? ", " : "");
    fprintf(f, "],\n");

    fprintf(f, "  \"cpu_A\": [");
    for (int i = 0; i < 8; i++)
        fprintf(f, "\"0x%08X\"%s", g_cpu.A[i], i < 7 ? ", " : "");
    fprintf(f, "],\n");

    fprintf(f, "  \"cpu_SR\": \"0x%04X\",\n", g_cpu.SR);
    fprintf(f, "  \"cpu_PC\": \"0x%06X\"\n",  g_cpu.PC);
    fprintf(f, "}\n");

    fclose(f);
}

/* ---- Public API ---- */

int framedump_on_frame(void) {
    if (!s_active) return 0;

    uint64_t frame = g_frame_count;
    if (frame >= FRAMEDUMP_MAX_FRAMES) {
        s_active = 0;
        return 0;
    }

    if (!s_dir_created) {
        ensure_dir();
        s_dir_created = 1;
    }

    char path[128];
    make_path(path, sizeof(path), frame);

    Hashes current = compute_hashes();

    /* Check if golden file exists */
    FILE *probe = fopen(path, "r");
    if (!probe) {
        /* New frame — write and continue */
        write_frame(path, frame, &current);
        fprintf(stderr, "[FRAMEDUMP] Wrote frame %llu\n", (unsigned long long)frame);
        return 1;
    }
    fclose(probe);

    Hashes stored;
    if (!read_hashes(path, &stored)) {
        /* Corrupt/unreadable — overwrite */
        write_frame(path, frame, &current);
        return 1;
    }

    /* Compare */
    int match = (stored.ram   == current.ram  &&
                 stored.vram  == current.vram &&
                 stored.cram  == current.cram &&
                 stored.vsram == current.vsram);

    if (match) return 1;  /* Golden matches — no write needed */

    /* Regression detected */
    fprintf(stderr, "[FRAMEDUMP] *** REGRESSION at frame %llu ***\n",
            (unsigned long long)frame);
    if (stored.ram   != current.ram)
        fprintf(stderr, "[FRAMEDUMP]   RAM:   golden=0x%08X  now=0x%08X\n",
                stored.ram, current.ram);
    if (stored.vram  != current.vram)
        fprintf(stderr, "[FRAMEDUMP]   VRAM:  golden=0x%08X  now=0x%08X\n",
                stored.vram, current.vram);
    if (stored.cram  != current.cram)
        fprintf(stderr, "[FRAMEDUMP]   CRAM:  golden=0x%08X  now=0x%08X\n",
                stored.cram, current.cram);
    if (stored.vsram != current.vsram)
        fprintf(stderr, "[FRAMEDUMP]   VSRAM: golden=0x%08X  now=0x%08X\n",
                stored.vsram, current.vsram);

    delete_from(frame);
    write_frame(path, frame, &current);
    s_active = 0;
    return 0;
}
