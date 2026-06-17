/*
 * genesis_machine.h — clean-room own-backend: the Genesis "machine" that ties
 * our VDP + bus + superzazu Z80 together and runs the frame, replacing
 * ClownMDEmu_Iterate. AGPL-free runtime teardown items #4/#5. Built behind
 * OWN_BACKEND; brought up incrementally (video first, then audio/interrupt
 * timing) against the clownmdemu oracle.
 *
 * The recompiled 68K still runs in glue.c's fiber; this scheduler drives it via
 * glue_run_game_chunk() and delivers our VDP's V/H interrupts through the
 * own-backend interrupt path (glue_own_interrupt).
 */
#ifndef GENESIS_MACHINE_H
#define GENESIS_MACHINE_H

#include <stdint.h>
#include "genesis_vdp.h"
#include "genesis_bus.h"
#include "z80.h"           /* superzazu (MIT) — the Z80 core, embedded directly */

typedef struct GenesisMachine {
    GVDP        vdp;
    GenesisBus  bus;
    z80         z80;                /* superzazu state incl. bus thunks (re-wired
                                       on load by machine_wire_pointers)        */
    uint32_t    z80_cycle_debt;     /* carry of Z80 cycles across scanlines    */
    uint32_t    master_cycle;       /* running master-clock cycle (audio stamp)*/
} GenesisMachine;

extern GenesisMachine g_machine;

/* Emit a rendered scanline (ARGB row) to the display. The host (main.c) copies
 * it into its framebuffer. */
typedef void (*GenesisScanlineSink)(void *user, int line, const uint32_t *argb, int width);

void machine_init(void);
void machine_set_pad(int port, uint16_t buttons);  /* GPAD_* bits (12)         */
void machine_set_pad_type(int port, int six_button); /* 0 = 3-btn, 1 = 6-btn  */

/* Own-backend save states: snapshot/restore the whole machine (+ hidden Z80
 * ext bits), re-wiring internal pointers and derived caches on load. Returns
 * 1 on success. Raw-struct format, private to a build. */
#include <stdio.h>
int machine_save_state(FILE *f);
int machine_load_state(FILE *f);

/* Run one full frame: per scanline, advance the 68K (fiber), step the Z80, tick
 * the VDP, deliver interrupts, and emit active scanlines via `sink`. */
void machine_run_frame(GenesisScanlineSink sink, void *user);

#endif /* GENESIS_MACHINE_H */
