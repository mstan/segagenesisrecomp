/*
 * sonic2_spec.c — instantiates the GameSpec for Sonic the Hedgehog 2
 * (Sega Genesis/Mega Drive, 1992, JUE REV01).
 *
 * Minimal step-6 stub. Just enough for the runner to boot:
 *   - Identity (CRC, size, name)
 *   - Recompiled entry / VBlank / HBlank trampolines
 *
 * Deliberately NULL/empty:
 *   - on_post_reset       — no SMPS-style RAM seeding required (pure
 *                           native build, no Z80 stub interaction
 *                           with the sound driver in this stub).
 *   - call_periodic       — no PLC-style periodic hook wired yet.
 *   - hybrid_table        — Sonic 2 oracle build deferred; native
 *                           build runs everything as recompiled C.
 *   - commands            — no Sonic-2-specific TCP commands yet.
 *   - fill_frame_record   — no per-game ring-buffer schema yet
 *                           (FrameRecord.game_data stays zeroed).
 *
 * Sonic 2 RAM layout differs from Sonic 1 (two-player split-screen,
 * different object slot offsets, different mode flags). When that
 * layout gets surfaced for debugging, populate the same fields here.
 */
#include "game_spec.h"

#include <stddef.h>     /* NULL */
#include <stdint.h>

/* ---- Recompiled entry points (defined in generated/sonic2_full.c) ---- */
extern void func_000206(void);   /* EntryPoint    ($000206) */
extern void func_000408(void);   /* VBlank IRQ6   ($000408) */
extern void func_000F54(void);   /* HBlank IRQ4   ($000F54) */

/* ---- Legacy stub still required by generated sonic2_dispatch.c ---- */
/* The generator currently emits `if (!game_dispatch_override(addr))`
 * inline in the dispatch fallback. Until codegen routes through
 * g_game_spec.dispatch_override we satisfy the symbol with a trivial
 * passthrough. Returning 0 means "no game override; fall through to
 * the dispatch-miss log", matching Sonic 1's existing behavior. */
int game_dispatch_override(uint32_t addr) {
    (void)addr;
    return 0;
}

const GameSpec g_game_spec = {
    .display_name           = "Sonic the Hedgehog 2",
    .short_name             = "Sonic2",

    /* JUE REV01 — verified against s2disasm reference build */
    .expected_rom_crc32     = 0u,           /* skip CRC verification (set when measured) */
    .expected_rom_size      = 0x100000u,    /* 1 MB cart */

    .call_entry_point       = func_000206,
    .call_vblank            = func_000408,
    .call_hblank            = func_000F54,
    .call_periodic          = NULL,

    .on_post_reset          = NULL,
    .on_frame_pre           = NULL,
    .on_frame_post          = NULL,
    .on_hblank              = NULL,

    .handle_arg             = NULL,
    .arg_usage              = NULL,
    .dispatch_override      = NULL,

    .fill_frame_record      = NULL,
    .frame_record_version   = 0,

    .commands               = NULL,
    .command_count          = 0,

    .hybrid_table           = NULL,
    .hybrid_table_size      = 0,
};
