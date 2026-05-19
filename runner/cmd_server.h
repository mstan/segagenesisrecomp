/*
 * cmd_server.h - Non-blocking TCP command server for debug queries.
 *
 * Listens on localhost, accepts line-delimited JSON commands,
 * returns JSON responses. Polled once per frame from main loop.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     should_quit;
    int      run_extra_frames;
    bool     input_override;
    uint8_t  input_keys;      /* Genesis: Up/Down/Left/Right/A/B/C/Start */
} CmdResult;

/* Start listening on the given port. */
void cmd_server_init(int port);

/* Poll for incoming commands. Call once per frame. Returns actions for main loop. */
CmdResult cmd_server_poll(void);

/* After run_extra_frames completes, send the result back to the client. */
void cmd_server_send_frame_result(int frames_run);

/* Record per-frame state into the history ring buffer. Call after each frame. */
void cmd_server_record_frame(uint32_t frame_num);

/* Tick FM trace (check frame limit). Call once per frame. */
void cmd_server_fm_trace_tick(void);

/* Tick memory-write log (check frame limit). Call once per frame. */
void cmd_server_mem_write_log_tick(void);

/* Arm the memory-write log directly (bypasses TCP).  Used by the
 * --mem-write-log CLI flag to capture writes from frame 0.  Returns 1 on
 * success.  `frames` is the wall-frame cap (0 = unlimited). */
int cmd_server_mem_write_log_start(const uint32_t *addrs, int n_addrs,
                                   int frames, const char *path);
int cmd_server_mem_write_log_start_ranges(const uint32_t *lo,
                                          const uint32_t *hi,
                                          int n_ranges,
                                          int frames,
                                          const char *path);

/* True while a TCP client has issued "pause". Main loop should poll
 * cmd_server but skip game-frame advancement until cleared by
 * "continue". Used by tools/compare_runs.py to hold the ring buffer
 * still during multi-fetch comparisons. */
bool cmd_server_is_paused(void);

/* Cleanup sockets. */
void cmd_server_shutdown(void);

/* ---- Shared accessors for in-tree tracers ---- */
/* Wall-frame counter (incremented by cmd_server_record_frame). */
uint32_t cmd_server_current_frame(void);
/* Current 68K stack pointer (A7) — works in both native + oracle builds. */
uint32_t cmd_server_current_a7(void);
/* Read 4 big-endian bytes from 68K work RAM. Address masked to $FFFF. */
uint32_t cmd_server_stack_read32(uint32_t addr);
