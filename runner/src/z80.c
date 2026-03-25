/*
 * z80.c — Z80 sub-CPU stub for the Genesis runner.
 *
 * On the Genesis, the Z80 runs at 3.58 MHz and serves as a dedicated
 * sound CPU. It controls the YM2612 and PSG directly.
 *
 * The 68K communicates with the Z80 via:
 *   $A11100 — BUSREQ (take bus from Z80)
 *   $A11200 — RESET  (assert Z80 reset)
 *   $A00000–$A0FFFF — Z80 address space (8KB RAM + bank-switched window)
 *
 * For Sonic 1, the Z80 plays back sound effects and music using
 * a driver loaded from the 68K ROM at init time.
 *
 * TODO: integrate a Z80 core (e.g. DrZ80, Z80ex, or similar).
 * This stub silently ignores bus requests and returns 0xFF on all reads.
 */
#include "z80.h"
#include "ym2612.h"
#include "psg.h"
#include <string.h>
#include <stdio.h>

uint8_t g_z80_ram[Z80_RAM_SIZE];
int     g_z80_busreq = 0;
int     g_z80_reset  = 1;  /* Starts in reset state */

void z80_init(void) {
    memset(g_z80_ram, 0, sizeof(g_z80_ram));
    g_z80_busreq = 0;
    g_z80_reset  = 1;
}

void z80_reset(void) {
    g_z80_reset = 1;
    /* TODO: reset Z80 registers */
}

uint8_t z80_read(uint16_t addr) {
    if (addr < Z80_RAM_SIZE) return g_z80_ram[addr];
    if (addr == 0x4000 || addr == 0x4001 || addr == 0x4002 || addr == 0x4003) {
        /* YM2612 status */
        return ym2612_read_status();
    }
    return 0xFF;
}

void z80_write(uint16_t addr, uint8_t val) {
    if (addr < Z80_RAM_SIZE) { g_z80_ram[addr] = val; return; }
    if (addr == 0x4000) { ym2612_write(0, /* use latched reg */ 0, val); return; }
    if (addr == 0x4001) { ym2612_write(0, val, 0); return; }
    if (addr == 0x4002) { ym2612_write(1, 0, val); return; }
    if (addr == 0x4003) { ym2612_write(1, val, 0); return; }
    if (addr == 0x6000) { /* bank register */ return; }
    if (addr == 0x7F11) { psg_write(val); return; }
}

void z80_run(int cycles) {
    /* TODO: step Z80 core by 'cycles' cycles */
    (void)cycles;
}
