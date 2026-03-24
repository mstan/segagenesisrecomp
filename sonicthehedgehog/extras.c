/*
 * extras.c — Sonic the Hedgehog (Genesis) runner hooks.
 * Implements game_extras.h.
 *
 * ROM: Sonic the Hedgehog (JUE) [!].bin — REV00, product GM 00001009-00
 * CRC32: 0xF9394E97  (verified against s1disasm reference ROM)
 */
#include "extras.h"
#include "genesis_runtime.h"
#include <string.h>

/* ---- game_extras.h implementation ---- */

uint32_t game_get_expected_crc32(void) {
    return 0xF9394E97U; /* JUE REV00 — verified */
}

const char *game_get_name(void) {
    return "Sonic the Hedgehog";
}

void game_on_init(void) {
    /* Initialize SMPS2 sound driver RAM so UpdateMusic exits cleanly.
     * The Z80 is stubbed and never runs the sound driver init sequence,
     * so we pre-seed the minimum required fields:
     *   v_sound_id ($FFF009) = $80 → "silence" → skips PlaySoundID
     * All PlaybackControl bytes remain 0 (tracks stopped) → skips track updates.
     * UpdateMusic then falls straight through to DoStartZ80 + rts. */
    g_ram[0xF009] = 0x80;  /* v_sound_id at $FFF009 */
}

void game_on_frame(uint64_t frame_count) {
    (void)frame_count;
}

void game_post_vblank(uint64_t frame_count) {
    (void)frame_count;
}

void game_on_hblank(uint32_t line) {
    (void)line;
}

int game_handle_arg(const char *key, const char *val) {
    (void)key; (void)val;
    return 0;
}

const char *game_arg_usage(void) {
    return NULL;
}

int game_dispatch_override(uint32_t addr) {
    (void)addr;
    return 0;
}

/* ---- Recompiled entry point routing ---- */

void game_call_entry_point(void) {
    func_000206();
}
void game_call_vblank(void)      {
    func_000B10();
}
void game_call_hblank(void)      { func_001126(); }
