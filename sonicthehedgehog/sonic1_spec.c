/*
 * sonic1_spec.c — instantiates the GameSpec for Sonic the Hedgehog
 * (Genesis, 1991, JUE REV00).
 *
 * Runtime lifecycle and dispatch enter through g_game_spec. Sonic-specific
 * frame/debug helpers remain in sonic_extras.c until their compatibility API
 * is removed together with consumer game source lists.
 */
#include "game_spec.h"
#include "genesis_runtime.h"

#include <stdint.h>
#ifdef GENESIS_Z80_RECOMP
#include "s1z80_step.h"
#endif
#include <stddef.h>   /* NULL — not pulled in transitively under gcc/glibc */

/* ---- 68K RAM shadow (defined in glue.c) ---- */
extern uint8_t g_ram[0x10000];

/* ---- Recompiled entry trampolines ---- */
/* Routed through recomp_call_addr so the tail-call trampoline frame
 * is set up before the recompiled function runs. */
static void s1_call_entry_point(void) { recomp_call_addr(0x000206u); }
static void s1_call_vblank(void)      { recomp_call_addr(0x000B10u); }
static void s1_call_hblank(void)      { recomp_call_addr(0x001126u); }
static void s1_call_periodic(void)    { recomp_call_addr(0x001642u); }

/* Mode-aware save-state resume (s1disasm sonic.asm; GameModeArray masks $1C:
 * $00 Sega, $04 Title, $08 Demo->GM_Level, $0C Level, $10 Special,
 * $14 Continue, $18 Ending, $1C Credits). Each target is that mode's
 * per-frame loop top, so a load continues moment-in-time. Unmapped / bit-7-
 * flagged modes fall back to resume_main_loop_pc (Level_MainLoop, whose
 * mode-check tail RTSes to the dispatcher and reinits — old behaviour). */
static uint32_t s1_save_resume_pc(uint8_t game_mode)
{
    switch (game_mode) {
        case 0x08:
        case 0x0C: return 0x003AE2u;   /* Level_MainLoop (handles demo)  */
        case 0x10: return 0x00472Au;   /* SS_MainLoop                    */
        case 0x14: return 0x004DC4u;   /* Cont_MainLoop                  */
        case 0x04: return 0x00317Cu;   /* Tit_MainLoop                   */
        default:   return 0u;
    }
}

/* ---- Sonic-specific debug-server handlers (sonic_extras.c) ---- */
extern void handle_sonic_state(int id);
extern void handle_object_table(int id, const char *json);

/* ---- Frame-record packer (sonic_extras.c) ---- */
extern void game_fill_frame_record(uint8_t game_data[256]);


/* ---- Lifecycle hooks ---- */

/*
 * Sonic 1 post-reset: seed the SMPS sound-driver RAM so UpdateMusic
 * exits cleanly. The Z80 is stubbed and never runs the real driver
 * init sequence, so we set v_sound_id ($FFF009) to $80 ("silence")
 * which makes UpdateMusic skip PlaySoundID and fall through to
 * DoStartZ80 + rts. All PlaybackControl bytes stay 0 (tracks
 * stopped) so per-track update paths also short-circuit cleanly.
 */
static void s1_on_post_reset(void) {
    g_ram[0xF009] = 0x80;
}

/*
 * Periodic hook: while the game thread is parked at WaitForVBlank
 * (func_0029A8) the runner calls this so music timing keeps
 * advancing. Sonic 1 routes it to func_001642 via the trampoline
 * declared above.
 */

/* ---- Debug-server command adapters ---- */
/* GameDebugCommand handlers take (id, json); handle_sonic_state
 * doesn't need the json so we wrap to match the spec signature. */

static void cmd_sonic_state(int id, const char *json) {
    (void)json;
    handle_sonic_state(id);
}

static void cmd_object_table(int id, const char *json) {
    handle_object_table(id, json);
}

static const GameDebugCommand s1_commands[] = {
    { "sonic_state",  cmd_sonic_state  },
    { "object_table", cmd_object_table },
};

/* ---- The spec ---- */

const GameSpec g_game_spec = {
    .display_name           = "Sonic the Hedgehog",
    .short_name             = "Sonic1",

    /* JUE REV00 — verified against s1disasm reference ROM */
    .expected_rom_crc32     = 0xF9394E97u,
    .expected_rom_size      = 0x80000u,
#ifdef GENESIS_Z80_RECOMP
    .z80_step               = s1z80_step,
#endif

    .call_entry_point       = s1_call_entry_point,
    .call_vblank            = s1_call_vblank,
    .call_hblank            = s1_call_hblank,
    .resume_main_loop_pc    = 0x003AE2u,
    .save_resume_pc         = s1_save_resume_pc,   /* mode -> loop top (moment-in-time) */
    .dispatch_main_loop_pc  = 0x000388u,
    .call_periodic          = s1_call_periodic,

    .on_post_reset          = s1_on_post_reset,
    .on_frame_pre           = NULL,
    .on_frame_post          = NULL,
    .on_hblank              = NULL,

    .handle_arg             = NULL,
    .arg_usage              = NULL,
    .dispatch_override      = NULL,

    .fill_frame_record      = game_fill_frame_record,
    .frame_record_version   = 2,                /* SONIC_GAME_DATA_VERSION */

    .commands               = s1_commands,
    .command_count          = (int)(sizeof(s1_commands) / sizeof(s1_commands[0])),

};
