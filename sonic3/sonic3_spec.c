/*
 * sonic3_spec.c — GameSpec for Sonic the Hedgehog 3 (USA, 1994), STANDALONE.
 *
 * ROM layout:  flat 2 MB at $000000-$1FFFFF, identity-mapped (no lock-on,
 *              no bank-switching). File offset == runtime address.
 * Boot/dispatch (from s3.lst, org 0):
 *   EntryPoint  = $000206
 *   GameLoop    = $000734   (main loop dispatched by Game_mode)
 *   VInt  (VBlank ISR) = $000802   (vector $78 -> VInt)
 *   HInt  (HBlank ISR) = $000F9E   (vector $70 -> JmpTo_HInt -> RAM H_int_addr)
 *
 * All addresses verified against skdisasm s3.lst. This is "Sonic 3 mode" —
 * the clean single-ROM target; S&K-alone and S3K-locked-on are separate specs.
 */
#include "game_spec.h"
#include "genesis_runtime.h"
#include "sonic_extras.h"

#include <stddef.h>
#include <string.h>
#include <stdint.h>
#ifdef GENESIS_Z80_RECOMP
#include "s3z80_step.h"
#endif
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

/* ---- Recompiled entry points (Sonic 3 standalone, org-0 addresses) ---- */
extern void func_000206(void);  /* EntryPoint   ($000206) */
extern void func_000802(void);  /* VInt         ($000802) */
extern void func_000F9E(void);  /* JmpTo_HInt   ($000F9E) */
extern void func_000FA2(void);  /* HInt         ($000FA2) */

static void s3_call_entry_point(void) { recomp_call_addr(0x000206u); }
static void s3_call_vblank(void)      { recomp_call_addr(0x000802u); }
static void s3_call_hblank(void)      { recomp_call_addr(0x000F9Eu); }

/* Mode-aware save-state resume (s3.asm, addresses from s3.lst org 0):
 *   GM $0C Level / $08 Demo -> LevelLoop  ($004B0C): the per-frame loop —
 *     "bsr Pause_Game / Wait_VSync / Process_Sprites / ... / cmpi.b #$C,
 *      (Game_mode) / beq LevelLoop". Re-entering here continues the level
 *     moment-in-time from the restored RAM.
 *   GM $34 SpecialStage (running) -> loc_77D2 ($0077D2): the SS per-frame
 *     loop ("bsr Pause_Game / ... / cmpi.b #$34,(Game_mode) / beq loc_77D2").
 *   Anything else (Sega/title/menus, SS init modes $2C/$30, or any mode with
 *   the bit-7 reinit flag, e.g. $88) -> 0: fall back to GameLoop dispatch,
 *   which re-runs the mode handler — those states WANT their init. */
static uint32_t s3_save_resume_pc(uint8_t game_mode)
{
    switch (game_mode) {
        case 0x08:
        case 0x0C: return 0x004B0Cu;   /* LevelLoop                */
        case 0x34: return 0x0077D2u;   /* SS per-frame loop        */
        default:   return 0u;
    }
}

int game_dispatch_override(uint32_t addr) { (void)addr; return 0; }

/* ---- 68K work-RAM accessors ---- */

#if OWN_BACKEND
extern uint8_t g_ram[0x10000];   /* authoritative own-backend WRAM */
static uint8_t s3_read8(uint32_t addr) {
    return g_ram[addr & 0xFFFF];
}

static uint16_t s3_read16(uint32_t addr) {
    uint16_t off = (uint16_t)(addr & 0xFFFF);
    return (uint16_t)(((uint16_t)g_ram[off] << 8) | g_ram[(uint16_t)(off + 1)]);
}
#else
static uint8_t s3_read8(uint32_t addr) {
    uint16_t off = (uint16_t)(addr & 0xFFFF);
    uint16_t w   = g_clownmdemu.state.m68k.ram[off / 2];
    return (off & 1) ? (uint8_t)(w & 0xFF) : (uint8_t)(w >> 8);
}

static uint16_t s3_read16(uint32_t addr) {
    uint16_t off = (uint16_t)(addr & 0xFFFF);
    return g_clownmdemu.state.m68k.ram[off / 2];
}
#endif

static uint32_t s3_read32(uint32_t addr) {
    return ((uint32_t)s3_read16(addr) << 16) | (uint32_t)s3_read16(addr + 2);
}

/* ---- Sonic 3 RAM offsets (relative to $FF0000); shared Sonic engine map ---- */
#define S3_GAME_MODE       0xF600  /* Game_mode */
#define S3_VINT_ROUTINE    0xF62A  /* V_int_routine */
#define S3_CTRL_1_HELD     0xF604  /* Ctrl_1_held */
#define S3_CTRL_1_PRESS    0xF605  /* Ctrl_1_pressed */
#define S3_CAMERA_X_POS    0xEE78  /* Camera_X_pos */
#define S3_VINT_RUNCOUNT   0xFE0C  /* V_int_run_count (longword) */

#define S3_OBJECT_BASE     0xB000  /* Player_1 / Object_RAM start */
#define S3_OBJECT_SIZE     0x4A    /* object_size */
#define S3_OBJECT_COUNT    110     /* object slots */

/* object field offsets (from s3.lst constants) */
#define S3_OBJ_CODE        0x00
#define S3_OBJ_X_POS       0x10
#define S3_OBJ_Y_POS       0x14
#define S3_OBJ_X_VEL       0x18
#define S3_OBJ_Y_VEL       0x1A
#define S3_OBJ_GROUND_VEL  0x1C
#define S3_OBJ_ROUTINE     0x05
#define S3_OBJ_STATUS      0x2A
#define S3_OBJ_ANGLE       0x26
#define S3_OBJ_CHAR_ID     0x38  /* character_id: 0=Sonic,1=Tails,2=Knuckles */

void cmd_send_response(const char *json);
void cmd_send_err(int id, const char *msg);

static void s3_fill_frame_record(uint8_t game_data[256]) {
    SonicGameData *sd = (SonicGameData *)game_data;
    memset(sd, 0, sizeof(*sd));
    sd->version            = SONIC_GAME_DATA_VERSION;
    sd->game_mode          = s3_read8 (S3_GAME_MODE);
    sd->vblank_flag        = s3_read8 (S3_VINT_ROUTINE);
    sd->joy_held           = s3_read8 (S3_CTRL_1_HELD);
    sd->joy_press          = s3_read8 (S3_CTRL_1_PRESS);
    sd->scroll_x           = s3_read16(S3_CAMERA_X_POS);
    sd->sonic_x            = s3_read16(S3_OBJECT_BASE + S3_OBJ_X_POS);
    sd->sonic_y            = s3_read16(S3_OBJECT_BASE + S3_OBJ_Y_POS);
    sd->sonic_xvel         = (int16_t)s3_read16(S3_OBJECT_BASE + S3_OBJ_X_VEL);
    sd->sonic_yvel         = (int16_t)s3_read16(S3_OBJECT_BASE + S3_OBJ_Y_VEL);
    sd->sonic_inertia      = (int16_t)s3_read16(S3_OBJECT_BASE + S3_OBJ_GROUND_VEL);
    sd->sonic_routine      = s3_read8 (S3_OBJECT_BASE + S3_OBJ_ROUTINE);
    sd->sonic_status       = s3_read8 (S3_OBJECT_BASE + S3_OBJ_STATUS);
    sd->sonic_angle        = s3_read8 (S3_OBJECT_BASE + S3_OBJ_ANGLE);
    sd->sonic_obj_id       = s3_read8 (S3_OBJECT_BASE + S3_OBJ_CHAR_ID);
    sd->internal_frame_ctr = s3_read32(S3_VINT_RUNCOUNT);
}

static void s3_cmd_state(int id, const char *json) {
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
        (uint32_t)s3_read16(S3_OBJECT_BASE + S3_OBJ_X_POS),
        (uint32_t)s3_read16(S3_OBJECT_BASE + S3_OBJ_Y_POS),
        (int)(int16_t)s3_read16(S3_OBJECT_BASE + S3_OBJ_X_VEL),
        (int)(int16_t)s3_read16(S3_OBJECT_BASE + S3_OBJ_Y_VEL),
        (int)(int16_t)s3_read16(S3_OBJECT_BASE + S3_OBJ_GROUND_VEL),
        (uint32_t)s3_read8(S3_OBJECT_BASE + S3_OBJ_ROUTINE),
        (uint32_t)s3_read8(S3_OBJECT_BASE + S3_OBJ_STATUS),
        (uint32_t)s3_read8(S3_OBJECT_BASE + S3_OBJ_ANGLE),
        (uint32_t)s3_read8(S3_OBJECT_BASE + S3_OBJ_CHAR_ID),
        (uint32_t)s3_read8(S3_GAME_MODE),
        (uint32_t)s3_read8(S3_CTRL_1_HELD),
        (uint32_t)s3_read8(S3_CTRL_1_PRESS),
        (uint32_t)s3_read8(S3_VINT_ROUTINE),
        (uint32_t)s3_read32(S3_VINT_RUNCOUNT),
        (uint32_t)s3_read16(S3_CAMERA_X_POS));
    cmd_send_response(buf);
}

static int s3_json_get_int(const char *json, const char *key, int def) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return def;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '-' || (*p >= '0' && *p <= '9')) return atoi(p);
    return def;
}

static void s3_cmd_object_table(int id, const char *json) {
    int max_objs = s3_json_get_int(json, "count", 64);
    if (max_objs < 0) max_objs = 0;
    if (max_objs > S3_OBJECT_COUNT) max_objs = S3_OBJECT_COUNT;

    size_t cap = (size_t)max_objs * 256 + 256;
    char *buf = (char *)malloc(cap);
    if (!buf) { cmd_send_err(id, "alloc failed"); return; }

    int pos = snprintf(buf, cap, "{\"id\":%d,\"ok\":true,\"objects\":[", id);
    int first = 1;
    for (int i = 0; i < max_objs; i++) {
        uint32_t base = S3_OBJECT_BASE + (uint32_t)i * S3_OBJECT_SIZE;
        uint32_t code = s3_read32(base + S3_OBJ_CODE);
        if (code == 0) continue;
        if (!first) buf[pos++] = ',';
        first = 0;
        pos += snprintf(buf + pos, cap - (size_t)pos,
            "{\"slot\":%d,\"code\":\"$%06X\",\"x\":%u,\"y\":%u,"
            "\"xvel\":%d,\"yvel\":%d,\"routine\":%u,\"status\":%u}",
            i, code,
            (uint32_t)s3_read16(base + S3_OBJ_X_POS),
            (uint32_t)s3_read16(base + S3_OBJ_Y_POS),
            (int)(int16_t)s3_read16(base + S3_OBJ_X_VEL),
            (int)(int16_t)s3_read16(base + S3_OBJ_Y_VEL),
            (uint32_t)s3_read8(base + S3_OBJ_ROUTINE),
            (uint32_t)s3_read8(base + S3_OBJ_STATUS));
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

static const GameDebugCommand s3_commands[] = {
    { "sonic_state",  s3_cmd_state },
    { "object_table", s3_cmd_object_table },
};

const GameSpec g_game_spec = {
    .display_name           = "Sonic 3",
    .short_name             = "Sonic3",
    .boxart                 = "boxart-sonic3.tga",

    /* Sonic 3 USA standalone. The launcher computes CRC32 over the RAW ROM file
     * (No-Intro CRC = 0x9BC192CE) and shows a MATCH badge — this is independent
     * of the runner's internal byteswapped-buffer CRC path. */
    .expected_rom_crc32     = 0x9BC192CEu,
    .expected_rom_size      = 0x200000u,   /* 2 MB standalone cart */
#ifdef GENESIS_Z80_RECOMP
    .z80_step               = s3z80_step,
#endif

    .call_entry_point       = s3_call_entry_point,
    .call_vblank            = s3_call_vblank,
    .call_hblank            = s3_call_hblank,
    .resume_main_loop_pc    = 0x000734u,   /* GameLoop — fallback for modes without a loop map */
    .save_resume_pc         = s3_save_resume_pc,   /* mode -> per-frame loop top (moment-in-time) */
    .dispatch_main_loop_pc  = 0x000734u,   /* GameLoop */
    .call_periodic          = NULL,

    .on_post_reset          = NULL,
    .on_frame_pre           = NULL,
    .on_frame_post          = NULL,
    .on_hblank              = NULL,

    .handle_arg             = NULL,
    .arg_usage              = NULL,
    .dispatch_override      = NULL,

    .fill_frame_record      = s3_fill_frame_record,
    .frame_record_version   = SONIC_GAME_DATA_VERSION,

    .commands               = s3_commands,
    .command_count          = (int)(sizeof(s3_commands) / sizeof(s3_commands[0])),

    .hybrid_table           = NULL,
    .hybrid_table_size      = 0,
};
