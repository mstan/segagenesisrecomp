/*
 * cmd_server_stub.c — production no-op stand-in for cmd_server.c.
 *
 * Compiled instead of cmd_server.c when GEN_ENABLE_TRACE is OFF (the default /
 * shipping build). It satisfies every public cmd_server_* symbol referenced by
 * the always-compiled runner (main.c, glue.c) so the native target links while
 * carrying NO TCP command server, NO history ring, and NO dev tracing. The
 * release binary therefore never opens a localhost port.
 *
 * This is purely additive to the RELEASING.md prod config (which already builds
 * with GEN_DEV_TRACE=OFF / SONIC_REVERSE_DEBUG=OFF): it also strips the cmd
 * server itself. Get the real server back with -DGEN_ENABLE_TRACE=ON, which
 * compiles cmd_server.c in this file's place. Keep these signatures identical to
 * cmd_server.c / cmd_server.h.
 */
#include "cmd_server.h"
#include <stdint.h>

/* Per-game TCP command handlers (sonic_extras.c) emit replies via these two
 * helpers; in a server-less prod build they discard. */
void cmd_send_response(const char *json) { (void)json; }
void cmd_send_err(int id, const char *msg) { (void)id; (void)msg; }

void cmd_server_init(int port) { (void)port; }
CmdResult cmd_server_poll(void) {
    CmdResult r = { false, 0, false, 0 };  /* don't quit, 0 extra frames, no input override */
    return r;
}
void cmd_server_send_frame_result(int frames_run) { (void)frames_run; }
void cmd_server_record_frame(uint32_t frame_num) { (void)frame_num; }
void cmd_server_fm_trace_tick(void) {}
void cmd_server_mem_write_log_tick(void) {}
int cmd_server_mem_write_log_start(const uint32_t *addrs, int n_addrs,
                                   int frames, const char *path) {
    (void)addrs; (void)n_addrs; (void)frames; (void)path; return 0;
}
int cmd_server_mem_write_log_start_ranges(const uint32_t *lo, const uint32_t *hi,
                                          int n_ranges, int frames, const char *path) {
    (void)lo; (void)hi; (void)n_ranges; (void)frames; (void)path; return 0;
}
bool cmd_server_is_paused(void) { return false; }
void cmd_server_shutdown(void) {}
uint32_t cmd_server_current_frame(void) { return 0; }
uint32_t cmd_server_current_a7(void) { return 0; }
uint32_t cmd_server_stack_read32(uint32_t addr) { (void)addr; return 0; }
