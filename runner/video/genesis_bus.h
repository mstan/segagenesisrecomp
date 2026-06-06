/*
 * genesis_bus.h — clean-room Genesis 68K (and Z80) bus, AGPL-free runtime
 * teardown item #5 (the "bus / scheduler / cart" line in LICENSING.md). Routes
 * 68K memory accesses to ROM / work RAM / VDP / Z80 area / I-O / FM / PSG
 * ourselves, replacing clownmdemu's M68kRead/WriteCallback. Phase 2 of the VDP
 * effort: built behind OWN_BACKEND, brought up alongside the own scheduler.
 *
 * Original implementation from the documented Genesis memory map (Sega VDP/
 * system manuals, Charles MacDonald's gen-hw notes). No emulator code copied.
 */
#ifndef GENESIS_BUS_H
#define GENESIS_BUS_H

#include <stdint.h>
#include "genesis_vdp.h"

/* 3-button pad button bits (1 = pressed) in our internal representation. */
enum {
    GPAD_UP = 0x01, GPAD_DOWN = 0x02, GPAD_LEFT = 0x04, GPAD_RIGHT = 0x08,
    GPAD_B  = 0x10, GPAD_C    = 0x20, GPAD_A    = 0x40, GPAD_START = 0x80
};

typedef struct GenesisBus {
    GVDP    *vdp;

    /* Z80 subsystem (the CPU core itself lives in the scheduler; here we hold
     * its RAM and the 68K-visible bus-control state). */
    uint8_t  z80_ram[0x2000];          /* $A00000-$A01FFF (8 KB)             */
    uint32_t z80_bank;                  /* base for the Z80 $8000 ROM window  */
    int      bank_shift;                /* serial bank-register write counter */
    uint8_t  z80_busreq;                /* 68K has requested the Z80 bus      */
    uint8_t  z80_reset_off;             /* 1 = Z80 out of reset (running)     */

    /* Controllers / I-O. */
    uint8_t  pad[2];                    /* live button state (GPAD_* bits)    */
    uint8_t  io_data[3];                /* last value written to data ports   */
    uint8_t  io_ctrl[3];                /* TH direction/control per port      */
    uint8_t  version;                   /* $A10000 region/version register    */
} GenesisBus;

void gbus_init(GenesisBus *b, GVDP *vdp);

uint16_t gbus_read16 (GenesisBus *b, uint32_t addr);
uint8_t  gbus_read8  (GenesisBus *b, uint32_t addr);
void     gbus_write16(GenesisBus *b, uint32_t addr, uint16_t val);
void     gbus_write8 (GenesisBus *b, uint32_t addr, uint8_t  val);

/* Z80-side bus (the SMPS driver's address space): Z80 RAM, banked ROM window,
 * FM, PSG. Used by the scheduler when stepping the Z80. */
uint8_t  gbus_z80_read (GenesisBus *b, uint16_t addr);
void     gbus_z80_write(GenesisBus *b, uint16_t addr, uint8_t val);

#endif /* GENESIS_BUS_H */
