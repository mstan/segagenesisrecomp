/*
 * game_extras.h — per-game extension hook interface for cmd_server.
 *
 * The runner is game-agnostic; per-game data (object positions, level
 * state, custom debug commands) is supplied by the game project via
 * these hooks. Linked at compile time — exactly one .c file in the
 * build must implement each hook (sonic_extras.c does it for Sonic 1).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Fill the per-frame "game_data" tail of the ring-buffer record. The
 * runner gives you a 256-byte buffer; pack any game-specific telemetry
 * you want to query later (object positions, mode, internal counters).
 *
 * Called from cmd_server_record_frame() once per frame. Must not block
 * and must not allocate. */
void game_fill_frame_record(uint8_t game_data[256]);

/* Optional dispatcher for game-specific TCP commands. Receives the raw
 * JSON line and the parsed "cmd" string. Return true if the command was
 * handled (cmd_server then sends nothing further); false to let
 * cmd_server fall through to its built-in commands.
 *
 * Send responses via cmd_send_response() (declared in cmd_server.h). */
bool game_handle_debug_cmd(int id, const char *cmd, const char *json_line);

/* Returns the game's identifier ("Sonic1", "Sonic2", ...) for the
 * info / ping command. Static C string. */
const char *game_extras_name(void);
