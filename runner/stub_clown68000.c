/*
 * stub_clown68000.c — replaces the clown68000 interpreter for Step 2.
 *
 * In Step 2 the recompiled code drives 68K execution on its own thread.
 * clownmdemu still calls the Clown68000_* entry points for timing/interrupt
 * coordination; we convert those calls to glue.c signals.
 *
 * Compiled ONLY when ENABLE_RECOMPILED_CODE is set (see CMakeLists.txt).
 * At that point clown68000-interpreter is excluded from the link, so these
 * symbols are the sole definitions.
 */

#include <stdio.h>
#include "clown68000.h"   /* Clown68000_State, Clown68000_ReadWriteCallbacks */
#include "bus-main-m68k.h" /* CPUCallbackUserData */
#include "glue.h"

void Clown68000_SetErrorCallback(
    void (*error_callback)(void *user_data, const char *format, va_list arg),
    const void *user_data)
{
    (void)error_callback;
    (void)user_data;
}

void Clown68000_Reset(Clown68000_State *state,
                      const Clown68000_ReadWriteCallbacks *callbacks)
{
    (void)state;
    (void)callbacks;
}

void Clown68000_DoCycles(Clown68000_State *state,
                         const Clown68000_ReadWriteCallbacks *callbacks,
                         cc_u32f cycles_to_do)
{
    CPUCallbackUserData *ud = (CPUCallbackUserData *)callbacks->user_data;
    ud->clownmdemu->state.m68k.frozen_by_dma_transfer = cc_false;
    ud->clownmdemu->state.z80.frozen_by_dma_transfer  = cc_false;

    glue_set_callbacks(callbacks);

    /* Interleave: let game code run for cycles_to_do worth of execution.
     * This is called ~500 times per Iterate (once per scanline sync).
     * By yielding to the game fiber here, we interleave game code with
     * VDP scanline rendering — matching the interpreter's behavior. */
    if (cycles_to_do > 0) {
        static int s_first = 1;
        if (s_first) { s_first = 0; fprintf(stderr, "[INTERLEAVE] DoCycles active, cycles=%u\n", (unsigned)cycles_to_do); }
        extern void glue_run_game_chunk(cc_u32f cycles);
        glue_run_game_chunk(cycles_to_do);
    }

    (void)state;
}

void Clown68000_Interrupt(Clown68000_State *state, cc_u16f level)
{
    /* VBlank is driven by the generated cycle-threshold path. HBlank still
     * belongs to clownmdemu's scanline scheduler: Sonic 2's split-screen
     * paths depend on IRQ4 clearing Hint_flag during active display. */
    if (level == 4)
        glue_handle_interrupt(level);

    (void)state;
}
