/*
 * sonic1_spec.c — instantiates the GameSpec for Sonic the Hedgehog
 * (Genesis, 1991, JUE REV00).
 *
 * Step 2 of the runner-parameterization migration. This file is
 * compiled and linked but no consumer reads g_game_spec yet — the
 * runner still calls func_NNNNNN directly through glue.c, and the
 * legacy game_extras hooks (game_fill_frame_record,
 * game_handle_debug_cmd, game_extras_name) continue to provide the
 * per-game data. Step 3 will switch consumers over; once that lands
 * the legacy hooks can be deleted.
 *
 * The pointers below alias the existing implementations in
 *   runner/sonic_extras.c           (frame record + TCP commands)
 *   runner/hybrid_table.c           (verified-clean native overrides)
 *   sonicthehedgehog/generated/...  (recompiled func_NNNNNN bodies)
 * so flipping consumers from the old path to g_game_spec is a no-op
 * behaviorally — both go to the same code.
 */
#include "game_spec.h"
#include "genesis_runtime.h"
#include "hybrid.h"

#include <stdint.h>

/* ---- 68K RAM shadow (defined in glue.c) ---- */
extern uint8_t g_ram[0x10000];

/* ---- Recompiled entry trampolines ---- */
/* Routed through recomp_call_addr so the tail-call trampoline frame
 * is set up before the recompiled function runs. */
static void s1_call_entry_point(void) { recomp_call_addr(0x000206u); }
static void s1_call_vblank(void)      { recomp_call_addr(0x000B10u); }
static void s1_call_hblank(void)      { recomp_call_addr(0x001126u); }
static void s1_call_periodic(void)    { recomp_call_addr(0x001642u); }

/* ---- Sonic-specific debug-server handlers (sonic_extras.c) ---- */
extern void handle_sonic_state(int id);
extern void handle_object_table(int id, const char *json);

/* ---- Frame-record packer (sonic_extras.c) ---- */
extern void game_fill_frame_record(uint8_t game_data[256]);

/* ---- Hybrid native overrides (hybrid_table.c) ---- */
extern HybridEntry g_hybrid_table[];
extern int         g_hybrid_table_size;

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

    .call_entry_point       = s1_call_entry_point,
    .call_vblank            = s1_call_vblank,
    .call_hblank            = s1_call_hblank,
    .resume_main_loop_pc    = 0x003AE2u,
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

    .hybrid_table           = NULL,             /* see note below */
    .hybrid_table_size      = 0,
};

/*
 * NOTE on the hybrid table: g_hybrid_table is currently declared as
 * `HybridEntry g_hybrid_table[]` (non-const) in hybrid_table.c, so
 * we can't initialize a const-pointer field with it at file scope
 * without const-casting (illegal in C99 for static initializers).
 * Rather than const-casting at every reader, hybrid_table_size = 0
 * here means "consult the legacy g_hybrid_table directly". Step 3
 * will either: (a) make the table const-correct and point this
 * field at it, or (b) keep the current per-process global and have
 * consumers fall back when this field is NULL. Either way the
 * behavior is identical to what ships today.
 */
