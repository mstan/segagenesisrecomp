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

/* Pad button bits (1 = pressed) in our internal representation. Low byte is the
 * 3-button set; the high nibble is the 6-button extension (X/Y/Z/Mode), surfaced
 * by pad_read() only when the port is configured PAD_6BUTTON. MUST stay in sync
 * with k_btn_bit[] in input_map.c. */
enum {
    GPAD_UP = 0x01, GPAD_DOWN = 0x02, GPAD_LEFT = 0x04, GPAD_RIGHT = 0x08,
    GPAD_B  = 0x10, GPAD_C    = 0x20, GPAD_A    = 0x40, GPAD_START = 0x80,
    GPAD_X  = 0x100, GPAD_Y   = 0x200, GPAD_Z   = 0x400, GPAD_MODE  = 0x800
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

    /* YM2612 timer/status state. Audio register writes are rendered later by
     * the cycle-stamped mixer, but the emulated CPUs must observe timer flags
     * synchronously while polling $4000/$A04000 (RKA's Z80 driver uses timer
     * B as its music tick). */
    uint8_t  ym_addr[2];                /* address latch for ports 0/1         */
    uint8_t  ym_timer_b;                /* register $26                        */
    uint8_t  ym_mode;                   /* register $27                        */
    uint8_t  ym_status;                 /* timer overflow flags, bits 1..0     */
    uint8_t  ym_timer_b_running;
    uint64_t ym_timer_b_deadline;       /* absolute scheduler master cycles    */
    uint64_t ym_timer_b_period;         /* cached period in master cycles      */

    /* Controllers / I-O. */
    uint16_t pad[2];                    /* live button state (GPAD_* bits, 12) */
    uint8_t  pad_type[2];               /* 0 = 3-button, 1 = 6-button          */
    uint8_t  pad_th_count[2];           /* 6-button TH 1->0 edge counter       */
    uint8_t  pad_th_prev[2];            /* previous TH level (edge detect)     */
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

/* Map battery SRAM over the inclusive byte range [start & ~1, end] when the
 * cart's standard header does NOT declare it (the Sonic 3 & Knuckles lock-on
 * cart and standalone S&K — see GameSpec.sram_start). No-op if a header "RA"
 * marker already mapped SRAM (gbus_sram_setup wins) or the range is invalid /
 * larger than the SRAM buffer. Must run AFTER gbus_sram_setup. */
void gbus_sram_set_geometry(GenesisBus *b, uint32_t start, uint32_t end);

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
