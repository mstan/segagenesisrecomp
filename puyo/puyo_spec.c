/*
 * puyo_spec.c — GameSpec for Puyo Puyo (Compile / SEGA, 1992), Japan NTSC.
 *
 * ROM layout: flat 512 KB at $000000-$07FFFF, identity-mapped, no SRAM
 * (header ROM range $00000000-$0007FFFF, RAM $00FF0000-$00FFFFFF, no "RA" tag).
 * Boot/dispatch (from the 68K vector table — see game.toml):
 *   Reset SSP   = $00FFFC00
 *   EntryPoint  = $000200
 *   VInt  (IRQ6) = $000524
 *   HInt  (IRQ4) = $0006AC
 * Every other vector points at a single shared handler ($00050C), so there is
 * no per-exception dispatch to model.
 *
 * Minimal bring-up spec, mirroring rka_spec.c: the three mandatory
 * entry/interrupt dispatchers plus ROM identity. No fill_frame_record and no
 * per-game TCP commands — Puyo Puyo's WRAM layout is not reverse-engineered,
 * and inventing addresses would put unverified per-game literals in the shared
 * runner (PRINCIPLES #21). Dispatchers route through recomp_call_addr(), which
 * uses the generated dispatch table and falls back to the Tier-3 floor.
 */
#include "game_spec.h"
#include "genesis_runtime.h"

#include <stddef.h>
#include <stdint.h>

/* ---- Entry-point / interrupt dispatchers (canonical org-0 addresses) ---- */
static void puyo_call_entry_point(void) { recomp_call_addr(0x000200u); }
static void puyo_call_vblank(void)      { recomp_call_addr(0x000524u); }
static void puyo_call_hblank(void)      { recomp_call_addr(0x0006ACu); }

/* Generated-dispatch override hook (global symbol, mirrors rka_spec.c). */
int game_dispatch_override(uint32_t addr) { (void)addr; return 0; }

const GameSpec g_game_spec = {
    .display_name           = "Puyo Puyo",
    .short_name             = "PUYO",

    /* Japan cartridge dump: 512 KB, CRC32 7F26614E, serial GM G-4082 -00. */
    .expected_rom_crc32     = 0x7F26614Eu,
    .expected_rom_size      = 0x80000u,    /* 512 KB */

    /* No community disasm for Puyo Puyo, so static discovery is heuristic and
     * will have holes. Run the tier-3 miss fallback always-on: a miss executes
     * correctly on the interpreter capsule and feeds floor_coverage.txt for the
     * next regen — the same posture RKA needed. */
    .tier3_floor_default    = 1,

    /* No battery SRAM (header carries no "RA"); sram_start/end stay 0. */

    .call_entry_point       = puyo_call_entry_point,
    .call_vblank            = puyo_call_vblank,
    .call_hblank            = puyo_call_hblank,
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

};
