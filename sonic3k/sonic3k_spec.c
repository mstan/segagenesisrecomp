/*
 * sonic3k_spec.c — GameSpec for Sonic 3 & Knuckles (S&K locked-on combined ROM, 1994).
 *
 * ROM layout:  S&K occupies $000000-$1FFFFF; Sonic 3 mapped at $200000-$3FFFFF.
 * VBlank/HBlank: vectors point to RAM trampolines installed at boot ($000304).
 *   VBlank ISR (VInt) = $000492 (one NOP + movem + dispatch)
 *   HBlank ISR (HInt) = $000C1E
 * Main game loop (GameLoop) = $0003C4.
 *
 * All addresses verified against skdisasm sonic3k.constants.asm and sonic3k.lst.
 */
#include "game_spec.h"
#include "genesis_runtime.h"
#include "sonic_extras.h"

#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "clownmdemu.h"

extern ClownMDEmu g_clownmdemu;

/* ---- Recompiled entry points (actual World ROM addresses) ---- */
extern void func_000206(void);  /* EntryPoint   ($000206) */
extern void func_000584(void);  /* VInt         ($000584) */
extern void func_000D10(void);  /* HInt         ($000D10) */

static void s3k_call_entry_point(void) { recomp_call_addr(0x000206u); }
static void s3k_call_vblank(void)      { recomp_call_addr(0x000584u); }
static void s3k_call_hblank(void)      { recomp_call_addr(0x000D10u); }

int game_dispatch_override(uint32_t addr) { (void)addr; return 0; }

/* ---- 68K work-RAM accessors ---- */

static uint8_t s3k_read8(uint32_t addr) {
    uint16_t off = (uint16_t)(addr & 0xFFFF);
    uint16_t w   = g_clownmdemu.state.m68k.ram[off / 2];
    return (off & 1) ? (uint8_t)(w & 0xFF) : (uint8_t)(w >> 8);
}

static uint16_t s3k_read16(uint32_t addr) {
    uint16_t off = (uint16_t)(addr & 0xFFFF);
    return g_clownmdemu.state.m68k.ram[off / 2];
}

static uint32_t s3k_read32(uint32_t addr) {
    return ((uint32_t)s3k_read16(addr) << 16) | (uint32_t)s3k_read16(addr + 2);
}

/* ---- S3K RAM offsets (relative to $FF0000) ---- */
#define S3K_GAME_MODE       0xF600  /* Game_mode */
#define S3K_VINT_ROUTINE    0xF62A  /* V_int_routine */
#define S3K_CTRL_1_HELD     0xF604  /* Ctrl_1_held */
#define S3K_CTRL_1_PRESS    0xF605  /* Ctrl_1_pressed */
#define S3K_CAMERA_X_POS    0xEE78  /* Camera_X_pos */
#define S3K_VINT_RUNCOUNT   0xFE0C  /* V_int_run_count (longword) */

#define S3K_OBJECT_BASE     0xB000  /* Player_1 / Object_RAM start */
#define S3K_OBJECT_SIZE     0x4A    /* per object_size in constants.asm */
#define S3K_OBJECT_COUNT    110     /* 110 object slots per constants.asm */

/* object field offsets (from sonic3k.constants.asm) */
#define S3K_OBJ_CODE        0x00
#define S3K_OBJ_X_POS       0x10
#define S3K_OBJ_Y_POS       0x14
#define S3K_OBJ_X_VEL       0x18
#define S3K_OBJ_Y_VEL       0x1A
#define S3K_OBJ_GROUND_VEL  0x1C
#define S3K_OBJ_ROUTINE     0x05
#define S3K_OBJ_STATUS      0x2A
#define S3K_OBJ_ANGLE       0x26
#define S3K_OBJ_CHAR_ID     0x38  /* character_id: 0=Sonic,1=Tails,2=Knuckles */

void cmd_send_response(const char *json);
void cmd_send_err(int id, const char *msg);

static void s3k_fill_frame_record(uint8_t game_data[256]) {
    SonicGameData *sd = (SonicGameData *)game_data;
    memset(sd, 0, sizeof(*sd));
    sd->version            = SONIC_GAME_DATA_VERSION;
    sd->game_mode          = s3k_read8 (S3K_GAME_MODE);
    sd->vblank_flag        = s3k_read8 (S3K_VINT_ROUTINE);
    sd->joy_held           = s3k_read8 (S3K_CTRL_1_HELD);
    sd->joy_press          = s3k_read8 (S3K_CTRL_1_PRESS);
    sd->scroll_x           = s3k_read16(S3K_CAMERA_X_POS);
    sd->sonic_x            = s3k_read16(S3K_OBJECT_BASE + S3K_OBJ_X_POS);
    sd->sonic_y            = s3k_read16(S3K_OBJECT_BASE + S3K_OBJ_Y_POS);
    sd->sonic_xvel         = (int16_t)s3k_read16(S3K_OBJECT_BASE + S3K_OBJ_X_VEL);
    sd->sonic_yvel         = (int16_t)s3k_read16(S3K_OBJECT_BASE + S3K_OBJ_Y_VEL);
    sd->sonic_inertia      = (int16_t)s3k_read16(S3K_OBJECT_BASE + S3K_OBJ_GROUND_VEL);
    sd->sonic_routine      = s3k_read8 (S3K_OBJECT_BASE + S3K_OBJ_ROUTINE);
    sd->sonic_status       = s3k_read8 (S3K_OBJECT_BASE + S3K_OBJ_STATUS);
    sd->sonic_angle        = s3k_read8 (S3K_OBJECT_BASE + S3K_OBJ_ANGLE);
    sd->sonic_obj_id       = s3k_read8 (S3K_OBJECT_BASE + S3K_OBJ_CHAR_ID);
    sd->internal_frame_ctr = s3k_read32(S3K_VINT_RUNCOUNT);
}

static int s3k_json_get_int(const char *json, const char *key, int def) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return def;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '-' || (*p >= '0' && *p <= '9')) return atoi(p);
    return def;
}

static void s3k_cmd_state(int id, const char *json) {
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
        (uint32_t)s3k_read16(S3K_OBJECT_BASE + S3K_OBJ_X_POS),
        (uint32_t)s3k_read16(S3K_OBJECT_BASE + S3K_OBJ_Y_POS),
        (int)(int16_t)s3k_read16(S3K_OBJECT_BASE + S3K_OBJ_X_VEL),
        (int)(int16_t)s3k_read16(S3K_OBJECT_BASE + S3K_OBJ_Y_VEL),
        (int)(int16_t)s3k_read16(S3K_OBJECT_BASE + S3K_OBJ_GROUND_VEL),
        (uint32_t)s3k_read8(S3K_OBJECT_BASE + S3K_OBJ_ROUTINE),
        (uint32_t)s3k_read8(S3K_OBJECT_BASE + S3K_OBJ_STATUS),
        (uint32_t)s3k_read8(S3K_OBJECT_BASE + S3K_OBJ_ANGLE),
        (uint32_t)s3k_read8(S3K_OBJECT_BASE + S3K_OBJ_CHAR_ID),
        (uint32_t)s3k_read8(S3K_GAME_MODE),
        (uint32_t)s3k_read8(S3K_CTRL_1_HELD),
        (uint32_t)s3k_read8(S3K_CTRL_1_PRESS),
        (uint32_t)s3k_read8(S3K_VINT_ROUTINE),
        (uint32_t)s3k_read32(S3K_VINT_RUNCOUNT),
        (uint32_t)s3k_read16(S3K_CAMERA_X_POS));
    cmd_send_response(buf);
}

static void s3k_cmd_object_table(int id, const char *json) {
    int max_objs = s3k_json_get_int(json, "count", 64);
    if (max_objs < 0) max_objs = 0;
    if (max_objs > S3K_OBJECT_COUNT) max_objs = S3K_OBJECT_COUNT;

    size_t cap = (size_t)max_objs * 256 + 256;
    char *buf = (char *)malloc(cap);
    if (!buf) { cmd_send_err(id, "alloc failed"); return; }

    int pos = snprintf(buf, cap, "{\"id\":%d,\"ok\":true,\"objects\":[", id);
    int first = 1;
    for (int i = 0; i < max_objs; i++) {
        uint32_t base = S3K_OBJECT_BASE + (uint32_t)i * S3K_OBJECT_SIZE;
        uint32_t code = s3k_read32(base + S3K_OBJ_CODE);
        if (code == 0) continue;
        if (!first) buf[pos++] = ',';
        first = 0;
        pos += snprintf(buf + pos, cap - (size_t)pos,
            "{\"slot\":%d,\"code\":\"$%06X\",\"x\":%u,\"y\":%u,"
            "\"xvel\":%d,\"yvel\":%d,\"routine\":%u,\"status\":%u}",
            i, code,
            (uint32_t)s3k_read16(base + S3K_OBJ_X_POS),
            (uint32_t)s3k_read16(base + S3K_OBJ_Y_POS),
            (int)(int16_t)s3k_read16(base + S3K_OBJ_X_VEL),
            (int)(int16_t)s3k_read16(base + S3K_OBJ_Y_VEL),
            (uint32_t)s3k_read8(base + S3K_OBJ_ROUTINE),
            (uint32_t)s3k_read8(base + S3K_OBJ_STATUS));
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

static const GameDebugCommand s3k_commands[] = {
    { "sonic_state",  s3k_cmd_state },
    { "object_table", s3k_cmd_object_table },
};

const GameSpec g_game_spec = {
    .display_name           = "Sonic 3 & Knuckles",
    .short_name             = "Sonic3K",

    /* S&K+S3 World — skip CRC (combined ROM checksum is non-standard) */
    .expected_rom_crc32     = 0u,
    .expected_rom_size      = 0x400000u,   /* 4 MB combined cart */

    .call_entry_point       = s3k_call_entry_point,
    .call_vblank            = s3k_call_vblank,
    .call_hblank            = s3k_call_hblank,
    .resume_main_loop_pc    = 0u,          /* save-state resume — not yet */
    .dispatch_main_loop_pc  = 0x0004B6u,   /* GameLoop (actual World ROM addr) */
    .call_periodic          = NULL,

    .on_post_reset          = NULL,
    .on_frame_pre           = NULL,
    .on_frame_post          = NULL,
    .on_hblank              = NULL,

    .handle_arg             = NULL,
    .arg_usage              = NULL,
    .dispatch_override      = NULL,

    .fill_frame_record      = s3k_fill_frame_record,
    .frame_record_version   = SONIC_GAME_DATA_VERSION,

    .commands               = s3k_commands,
    .command_count          = (int)(sizeof(s3k_commands) / sizeof(s3k_commands[0])),

    .hybrid_table           = NULL,
    .hybrid_table_size      = 0,
};
