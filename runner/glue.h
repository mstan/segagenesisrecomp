#ifndef GLUE_H
#define GLUE_H

#include "clownmdemu.h"
#include <stdio.h>
#include <stdint.h>

/* Call after ClownMDEmu_Initialise() and ROM load.
 * Stores the emulator pointer for m68k_read/write routing, and
 * (when ENABLE_RECOMPILED_CODE is set) starts the game thread. */
void glue_init(ClownMDEmu *emu, const cc_u8l *rom_bytes, cc_u32l rom_byte_len);

/* Called by stub_clown68000 when clownmdemu raises the VBlank interrupt
 * (level 6).  In Step 2, this signals the game thread to service VBlank. */
void glue_signal_vblank(void);

/* Called by stub_clown68000 when clownmdemu raises the HBlank interrupt
 * (level 4).  Currently a hint only; game thread polls g_vblank_pending. */
void glue_signal_hblank(void);

/* Called by the native Clown68000 stub when clownmdemu raises an IRQ that
 * the recompiled runtime must service on the saved 68K state. */
void glue_handle_interrupt(cc_u16f level);

/* Called by stub_clown68000 to hand us the read/write callbacks that
 * clownmdemu set up for the current Clown68000_DoCycles call. */
void glue_set_callbacks(const void *callbacks);

/* Block until the game thread has finished servicing VBlank.
 * Called from the main loop after ClownMDEmu_Iterate(). */
void glue_wait_vblank_done(void);

/* Runner save-state support for recompiled/hybrid state that lives
 * outside the emulator core. */
void glue_save_state(FILE *sf);
void glue_load_state(FILE *sf);
void glue_restart_game_fiber(uint32_t resume_pc);

/* Shutdown: signal game thread to stop (if running). */
void glue_shutdown(void);

/* Pacing mode selector.
 *   FIBER_FULL      — game fiber runs to WaitForVBlank per wall frame at
 *                     full host speed. Heavy game frames get multi-fire
 *                     VBla.
 *   CYCLE_ACCURATE  — game fiber capped at NTSC wall-frame cycle budget
 *                     (127,856 cycles). Exactly 1 VBla handler per wall
 *                     frame. Default since the cycle-tables fix made
 *                     per-instruction estimates exact.
 *
 * Override at startup via --pacing=fiber|accurate (CLI) or
 * pacing=fiber|accurate (debug.ini). */
typedef enum {
    GLUE_PACING_FIBER_FULL     = 0,
    GLUE_PACING_CYCLE_ACCURATE = 1,
} GluePacingMode;

extern GluePacingMode g_pacing_mode;

/* Per-wall-frame fire guarantee (CYCLE_ACCURATE mode only). Called from
 * main.c AFTER glue_service_vblank each wall frame. If the game fiber
 * didn't cross the VBla threshold and didn't call WaitForVBlank this
 * wall frame (e.g., boot ROM copy), force a fire so hardware's
 * 1-VBla-per-wall-frame invariant holds. Also resets the per-wall-frame
 * fired latch either way. */
void glue_end_of_wall_frame(void);

#if SONIC_REVERSE_DEBUG
/* Tier-2 reverse debugger: yield the game fiber for a breakpoint /
 * step. Called from rdb_on_block_slow when a block-entry hook decides
 * to park. Mechanism mirrors glue_yield_for_vblank — we SwitchToFiber
 * back to the main fiber, the main loop drains cmd_server until a
 * resume command arrives, then switches back and execution continues
 * from the same block-entry point. Single-threaded cooperative; no
 * locking, no deadlock risk. Native-only; not defined in oracle /
 * hybrid-only builds. */
void glue_yield_for_break(void);

/* True if the most recent SwitchToFiber-return was triggered by a
 * block-entry break (not by a VBlank yield). Checked by main.c after
 * ClownMDEmu_Iterate so it knows to run the park-drain loop instead
 * of servicing VBlank. Cleared when the game fiber resumes. */
int  glue_game_yielded_for_break(void);

/* Called from main.c's park-drain loop when a rdb_step/rdb_continue
 * TCP command arrives. Switches to the game fiber so it can continue
 * executing from the yield point in rdb_on_block_slow. Returns when
 * the fiber yields again (break, vblank, or cycle budget) or exits.
 * Native-only; oracle build has a stub. */
void glue_resume_from_break(void);
#endif

#endif /* GLUE_H */
