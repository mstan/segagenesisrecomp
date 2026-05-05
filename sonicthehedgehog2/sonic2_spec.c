/*
 * sonic2_spec.c — instantiates the GameSpec for Sonic the Hedgehog 2
 * (Sega Genesis/Mega Drive, 1992, JUE REV01).
 *
 * Identity, recompiled trampolines, and the FrameRecord packer used as
 * the state-sync key for divergence diffing.
 *
 * NOTE on the sync key — Sonic 2's `Vint_runcount` longword sits at
 * $FFFFFE0C, exactly where Sonic 1's `v_vblank_count` lives. Both
 * VBlank handlers do `addq.l #1,(Vint_runcount).w` once per serviced
 * VBlank (s2.asm:508), so it advances at the same rate in native and
 * oracle and is the canonical state-sync sample point. This is the
 * field divergence_diff.py matches on instead of wall-frame number.
 *
 * Deliberately NULL/empty (vs Sonic 1):
 *   - on_post_reset       — Sonic 2's SMPS driver init is reached via
 *                           normal recompiled flow; no RAM seeding
 *                           required for the native build.
 *   - call_periodic       — no PLC-style periodic hook wired yet.
 *   - hybrid_table        — empty (sonic2_hybrid_table.c size=0).
 *   - commands            — no Sonic-2-specific TCP commands yet
 *                           (sonic_state/object_table can be ported
 *                           when the Sonic 2 object slot layout gets
 *                           audited; until then frame_range + the
 *                           game_data tail provide the same data).
 */
#include "game_spec.h"

#include <stddef.h>
#include <string.h>
#include <stdint.h>

#include "clownmdemu.h"

extern ClownMDEmu g_clownmdemu;

/* ---- Recompiled entry points (defined in generated/sonic2_full.c) ---- */
extern void func_000206(void);   /* EntryPoint    ($000206) */
extern void func_000408(void);   /* VBlank IRQ6   ($000408) */
extern void func_000F54(void);   /* HBlank IRQ4   ($000F54) */

/* ---- Legacy stub still required by generated sonic2_dispatch.c ---- */
int game_dispatch_override(uint32_t addr) {
    (void)addr;
    return 0;
}

/* ---- 68K work-RAM accessors (mirror sonic_extras.c) ---- */

static uint8_t s2_read8(uint32_t addr) {
    uint16_t off = (uint16_t)(addr & 0xFFFF);
    uint16_t w   = g_clownmdemu.state.m68k.ram[off / 2];
    return (off & 1) ? (uint8_t)(w & 0xFF) : (uint8_t)(w >> 8);
}

static uint16_t s2_read16(uint32_t addr) {
    uint16_t off = (uint16_t)(addr & 0xFFFF);
    return g_clownmdemu.state.m68k.ram[off / 2];
}

static uint32_t s2_read32(uint32_t addr) {
    return ((uint32_t)s2_read16(addr) << 16) | (uint32_t)s2_read16(addr + 2);
}

/* ---- Sonic 2 RAM offsets (relative to $FF0000), from s2disasm/s2.constants.asm ---- */
#define S2_GAME_MODE      0xF600   /* Game_Mode */
#define S2_CTRL_1_HELD    0xF604   /* Ctrl_1_Held */
#define S2_CTRL_1_PRESS   0xF605   /* Ctrl_1_Press */
#define S2_DEMO_TIME_LEFT 0xF614   /* Demo_Time_left (word) */
#define S2_VINT_RUNCOUNT  0xFE0C   /* Vint_runcount (longword) — sync key */

/* ---- Sonic 2 FrameRecord game_data tail ---- */

#define SONIC2_GAME_DATA_VERSION 1u

typedef struct {
    uint32_t version;            /* SONIC2_GAME_DATA_VERSION */
    uint8_t  game_mode;          /* $FFF600 */
    uint8_t  ctrl_1_held;        /* $FFF604 */
    uint8_t  ctrl_1_press;       /* $FFF605 */
    uint8_t  _pad0;
    uint16_t demo_time_left;     /* $FFF614 */
    uint16_t _pad1;
    /* Vint_runcount at $FFFFFE0C. Incremented once per serviced VBlank
     * by the recompiled handler (s2.asm:508 — addq.l #1,(Vint_runcount).w).
     * Use this as the cross-binary sync key in divergence_diff.py:
     * native and oracle see the same value at the same logical point
     * even when their wall-frame numbers diverge. */
    uint32_t internal_frame_ctr; /* $FFFFFE0C longword */
} Sonic2GameData;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(Sonic2GameData) <= 256,
               "Sonic2GameData must fit in FrameRecord.game_data[256]");
#endif

static void sonic2_fill_frame_record(uint8_t game_data[256]) {
    Sonic2GameData *sd = (Sonic2GameData *)game_data;
    memset(sd, 0, sizeof(*sd));
    sd->version            = SONIC2_GAME_DATA_VERSION;
    sd->game_mode          = s2_read8 (S2_GAME_MODE);
    sd->ctrl_1_held        = s2_read8 (S2_CTRL_1_HELD);
    sd->ctrl_1_press       = s2_read8 (S2_CTRL_1_PRESS);
    sd->demo_time_left     = s2_read16(S2_DEMO_TIME_LEFT);
    sd->internal_frame_ctr = s2_read32(S2_VINT_RUNCOUNT);
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

    .fill_frame_record      = sonic2_fill_frame_record,
    .frame_record_version   = SONIC2_GAME_DATA_VERSION,

    .commands               = NULL,
    .command_count          = 0,

    .hybrid_table           = NULL,
    .hybrid_table_size      = 0,
};
