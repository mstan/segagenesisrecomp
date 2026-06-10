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
    uint8_t  z80_reset_pending;         /* reset line asserted; core reset due
                                         * before the Z80 next runs (so it
                                         * restarts at $0000 post-upload)     */

    /* Controllers / I-O. */
    uint8_t  pad[2];                    /* live button state (GPAD_* bits)    */
    uint8_t  io_data[3];                /* last value written to data ports   */
    uint8_t  io_ctrl[3];                /* TH direction/control per port      */
    uint8_t  version;                   /* $A10000 region/version register    */

    /* Battery-backed cartridge SRAM, geometry parsed from the ROM header
     * ($1B0 "RA", device type, start/end) at gbus_init. Mapped as raw bus
     * bytes over [sram_start & ~1, sram_end] when the $A130F1 overlay bit is
     * set (or always, for carts whose SRAM range lies outside ROM). Sonic 3
     * declares $200001-$2003FF odd-byte serial FRAM; raw-byte storage covers
     * odd/even/both layouts uniformly. (S3K lock-on declares no "RA" header —
     * its SRAM support will need a spec-driven override when the combined
     * target is brought up.) */
    uint8_t  sram[0x4000];              /* raw bus bytes from sram_base       */
    uint32_t sram_base;                 /* sram_start & ~1                    */
    uint32_t sram_end;                  /* inclusive end address              */
    uint32_t sram_size;                 /* bytes spanned (0 = none)           */
    uint8_t  sram_present;              /* "RA" header found                  */
    uint8_t  sram_enabled;              /* $A130F1 bit0 overlay state         */
} GenesisBus;

void gbus_init(GenesisBus *b, GVDP *vdp);

/* Parse battery-SRAM geometry from the ROM header. Must run AFTER g_rom is
 * populated (glue_init) — gbus_init runs before the ROM copy exists. */
void gbus_sram_setup(GenesisBus *b);

uint16_t gbus_read16 (GenesisBus *b, uint32_t addr);
uint8_t  gbus_read8  (GenesisBus *b, uint32_t addr);
void     gbus_write16(GenesisBus *b, uint32_t addr, uint16_t val);
void     gbus_write8 (GenesisBus *b, uint32_t addr, uint8_t  val);

/* Z80-side bus (the SMPS driver's address space): Z80 RAM, banked ROM window,
 * FM, PSG. Used by the scheduler when stepping the Z80. */
uint8_t  gbus_z80_read (GenesisBus *b, uint16_t addr);
void     gbus_z80_write(GenesisBus *b, uint16_t addr, uint8_t val);

/* Battery SRAM persistence accessors (main.c .srm load/flush). size == 0 when
 * the cartridge declares no battery SRAM. */
uint8_t *gbus_sram_buffer(GenesisBus *b);
uint32_t gbus_sram_size(const GenesisBus *b);

#endif /* GENESIS_BUS_H */

