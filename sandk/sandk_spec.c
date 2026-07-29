/*
 * sandk_spec.c — GameSpec for Sonic & Knuckles (USA/World, 1994), STANDALONE.
 *
 * ROM layout:  flat 2 MB at $000000-$1FFFFF, identity-mapped (no lock-on,
 *              no bank-switching). File offset == runtime address. When nothing
 *              is locked on, the $200000-$3FFFFF bank reads back as 0 and S&K's
 *              lock-on detector takes the standalone path.
 * Boot/dispatch (verified against the byte-perfect skdisasm sonic3k.lst, org 0):
 *   EntryPoint  = $000206
 *   GameLoop    = $0004B6   (main loop dispatched by Game_mode)
 *   VInt  (VBlank ISR) = $000584
 *   HInt  (HBlank ISR) = $000D10   (via JmpTo_HInt $000D0C)
 *   LevelLoop          = $00650C   (per-frame level loop; save-state resume)
 *
 * This is the S&K-alone mode; it shares boot addresses with the S&K master half
 * of the locked-on combined ROM, since the lock-on cart boots from S&K.
 */
#include "game_spec.h"
#include "genesis_runtime.h"
#include "sonic_extras.h"

#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if OWN_BACKEND
#include "backend_decls.h"   /* own decls — native builds have no clownmdemu paths */
#else
#include "clownmdemu.h"
#endif

#if !OWN_BACKEND
extern ClownMDEmu g_clownmdemu;
#endif

/* ---- Recompiled entry points (S&K standalone, org-0 addresses) ---- */
extern void func_000206(void);  /* EntryPoint   ($000206) */
extern void func_000584(void);  /* VInt         ($000584) */
extern void func_000D0C(void);  /* JmpTo_HInt   ($000D0C) */
extern void func_000D10(void);  /* HInt         ($000D10) */

static void sandk_call_entry_point(void) { recomp_call_addr(0x000206u); }
static void sandk_call_vblank(void)      { recomp_call_addr(0x000584u); }
static void sandk_call_hblank(void)      { recomp_call_addr(0x000D0Cu); }

/* Mode-aware save-state resume (sonic3k.lst, org 0):
 *   GM $0C Level / $08 Demo -> LevelLoop ($00650C): the per-frame loop top.
 *     Re-entering here continues the level moment-in-time from restored RAM.
 *   Anything else (Sega/title/menus, bonus-stage init) -> 0: fall back to
 *   GameLoop dispatch, which re-runs the mode handler — those states WANT
 *   their init. */
static uint32_t sandk_save_resume_pc(uint8_t game_mode)
{
    switch (game_mode) {
        case 0x08:
        case 0x0C: return 0x00650Cu;   /* LevelLoop */
        default:   return 0u;
    }
}

int game_dispatch_override(uint32_t addr) { (void)addr; return 0; }

/* ---- 68K work-RAM accessors ---- */

#if OWN_BACKEND
extern uint8_t g_ram[0x10000];   /* authoritative own-backend WRAM */
static uint8_t sandk_read8(uint32_t addr) {
    return g_ram[addr & 0xFFFF];
}

static uint16_t sandk_read16(uint32_t addr) {
    uint16_t off = (uint16_t)(addr & 0xFFFF);
    return (uint16_t)(((uint16_t)g_ram[off] << 8) | g_ram[(uint16_t)(off + 1)]);
}
#else
static uint8_t sandk_read8(uint32_t addr) {
    uint16_t off = (uint16_t)(addr & 0xFFFF);
    uint16_t w   = g_clownmdemu.state.m68k.ram[off / 2];
    return (off & 1) ? (uint8_t)(w & 0xFF) : (uint8_t)(w >> 8);
}

static uint16_t sandk_read16(uint32_t addr) {
    uint16_t off = (uint16_t)(addr & 0xFFFF);
    return g_clownmdemu.state.m68k.ram[off / 2];
}
#endif

static uint32_t sandk_read32(uint32_t addr) {
    return ((uint32_t)sandk_read16(addr) << 16) | (uint32_t)sandk_read16(addr + 2);
}

/* ---- S&K RAM offsets (relative to $FF0000); shared Sonic 3-family map ---- */
#define SANDK_GAME_MODE       0xF600  /* Game_mode */
#define SANDK_VINT_ROUTINE    0xF62A  /* V_int_routine */
#define SANDK_CTRL_1_HELD     0xF604  /* Ctrl_1_held */
#define SANDK_CTRL_1_PRESS    0xF605  /* Ctrl_1_pressed */
#define SANDK_CAMERA_X_POS    0xEE78  /* Camera_X_pos */
#define SANDK_VINT_RUNCOUNT   0xFE0C  /* V_int_run_count (longword) */

#define SANDK_OBJECT_BASE     0xB000  /* Player_1 / Object_RAM start */
#define SANDK_OBJECT_SIZE     0x4A    /* object_size */
#define SANDK_OBJECT_COUNT    110     /* object slots */

/* object field offsets (from sonic3k.constants.asm) */
#define SANDK_OBJ_CODE        0x00
#define SANDK_OBJ_X_POS       0x10
#define SANDK_OBJ_Y_POS       0x14
#define SANDK_OBJ_X_VEL       0x18
#define SANDK_OBJ_Y_VEL       0x1A
#define SANDK_OBJ_GROUND_VEL  0x1C
#define SANDK_OBJ_ROUTINE     0x05
#define SANDK_OBJ_STATUS      0x2A
#define SANDK_OBJ_ANGLE       0x26
#define SANDK_OBJ_CHAR_ID     0x38  /* character_id: 0=Sonic,1=Tails,2=Knuckles */

void cmd_send_response(const char *json);
void cmd_send_err(int id, const char *msg);

static void sandk_fill_frame_record(uint8_t game_data[256]) {
    SonicGameData *sd = (SonicGameData *)game_data;
    memset(sd, 0, sizeof(*sd));
    sd->version            = SONIC_GAME_DATA_VERSION;
    sd->game_mode          = sandk_read8 (SANDK_GAME_MODE);
    sd->vblank_flag        = sandk_read8 (SANDK_VINT_ROUTINE);
    sd->joy_held           = sandk_read8 (SANDK_CTRL_1_HELD);
    sd->joy_press          = sandk_read8 (SANDK_CTRL_1_PRESS);
    sd->scroll_x           = sandk_read16(SANDK_CAMERA_X_POS);
    sd->sonic_x            = sandk_read16(SANDK_OBJECT_BASE + SANDK_OBJ_X_POS);
    sd->sonic_y            = sandk_read16(SANDK_OBJECT_BASE + SANDK_OBJ_Y_POS);
    sd->sonic_xvel         = (int16_t)sandk_read16(SANDK_OBJECT_BASE + SANDK_OBJ_X_VEL);
    sd->sonic_yvel         = (int16_t)sandk_read16(SANDK_OBJECT_BASE + SANDK_OBJ_Y_VEL);
    sd->sonic_inertia      = (int16_t)sandk_read16(SANDK_OBJECT_BASE + SANDK_OBJ_GROUND_VEL);
    sd->sonic_routine      = sandk_read8 (SANDK_OBJECT_BASE + SANDK_OBJ_ROUTINE);
    sd->sonic_status       = sandk_read8 (SANDK_OBJECT_BASE + SANDK_OBJ_STATUS);
    sd->sonic_angle        = sandk_read8 (SANDK_OBJECT_BASE + SANDK_OBJ_ANGLE);
    sd->sonic_obj_id       = sandk_read8 (SANDK_OBJECT_BASE + SANDK_OBJ_CHAR_ID);
    sd->internal_frame_ctr = sandk_read32(SANDK_VINT_RUNCOUNT);
}

static int sandk_json_get_int(const char *json, const char *key, int def) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return def;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '-' || (*p >= '0' && *p <= '9')) return atoi(p);
    return def;
}

static void sandk_cmd_state(int id, const char *json) {
    (void)json;
    char buf[768];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,"
        "\"x\":%u,\"y\":%u,\"xvel\":%d,\"yvel\":%d,"
        "\"ground_vel\":%d,\"routine\":%u,\"status\":%u,"
        "\"angle\":%u,\"char_id\":%u,\"game_mode\":%u,"
        "\"joy_held\":%u,\"joy_press\":%u,"
        "\"vint_routine\":%u,\"internal_frame\":%u,"
        "\"camera_x\":%u}",
        id,
        (uint32_t)sandk_read16(SANDK_OBJECT_BASE + SANDK_OBJ_X_POS),
        (uint32_t)sandk_read16(SANDK_OBJECT_BASE + SANDK_OBJ_Y_POS),
        (int)(int16_t)sandk_read16(SANDK_OBJECT_BASE + SANDK_OBJ_X_VEL),
        (int)(int16_t)sandk_read16(SANDK_OBJECT_BASE + SANDK_OBJ_Y_VEL),
        (int)(int16_t)sandk_read16(SANDK_OBJECT_BASE + SANDK_OBJ_GROUND_VEL),
        (uint32_t)sandk_read8(SANDK_OBJECT_BASE + SANDK_OBJ_ROUTINE),
        (uint32_t)sandk_read8(SANDK_OBJECT_BASE + SANDK_OBJ_STATUS),
        (uint32_t)sandk_read8(SANDK_OBJECT_BASE + SANDK_OBJ_ANGLE),
        (uint32_t)sandk_read8(SANDK_OBJECT_BASE + SANDK_OBJ_CHAR_ID),
        (uint32_t)sandk_read8(SANDK_GAME_MODE),
        (uint32_t)sandk_read8(SANDK_CTRL_1_HELD),
        (uint32_t)sandk_read8(SANDK_CTRL_1_PRESS),
        (uint32_t)sandk_read8(SANDK_VINT_ROUTINE),
        (uint32_t)sandk_read32(SANDK_VINT_RUNCOUNT),
        (uint32_t)sandk_read16(SANDK_CAMERA_X_POS));
    cmd_send_response(buf);
}

static void sandk_cmd_object_table(int id, const char *json) {
    int max_objs = sandk_json_get_int(json, "count", 64);
    if (max_objs < 0) max_objs = 0;
    if (max_objs > SANDK_OBJECT_COUNT) max_objs = SANDK_OBJECT_COUNT;

    size_t cap = (size_t)max_objs * 256 + 256;
    char *buf = (char *)malloc(cap);
    if (!buf) { cmd_send_err(id, "alloc failed"); return; }

    int pos = snprintf(buf, cap, "{\"id\":%d,\"ok\":true,\"objects\":[", id);
    int first = 1;
    for (int i = 0; i < max_objs; i++) {
        uint32_t base = SANDK_OBJECT_BASE + (uint32_t)i * SANDK_OBJECT_SIZE;
        uint32_t code = sandk_read32(base + SANDK_OBJ_CODE);
        if (code == 0) continue;
        if (!first) buf[pos++] = ',';
        first = 0;
        pos += snprintf(buf + pos, cap - (size_t)pos,
            "{\"slot\":%d,\"code\":\"$%06X\",\"x\":%u,\"y\":%u,"
            "\"xvel\":%d,\"yvel\":%d,\"routine\":%u,\"status\":%u}",
            i, code,
            (uint32_t)sandk_read16(base + SANDK_OBJ_X_POS),
            (uint32_t)sandk_read16(base + SANDK_OBJ_Y_POS),
            (int)(int16_t)sandk_read16(base + SANDK_OBJ_X_VEL),
            (int)(int16_t)sandk_read16(base + SANDK_OBJ_Y_VEL),
            (uint32_t)sandk_read8(base + SANDK_OBJ_ROUTINE),
            (uint32_t)sandk_read8(base + SANDK_OBJ_STATUS));
        if ((size_t)pos > cap - 512) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); return; }
            buf = nb;
        }
    }
    snprintf(buf + pos, cap - (size_t)pos, "]}");
    cmd_send_response(buf);
    free(buf);
}

static const GameDebugCommand sandk_commands[] = {
    { "sonic_state",  sandk_cmd_state },
    { "object_table", sandk_cmd_object_table },
};

const GameSpec g_game_spec = {
    .display_name           = "Sonic & Knuckles",
    .short_name             = "SonicK",
    .boxart                 = "boxart-sonick.tga",

    /* S&K standalone — CRC check skipped (runner CRC calc vs file byteswap
     * unverified); ROM identity is gated by size + external hash check.
     * Expected MD5 of the file is 4ea493ea4e9f6c9ebfccbdb15110367e. */
    .expected_rom_crc32     = 0u,
    .expected_rom_size      = 0x200000u,   /* 2 MB standalone cart */

    /* No battery SRAM: standalone S&K has no data-select / save game (Start at
     * the title goes straight to Mushroom Hill). The S&K master ROM *contains*
     * the save code, but it runs only when Sonic 3 is locked on (the combined
     * sonic3k mode) — so sram_start stays 0 and the runtime maps no SRAM here.
     * Confirmed: standalone boot never writes the $200001-$203FFF window. */

    .call_entry_point       = sandk_call_entry_point,
    .call_vblank            = sandk_call_vblank,
    .call_hblank            = sandk_call_hblank,
    .resume_main_loop_pc    = 0x0004B6u,   /* GameLoop — fallback for modes without a loop map */
    .save_resume_pc         = sandk_save_resume_pc,   /* mode -> per-frame loop top */
    .dispatch_main_loop_pc  = 0x0004B6u,   /* GameLoop */
    .call_periodic          = NULL,

    .on_post_reset          = NULL,
    .on_frame_pre           = NULL,
    .on_frame_post          = NULL,
    .on_hblank              = NULL,

    .handle_arg             = NULL,
    .arg_usage              = NULL,
    .dispatch_override      = NULL,

    .fill_frame_record      = sandk_fill_frame_record,
    .frame_record_version   = SONIC_GAME_DATA_VERSION,

    .commands               = sandk_commands,
    .command_count          = (int)(sizeof(sandk_commands) / sizeof(sandk_commands[0])),

};
