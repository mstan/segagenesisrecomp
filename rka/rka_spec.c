/*
 * rka_spec.c — GameSpec for Rocket Knight Adventures (Konami, 1993), USA NTSC.
 *
 * ROM layout: flat 1 MB at $000000-$0FFFFF, identity-mapped, no SRAM.
 * Boot/dispatch (from the 68K vector table — see game.toml):
 *   EntryPoint  = $000208   (reset PC; does not override the reset SSP)
 *   VInt (VBlank ISR) = $0003D4
 *   HInt (HBlank ISR) = $FFFFE010  (RAM-installed trampoline — resolved at
 *                                   runtime via the dispatch / RAM-trampoline
 *                                   path; no static func_ exists for RAM code.)
 *
 * Minimal bring-up spec: only the three mandatory interrupt/entry dispatchers
 * + ROM identity. No fill_frame_record / per-game TCP commands yet (RKA's WRAM
 * layout is not yet reverse-engineered). Dispatchers use recomp_call_addr(),
 * which routes through the generated dispatch table (and the Tier-3 floor on a
 * miss) — same pattern as sandk_spec.c.
 */
#include "game_spec.h"
#include "genesis_runtime.h"

#include <stddef.h>
#include <stdint.h>

/* ---- Entry-point / interrupt dispatchers (canonical org-0 addresses) ---- */
static void rka_call_entry_point(void) { recomp_call_addr(0x000208u); }
static void rka_call_vblank(void)      { recomp_call_addr(0x0003D4u); }
/* H-int is a RAM-installed trampoline ($FFFFE010); recomp_call_addr resolves it
 * at runtime (RAM JMP-trampoline path). Harmless if the game enables H-int only
 * later (it won't fire during early boot). */
static void rka_call_hblank(void)      { recomp_call_addr(0xFFFFE010u); }

/* Generated-dispatch override hook (global symbol, mirrors sandk_spec.c). */
int game_dispatch_override(uint32_t addr) { (void)addr; return 0; }

const GameSpec g_game_spec = {
    .display_name           = "Rocket Knight Adventures",
    .short_name             = "RKA",

    /* CRC check skipped (0); identity gated by size. USA file is 1 MB. */
    .expected_rom_crc32     = 0u,
    .expected_rom_size      = 0x100000u,   /* 1 MB */

    /* No battery SRAM (header carries no "RA"); sram_start/end stay 0. */

    .call_entry_point       = rka_call_entry_point,
    .call_vblank            = rka_call_vblank,
    .call_hblank            = rka_call_hblank,
    .resume_main_loop_pc    = 0u,    /* main-loop PC unknown — dispatcher re-entry */
    .save_resume_pc         = NULL,
    .dispatch_main_loop_pc  = 0u,
    .call_periodic          = NULL,

    .on_post_reset          = NULL,
    .on_frame_pre           = NULL,
    .on_frame_post          = NULL,
    .on_hblank              = NULL,

    .handle_arg             = NULL,
    .arg_usage              = NULL,
    .dispatch_override      = NULL,

    .fill_frame_record      = NULL,
    .frame_record_version   = 0u,

    .commands               = NULL,
    .command_count          = 0,

    .hybrid_table           = NULL,
    .hybrid_table_size      = 0,
};
