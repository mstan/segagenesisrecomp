/*
 * sonic_extras.c — Sonic 1 implementation of game_extras hooks.
 *
 * Provides:
 *   - game_fill_frame_record(): packs Sonic-specific RAM bytes into the
 *     FrameRecord.game_data tail every frame.
 *   - game_handle_debug_cmd(): dispatches "sonic_state", "object_table"
 *     to Sonic-specific handlers. (sonic_history is served by the
 *     framework's frame_range using sonic_extras_view().)
 *   - game_extras_name(): identifier surfaced by ping/info responses.
 */

#include "game_extras.h"
#include "sonic_extras.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "clownmdemu.h"

extern ClownMDEmu g_clownmdemu;

/* cmd_server's send_response is the only thing we need from there.
 * Re-declare it here rather than pulling the whole cmd_server.h surface. */
void cmd_send_response(const char *json);
void cmd_send_err(int id, const char *msg);

/* ---------------------------------------------------------------- */
/* Local 68K work-RAM accessors (mirror cmd_server's). */

static uint8_t emu_read8(uint32_t addr)
{
    uint16_t off = (uint16_t)(addr & 0xFFFF);
    uint16_t w = g_clownmdemu.state.m68k.ram[off / 2];
    return (off & 1) ? (uint8_t)(w & 0xFF) : (uint8_t)(w >> 8);
}

static uint16_t emu_read16(uint32_t addr)
{
    uint16_t off = (uint16_t)(addr & 0xFFFF);
    return g_clownmdemu.state.m68k.ram[off / 2];
}

static int16_t emu_read16s(uint32_t addr) { return (int16_t)emu_read16(addr); }

static uint32_t emu_read32(uint32_t addr)
{
    return ((uint32_t)emu_read16(addr) << 16) | (uint32_t)emu_read16(addr + 2);
}

/* ---------------------------------------------------------------- */
/* Sonic 1 RAM offsets (relative to $FF0000) */

#define ADDR_GAME_MODE    0xF600
#define ADDR_VBLANK_FLAG  0xF62A
#define ADDR_JOY_HELD     0xF602
#define ADDR_JOY_PRESS    0xF603
#define ADDR_SCROLL_X     0xF700
/* VBlank frame counter — empirically observed to be the longword at
 * $FFFE0C: increments by 1 every game frame. ($FFFE04 is a routine
 * pointer, not a counter.) Confirmed by reading 5 consecutive frames
 * during native execution. */
#define ADDR_VINT_CTR     0xFE0C

#define SONIC_OBJ_BASE    0xD000
#define ADDR_SONIC_OBJ_ID (SONIC_OBJ_BASE + 0x00)
#define ADDR_SONIC_X_POS  (SONIC_OBJ_BASE + 0x08)
#define ADDR_SONIC_Y_POS  (SONIC_OBJ_BASE + 0x0C)
#define ADDR_SONIC_X_VEL  (SONIC_OBJ_BASE + 0x10)
#define ADDR_SONIC_Y_VEL  (SONIC_OBJ_BASE + 0x12)
#define ADDR_SONIC_INERTIA (SONIC_OBJ_BASE + 0x14)
#define ADDR_SONIC_STATUS (SONIC_OBJ_BASE + 0x22)
#define ADDR_SONIC_ROUTINE (SONIC_OBJ_BASE + 0x24)
#define ADDR_SONIC_ANGLE  (SONIC_OBJ_BASE + 0x26)

/* ---------------------------------------------------------------- */
/* game_extras hook implementations */

const char *game_extras_name(void) { return "Sonic1"; }

void game_fill_frame_record(uint8_t game_data[256])
{
    SonicGameData *sd = (SonicGameData *)game_data;
    memset(sd, 0, sizeof(*sd));
    sd->version       = SONIC_GAME_DATA_VERSION;
    sd->game_mode     = emu_read8 (ADDR_GAME_MODE);
    sd->vblank_flag   = emu_read8 (ADDR_VBLANK_FLAG);
    sd->joy_held      = emu_read8 (ADDR_JOY_HELD);
    sd->joy_press     = emu_read8 (ADDR_JOY_PRESS);
    sd->scroll_x      = emu_read16(ADDR_SCROLL_X);
    sd->sonic_x       = emu_read16(ADDR_SONIC_X_POS);
    sd->sonic_y       = emu_read16(ADDR_SONIC_Y_POS);
    sd->sonic_xvel    = emu_read16s(ADDR_SONIC_X_VEL);
    sd->sonic_yvel    = emu_read16s(ADDR_SONIC_Y_VEL);
    sd->sonic_inertia = emu_read16s(ADDR_SONIC_INERTIA);
    sd->sonic_routine = emu_read8 (ADDR_SONIC_ROUTINE);
    sd->sonic_status  = emu_read8 (ADDR_SONIC_STATUS);
    sd->sonic_angle   = emu_read8 (ADDR_SONIC_ANGLE);
    sd->sonic_obj_id  = emu_read8 (ADDR_SONIC_OBJ_ID);
    sd->internal_frame_ctr = emu_read32(ADDR_VINT_CTR);
}

/* ---------------------------------------------------------------- */
/* Sonic-specific TCP commands */

static int json_get_int(const char *json, const char *key, int def)
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

void handle_sonic_state(int id)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,"
        "\"x\":%u,\"y\":%u,\"xvel\":%d,\"yvel\":%d,"
        "\"inertia\":%d,\"ground_speed\":%d,"
        "\"routine\":%u,\"status\":%u,\"angle\":%u,"
        "\"obj_id\":%u,\"game_mode\":%u,"
        "\"joy_held\":%u,\"joy_press\":%u}",
        id,
        (uint32_t)emu_read16(ADDR_SONIC_X_POS),
        (uint32_t)emu_read16(ADDR_SONIC_Y_POS),
        (int)emu_read16s(ADDR_SONIC_X_VEL),
        (int)emu_read16s(ADDR_SONIC_Y_VEL),
        (int)emu_read16s(ADDR_SONIC_INERTIA),
        (int)emu_read16s(ADDR_SONIC_INERTIA),
        (uint32_t)emu_read8(ADDR_SONIC_ROUTINE),
        (uint32_t)emu_read8(ADDR_SONIC_STATUS),
        (uint32_t)emu_read8(ADDR_SONIC_ANGLE),
        (uint32_t)emu_read8(ADDR_SONIC_OBJ_ID),
        (uint32_t)emu_read8(ADDR_GAME_MODE),
        (uint32_t)emu_read8(ADDR_JOY_HELD),
        (uint32_t)emu_read8(ADDR_JOY_PRESS));
    cmd_send_response(buf);
}

void handle_object_table(int id, const char *json)
{
    int max_objs = json_get_int(json, "count", 64);
    if (max_objs > 64) max_objs = 64;

    size_t cap = (size_t)max_objs * 256 + 256;
    char *buf = (char *)malloc(cap);
    if (!buf) { cmd_send_err(id, "alloc failed"); return; }

    int pos = snprintf(buf, cap,
                       "{\"id\":%d,\"ok\":true,\"objects\":[", id);
    int first = 1;
    for (int i = 0; i < max_objs; i++) {
        uint32_t base = SONIC_OBJ_BASE + (uint32_t)(i * 0x40);
        uint8_t obj_id = emu_read8(base);
        if (obj_id == 0) continue;
        if (!first) buf[pos++] = ',';
        first = 0;
        pos += snprintf(buf + pos, cap - pos,
            "{\"slot\":%d,\"id\":%u,\"x\":%u,\"y\":%u,"
            "\"xvel\":%d,\"yvel\":%d,\"routine\":%u,\"status\":%u}",
            i, obj_id,
            (uint32_t)emu_read16(base + 0x08),
            (uint32_t)emu_read16(base + 0x0C),
            (int)emu_read16s(base + 0x10),
            (int)emu_read16s(base + 0x12),
            (uint32_t)emu_read8(base + 0x24),
            (uint32_t)emu_read8(base + 0x22));
        if ((size_t)pos > cap - 512) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); return; }
            buf = nb;
        }
    }
    pos += snprintf(buf + pos, cap - pos, "]}");
    cmd_send_response(buf);
    free(buf);
}

bool game_handle_debug_cmd(int id, const char *cmd, const char *json)
{
    if (strcmp(cmd, "sonic_state") == 0)   { handle_sonic_state(id);          return true; }
    if (strcmp(cmd, "object_table") == 0)  { handle_object_table(id, json);   return true; }
    return false;
}
