/*
 * rom_parser.c — Genesis/Mega Drive ROM header parsing.
 *
 * Genesis ROMs begin with a 512-byte system header at $000000.
 * The header contains the initial SSP (supervisor stack pointer),
 * initial PC, interrupt vector table, and ROM metadata strings.
 *
 * No bank switching: the entire ROM is flat in the 68K address space
 * starting at $000000, up to 4MB for standard cartridges.
 */
#include "rom_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Genesis header field offsets */
#define HDR_SYSTEM_TYPE    0x100   /* "SEGA MEGA DRIVE" or "SEGA GENESIS" */
#define HDR_COPYRIGHT      0x110   /* "(C)SEGA ..." */
#define HDR_DOMESTIC_NAME  0x120   /* Domestic (Japanese) game name, 48 bytes */
#define HDR_OVERSEAS_NAME  0x150   /* Overseas game name, 48 bytes */
#define HDR_SERIAL         0x180   /* Serial/revision, 14 bytes */
#define HDR_CHECKSUM       0x18E   /* 16-bit checksum */
#define HDR_IO_SUPPORT     0x190   /* I/O device support, 16 bytes */
#define HDR_ROM_START      0x1A0   /* ROM start address (big-endian 32-bit) */
#define HDR_ROM_END        0x1A4   /* ROM end address (big-endian 32-bit) */
#define HDR_RAM_START      0x1A8   /* RAM start address */
#define HDR_RAM_END        0x1AC   /* RAM end address */
#define HDR_MODEM_SUPPORT  0x1B0   /* Modem support string */
#define HDR_REGION         0x1F0   /* Region codes: J/U/E */

bool rom_parse(const char *path, GenesisRom *out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "rom_parser: cannot open %s\n", path);
        return false;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < GENESIS_HEADER_SIZE) {
        fprintf(stderr, "rom_parser: file too small (%ld bytes)\n", file_size);
        fclose(f);
        return false;
    }
    if (file_size > GENESIS_ROM_MAX_SIZE) {
        fprintf(stderr, "rom_parser: ROM too large (%ld bytes, max %d)\n",
                file_size, GENESIS_ROM_MAX_SIZE);
        fclose(f);
        return false;
    }

    out->rom_size = (uint32_t)file_size;
    out->rom_data = (uint8_t *)malloc(out->rom_size);
    if (!out->rom_data) {
        fclose(f);
        return false;
    }

    if (fread(out->rom_data, 1, out->rom_size, f) != out->rom_size) {
        fprintf(stderr, "rom_parser: read failed\n");
        free(out->rom_data);
        fclose(f);
        return false;
    }
    fclose(f);

    /* Validate "SEGA" magic in system type field */
    if (memcmp(out->rom_data + HDR_SYSTEM_TYPE, "SEGA", 4) != 0) {
        fprintf(stderr, "rom_parser: no SEGA magic at $100 — not a Genesis ROM?\n");
        /* Warn but continue — some ROMs have unusual headers */
    }

    /* Read vector table (big-endian 32-bit values at $000000) */
    out->initial_sp = (uint32_t)(out->rom_data[0] << 24 | out->rom_data[1] << 16 |
                                  out->rom_data[2] << 8  | out->rom_data[3]);
    out->initial_pc = (uint32_t)(out->rom_data[4] << 24 | out->rom_data[5] << 16 |
                                  out->rom_data[6] << 8  | out->rom_data[7]);

    /* Extract header checksum (big-endian 16-bit) */
    out->header_checksum = (uint16_t)(out->rom_data[HDR_CHECKSUM] << 8 |
                                       out->rom_data[HDR_CHECKSUM + 1]);

    /* Compute checksum over ROM bytes $000200–end */
    uint16_t sum = 0;
    for (uint32_t i = 0x200; i + 1 < out->rom_size; i += 2)
        sum += (uint16_t)(out->rom_data[i] << 8 | out->rom_data[i + 1]);
    out->computed_checksum = sum;

    /* Copy name strings (null-terminate) */
    memcpy(out->domestic_name, out->rom_data + HDR_DOMESTIC_NAME, 48);
    out->domestic_name[48] = '\0';
    /* Trim trailing spaces */
    for (int i = 47; i >= 0 && out->domestic_name[i] == ' '; i--)
        out->domestic_name[i] = '\0';

    memcpy(out->overseas_name, out->rom_data + HDR_OVERSEAS_NAME, 48);
    out->overseas_name[48] = '\0';
    for (int i = 47; i >= 0 && out->overseas_name[i] == ' '; i--)
        out->overseas_name[i] = '\0';

    /* Region */
    memcpy(out->region, out->rom_data + HDR_REGION, 3);
    out->region[3] = '\0';

    return true;
}

void rom_free(GenesisRom *rom) {
    free(rom->rom_data);
    rom->rom_data = NULL;
    rom->rom_size = 0;
}

uint8_t rom_read8(const GenesisRom *rom, uint32_t addr) {
    if (addr < rom->rom_size) return rom->rom_data[addr];
    return 0xFF;
}

uint16_t rom_read16(const GenesisRom *rom, uint32_t addr) {
    if (addr + 1 < rom->rom_size)
        return (uint16_t)(rom->rom_data[addr] << 8 | rom->rom_data[addr + 1]);
    return 0xFFFF;
}

uint32_t rom_read32(const GenesisRom *rom, uint32_t addr) {
    if (addr + 3 < rom->rom_size)
        return (uint32_t)(rom->rom_data[addr]   << 24 | rom->rom_data[addr+1] << 16 |
                          rom->rom_data[addr+2] << 8  | rom->rom_data[addr+3]);
    return 0xFFFFFFFF;
}
