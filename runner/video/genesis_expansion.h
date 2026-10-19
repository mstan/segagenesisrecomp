#pragma once
#include <stdint.h>
extern uint8_t *g_rom_ext;
extern uint32_t g_rom_ext_size;
/* Optional game-owned read-only bank at $400000-$7FFFFF. The caller supplies
 * a 24-bit bus address. Never alias RAM/devices, dereference NULL, or read a
 * partial final word beyond the buffer. With no bank the bus is unchanged. */
static inline int gbus_expansion_word(uint32_t address, uint16_t *word)
{
    if (!g_rom_ext || address < 0x400000u || address >= 0x800000u) return 0;
    uint32_t offset = address - 0x400000u;
    if (offset >= g_rom_ext_size) return 0;
    uint8_t low = offset + 1u < g_rom_ext_size && address + 1u < 0x800000u
        ? g_rom_ext[offset + 1u] : 0xFF;
    *word = (uint16_t)(((uint16_t)g_rom_ext[offset] << 8) | low);
    return 1;
}
