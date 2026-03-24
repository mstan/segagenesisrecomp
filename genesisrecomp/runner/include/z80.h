/*
 * z80.h — Z80 sub-CPU interface (sound CPU on Genesis).
 * The Z80 runs at 3.58 MHz and controls the YM2612 and PSG.
 * On Sonic 1, the Z80 program is loaded from the main ROM at init.
 * Accessed from the 68K via $A00000-$A0FFFF (Z80 address space).
 */
#pragma once
#include <stdint.h>

/* Z80 RAM: 8KB at $A00000 */
#define Z80_RAM_SIZE 0x2000
extern uint8_t g_z80_ram[Z80_RAM_SIZE];

/* Bus control */
extern int g_z80_busreq;   /* 1 = 68K holds Z80 bus */
extern int g_z80_reset;    /* 1 = Z80 in reset */

void    z80_init(void);
void    z80_reset(void);
uint8_t z80_read(uint16_t addr);
void    z80_write(uint16_t addr, uint8_t val);
/* Run approximately 'cycles' Z80 cycles */
void    z80_run(int cycles);
