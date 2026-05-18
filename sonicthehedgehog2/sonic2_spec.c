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

/* ---- Recompiled entry points (defined in generated/sonic2_full.c) ---- */
extern void func_000206(void);   /* EntryPoint    ($000206) */
extern void func_000408(void);   /* VBlank IRQ6   ($000408) */
extern void func_000F54(void);   /* HBlank IRQ4   ($000F54) */

static void s2_call_entry_point(void) { recomp_call_addr(0x000206u); }
static void s2_call_vblank(void)      { recomp_call_addr(0x000408u); }
static void s2_call_hblank(void)      { recomp_call_addr(0x000F54u); }

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
#define S2_VINT_ROUTINE   0xF62A   /* Vint_routine */
#define S2_CTRL_1_HELD    0xF604   /* Ctrl_1_Held */
#define S2_CTRL_1_PRESS   0xF605   /* Ctrl_1_Press */
#define S2_CAMERA_X_POS   0xEE00   /* Camera_X_pos (longword; coarse word at +0) */
#define S2_VINT_RUNCOUNT  0xFE0C   /* Vint_runcount (longword) — sync key */

#define S2_OBJECT_BASE    0xB000   /* MainCharacter / Object_RAM start */
#define S2_OBJECT_SIZE    0x40
#define S2_OBJECT_COUNT   0x80

#define S2_OBJ_ID         0x00
#define S2_OBJ_X_POS      0x08
#define S2_OBJ_Y_POS      0x0C
#define S2_OBJ_X_VEL      0x10
#define S2_OBJ_Y_VEL      0x12
#define S2_OBJ_INERTIA    0x14
#define S2_OBJ_STATUS     0x22
#define S2_OBJ_ROUTINE    0x24
#define S2_OBJ_ANGLE      0x26

void cmd_send_response(const char *json);
void cmd_send_err(int id, const char *msg);

static void sonic2_fill_frame_record(uint8_t game_data[256]) {
    SonicGameData *sd = (SonicGameData *)game_data;
    memset(sd, 0, sizeof(*sd));
    sd->version       = SONIC_GAME_DATA_VERSION;
    sd->game_mode     = s2_read8 (S2_GAME_MODE);
    sd->vblank_flag   = s2_read8 (S2_VINT_ROUTINE);
    sd->joy_held      = s2_read8 (S2_CTRL_1_HELD);
    sd->joy_press     = s2_read8 (S2_CTRL_1_PRESS);
    sd->scroll_x      = s2_read16(S2_CAMERA_X_POS);
    sd->sonic_x       = s2_read16(S2_OBJECT_BASE + S2_OBJ_X_POS);
    sd->sonic_y       = s2_read16(S2_OBJECT_BASE + S2_OBJ_Y_POS);
    sd->sonic_xvel    = (int16_t)s2_read16(S2_OBJECT_BASE + S2_OBJ_X_VEL);
    sd->sonic_yvel    = (int16_t)s2_read16(S2_OBJECT_BASE + S2_OBJ_Y_VEL);
    sd->sonic_inertia = (int16_t)s2_read16(S2_OBJECT_BASE + S2_OBJ_INERTIA);
    sd->sonic_routine = s2_read8 (S2_OBJECT_BASE + S2_OBJ_ROUTINE);
    sd->sonic_status  = s2_read8 (S2_OBJECT_BASE + S2_OBJ_STATUS);
    sd->sonic_angle   = s2_read8 (S2_OBJECT_BASE + S2_OBJ_ANGLE);
    sd->sonic_obj_id  = s2_read8 (S2_OBJECT_BASE + S2_OBJ_ID);
    sd->internal_frame_ctr = s2_read32(S2_VINT_RUNCOUNT);
}

static int sonic2_json_get_int(const char *json, const char *key, int def)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return def;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '-' || (*p >= '0' && *p <= '9')) return atoi(p);
    return def;
}

static void sonic2_cmd_state(int id, const char *json)
{
    (void)json;
    char buf[640];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,"
        "\"x\":%u,\"y\":%u,\"xvel\":%d,\"yvel\":%d,"
        "\"inertia\":%d,\"ground_speed\":%d,"
        "\"routine\":%u,\"status\":%u,\"angle\":%u,"
        "\"obj_id\":%u,\"game_mode\":%u,"
        "\"joy_held\":%u,\"joy_press\":%u,"
        "\"vint_routine\":%u,\"internal_frame\":%u}",
        id,
        (uint32_t)s2_read16(S2_OBJECT_BASE + S2_OBJ_X_POS),
        (uint32_t)s2_read16(S2_OBJECT_BASE + S2_OBJ_Y_POS),
        (int)(int16_t)s2_read16(S2_OBJECT_BASE + S2_OBJ_X_VEL),
        (int)(int16_t)s2_read16(S2_OBJECT_BASE + S2_OBJ_Y_VEL),
        (int)(int16_t)s2_read16(S2_OBJECT_BASE + S2_OBJ_INERTIA),
        (int)(int16_t)s2_read16(S2_OBJECT_BASE + S2_OBJ_INERTIA),
        (uint32_t)s2_read8(S2_OBJECT_BASE + S2_OBJ_ROUTINE),
        (uint32_t)s2_read8(S2_OBJECT_BASE + S2_OBJ_STATUS),
        (uint32_t)s2_read8(S2_OBJECT_BASE + S2_OBJ_ANGLE),
        (uint32_t)s2_read8(S2_OBJECT_BASE + S2_OBJ_ID),
        (uint32_t)s2_read8(S2_GAME_MODE),
        (uint32_t)s2_read8(S2_CTRL_1_HELD),
        (uint32_t)s2_read8(S2_CTRL_1_PRESS),
        (uint32_t)s2_read8(S2_VINT_ROUTINE),
        (uint32_t)s2_read32(S2_VINT_RUNCOUNT));
    cmd_send_response(buf);
}

static void sonic2_cmd_object_table(int id, const char *json)
{
    int max_objs = sonic2_json_get_int(json, "count", 64);
    if (max_objs < 0) max_objs = 0;
    if (max_objs > S2_OBJECT_COUNT) max_objs = S2_OBJECT_COUNT;

    size_t cap = (size_t)max_objs * 256 + 256;
    char *buf = (char *)malloc(cap);
    if (!buf) { cmd_send_err(id, "alloc failed"); return; }

    int pos = snprintf(buf, cap, "{\"id\":%d,\"ok\":true,\"objects\":[", id);
    int first = 1;
    for (int i = 0; i < max_objs; i++) {
        uint32_t base = S2_OBJECT_BASE + (uint32_t)i * S2_OBJECT_SIZE;
        uint8_t obj_id = s2_read8(base + S2_OBJ_ID);
        if (obj_id == 0) continue;
        if (!first) buf[pos++] = ',';
        first = 0;
        pos += snprintf(buf + pos, cap - (size_t)pos,
            "{\"slot\":%d,\"id\":%u,\"x\":%u,\"y\":%u,"
            "\"xvel\":%d,\"yvel\":%d,\"routine\":%u,\"status\":%u}",
            i, (uint32_t)obj_id,
            (uint32_t)s2_read16(base + S2_OBJ_X_POS),
            (uint32_t)s2_read16(base + S2_OBJ_Y_POS),
            (int)(int16_t)s2_read16(base + S2_OBJ_X_VEL),
            (int)(int16_t)s2_read16(base + S2_OBJ_Y_VEL),
            (uint32_t)s2_read8(base + S2_OBJ_ROUTINE),
            (uint32_t)s2_read8(base + S2_OBJ_STATUS));
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

static const GameDebugCommand sonic2_commands[] = {
    { "sonic_state",  sonic2_cmd_state },
    { "object_table", sonic2_cmd_object_table },
};

const GameSpec g_game_spec = {
    .display_name           = "Sonic the Hedgehog 2",
    .short_name             = "Sonic2",

    /* JUE REV01 — verified against s2disasm reference build */
    .expected_rom_crc32     = 0u,           /* skip CRC verification (set when measured) */
    .expected_rom_size      = 0x100000u,    /* 1 MB cart */

    .call_entry_point       = s2_call_entry_point,
    .call_vblank            = s2_call_vblank,
    .call_hblank            = s2_call_hblank,
    .resume_main_loop_pc    = 0x004360u,
    .dispatch_main_loop_pc  = 0x000394u,
    .call_periodic          = NULL,

    .on_post_reset          = NULL,
    .on_frame_pre           = NULL,
    .on_frame_post          = NULL,
    .on_hblank              = NULL,

    .handle_arg             = NULL,
    .arg_usage              = NULL,
    .dispatch_override      = NULL,

    .fill_frame_record      = sonic2_fill_frame_record,
    .frame_record_version   = SONIC_GAME_DATA_VERSION,

    .commands               = sonic2_commands,
    .command_count          = (int)(sizeof(sonic2_commands) / sizeof(sonic2_commands[0])),

    .hybrid_table           = NULL,
    .hybrid_table_size      = 0,
};
