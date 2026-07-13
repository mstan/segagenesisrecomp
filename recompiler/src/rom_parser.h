/*
 * rom_parser.h — Genesis ROM parsing interface.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define GENESIS_HEADER_SIZE  0x200       /* 512-byte system header */
#define GENESIS_ROM_MAX_SIZE 0x400000    /* 4MB maximum */

typedef struct {
    uint8_t  *rom_data;
    uint32_t  rom_size;
    /* Optional byte-fetch override: when non-NULL, rom_read8/16/32 route
     * every access through it (addr is the caller's full address). Lets the
     * runtime's tier-3 interpreter decode RAM-RESIDENT code from live work
     * RAM (self-modifying copied handlers) with the same decoder the static
     * recompiler uses. NULL (the zero-init default) = plain ROM array. */
    uint8_t (*read8_override)(uint32_t addr, void *user);
    void     *read8_user;
    uint32_t  initial_sp;        /* Supervisor stack pointer from vector table */
    uint32_t  initial_pc;        /* Initial PC (RESET entry point) */
    uint16_t  header_checksum;   /* Checksum from ROM header at $018E */
    uint16_t  computed_checksum; /* Computed over $000200–end */
    char      domestic_name[49]; /* Null-terminated, trailing spaces stripped */
    char      overseas_name[49];
    char      region[4];         /* e.g. "JUE" */
} GenesisRom;

bool     rom_parse(const char *path, GenesisRom *out);
void     rom_free(GenesisRom *rom);
uint8_t  rom_read8 (const GenesisRom *rom, uint32_t addr);
uint16_t rom_read16(const GenesisRom *rom, uint32_t addr);
uint32_t rom_read32(const GenesisRom *rom, uint32_t addr);
