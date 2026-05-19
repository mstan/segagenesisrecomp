/*
 * game_layout.h — per-game RAM layout + state-shape constants.
 *
 * Companion to game_spec.h, but for raw values rather than entry-point
 * function pointers. The two are split because:
 *   - GameSpec describes recompiled-code entry points (call_vblank,
 *     call_entry_point, etc.) that have to be hand-written per game.
 *   - GameRamLayout is pure data, generated mechanically from the
 *     per-game game.toml's [ram_layout] table.
 *
 * Shared runner code (glue.c, main.c, cmd_server.c, ...) MUST read
 * from g_game_layout instead of baking 68K addresses or game-mode
 * constants. There is no per-game knowledge in shared code.
 *
 * Codegen flow:
 *   game.toml [ram_layout] -> recompiler -> <prefix>_layout.c
 *   <prefix>_layout.c -> defines `const GameRamLayout g_game_layout`
 *   Shared runner -> includes this header -> reads g_game_layout
 *
 * Adding a new field:
 *   1. Add it here (with a short comment explaining the address /
 *      semantic / why it varies per game).
 *   2. Parse it in game_config.c's [ram_layout] reader.
 *   3. Emit it in code_generator.c's layout writer.
 *   4. Populate it in each per-game game.toml.
 *   5. Replace the corresponding shared-code hardcode.
 */
#pragma once

#include <stdint.h>

#define GAME_LAYOUT_LEVEL_MODES_MAX 16

typedef struct {
    /* ---- 68K WRAM addresses (24-bit, fully qualified $FFxxxx) ---- */

    /* Game_Mode byte. Sonic 1/2: $FFF600. Drives main-loop dispatch
     * (title vs level vs demo) and is the canonical "what mode are
     * we in" register for ramdump triggers. */
    uint32_t game_mode_addr;

    /* Vint_runcount longword. Sonic 1: v_vblank_count. Sonic 2:
     * Vint_runcount. Both at $FFFE0C (S1 used the same address but
     * a different name). Incremented by the VInt handler's
     * `addq.l #1,(...)` once per serviced VBlank — used as the
     * cross-binary state-sync key by divergence_diff.py. */
    uint32_t vint_runcount_addr;

    /* Vint_routine byte. $FFF62A in S1/S2. Indexes into the VInt
     * dispatch table; 0 = Vint_Lag (no rendering). The main loop
     * writes the desired routine before each `stop #$2300`; the
     * VInt prologue resets it to 0 on entry. Used by the bus-tap
     * yield-site logger. */
    uint32_t vint_routine_addr;

    /* PLC pending counter. $FFF6F8 in Sonic 1 (gates the periodic
     * RunPLC tile-decoder hook in glue_yield_for_vblank). 0 = the
     * game has no SMPS-style PLC system; periodic hook never fires. */
    uint32_t plc_pending_addr;

    /* Initial SSP from the 68K reset vector ($00000000 longword in
     * ROM). Used as both:
     *   (a) the upper bound for the A7 clamp in glue_yield_for_vblank
     *       — recompiler-imperfection drift can push A7 above this,
     *       at which point reads/writes target invalid scratch.
     *   (b) the safe stack base for fire_vblank_handler_once when
     *       vbla_stack is set to it (see below).
     * Sonic 1/2 use $FFFFFE00; Sonic 3K uses $FFFFFE00 too. */
    uint32_t initial_ssp;

    /* VBlank-handler stack base. fire_vblank_handler_once switches
     * A7 to this address before invoking the recompiled VInt body
     * so any push depth fits inside a known-safe scratch region.
     * The 256 bytes immediately below this address are saved before
     * the call and restored after.
     *
     * Per-game caveats:
     *   Sonic 1: $FFD000 (immediately past Sonic's player object
     *            slot at $FFD000 — but reads downward into pre-
     *            object scratch).
     *   Sonic 2: must NOT be $FFD000 — that's Object_RAM_End and
     *            the save area at $FFCF00 lands inside the dynamic
     *            object table. Use $FFFE00 (initial SSP top).
     *   Sonic 3K: TBD per its RAM map.
     */
    uint32_t vbla_stack;

    /* Generic IRQ-handler stack base. Shape-identical to vbla_stack
     * but for the level-6/4 IRQ trampolines that run BEFORE the
     * recompiled handler is dispatched. Usually the same value as
     * vbla_stack. */
    uint32_t intr_stack;

    /* Player object slot (byte 0 = object ID, $01 = Sonic in S1/S2).
     * Used by the ramdump trigger to confirm the player is alive
     * before dumping. Sonic 1: $FFD000. Sonic 2: $FFB000. */
    uint32_t player_object_addr;

    /* ---- Game-mode classification ---- */

    /* Game_Mode values that count as "the game is running gameplay"
     * for the ramdump trigger. Sonic 1: 0x08 (Demo) and 0x0C (Level).
     * Sonic 2 inherits the same convention with bit 7 sometimes set
     * during transitions. count==0 means "always allow ramdump". */
    uint8_t  level_modes[GAME_LAYOUT_LEVEL_MODES_MAX];
    int      level_mode_count;
} GameRamLayout;

/* Defined in <prefix>_layout.c (auto-generated from game.toml). */
extern const GameRamLayout g_game_layout;
