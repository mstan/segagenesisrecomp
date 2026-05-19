/*
 * cmd_server.c - Non-blocking TCP command server for debug queries.
 *
 * Protocol: line-delimited JSON over TCP (one JSON object per line).
 * Single client at a time. Polled each frame with non-blocking recv().
 *
 * Port: 4378 (Sega Genesis recomp project)
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int sock_t;
#define SOCK_INVALID (-1)
#define sock_close close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <inttypes.h>

#include "cmd_server.h"
#include "clownmdemu.h"
#include "audio.h"
#include "game_layout.h"
#if SONIC_REVERSE_DEBUG
#include "reverse_debug.h"
#if defined(SONIC_ORACLE_BUILD)
#include "oracle_trace.h"
#endif
#endif

#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
#include "genesis_runtime.h"
#endif

/* =========================================================================
 * External state we need to inspect
 * ========================================================================= */

extern ClownMDEmu g_clownmdemu;       /* defined in main.c */

#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
extern uint32_t   g_cycle_accumulator;
extern uint32_t   g_vblank_threshold;
extern uint64_t   g_frame_count;

/* IO port logging control (defined in glue.c) */
extern int s_io_log_enabled;
extern int s_io_log_count;
#endif

/* =========================================================================
 * State
 * ========================================================================= */

static sock_t s_listen = SOCK_INVALID;
static sock_t s_client = SOCK_INVALID;

/* Recv line buffer */
#define RECV_BUF_SIZE 8192
static char s_recv_buf[RECV_BUF_SIZE];
static int  s_recv_len = 0;

/* Pending frame request */
static int s_pending_frame_id = -1;

/* Pause flag — set by "pause" cmd, cleared by "continue". When true,
 * the runner main loop polls cmd_server but does not advance the game
 * frame, so the ring buffer stays still for multi-fetch tools. */
static int s_paused = 0;
bool cmd_server_is_paused(void) { return s_paused != 0; }

/* Wall-frame counter, kept in sync by cmd_server_record_frame() each
 * frame. Lives here (not in glue.c) because glue's g_frame_count is
 * gated by ENABLE_RECOMPILED_CODE and never advances in oracle builds.
 * Forward-declared here so fm_trace handler (above the definition
 * site) can read it. */
static uint32_t s_current_frame = 0;

/* =========================================================================
 * FM write trace — captures every YM2612 register write with master-clock
 * cycle position.  Controllable via TCP "fm_trace" command.
 * Works in BOTH native and interpreter builds since the hook is in
 * bus-main-m68k.c's M68kWriteCallbackWithCycle (shared code path).
 * ========================================================================= */

extern void (*g_fm_write_trace_fn)(uint32_t address, uint8_t value, uint32_t target_cycle);
extern uint64_t g_frame_count;

static FILE *s_fm_trace_file = NULL;
static int    s_fm_trace_active = 0;
static int    s_fm_trace_max_frames = 0;
static int    s_fm_trace_frame_count = 0;
static uint64_t s_fm_trace_start_frame = 0;

/* Read the current 68K A7 (stack pointer). In native builds the recompiled
 * code drives g_cpu directly; in oracle builds clown68000 keeps the active
 * SP in address_registers[7] (the supervisor_stack_pointer / user_stack
 * _pointer fields only hold the *inactive* alternate SP).  Also exposed as
 * a helper because the FM-trace callback and any future stack-unwinding
 * tool need a uniform accessor. */
static uint32_t fm_trace_current_a7(void)
{
#if ENABLE_RECOMPILED_CODE && !defined(SONIC_ORACLE_BUILD)
    return g_cpu.A[7];
#else
    return (uint32_t)g_clownmdemu.m68k.address_registers[7];
#endif
}

/* Read 4 bytes big-endian from 68K work RAM ($FF0000-$FFFFFF).  Safe to
 * call with any 24-bit address — high bits masked, low bits folded into
 * the RAM array.  Used to walk the return-address chain on the 68K stack
 * at the moment of an FM write. */
static uint32_t fm_trace_stack_read32(uint32_t addr)
{
    uint16_t offset = (uint16_t)(addr & 0xFFFF);
    uint16_t hi = g_clownmdemu.state.m68k.ram[offset / 2];
    uint16_t lo = g_clownmdemu.state.m68k.ram[((offset + 2) & 0xFFFF) / 2];
    return ((uint32_t)hi << 16) | (uint32_t)lo;
}

static void fm_trace_callback(uint32_t address, uint8_t value, uint32_t target_cycle)
{
    if (!s_fm_trace_file) return;
    /* Capture the 68K call chain at write time: A7 + 4 return addresses.
     * The recompiler emits JSR as `m68k_write32(--A7, ret_pc)` so the
     * stack holds the same return-address sequence in both native and
     * oracle — letting us identify the 68K caller (e.g. WriteFMI caller,
     * FMUpdateFreq caller) by matching against annotations. */
    uint32_t a7  = fm_trace_current_a7();
    uint32_t r0  = fm_trace_stack_read32(a7);
    uint32_t r1  = fm_trace_stack_read32(a7 + 4);
    uint32_t r2  = fm_trace_stack_read32(a7 + 8);
    uint32_t r3  = fm_trace_stack_read32(a7 + 12);
    /* Use our own frame counter (incremented by tick) rather than
     * g_frame_count which only works in Step 2 mode. */
    fprintf(s_fm_trace_file,
            "%d %u 0x%06X 0x%02X 0x%06X 0x%06X 0x%06X 0x%06X 0x%06X\n",
            s_fm_trace_frame_count, (unsigned)target_cycle, address, value,
            a7 & 0xFFFFFFu, r0 & 0xFFFFFFu, r1 & 0xFFFFFFu,
            r2 & 0xFFFFFFu, r3 & 0xFFFFFFu);
}

/* Called from main loop each frame to check if trace should stop */
void cmd_server_fm_trace_tick(void)
{
    if (!s_fm_trace_active) return;
    s_fm_trace_frame_count++;
    if (s_fm_trace_max_frames > 0 && s_fm_trace_frame_count >= s_fm_trace_max_frames) {
        fclose(s_fm_trace_file);
        s_fm_trace_file = NULL;
        s_fm_trace_active = 0;
        g_fm_write_trace_fn = NULL;
        fprintf(stderr, "[FM-TRACE] Captured %d frames\n", s_fm_trace_frame_count);
    }
}

/* =========================================================================
 * Memory write trace — watchlist-filtered 68K write logger.  Shares the
 * bus-main-m68k.c hook infrastructure so works in both native and oracle.
 * Output columns: wall_frame internal_frame game_mode address value a7
 *                 ret0 ret1 ret2 ret3
 *   internal_frame = v_vblank_count at $FFFE0C (longword)
 *   game_mode      = byte at $FFF600
 *   ret0..ret3     = 4 consecutive longs starting at A7 (68K stack)
 * ========================================================================= */

extern void (*g_mem_write_trace_fn)(uint32_t byte_address, uint8_t value, uint32_t target_cycle);

#define MEM_WRITE_LOG_MAX_WATCH 32
static FILE    *s_mem_write_log_file = NULL;
static int      s_mem_write_log_active = 0;
static int      s_mem_write_log_max_frames = 0;
static int      s_mem_write_log_frame_count = 0;
static uint32_t s_mem_write_log_watch_lo[MEM_WRITE_LOG_MAX_WATCH];
static uint32_t s_mem_write_log_watch_hi[MEM_WRITE_LOG_MAX_WATCH];
static int      s_mem_write_log_watch_count = 0;

/* Forward decls — emu_read8/emu_read32 are defined later in this file but
 * we need them in the callback above that definition site. */
static uint8_t  emu_read8 (uint32_t addr);
static uint32_t emu_read32(uint32_t addr);

static void mem_write_log_callback(uint32_t byte_address, uint8_t value, uint32_t target_cycle)
{
    if (!s_mem_write_log_file) return;
    /* Watchlist filter — small N, linear scan is fine. */
    int hit = 0;
    uint32_t watched_addr = byte_address & 0xFFFFFFu;
    for (int i = 0; i < s_mem_write_log_watch_count; i++) {
        if (watched_addr >= s_mem_write_log_watch_lo[i] &&
            watched_addr <= s_mem_write_log_watch_hi[i]) { hit = 1; break; }
    }
    if (!hit) return;

    uint32_t a7 = fm_trace_current_a7();
    uint32_t r0 = fm_trace_stack_read32(a7);
    uint32_t r1 = fm_trace_stack_read32(a7 + 4);
    uint32_t r2 = fm_trace_stack_read32(a7 + 8);
    uint32_t r3 = fm_trace_stack_read32(a7 + 12);

    uint32_t internal_frame = emu_read32(g_game_layout.vint_runcount_addr);
    uint8_t  game_mode      = emu_read8 (g_game_layout.game_mode_addr);

    extern uint64_t g_chunk_yield_count;
    fprintf(s_mem_write_log_file,
            "%d %u %u 0x%06X 0x%02X 0x%06X 0x%06X 0x%06X 0x%06X 0x%06X %u %llu "
#if SONIC_REVERSE_DEBUG && (ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE)
            "0x%06X 0x%06X "
            "D0=0x%08X D1=0x%08X D2=0x%08X D3=0x%08X D4=0x%08X D5=0x%08X D6=0x%08X D7=0x%08X "
            "A0=0x%08X A1=0x%08X A2=0x%08X A3=0x%08X A4=0x%08X A5=0x%08X A6=0x%08X A7=0x%08X"
#endif
            "\n",
            s_mem_write_log_frame_count,
            (unsigned)internal_frame,
            (unsigned)game_mode,
            byte_address & 0xFFFFFFu, value,
            a7 & 0xFFFFFFu, r0 & 0xFFFFFFu, r1 & 0xFFFFFFu,
            r2 & 0xFFFFFFu, r3 & 0xFFFFFFu,
            (unsigned)target_cycle,
            (unsigned long long)g_chunk_yield_count
#if SONIC_REVERSE_DEBUG && (ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE)
            , (unsigned)(g_rdb_current_func & 0xFFFFFFu),
            (unsigned)(g_cpu.PC & 0xFFFFFFu),
            (unsigned)g_cpu.D[0], (unsigned)g_cpu.D[1],
            (unsigned)g_cpu.D[2], (unsigned)g_cpu.D[3],
            (unsigned)g_cpu.D[4], (unsigned)g_cpu.D[5],
            (unsigned)g_cpu.D[6], (unsigned)g_cpu.D[7],
            (unsigned)g_cpu.A[0], (unsigned)g_cpu.A[1],
            (unsigned)g_cpu.A[2], (unsigned)g_cpu.A[3],
            (unsigned)g_cpu.A[4], (unsigned)g_cpu.A[5],
            (unsigned)g_cpu.A[6], (unsigned)g_cpu.A[7]
#endif
            );
}

/* Arm the logger directly (no TCP needed).  Returns 1 on success, 0 on
 * failure.  Safe to call before cmd_server_init.  Used by the --mem-write-log
 * CLI flag so we can capture writes from frame 0 (TCP arming has ~tens of
 * wall-frames of startup latency — too late to catch gm=0 boot music). */
int cmd_server_mem_write_log_start_ranges(const uint32_t *lo, const uint32_t *hi,
                                          int n_ranges, int frames, const char *path)
{
    if (n_ranges <= 0 || n_ranges > MEM_WRITE_LOG_MAX_WATCH) return 0;
    if (s_mem_write_log_file) fclose(s_mem_write_log_file);
    s_mem_write_log_file = fopen(path, "w");
    if (!s_mem_write_log_file) return 0;

    s_mem_write_log_watch_count = n_ranges;
    for (int i = 0; i < n_ranges; i++) {
        uint32_t a = lo[i] & 0xFFFFFFu;
        uint32_t b = hi[i] & 0xFFFFFFu;
        if (b < a) { uint32_t t = a; a = b; b = t; }
        s_mem_write_log_watch_lo[i] = a;
        s_mem_write_log_watch_hi[i] = b;
    }

    fprintf(s_mem_write_log_file,
        "# wall_frame internal_frame game_mode address value a7 ret0 ret1 ret2 ret3 target_cycle yield"
#if SONIC_REVERSE_DEBUG && (ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE)
        " func pc D0 D1 D2 D3 D4 D5 D6 D7 A0 A1 A2 A3 A4 A5 A6 A7"
#endif
        "\n");
    fprintf(s_mem_write_log_file, "# watching:");
    for (int i = 0; i < n_ranges; i++) {
        if (s_mem_write_log_watch_lo[i] == s_mem_write_log_watch_hi[i])
            fprintf(s_mem_write_log_file, " 0x%06X", s_mem_write_log_watch_lo[i]);
        else
            fprintf(s_mem_write_log_file, " 0x%06X-0x%06X",
                    s_mem_write_log_watch_lo[i], s_mem_write_log_watch_hi[i]);
    }
    fprintf(s_mem_write_log_file, "\n");

    s_mem_write_log_active = 1;
    s_mem_write_log_max_frames = frames > 0 ? frames : 0;
    s_mem_write_log_frame_count = 0;
    g_mem_write_trace_fn = mem_write_log_callback;
    fprintf(stderr, "[MEM-WRITE-LOG] Started (CLI): %s (%d addrs, %d frames)\n",
            path, n_ranges, frames);
    return 1;
}

int cmd_server_mem_write_log_start(const uint32_t *addrs, int n_addrs, int frames, const char *path)
{
    uint32_t hi[MEM_WRITE_LOG_MAX_WATCH];
    if (n_addrs <= 0 || n_addrs > MEM_WRITE_LOG_MAX_WATCH) return 0;
    for (int i = 0; i < n_addrs; i++) hi[i] = addrs[i];
    return cmd_server_mem_write_log_start_ranges(addrs, hi, n_addrs, frames, path);
}

void cmd_server_mem_write_log_tick(void)
{
    if (!s_mem_write_log_active) return;
    s_mem_write_log_frame_count++;
    if (s_mem_write_log_max_frames > 0 && s_mem_write_log_frame_count >= s_mem_write_log_max_frames) {
        fclose(s_mem_write_log_file);
        s_mem_write_log_file = NULL;
        s_mem_write_log_active = 0;
        g_mem_write_trace_fn = NULL;
        fprintf(stderr, "[MEM-WRITE-LOG] Captured %d frames\n", s_mem_write_log_frame_count);
    }
}

/* =========================================================================
 * Frame history ring buffer — full per-frame Genesis hardware snapshot.
 * Layout in frame_record.h; subsystem snapshot accessors in
 * frame_snapshots.c; per-game tail filled via game_extras hook.
 * ========================================================================= */

#include "frame_record.h"
#include "game_extras.h"
#include "game_spec.h"
#include "sonic_extras.h"

static FrameRecord s_frame_history[FRAME_HISTORY_CAP];
static uint32_t s_history_count = 0;  /* total frames recorded */

/* Watchpoints */
#define MAX_WATCHPOINTS 8
typedef struct {
    uint32_t addr;    /* 68K address */
    uint8_t  prev;    /* previous value */
    bool     active;
} Watchpoint;
static Watchpoint s_watchpoints[MAX_WATCHPOINTS];

/* =========================================================================
 * Read from clownmdemu RAM (word-addressed, big-endian)
 * ========================================================================= */

static uint8_t emu_read8(uint32_t addr)
{
    uint16_t offset = (uint16_t)(addr & 0xFFFF);
    uint16_t word = g_clownmdemu.state.m68k.ram[offset / 2];
    return (offset & 1) ? (uint8_t)(word & 0xFF) : (uint8_t)(word >> 8);
}

static uint16_t emu_read16(uint32_t addr)
{
    uint16_t offset = (uint16_t)(addr & 0xFFFF);
    return g_clownmdemu.state.m68k.ram[offset / 2];
}

static int16_t emu_read16s(uint32_t addr)
{
    return (int16_t)emu_read16(addr);
}

static uint32_t emu_read32(uint32_t addr)
{
    return ((uint32_t)emu_read16(addr) << 16) | emu_read16(addr + 2);
}

/* =========================================================================
 * Minimal JSON helpers (no external lib)
 * ========================================================================= */

static const char *json_get_str(const char *json, const char *key, char *out, int out_sz)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') {
        p++;
        int i = 0;
        while (*p && *p != '"' && i < out_sz - 1)
            out[i++] = *p++;
        out[i] = '\0';
        return out;
    }
    return NULL;
}

static int json_get_int(const char *json, const char *key, int def)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return def;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '-' || (*p >= '0' && *p <= '9'))
        return atoi(p);
    return def;
}

static uint32_t hex_to_u32(const char *s)
{
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    return (uint32_t)strtoul(s, NULL, 16);
}

/* =========================================================================
 * Send response
 * ========================================================================= */

static void send_response(const char *json)
{
    if (s_client == SOCK_INVALID) return;
    int len = (int)strlen(json);
    send(s_client, json, len, 0);
    send(s_client, "\n", 1, 0);
}

static void send_ok(int id)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"id\":%d,\"ok\":true}", id);
    send_response(buf);
}

static void send_err(int id, const char *msg)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "{\"id\":%d,\"ok\":false,\"error\":\"%s\"}", id, msg);
    send_response(buf);
}

/* Public exports for game extension modules (sonic_extras.c).
 * Same signatures as send_response/send_err but pulled out of file scope. */
void cmd_send_response(const char *json)       { send_response(json); }
void cmd_send_err(int id, const char *msg)     { send_err(id, msg); }

/* Shared accessors for in-tree tracers (reverse_debug.c today).
 * Wrap the static helpers above; we don't change their internal linkage
 * so existing call sites keep working unchanged. */
uint32_t cmd_server_current_frame(void) { return s_current_frame; }
uint32_t cmd_server_current_a7(void)    { return fm_trace_current_a7(); }
uint32_t cmd_server_stack_read32(uint32_t a) { return fm_trace_stack_read32(a); }

/* =========================================================================
 * Command handlers
 * ========================================================================= */

static void handle_ping(int id, uint32_t frame_num)
{
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"frame\":%u}", id, frame_num);
    send_response(buf);
}

static void handle_get_registers(int id)
{
#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,"
        "\"D0\":%u,\"D1\":%u,\"D2\":%u,\"D3\":%u,"
        "\"D4\":%u,\"D5\":%u,\"D6\":%u,\"D7\":%u,"
        "\"A0\":%u,\"A1\":%u,\"A2\":%u,\"A3\":%u,"
        "\"A4\":%u,\"A5\":%u,\"A6\":%u,\"A7\":%u,"
        "\"PC\":%u,\"SR\":%u,\"USP\":%u,"
        "\"C\":%d,\"V\":%d,\"Z\":%d,\"N\":%d,\"X\":%d,"
        "\"S\":%d,\"imask\":%d}",
        id,
        g_cpu.D[0], g_cpu.D[1], g_cpu.D[2], g_cpu.D[3],
        g_cpu.D[4], g_cpu.D[5], g_cpu.D[6], g_cpu.D[7],
        g_cpu.A[0], g_cpu.A[1], g_cpu.A[2], g_cpu.A[3],
        g_cpu.A[4], g_cpu.A[5], g_cpu.A[6], g_cpu.A[7],
        g_cpu.PC, (uint32_t)g_cpu.SR, g_cpu.USP,
        (g_cpu.SR & SR_C) ? 1 : 0,
        (g_cpu.SR & SR_V) ? 1 : 0,
        (g_cpu.SR & SR_Z) ? 1 : 0,
        (g_cpu.SR & SR_N) ? 1 : 0,
        (g_cpu.SR & SR_X) ? 1 : 0,
        (g_cpu.SR & SR_S) ? 1 : 0,
        (int)((g_cpu.SR >> 8) & 7));
    send_response(buf);
#else
    send_err(id, "get_registers requires Step 2 or Hybrid build");
#endif
}

static uint8_t bus_read8(uint32_t addr)
{
    /* For work RAM range, use direct emu read (always available) */
    uint32_t masked = addr & 0xFFFFFF;
    if (masked >= 0xFF0000)
        return emu_read8(masked);
#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
    return m68k_read8(addr);
#else
    /* Step 0: ROM access via clownmdemu cartridge buffer */
    if (masked < 0x400000) {
        uint16_t word = g_clownmdemu.cartridge_buffer[masked / 2];
        return (masked & 1) ? (uint8_t)(word & 0xFF) : (uint8_t)(word >> 8);
    }
    return 0xFF;
#endif
}

static void handle_read_memory(int id, const char *json)
{
    char addr_str[32];
    if (!json_get_str(json, "addr", addr_str, sizeof(addr_str))) {
        send_err(id, "missing addr");
        return;
    }
    int size = json_get_int(json, "size", 16);
    if (size <= 0 || size > 4096) {
        send_err(id, "size must be 1-4096");
        return;
    }
    uint32_t addr = hex_to_u32(addr_str);

    char *hex = (char *)malloc(size * 2 + 1);
    for (int i = 0; i < size; i++) {
        uint8_t b = bus_read8(addr + i);
        sprintf(hex + i * 2, "%02X", b);
    }
    hex[size * 2] = '\0';

    char *resp = (char *)malloc(size * 2 + 256);
    snprintf(resp, size * 2 + 256,
             "{\"id\":%d,\"ok\":true,\"addr\":\"0x%06X\",\"size\":%d,\"hex\":\"%s\"}",
             id, addr & 0xFFFFFF, size, hex);
    send_response(resp);
    free(hex);
    free(resp);
}

static void handle_write_memory(int id, const char *json)
{
    char addr_str[32], hex[8192];
    if (!json_get_str(json, "addr", addr_str, sizeof(addr_str))) {
        send_err(id, "missing addr");
        return;
    }
    if (!json_get_str(json, "hex", hex, sizeof(hex))) {
        send_err(id, "missing hex data");
        return;
    }
    uint32_t addr = hex_to_u32(addr_str);
    int len = (int)strlen(hex) / 2;

#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
    for (int i = 0; i < len; i++) {
        char byte_str[3] = { hex[i*2], hex[i*2+1], '\0' };
        uint8_t val = (uint8_t)strtoul(byte_str, NULL, 16);
        m68k_write8(addr + i, val);
    }
#else
    /* Step 0: only support writing to work RAM */
    uint32_t masked = addr & 0xFFFFFF;
    if (masked < 0xFF0000) {
        send_err(id, "write_memory only supports RAM ($FF0000+) in Step 0");
        return;
    }
    for (int i = 0; i < len; i++) {
        char byte_str[3] = { hex[i*2], hex[i*2+1], '\0' };
        uint8_t val = (uint8_t)strtoul(byte_str, NULL, 16);
        uint16_t off = (uint16_t)((masked + i) & 0xFFFF);
        uint16_t word = g_clownmdemu.state.m68k.ram[off / 2];
        if (off & 1)
            word = (word & 0xFF00) | val;
        else
            word = (val << 8) | (word & 0xFF);
        g_clownmdemu.state.m68k.ram[off / 2] = word;
    }
#endif

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"id\":%d,\"ok\":true,\"bytes_written\":%d}", id, len);
    send_response(buf);
}

static void handle_read_ram(int id, const char *json)
{
    /* Direct read from g_ram[] shadow — no bus routing */
    char addr_str[32];
    if (!json_get_str(json, "addr", addr_str, sizeof(addr_str))) {
        send_err(id, "missing addr");
        return;
    }
    int size = json_get_int(json, "size", 16);
    if (size <= 0 || size > 4096) {
        send_err(id, "size must be 1-4096");
        return;
    }
    uint32_t addr = hex_to_u32(addr_str);
    uint16_t offset = (uint16_t)(addr & 0xFFFF);

    char *hex = (char *)malloc(size * 2 + 1);
    for (int i = 0; i < size; i++) {
        uint8_t b = emu_read8(offset + i);
        sprintf(hex + i * 2, "%02X", b);
    }
    hex[size * 2] = '\0';

    char *resp = (char *)malloc(size * 2 + 256);
    snprintf(resp, size * 2 + 256,
             "{\"id\":%d,\"ok\":true,\"addr\":\"0x%04X\",\"size\":%d,\"hex\":\"%s\"}",
             id, offset, size, hex);
    send_response(resp);
    free(hex);
    free(resp);
}

/* sonic_state and object_table moved to runner/sonic_extras.c — they
 * are dispatched via game_handle_debug_cmd(). sonic_history stays here
 * because it walks the framework ring buffer; it just casts game_data
 * to SonicGameData (see sonic_extras.h) to decode each frame. */

static void handle_sonic_history(int id, const char *json)
{
    int start = json_get_int(json, "start", -1);
    int end   = json_get_int(json, "end",   -1);
    if (start < 0 || end < 0 || end < start) {
        send_err(id, "invalid start/end");
        return;
    }
    if (end - start + 1 > 600) {
        send_err(id, "max 600 frames per request");
        return;
    }

    int nframes = end - start + 1;
    size_t buf_size = (size_t)nframes * 256 + 256;
    char *buf = (char *)malloc(buf_size);
    if (!buf) { send_err(id, "alloc failed"); return; }

    int pos = 0;
    pos += snprintf(buf + pos, buf_size - pos,
                    "{\"id\":%d,\"ok\":true,\"frames\":[", id);

    for (int f = start; f <= end; f++) {
        /* Find frame in ring buffer */
        if (f > start) buf[pos++] = ',';

        bool found = false;
        if (s_history_count > 0) {
            /* Calculate ring buffer position */
            uint32_t oldest = (s_history_count > FRAME_HISTORY_CAP)
                ? s_history_count - FRAME_HISTORY_CAP : 0;
            if ((uint32_t)f >= oldest && (uint32_t)f < s_history_count) {
                uint32_t idx = (uint32_t)f % FRAME_HISTORY_CAP;
                const FrameRecord *r = &s_frame_history[idx];
                if (r->frame == (uint32_t)f) {
                    const SonicGameData *sd = sonic_extras_view(r->game_data);
                    pos += snprintf(buf + pos, buf_size - pos,
                        "{\"frame\":%u,\"x\":%u,\"y\":%u,"
                        "\"xvel\":%d,\"yvel\":%d,\"inertia\":%d,"
                        "\"routine\":%u,\"status\":%u,\"angle\":%u,"
                        "\"game_mode\":%u,\"joy_held\":%u,\"joy_press\":%u}",
                        r->frame, sd->sonic_x, sd->sonic_y,
                        (int)sd->sonic_xvel, (int)sd->sonic_yvel,
                        (int)sd->sonic_inertia,
                        sd->sonic_routine, sd->sonic_status, sd->sonic_angle,
                        sd->game_mode, sd->joy_held, sd->joy_press);
                    found = true;
                }
            }
        }
        if (!found) {
            pos += snprintf(buf + pos, buf_size - pos,
                "{\"frame\":%d,\"available\":false}", f);
        }

        /* Grow buffer if needed */
        if ((size_t)pos > buf_size - 512) {
            buf_size *= 2;
            buf = (char *)realloc(buf, buf_size);
            if (!buf) return;
        }
    }

    pos += snprintf(buf + pos, buf_size - pos, "]}");
    send_response(buf);
    free(buf);
}

static void handle_addr_history(int id, const char *json)
{
    char addr_str[32];
    if (!json_get_str(json, "addr", addr_str, sizeof(addr_str))) {
        send_err(id, "missing addr");
        return;
    }
    int start = json_get_int(json, "start", -1);
    int end   = json_get_int(json, "end",   -1);
    int size  = json_get_int(json, "size",   1);  /* 1=byte, 2=word */
    if (start < 0 || end < 0 || end < start) {
        send_err(id, "invalid start/end");
        return;
    }
    if (end - start + 1 > 600) {
        send_err(id, "max 600 frames per request");
        return;
    }
    uint32_t addr = hex_to_u32(addr_str);

    /* For addr_history, we just read the current value since we don't store
     * arbitrary address history. Return current value and frame range info. */
    send_err(id, "addr_history requires watchpoint recording (use watch + sonic_history for now)");
}

static void handle_frame_info(int id)
{
    uint32_t oldest = (s_history_count > FRAME_HISTORY_CAP)
        ? s_history_count - FRAME_HISTORY_CAP : 0;
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"current_frame\":%u,"
        "\"oldest_frame\":%u,\"capacity\":%u}",
        id, s_history_count, oldest, (uint32_t)FRAME_HISTORY_CAP);
    send_response(buf);
}

static void handle_frame_range(int id, const char *json)
{
    int start = json_get_int(json, "start", -1);
    int end   = json_get_int(json, "end",   -1);
    if (start < 0 || end < 0 || end < start) {
        send_err(id, "invalid start/end");
        return;
    }
    if (end - start + 1 > 600) {
        send_err(id, "max 600 frames per request");
        return;
    }

    int nframes = end - start + 1;
    size_t buf_size = (size_t)nframes * 256 + 256;
    char *buf = (char *)malloc(buf_size);
    if (!buf) { send_err(id, "alloc failed"); return; }

    int pos = 0;
    pos += snprintf(buf + pos, buf_size - pos,
                    "{\"id\":%d,\"ok\":true,\"frames\":[", id);

    for (int f = start; f <= end; f++) {
        if (f > start) buf[pos++] = ',';

        bool found = false;
        if (s_history_count > 0) {
            uint32_t oldest = (s_history_count > FRAME_HISTORY_CAP)
                ? s_history_count - FRAME_HISTORY_CAP : 0;
            if ((uint32_t)f >= oldest && (uint32_t)f < s_history_count) {
                uint32_t idx = (uint32_t)f % FRAME_HISTORY_CAP;
                const FrameRecord *r = &s_frame_history[idx];
                if (r->frame == (uint32_t)f) {
                    const SonicGameData *sd = sonic_extras_view(r->game_data);
                    pos += snprintf(buf + pos, buf_size - pos,
                        "{\"frame\":%u,\"game_mode\":%u,\"yvel\":%d,"
                        "\"routine\":%u,\"joy\":%u,\"scroll_x\":%u}",
                        r->frame, sd->game_mode, (int)sd->sonic_yvel,
                        sd->sonic_routine, sd->joy_held, sd->scroll_x);
                    found = true;
                }
            }
        }
        if (!found) {
            pos += snprintf(buf + pos, buf_size - pos,
                "{\"frame\":%d,\"available\":false}", f);
        }

        if ((size_t)pos > buf_size - 512) {
            buf_size *= 2;
            buf = (char *)realloc(buf, buf_size);
            if (!buf) return;
        }
    }

    pos += snprintf(buf + pos, buf_size - pos, "]}");
    send_response(buf);
    free(buf);
}

static void handle_vblank_info(int id)
{
#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"cycle_accum\":%u,\"threshold\":%u,"
        "\"imask\":%d,\"frame_count\":%llu}",
        id, g_cycle_accumulator, g_vblank_threshold,
        (int)((g_cpu.SR >> 8) & 7),
        (unsigned long long)g_frame_count);
    send_response(buf);
#else
    send_err(id, "vblank_info requires Step 2 or Hybrid build");
#endif
}

/* object_table moved to runner/sonic_extras.c. */

static void handle_watch(int id, const char *json)
{
    char addr_str[32];
    if (!json_get_str(json, "addr", addr_str, sizeof(addr_str))) {
        send_err(id, "missing addr");
        return;
    }
    uint32_t addr = hex_to_u32(addr_str);

    for (int i = 0; i < MAX_WATCHPOINTS; i++) {
        if (!s_watchpoints[i].active) {
            s_watchpoints[i].addr = addr;
            s_watchpoints[i].prev = emu_read8(addr & 0xFFFF);
            s_watchpoints[i].active = true;
            char buf[128];
            snprintf(buf, sizeof(buf),
                "{\"id\":%d,\"ok\":true,\"slot\":%d,\"addr\":\"0x%04X\"}",
                id, i, addr & 0xFFFF);
            send_response(buf);
            return;
        }
    }
    send_err(id, "all 8 watchpoint slots full");
}

static void handle_unwatch(int id, const char *json)
{
    char addr_str[32];
    if (!json_get_str(json, "addr", addr_str, sizeof(addr_str))) {
        send_err(id, "missing addr");
        return;
    }
    uint32_t addr = hex_to_u32(addr_str);

    for (int i = 0; i < MAX_WATCHPOINTS; i++) {
        if (s_watchpoints[i].active && s_watchpoints[i].addr == addr) {
            s_watchpoints[i].active = false;
            char buf[128];
            snprintf(buf, sizeof(buf),
                "{\"id\":%d,\"ok\":true,\"cleared\":\"0x%04X\"}",
                id, addr & 0xFFFF);
            send_response(buf);
            return;
        }
    }
    send_err(id, "watchpoint not found");
}

static void handle_read_vram(int id, const char *json)
{
    char addr_str[32];
    if (!json_get_str(json, "addr", addr_str, sizeof(addr_str))) {
        send_err(id, "missing addr");
        return;
    }
    int size = json_get_int(json, "size", 32);
    if (size <= 0 || size > 4096) { send_err(id, "size must be 1-4096"); return; }
    uint32_t addr = hex_to_u32(addr_str);
    if (addr + size > 0x10000) { send_err(id, "addr+size exceeds 64KB VRAM"); return; }

    char *hex = (char *)malloc(size * 2 + 1);
    for (int i = 0; i < size; i++)
        sprintf(hex + i * 2, "%02X", g_clownmdemu.vdp.state.vram[addr + i]);
    hex[size * 2] = '\0';

    char *resp = (char *)malloc(size * 2 + 256);
    snprintf(resp, size * 2 + 256,
             "{\"id\":%d,\"ok\":true,\"addr\":\"0x%04X\",\"size\":%d,\"hex\":\"%s\"}",
             id, addr, size, hex);
    send_response(resp);
    free(hex);
    free(resp);
}

static void handle_read_cram(int id)
{
    /* CRAM: 64 entries × 16-bit = 128 bytes. Return as hex. */
    char hex[256 + 1];
    for (int i = 0; i < 64; i++) {
        uint16_t c = g_clownmdemu.vdp.state.cram[i];
        sprintf(hex + i * 4, "%04X", c);
    }
    hex[256] = '\0';
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"entries\":64,\"hex\":\"%s\"}", id, hex);
    send_response(buf);
}

static void handle_dump_vram(int id, const char *json)
{
    /* Dump VRAM region to a file for offline comparison */
    char path[256];
    if (!json_get_str(json, "path", path, sizeof(path)))
        strcpy(path, "vram_dump.bin");
    int offset = json_get_int(json, "offset", 0);
    int size = json_get_int(json, "size", 0x10000);
    if (offset < 0 || offset + size > 0x10000) {
        send_err(id, "offset+size exceeds 64KB VRAM");
        return;
    }
    FILE *f = fopen(path, "wb");
    if (!f) { send_err(id, "cannot open file"); return; }
    fwrite(g_clownmdemu.vdp.state.vram + offset, 1, size, f);
    fclose(f);
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"path\":\"%s\",\"offset\":%d,\"size\":%d}",
        id, path, offset, size);
    send_response(buf);
}

static void handle_audio_stats(int id)
{
    AudioStats st;
    audio_get_stats(&st);
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,"
        "\"last_fm_frames\":%zu,\"last_psg_frames\":%zu,"
        "\"total_fm_frames\":%zu,\"total_psg_frames\":%zu,"
        "\"total_flushes\":%u,\"dropped_flushes\":%u,"
        "\"wav_active\":%d}",
        id, st.last_fm_frames, st.last_psg_frames,
        st.total_fm_frames, st.total_psg_frames,
        st.total_flushes, st.dropped_flushes,
        audio_wav_active());
    send_response(buf);
}

static void handle_audio_wav(int id, const char *json)
{
    char action[32];
    if (!json_get_str(json, "action", action, sizeof(action))) {
        send_err(id, "missing action (start/stop/status)");
        return;
    }
    if (strcmp(action, "start") == 0) {
        char path[256];
        if (!json_get_str(json, "path", path, sizeof(path)))
            strcpy(path, "audio_capture.wav");
        if (audio_wav_start(path) == 0)
            send_ok(id);
        else
            send_err(id, "cannot open wav file");
    } else if (strcmp(action, "stop") == 0) {
        audio_wav_stop();
        send_ok(id);
    } else if (strcmp(action, "status") == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf),
            "{\"id\":%d,\"ok\":true,\"active\":%d}", id, audio_wav_active());
        send_response(buf);
    } else {
        send_err(id, "action must be start/stop/status");
    }
}

static void handle_io_log(int id, const char *json)
{
#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
    int enable = json_get_int(json, "enable", -1);
    if (enable >= 0) {
        s_io_log_enabled = enable;
        s_io_log_count = 0;
    }
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"enabled\":%d,\"logged\":%d}",
        id, s_io_log_enabled, s_io_log_count);
    send_response(buf);
#else
    send_err(id, "io_log requires Step 2 or Hybrid build");
#endif
}

static void handle_read_joypad_port(int id)
{
    /* Manually read the joypad port the same way ReadJoypads does.
     * Phase 1: write 0x00 to $A10003, read $A10003
     * Phase 2: write 0x40 to $A10003, read $A10003
     * Combine and invert. */
#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
    extern uint8_t m68k_read8(uint32_t);
    extern void m68k_write8(uint32_t, uint8_t);

    /* TH=0 phase */
    m68k_write8(0xA10003, 0x00);
    uint8_t phase0 = m68k_read8(0xA10003);

    /* TH=1 phase */
    m68k_write8(0xA10003, 0x40);
    uint8_t phase1 = m68k_read8(0xA10003);

    /* Combine: phase0 bits 6-7 (shifted from Start,A), phase1 bits 0-5 (CBRLDU) */
    uint8_t combined = ((phase0 << 2) & 0xC0) | (phase1 & 0x3F);
    uint8_t buttons = ~combined;  /* invert: hardware is active-low */

    /* Also read current $F604/$F605 for comparison */
    uint8_t f604 = emu_read8(0xF604);
    uint8_t f605 = emu_read8(0xF605);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,"
        "\"phase0_raw\":\"0x%02X\",\"phase1_raw\":\"0x%02X\","
        "\"combined\":\"0x%02X\",\"buttons\":\"0x%02X\","
        "\"F604_held\":\"0x%02X\",\"F605_press\":\"0x%02X\"}",
        id, phase0, phase1, combined, buttons, f604, f605);
    send_response(buf);
#else
    send_err(id, "read_joypad_port requires Step 2 or Hybrid build");
#endif
}

/* =========================================================================
 * Phase 4 — full ring-buffer queries + live subsystem snapshots
 *
 * These commands surface the per-frame hardware snapshot captured in
 * cmd_server_record_frame() (see frame_record.h). For the audio-debug
 * use case the consumer is tools/compare_runs.py which polls both
 * native and oracle and walks until first divergence.
 * ========================================================================= */

/* Hex-encode a byte buffer into out (must hold at least 2*len+1 bytes,
 * caller-allocated). Trailing NUL written. Returns bytes written. */
static int hex_encode(const uint8_t *in, int len, char *out)
{
    static const char H[] = "0123456789abcdef";
    int i;
    for (i = 0; i < len; i++) {
        out[i * 2 + 0] = H[(in[i] >> 4) & 0xF];
        out[i * 2 + 1] = H[ in[i]       & 0xF];
    }
    out[len * 2] = '\0';
    return len * 2;
}

/* Locate a frame in the ring; returns NULL if not present. */
static const FrameRecord *frame_lookup(uint32_t f)
{
    if (s_history_count == 0) return NULL;
    uint32_t oldest = (s_history_count > FRAME_HISTORY_CAP)
        ? s_history_count - FRAME_HISTORY_CAP : 0;
    if (f < oldest || f >= s_history_count) return NULL;
    const FrameRecord *r = &s_frame_history[f % FRAME_HISTORY_CAP];
    if (r->frame != f) return NULL;
    return r;
}

/* True if "include" string contains the named field token. The include
 * arg is a comma-list, e.g. "m68k,vdp,vram". A NULL or empty include
 * returns true for the small fields and false for blob fields. */
static bool include_has(const char *include, const char *tok)
{
    if (!include || !*include) return false;
    size_t tl = strlen(tok);
    const char *p = include;
    while (*p) {
        const char *c = strchr(p, ',');
        size_t n = c ? (size_t)(c - p) : strlen(p);
        if (n == tl && strncmp(p, tok, tl) == 0) return true;
        if (!c) break;
        p = c + 1;
    }
    return false;
}

/* Dynamic JSON buffer, grows as needed. */
typedef struct { char *buf; size_t len, cap; } JBuf;
static void jb_init(JBuf *j) { j->cap = 4096; j->len = 0; j->buf = (char *)malloc(j->cap); j->buf[0] = '\0'; }
static void jb_free(JBuf *j) { free(j->buf); j->buf = NULL; }
static void jb_reserve(JBuf *j, size_t extra)
{
    if (j->len + extra + 1 > j->cap) {
        while (j->len + extra + 1 > j->cap) j->cap *= 2;
        j->buf = (char *)realloc(j->buf, j->cap);
    }
}
static void jb_printf(JBuf *j, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* probe size */
    va_list ap2; va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) { va_end(ap); return; }
    jb_reserve(j, (size_t)n);
    vsnprintf(j->buf + j->len, j->cap - j->len, fmt, ap);
    j->len += n;
    va_end(ap);
}
static void jb_append_hex(JBuf *j, const uint8_t *data, int len)
{
    jb_reserve(j, (size_t)len * 2 + 4);
    j->buf[j->len++] = '"';
    j->len += hex_encode(data, len, j->buf + j->len);
    j->buf[j->len++] = '"';
    j->buf[j->len] = '\0';
}

/* ---------- Per-snapshot JSON serializers ---------- */

static void json_m68k(JBuf *j, const M68KRegSnap *m)
{
    jb_printf(j, "\"m68k\":{\"D\":[%u,%u,%u,%u,%u,%u,%u,%u],"
                 "\"A\":[%u,%u,%u,%u,%u,%u,%u,%u],"
                 "\"USP\":%u,\"PC\":%u,\"SR\":%u,"
                 "\"flags\":{\"C\":%u,\"V\":%u,\"Z\":%u,\"N\":%u,\"X\":%u,\"S\":%u,\"imask\":%u}}",
        m->D[0], m->D[1], m->D[2], m->D[3], m->D[4], m->D[5], m->D[6], m->D[7],
        m->A[0], m->A[1], m->A[2], m->A[3], m->A[4], m->A[5], m->A[6], m->A[7],
        m->USP, m->PC, m->SR,
        m->flag_C, m->flag_V, m->flag_Z, m->flag_N, m->flag_X, m->flag_S, m->imask);
}

static void json_z80(JBuf *j, const Z80RegSnap *z, bool include_ram)
{
    jb_printf(j,
        "\"z80\":{\"A\":%u,\"F\":%u,\"B\":%u,\"C\":%u,\"D\":%u,\"E\":%u,\"H\":%u,\"L\":%u,"
        "\"Ap\":%u,\"Fp\":%u,\"Bp\":%u,\"Cp\":%u,\"Dp\":%u,\"Ep\":%u,\"Hp\":%u,\"Lp\":%u,"
        "\"IXH\":%u,\"IXL\":%u,\"IYH\":%u,\"IYL\":%u,\"I\":%u,\"R\":%u,"
        "\"SP\":%u,\"PC\":%u,\"iff\":%u,\"irq_pending\":%u,"
        "\"bus_requested\":%u,\"reset_held\":%u,\"bank\":%u",
        z->A, z->F, z->B, z->C, z->D, z->E, z->H, z->L,
        z->Ap, z->Fp, z->Bp, z->Cp, z->Dp, z->Ep, z->Hp, z->Lp,
        z->IXH, z->IXL, z->IYH, z->IYL, z->I, z->R,
        z->SP, z->PC, z->iff_enabled, z->irq_pending,
        z->bus_requested, z->reset_held, z->bank);
    if (include_ram) {
        jb_printf(j, ",\"ram\":");
        jb_append_hex(j, z->ram, sizeof(z->ram));
    }
    jb_printf(j, "}");
}

static void json_vdp(JBuf *j, const VdpSnap *v, const char *include)
{
    jb_printf(j,
        "\"vdp\":{\"plane_a\":%u,\"plane_b\":%u,\"window\":%u,\"sprite_table\":%u,\"hscroll\":%u,"
        "\"access_addr\":%u,\"access_code\":%u,\"increment\":%u,"
        "\"display\":%u,\"vint\":%u,\"hint\":%u,\"h40\":%u,\"v30\":%u,"
        "\"shadow_hl\":%u,\"bg_color\":%u,\"hint_int\":%u,"
        "\"plane_w_shift\":%u,\"plane_h_mask\":%u,\"hscroll_mask\":%u,\"vscroll_mode\":%u,"
        "\"in_vblank\":%u,\"dma\":%u,\"dma_mode\":%u,\"dma_len\":%u,\"dma_src\":%u",
        v->plane_a_addr, v->plane_b_addr, v->window_addr, v->sprite_table_addr, v->hscroll_addr,
        v->access_address, v->access_code, v->access_increment,
        v->display_enabled, v->v_int_enabled, v->h_int_enabled, v->h40_enabled, v->v30_enabled,
        v->shadow_highlight_enabled, v->background_colour, v->h_int_interval,
        v->plane_width_shift, v->plane_height_bitmask, v->hscroll_mask, v->vscroll_mode,
        v->currently_in_vblank, v->dma_enabled, v->dma_mode, v->dma_length, v->dma_source);
    if (include_has(include, "vram")) {
        jb_printf(j, ",\"vram\":");
        jb_append_hex(j, v->vram, sizeof(v->vram));
    }
    if (include_has(include, "cram")) {
        jb_printf(j, ",\"cram\":[");
        for (int i = 0; i < 64; i++) jb_printf(j, "%s%u", i ? "," : "", v->cram[i]);
        jb_printf(j, "]");
    }
    if (include_has(include, "vsram")) {
        jb_printf(j, ",\"vsram\":[");
        for (int i = 0; i < 64; i++) jb_printf(j, "%s%u", i ? "," : "", v->vsram[i]);
        jb_printf(j, "]");
    }
    jb_printf(j, "}");
}

static void json_fm(JBuf *j, const FmSnap *fm)
{
    jb_printf(j, "\"fm\":{\"len\":%u,\"raw\":", (unsigned)fm->raw_len);
    jb_append_hex(j, fm->raw, fm->raw_len);
    jb_printf(j, "}");
}

static void json_psg(JBuf *j, const PsgSnap *p)
{
    jb_printf(j, "\"psg\":{\"len\":%u,\"raw\":", (unsigned)p->raw_len);
    jb_append_hex(j, p->raw, p->raw_len);
    jb_printf(j, "}");
}

static void json_game_data(JBuf *j, const uint8_t *gd)
{
    /* Sonic-decoded view + raw hex for general consumers. */
    const SonicGameData *sd = sonic_extras_view(gd);
    jb_printf(j,
        "\"game_data\":{\"sonic\":{\"version\":%u,\"game_mode\":%u,\"vblank_flag\":%u,"
        "\"joy_held\":%u,\"joy_press\":%u,\"scroll_x\":%u,"
        "\"x\":%u,\"y\":%u,\"xvel\":%d,\"yvel\":%d,\"inertia\":%d,"
        "\"routine\":%u,\"status\":%u,\"angle\":%u,\"obj_id\":%u,"
        "\"internal_frame\":%u},"
        "\"raw\":",
        sd->version, sd->game_mode, sd->vblank_flag,
        sd->joy_held, sd->joy_press, sd->scroll_x,
        sd->sonic_x, sd->sonic_y, (int)sd->sonic_xvel, (int)sd->sonic_yvel, (int)sd->sonic_inertia,
        sd->sonic_routine, sd->sonic_status, sd->sonic_angle, sd->sonic_obj_id,
        sd->internal_frame_ctr);
    jb_append_hex(j, gd, 64);
    jb_printf(j, "}");
}

/* ---------- get_frame ---------- */

static void handle_get_frame(int id, const char *json)
{
    int f = json_get_int(json, "frame", -1);
    if (f < 0) { send_err(id, "missing or invalid frame"); return; }
    const FrameRecord *r = frame_lookup((uint32_t)f);
    if (!r) { send_err(id, "frame not in ring buffer"); return; }

    char include_buf[256] = {0};
    json_get_str(json, "include", include_buf, sizeof(include_buf));
    const char *inc = include_buf;
    bool inc_all = include_has(inc, "all");

    JBuf j; jb_init(&j);
    jb_printf(&j, "{\"id\":%d,\"ok\":true,\"frame\":%u,\"verify_pass\":%d,",
              id, r->frame, r->verify_pass);

    json_m68k(&j, &r->m68k);
    jb_printf(&j, ",");
    json_z80(&j, &r->z80, inc_all || include_has(inc, "z80_ram"));
    jb_printf(&j, ",");
    json_vdp(&j, &r->vdp, inc_all ? "vram,cram,vsram" : inc);
    jb_printf(&j, ",");
    json_fm(&j, &r->fm);
    jb_printf(&j, ",");
    json_psg(&j, &r->psg);
    jb_printf(&j, ",");
    json_game_data(&j, r->game_data);
    if (inc_all || include_has(inc, "wram")) {
        jb_printf(&j, ",\"wram\":");
        jb_append_hex(&j, r->wram, sizeof(r->wram));
    }
    jb_printf(&j, "}");
    cmd_send_response(j.buf);
    jb_free(&j);
}

/* ---------- frame_timeseries ---------- */
/* Returns a single field across a frame range. Avoids the cost of
 * marshaling the full record for many frames when you only need one
 * scalar (e.g., "fm.raw[0x28] across frames 100-400"). */

static void handle_frame_timeseries(int id, const char *json)
{
    int from = json_get_int(json, "from", -1);
    int to   = json_get_int(json, "to",   -1);
    char field[64] = {0};
    if (from < 0 || to < 0 || to < from) { send_err(id, "invalid from/to"); return; }
    if (to - from + 1 > FRAME_HISTORY_CAP) { send_err(id, "range exceeds ring"); return; }
    if (!json_get_str(json, "field", field, sizeof(field))) { send_err(id, "missing field"); return; }

    JBuf j; jb_init(&j);
    jb_printf(&j, "{\"id\":%d,\"ok\":true,\"field\":\"%s\",\"values\":[", id, field);

    for (int f = from; f <= to; f++) {
        if (f > from) jb_printf(&j, ",");
        const FrameRecord *r = frame_lookup((uint32_t)f);
        if (!r) { jb_printf(&j, "null"); continue; }
        const SonicGameData *sd = sonic_extras_view(r->game_data);

        /* Field aliases — extend as needed. */
        if      (strcmp(field, "sonic.x")        == 0) jb_printf(&j, "%u",  sd->sonic_x);
        else if (strcmp(field, "sonic.y")        == 0) jb_printf(&j, "%u",  sd->sonic_y);
        else if (strcmp(field, "sonic.xvel")     == 0) jb_printf(&j, "%d",  sd->sonic_xvel);
        else if (strcmp(field, "sonic.yvel")     == 0) jb_printf(&j, "%d",  sd->sonic_yvel);
        else if (strcmp(field, "sonic.routine")  == 0) jb_printf(&j, "%u",  sd->sonic_routine);
        else if (strcmp(field, "game_mode")      == 0) jb_printf(&j, "%u",  sd->game_mode);
        else if (strcmp(field, "internal_frame") == 0) jb_printf(&j, "%u",  sd->internal_frame_ctr);
        else if (strcmp(field, "scroll_x")       == 0) jb_printf(&j, "%u",  sd->scroll_x);
        else if (strcmp(field, "m68k.SR")        == 0) jb_printf(&j, "%u",  r->m68k.SR);
        else if (strcmp(field, "m68k.A7")        == 0) jb_printf(&j, "%u",  r->m68k.A[7]);
        else if (strcmp(field, "z80.PC")         == 0) jb_printf(&j, "%u",  r->z80.PC);
        else if (strcmp(field, "z80.SP")         == 0) jb_printf(&j, "%u",  r->z80.SP);
        else if (strcmp(field, "vdp.access_addr")== 0) jb_printf(&j, "%u",  r->vdp.access_address);
        else if (strcmp(field, "verify_pass")    == 0) jb_printf(&j, "%d",  r->verify_pass);
        /* Generic memory taps — accept "wram[HEX]" / "wram16[HEX]" /
         * "wram32[HEX]" for arbitrary-address timeseries against the
         * 64 KB 68K work-RAM snapshot in this frame. HEX is a 16-bit
         * offset (low 16 bits of $FF0000-$FFFFFF). */
        else if (strncmp(field, "wram[",   5) == 0) {
            unsigned a = (unsigned)strtoul(field + 5, NULL, 16) & 0xFFFFu;
            jb_printf(&j, "%u", (unsigned)r->wram[a]);
        }
        else if (strncmp(field, "wram16[", 7) == 0) {
            unsigned a = (unsigned)strtoul(field + 7, NULL, 16) & 0xFFFEu;
            jb_printf(&j, "%u", ((unsigned)r->wram[a] << 8) | r->wram[a+1]);
        }
        else if (strncmp(field, "wram32[", 7) == 0) {
            unsigned a = (unsigned)strtoul(field + 7, NULL, 16) & 0xFFFCu;
            jb_printf(&j, "%u",
                ((unsigned)r->wram[a]   << 24) |
                ((unsigned)r->wram[a+1] << 16) |
                ((unsigned)r->wram[a+2] <<  8) |
                 (unsigned)r->wram[a+3]);
        }
        else if (strncmp(field, "z80ram[", 7) == 0) {
            unsigned a = (unsigned)strtoul(field + 7, NULL, 16) & 0x1FFFu;
            jb_printf(&j, "%u", (unsigned)r->z80.ram[a]);
        }
        else if (strncmp(field, "fm[",     3) == 0) {
            unsigned a = (unsigned)strtoul(field + 3, NULL, 16);
            jb_printf(&j, "%u", a < r->fm.raw_len ? (unsigned)r->fm.raw[a] : 0u);
        }
        else                                            jb_printf(&j, "null");
    }
    jb_printf(&j, "]}");
    cmd_send_response(j.buf);
    jb_free(&j);
}

/* ---------- live state snapshots ---------- */

static void handle_z80_state(int id, const char *json)
{
    Z80RegSnap z; z80_snapshot(&z, &g_clownmdemu);
    bool with_ram = json_get_int(json, "include_ram", 0) != 0;
    JBuf j; jb_init(&j);
    jb_printf(&j, "{\"id\":%d,\"ok\":true,", id);
    json_z80(&j, &z, with_ram);
    jb_printf(&j, "}");
    cmd_send_response(j.buf);
    jb_free(&j);
}

static void handle_read_z80_ram(int id, const char *json)
{
    int addr = json_get_int(json, "addr", 0);
    int len  = json_get_int(json, "len",  16);
    if (addr < 0 || len < 0 || addr + len > 0x2000) {
        send_err(id, "addr/len out of Z80 RAM range");
        return;
    }
    Z80RegSnap z; z80_snapshot(&z, &g_clownmdemu);
    JBuf j; jb_init(&j);
    jb_printf(&j, "{\"id\":%d,\"ok\":true,\"addr\":%d,\"len\":%d,\"data\":", id, addr, len);
    jb_append_hex(&j, &z.ram[addr], len);
    jb_printf(&j, "}");
    cmd_send_response(j.buf);
    jb_free(&j);
}

static void handle_fm_state(int id)
{
    FmSnap fm; fm_snapshot(&fm, &g_clownmdemu);
    JBuf j; jb_init(&j);
    jb_printf(&j, "{\"id\":%d,\"ok\":true,", id);
    json_fm(&j, &fm);
    jb_printf(&j, "}");
    cmd_send_response(j.buf);
    jb_free(&j);
}

static void handle_psg_state(int id)
{
    PsgSnap p; psg_snapshot(&p, &g_clownmdemu);
    JBuf j; jb_init(&j);
    jb_printf(&j, "{\"id\":%d,\"ok\":true,", id);
    json_psg(&j, &p);
    jb_printf(&j, "}");
    cmd_send_response(j.buf);
    jb_free(&j);
}

static void handle_vdp_state(int id, const char *json)
{
    VdpSnap v; vdp_snapshot(&v, &g_clownmdemu);
    char include_buf[64] = {0};
    json_get_str(json, "include", include_buf, sizeof(include_buf));
    JBuf j; jb_init(&j);
    jb_printf(&j, "{\"id\":%d,\"ok\":true,", id);
    json_vdp(&j, &v, include_buf);
    jb_printf(&j, "}");
    cmd_send_response(j.buf);
    jb_free(&j);
}

static void handle_read_vsram(int id)
{
    JBuf j; jb_init(&j);
    jb_printf(&j, "{\"id\":%d,\"ok\":true,\"vsram\":[", id);
    const VDP_State *vs = &g_clownmdemu.vdp.state;
    for (int i = 0; i < 64; i++) jb_printf(&j, "%s%u", i ? "," : "", (uint32_t)vs->vsram[i]);
    jb_printf(&j, "]}");
    cmd_send_response(j.buf);
    jb_free(&j);
}

/* ---------- dispatch_miss_info ---------- */

#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
static void handle_dispatch_miss_info(int id)
{
    JBuf j; jb_init(&j);
    jb_printf(&j, "{\"id\":%d,\"ok\":true,\"count\":%u,\"unique_count\":%d,\"last_addr\":%u,\"last_frame\":%llu,\"unique\":[",
              id,
              (unsigned)g_miss_count_any, g_miss_unique_count,
              (unsigned)g_miss_last_addr,
              (unsigned long long)g_miss_last_frame);
    for (int i = 0; i < g_miss_unique_count; i++)
        jb_printf(&j, "%s%u", i ? "," : "", (unsigned)g_miss_unique_addrs[i]);
    jb_printf(&j, "]}");
    cmd_send_response(j.buf);
    jb_free(&j);
}
#endif

#if SONIC_REVERSE_DEBUG
/* =========================================================================
 * Tier-1 reverse-debugger commands. Data + record path lives in
 * reverse_debug.c; these handlers are the TCP surface.
 *
 *   rdb_range {"lo":"0xA04000","hi":"0xA04003"}   — add a range filter
 *   rdb_reset                                     — clear ring + filters
 *   rdb_dump  {"start":0,"count":50000}            — page JSON entries
 * ========================================================================= */

static uint32_t rdb_parse_hex(const char *json, const char *key, uint32_t def)
{
    char tmp[32];
    if (json_get_str(json, key, tmp, sizeof(tmp))) return hex_to_u32(tmp);
    int i = json_get_int(json, key, -1);
    if (i < 0) return def;
    return (uint32_t)i;
}

static void handle_rdb_range(int id, const char *json)
{
    uint32_t lo = rdb_parse_hex(json, "lo", UINT32_MAX);
    uint32_t hi = rdb_parse_hex(json, "hi", UINT32_MAX);
    if (lo == UINT32_MAX || hi == UINT32_MAX) {
        send_err(id, "need lo + hi (hex strings like \"0xA04000\")");
        return;
    }
    if (!rdb_add_range(lo, hi)) {
        send_err(id, "range table full (max 8) — call rdb_reset first");
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"lo\":\"0x%06X\",\"hi\":\"0x%06X\",\"nranges\":%d}",
        id, (unsigned)lo, (unsigned)hi, rdb_range_count());
    send_response(buf);
}

static void handle_rdb_reset(int id)
{
    rdb_reset();
    send_ok(id);
}

/* Return just the snapshot count + the active range list. Cheap status
 * probe that lets divergence_diff check whether a target range has
 * captured any writes before paying for a full rdb_dump payload. */
static void handle_rdb_count(int id)
{
    rdb_snapshot_begin();
    uint32_t total = rdb_snapshot_count();
    char buf[512];
    int pos = snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"count\":%u,\"ranges\":[",
        id, (unsigned)total);
    for (int i = 0, n = rdb_range_count(); i < n && pos < (int)sizeof(buf) - 64; i++) {
        uint32_t lo = 0, hi = 0; rdb_range_get(i, &lo, &hi);
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "%s[\"0x%06X\",\"0x%06X\"]", i ? "," : "",
            (unsigned)lo, (unsigned)hi);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
    rdb_snapshot_end();
    send_response(buf);
}

static void handle_rdb_dump(int id, const char *json)
{
    int start = json_get_int(json, "start", 0);
    int count = json_get_int(json, "count", 50000);
    if (start < 0 || count <= 0) { send_err(id, "bad start/count"); return; }
    if (count > 200000) count = 200000;   /* safety cap per call */

    rdb_snapshot_begin();
    uint32_t total = rdb_snapshot_count();

    /* Upper bound on a single entry is ~140 bytes; allocate +headroom. */
    size_t cap = (size_t)count * 160 + 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) { rdb_snapshot_end(); send_err(id, "alloc failed"); return; }

    int pos = snprintf(buf, cap,
        "{\"id\":%d,\"ok\":true,\"total\":%u,\"start\":%d,\"ranges\":[",
        id, (unsigned)total, start);
    for (int i = 0, n = rdb_range_count(); i < n; i++) {
        uint32_t lo = 0, hi = 0; rdb_range_get(i, &lo, &hi);
        pos += snprintf(buf + pos, cap - pos,
            "%s[\"0x%06X\",\"0x%06X\"]", i ? "," : "",
            (unsigned)lo, (unsigned)hi);
    }
    pos += snprintf(buf + pos, cap - pos, "],\"log\":[");

    int emitted = 0;
    for (int i = 0; i < count; i++) {
        uint32_t idx = (uint32_t)(start + i);
        if (idx >= total) break;
        if (emitted) buf[pos++] = ',';
        int n = rdb_format_entry(idx, buf + pos, cap - pos);
        if (n < 0) break;
        pos += n;
        emitted++;
    }
    pos += snprintf(buf + pos, cap - pos,
        "],\"returned\":%d,\"done\":%s}",
        emitted, (start + emitted >= (int)total) ? "true" : "false");

    rdb_snapshot_end();
    send_response(buf);
    free(buf);
}

/* =========================================================================
 * Tier 2: breakpoints + stepping (native only)
 *
 * Oracle never enters recompiled function bodies, so rdb_on_block hooks
 * are inert there. We still accept the commands but return a
 * "native only" error so tooling fails loudly rather than silently.
 * ========================================================================= */

#if ENABLE_RECOMPILED_CODE
#define RDB_TIER2_NATIVE 1
#else
#define RDB_TIER2_NATIVE 0
#endif

static void handle_rdb_break(int id, const char *json)
{
#if RDB_TIER2_NATIVE
    uint32_t block = rdb_parse_hex(json, "block", UINT32_MAX);
    if (block == UINT32_MAX) { send_err(id, "need block (hex string)"); return; }
    if (!rdb_break_add(block)) { send_err(id, "break table full (max 64)"); return; }
    char buf[192];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"block\":\"0x%06X\",\"count\":%d}",
        id, (unsigned)block, rdb_break_count());
    send_response(buf);
#else
    (void)json; send_err(id, "rdb_break is native only (port 4378)");
#endif
}

static void handle_rdb_break_clear(int id)
{
#if RDB_TIER2_NATIVE
    rdb_break_clear_all();
    send_ok(id);
#else
    send_err(id, "rdb_break_clear is native only");
#endif
}

static void handle_rdb_break_list(int id)
{
#if RDB_TIER2_NATIVE
    extern uint64_t g_rdb_slow_count, g_rdb_park_count;
    extern int      g_rdb_break_pending;
    char buf[4096];
    int pos = snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"pending\":%d,"
         "\"slow_count\":%llu,\"park_count\":%llu,\"breaks\":[",
        id, g_rdb_break_pending,
        (unsigned long long)g_rdb_slow_count,
        (unsigned long long)g_rdb_park_count);
    for (int i = 0, n = rdb_break_count(); i < n; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "%s\"0x%06X\"", i ? "," : "", (unsigned)rdb_break_get(i));
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
    send_response(buf);
#else
    send_err(id, "rdb_break_list is native only");
#endif
}

static void handle_rdb_step(int id)
{
#if RDB_TIER2_NATIVE
    if (!rdb_is_parked()) { send_err(id, "not parked"); return; }
    rdb_cmd_step_one();
    send_ok(id);
#else
    send_err(id, "rdb_step is native only");
#endif
}

static void handle_rdb_step_over(int id)
{
#if RDB_TIER2_NATIVE
    if (!rdb_is_parked()) { send_err(id, "not parked"); return; }
    rdb_cmd_step_over();
    send_ok(id);
#else
    send_err(id, "rdb_step_over is native only");
#endif
}

static void handle_rdb_continue(int id)
{
#if RDB_TIER2_NATIVE
    /* continue is legal from paused OR running state; if not parked, it's
     * a no-op that just ensures STEP_NONE. */
    rdb_cmd_continue();
    send_ok(id);
#else
    send_err(id, "rdb_continue is native only");
#endif
}

static void handle_rdb_insn_break(int id, const char *json)
{
#if RDB_TIER2_NATIVE
    uint32_t pc = rdb_parse_hex(json, "pc", UINT32_MAX);
    if (pc == UINT32_MAX) { send_err(id, "need pc (hex string)"); return; }
    if (!rdb_insn_break_add(pc)) {
        send_err(id, "insn-break table full (max 128)"); return;
    }
    char buf[192];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"pc\":\"0x%06X\",\"count\":%d}",
        id, (unsigned)pc, rdb_insn_break_count());
    send_response(buf);
#else
    (void)json; send_err(id, "rdb_insn_break is native only");
#endif
}

static void handle_rdb_insn_break_clear(int id)
{
#if RDB_TIER2_NATIVE
    rdb_insn_break_clear_all();
    send_ok(id);
#else
    send_err(id, "rdb_insn_break_clear is native only");
#endif
}

static void handle_rdb_step_insn(int id)
{
#if RDB_TIER2_NATIVE
    if (!rdb_is_parked()) { send_err(id, "not parked"); return; }
    rdb_cmd_step_insn();
    send_ok(id);
#else
    send_err(id, "rdb_step_insn is native only");
#endif
}

static void handle_rdb_get_state(int id, const char *json)
{
#if RDB_TIER2_NATIVE
    /* Optional RAM window: {"ram":[lo,hi]} */
    int have_ram = 0;
    uint32_t ram_lo = 0, ram_hi = 0;
    char tmp[48];
    if (json_get_str(json, "ram_lo", tmp, sizeof(tmp))) {
        ram_lo = hex_to_u32(tmp);
        if (json_get_str(json, "ram_hi", tmp, sizeof(tmp))) {
            ram_hi = hex_to_u32(tmp);
            have_ram = 1;
        }
    }
    if (have_ram && (ram_hi < ram_lo || ram_hi - ram_lo > 2048)) {
        send_err(id, "ram window must be <=2048 bytes, lo<=hi"); return;
    }

    uint32_t a7  = cmd_server_current_a7();
    uint32_t r0  = cmd_server_stack_read32(a7);
    uint32_t r1  = cmd_server_stack_read32(a7 + 4);
    uint32_t r2  = cmd_server_stack_read32(a7 + 8);
    uint32_t r3  = cmd_server_stack_read32(a7 + 12);

    size_t cap = 2048 + (have_ram ? (size_t)(ram_hi - ram_lo + 1) * 3 : 0);
    char *buf = (char *)malloc(cap);
    if (!buf) { send_err(id, "alloc failed"); return; }

    int pos = snprintf(buf, cap,
        "{\"id\":%d,\"ok\":true,\"parked\":%s,\"block\":\"0x%06X\","
         "\"func\":\"0x%06X\",\"frame\":%u,"
         "\"D\":[%u,%u,%u,%u,%u,%u,%u,%u],"
         "\"A\":[%u,%u,%u,%u,%u,%u,%u,%u],"
         "\"SR\":\"0x%04X\",\"PC\":\"0x%06X\","
         "\"stack\":[\"0x%06X\",\"0x%06X\",\"0x%06X\",\"0x%06X\"]",
        id,
        rdb_is_parked() ? "true" : "false",
        (unsigned)rdb_parked_block(),
        (unsigned)(g_rdb_current_func & 0xFFFFFFu),
        (unsigned)cmd_server_current_frame(),
        (unsigned)g_cpu.D[0], (unsigned)g_cpu.D[1],
        (unsigned)g_cpu.D[2], (unsigned)g_cpu.D[3],
        (unsigned)g_cpu.D[4], (unsigned)g_cpu.D[5],
        (unsigned)g_cpu.D[6], (unsigned)g_cpu.D[7],
        (unsigned)g_cpu.A[0], (unsigned)g_cpu.A[1],
        (unsigned)g_cpu.A[2], (unsigned)g_cpu.A[3],
        (unsigned)g_cpu.A[4], (unsigned)g_cpu.A[5],
        (unsigned)g_cpu.A[6], (unsigned)g_cpu.A[7],
        (unsigned)g_cpu.SR, (unsigned)(g_cpu.PC & 0xFFFFFFu),
        (unsigned)(r0 & 0xFFFFFFu), (unsigned)(r1 & 0xFFFFFFu),
        (unsigned)(r2 & 0xFFFFFFu), (unsigned)(r3 & 0xFFFFFFu));

    if (have_ram) {
        pos += snprintf(buf + pos, cap - pos, ",\"ram\":\"");
        for (uint32_t a = ram_lo; a <= ram_hi; a++) {
            uint8_t byte = g_ram[a & 0xFFFFu];
            pos += snprintf(buf + pos, cap - pos, "%02X", byte);
        }
        pos += snprintf(buf + pos, cap - pos, "\",\"ram_lo\":\"0x%06X\","
                        "\"ram_hi\":\"0x%06X\"",
                        (unsigned)(0xFF0000u + ram_lo),
                        (unsigned)(0xFF0000u + ram_hi));
    }
    snprintf(buf + pos, cap - pos, "}");

    send_response(buf);
    free(buf);
#else
    (void)json; send_err(id, "rdb_get_state is native only");
#endif
}
/* =========================================================================
 * Stage-A instrumentation: VBla-fire histogram + Iterate count.
 *
 * Native records each VBla-handler fire (from glue_check_vblank's while-
 * loop) into a separate ring; TCP exposes the ring contents and an
 * Iterate-count sanity check that proves oracle is 1-Iterate-per-wall-frame.
 * ========================================================================= */

static void handle_rdb_vbla_dump(int id, const char *json)
{
    int start = json_get_int(json, "start", 0);
    int count = json_get_int(json, "count", 5000);
    if (start < 0 || count <= 0) { send_err(id, "bad start/count"); return; }
    if (count > 65536) count = 65536;

    rdb_vbla_snapshot_begin();
    uint32_t total = rdb_vbla_snapshot_count();

    /* ~80 bytes per entry. */
    size_t cap = (size_t)count * 96 + 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) { rdb_vbla_snapshot_end(); send_err(id, "alloc failed"); return; }

    int pos = snprintf(buf, cap,
        "{\"id\":%d,\"ok\":true,\"total\":%u,\"start\":%d,"
         "\"iterate_count\":%llu,\"log\":[",
        id, (unsigned)total, start,
        (unsigned long long)rdb_iterate_count());
    int emitted = 0;
    for (int i = 0; i < count; i++) {
        uint32_t idx = (uint32_t)(start + i);
        if (idx >= total) break;
        if (emitted) buf[pos++] = ',';
        int n = rdb_vbla_format_entry(idx, buf + pos, cap - pos);
        if (n < 0) break;
        pos += n;
        emitted++;
    }
    pos += snprintf(buf + pos, cap - pos,
        "],\"returned\":%d,\"done\":%s}",
        emitted, (start + emitted >= (int)total) ? "true" : "false");

    rdb_vbla_snapshot_end();
    send_response(buf);
    free(buf);
}

/* =========================================================================
 * Stage-C instruction-count telemetry.
 *
 * rdb_insn_counts returns { native, oracle, wall_frame }. On native the
 * "native" field is non-zero (ticked from generated C) and "oracle" is 0.
 * On oracle it's the mirror. The paired rdb_insn_diff.py tool queries
 * one target per count and compares per-wall-frame throughput.
 * ========================================================================= */

static void handle_rdb_insn_counts(int id)
{
    char buf[192];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"native\":%llu,\"oracle\":%llu,"
         "\"wall_frame\":%u}",
        id,
        (unsigned long long)rdb_native_insn_count(),
        (unsigned long long)rdb_oracle_insn_count(),
        (unsigned)cmd_server_current_frame());
    send_response(buf);
}

/* =========================================================================
 * Tier 3: per-instruction capture on the oracle side.
 *
 * Only the oracle build records. On native the handlers reply
 * "oracle only" for a symmetric TCP surface (mirrors Tier 2's
 * "native only" reply pattern).
 * ========================================================================= */

#if defined(SONIC_ORACLE_BUILD)
#define T3_ORACLE 1
#else
#define T3_ORACLE 0
#endif

static void handle_t3_range(int id, const char *json)
{
#if T3_ORACLE
    uint32_t lo = rdb_parse_hex(json, "lo", UINT32_MAX);
    uint32_t hi = rdb_parse_hex(json, "hi", UINT32_MAX);
    if (lo == UINT32_MAX || hi == UINT32_MAX) {
        send_err(id, "need lo + hi (hex strings)"); return;
    }
    if (!t3_add_range(lo, hi)) {
        send_err(id, "range table full (max 8) — call t3_reset first"); return;
    }
    char buf[192];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"lo\":\"0x%06X\",\"hi\":\"0x%06X\","
        "\"nranges\":%d}",
        id, (unsigned)lo, (unsigned)hi, t3_range_count());
    send_response(buf);
#else
    (void)json; send_err(id, "t3_range is oracle only (port 4379)");
#endif
}

static void handle_t3_reset(int id)
{
#if T3_ORACLE
    t3_reset();
    send_ok(id);
#else
    send_err(id, "t3_reset is oracle only");
#endif
}

/* ---- Phase 3: oracle-side break + step + state ---- */

static void handle_rdb_oracle_break(int id, const char *json)
{
#if T3_ORACLE
    uint32_t pc = rdb_parse_hex(json, "pc", UINT32_MAX);
    if (pc == UINT32_MAX) { send_err(id, "need pc (hex string)"); return; }
    if (!oracle_break_add(pc)) { send_err(id, "break table full"); return; }
    char buf[192];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"pc\":\"0x%06X\",\"count\":%d}",
        id, (unsigned)pc, oracle_break_count());
    send_response(buf);
#else
    (void)json; send_err(id, "rdb_oracle_break is oracle only");
#endif
}

static void handle_rdb_oracle_break_clear(int id)
{
#if T3_ORACLE
    oracle_break_clear_all();
    send_ok(id);
#else
    send_err(id, "rdb_oracle_break_clear is oracle only");
#endif
}

static void handle_rdb_oracle_step_insn(int id)
{
#if T3_ORACLE
    oracle_cmd_step_insn();
    send_ok(id);
#else
    send_err(id, "rdb_oracle_step_insn is oracle only");
#endif
}

static void handle_rdb_oracle_continue(int id)
{
#if T3_ORACLE
    oracle_cmd_continue();
    send_ok(id);
#else
    send_err(id, "rdb_oracle_continue is oracle only");
#endif
}

static void handle_rdb_oracle_step_back(int id)
{
#if T3_ORACLE
    uint64_t restored = 0;
    if (!oracle_step_back(&restored)) {
        send_err(id, "no snapshot available (run longer first)"); return;
    }
    char buf[192];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"restored_insn\":%llu,\"snap_count\":%d}",
        id, (unsigned long long)restored, oracle_snap_count());
    send_response(buf);
#else
    send_err(id, "rdb_oracle_step_back is oracle only");
#endif
}

static void handle_rdb_oracle_state(int id)
{
#if T3_ORACLE
    const Clown68000_State *st = &g_clownmdemu.m68k;
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"parked\":%s,\"pc\":\"0x%06X\","
         "\"frame\":%u,"
         "\"D\":[%u,%u,%u,%u,%u,%u,%u,%u],"
         "\"A\":[%u,%u,%u,%u,%u,%u,%u,%u],"
         "\"SR\":\"0x%04X\"}",
        id,
        oracle_is_parked() ? "true" : "false",
        (unsigned)(oracle_parked_pc() & 0xFFFFFFu),
        (unsigned)cmd_server_current_frame(),
        (unsigned)st->data_registers[0], (unsigned)st->data_registers[1],
        (unsigned)st->data_registers[2], (unsigned)st->data_registers[3],
        (unsigned)st->data_registers[4], (unsigned)st->data_registers[5],
        (unsigned)st->data_registers[6], (unsigned)st->data_registers[7],
        (unsigned)st->address_registers[0], (unsigned)st->address_registers[1],
        (unsigned)st->address_registers[2], (unsigned)st->address_registers[3],
        (unsigned)st->address_registers[4], (unsigned)st->address_registers[5],
        (unsigned)st->address_registers[6], (unsigned)st->address_registers[7],
        (unsigned)st->status_register);
    send_response(buf);
#else
    send_err(id, "rdb_oracle_state is oracle only");
#endif
}

static void handle_t3_dump(int id, const char *json)
{
#if T3_ORACLE
    int start = json_get_int(json, "start", 0);
    int count = json_get_int(json, "count", 20000);
    if (start < 0 || count <= 0) { send_err(id, "bad start/count"); return; }
    if (count > 100000) count = 100000;

    t3_snapshot_begin();
    uint32_t total = t3_snapshot_count();

    /* Per entry ~260 bytes of JSON. Budget +headroom. */
    size_t cap = (size_t)count * 280 + 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) { t3_snapshot_end(); send_err(id, "alloc failed"); return; }

    int pos = snprintf(buf, cap,
        "{\"id\":%d,\"ok\":true,\"total\":%u,\"start\":%d,\"ranges\":[",
        id, (unsigned)total, start);
    for (int i = 0, n = t3_range_count(); i < n; i++) {
        uint32_t lo = 0, hi = 0; t3_range_get(i, &lo, &hi);
        pos += snprintf(buf + pos, cap - pos,
            "%s[\"0x%06X\",\"0x%06X\"]", i ? "," : "",
            (unsigned)lo, (unsigned)hi);
    }
    pos += snprintf(buf + pos, cap - pos, "],\"log\":[");

    int emitted = 0;
    for (int i = 0; i < count; i++) {
        uint32_t idx = (uint32_t)(start + i);
        if (idx >= total) break;
        if (emitted) buf[pos++] = ',';
        int n = t3_format_entry(idx, buf + pos, cap - pos);
        if (n < 0) break;
        pos += n;
        emitted++;
    }
    pos += snprintf(buf + pos, cap - pos,
        "],\"returned\":%d,\"done\":%s}",
        emitted, (start + emitted >= (int)total) ? "true" : "false");

    t3_snapshot_end();
    send_response(buf);
    free(buf);
#else
    (void)json; send_err(id, "t3_dump is oracle only");
#endif
}
#endif /* SONIC_REVERSE_DEBUG */

/* =========================================================================
 * Command dispatch
 * ========================================================================= */

static CmdResult dispatch_command(const char *json, uint32_t frame_num)
{
    CmdResult cr = {0};
    char cmd[64];
    int id = json_get_int(json, "id", 0);

    if (!json_get_str(json, "cmd", cmd, sizeof(cmd))) {
        send_err(id, "missing cmd");
        return cr;
    }

    /* Game-specific commands first (g_game_spec.commands[]). Falls
     * through to the framework table below if no game command matches. */
    for (int i = 0; i < g_game_spec.command_count; i++) {
        if (strcmp(cmd, g_game_spec.commands[i].name) == 0) {
            g_game_spec.commands[i].handler(id, json);
            return cr;
        }
    }

    if (strcmp(cmd, "pause") == 0) {
        s_paused = 1;
        send_ok(id);
    } else if (strcmp(cmd, "continue") == 0) {
        s_paused = 0;
        send_ok(id);
    } else if (strcmp(cmd, "ping") == 0) {
        handle_ping(id, frame_num);
    } else if (strcmp(cmd, "get_registers") == 0) {
        handle_get_registers(id);
    } else if (strcmp(cmd, "read_memory") == 0) {
        handle_read_memory(id, json);
    } else if (strcmp(cmd, "write_memory") == 0) {
        handle_write_memory(id, json);
    } else if (strcmp(cmd, "read_ram") == 0) {
        handle_read_ram(id, json);
    } else if (strcmp(cmd, "sonic_history") == 0) {
        handle_sonic_history(id, json);
    } else if (strcmp(cmd, "vblank_info") == 0) {
        handle_vblank_info(id);
    } else if (strcmp(cmd, "frame_info") == 0) {
        handle_frame_info(id);
    } else if (strcmp(cmd, "frame_range") == 0) {
        handle_frame_range(id, json);
    } else if (strcmp(cmd, "watch") == 0) {
        handle_watch(id, json);
    } else if (strcmp(cmd, "unwatch") == 0) {
        handle_unwatch(id, json);
    } else if (strcmp(cmd, "run_frames") == 0) {
        int count = json_get_int(json, "count", 0);
        if (count > 0 && count <= 36000) {
            cr.run_extra_frames = count;
            s_pending_frame_id = id;
        } else {
            send_err(id, "count must be 1-36000");
        }
    } else if (strcmp(cmd, "set_input") == 0) {
        /* Genesis button bits: 0=Up,1=Down,2=Left,3=Right,4=B,5=C,6=A,7=Start */
        char keys_str[32];
        if (json_get_str(json, "keys", keys_str, sizeof(keys_str))) {
            cr.input_override = true;
            cr.input_keys = (uint8_t)hex_to_u32(keys_str);
            send_ok(id);
        } else {
            cr.input_override = true;
            cr.input_keys = (uint8_t)json_get_int(json, "keys", 0);
            send_ok(id);
        }
    } else if (strcmp(cmd, "addr_history") == 0) {
        handle_addr_history(id, json);
    } else if (strcmp(cmd, "read_vram") == 0) {
        handle_read_vram(id, json);
    } else if (strcmp(cmd, "read_cram") == 0) {
        handle_read_cram(id);
    } else if (strcmp(cmd, "dump_vram") == 0) {
        handle_dump_vram(id, json);
    } else if (strcmp(cmd, "audio_stats") == 0) {
        handle_audio_stats(id);
    } else if (strcmp(cmd, "audio_wav") == 0) {
        handle_audio_wav(id, json);
    } else if (strcmp(cmd, "io_log") == 0) {
        handle_io_log(id, json);
    } else if (strcmp(cmd, "read_joypad_port") == 0) {
        handle_read_joypad_port(id);
    /* ---- Phase 4: full ring-buffer queries + live snapshots ---- */
    } else if (strcmp(cmd, "get_frame") == 0) {
        handle_get_frame(id, json);
    } else if (strcmp(cmd, "frame_timeseries") == 0) {
        handle_frame_timeseries(id, json);
    } else if (strcmp(cmd, "z80_state") == 0) {
        handle_z80_state(id, json);
    } else if (strcmp(cmd, "read_z80_ram") == 0) {
        handle_read_z80_ram(id, json);
    } else if (strcmp(cmd, "fm_state") == 0) {
        handle_fm_state(id);
    } else if (strcmp(cmd, "psg_state") == 0) {
        handle_psg_state(id);
    } else if (strcmp(cmd, "vdp_state") == 0) {
        handle_vdp_state(id, json);
    } else if (strcmp(cmd, "read_vsram") == 0) {
        handle_read_vsram(id);
#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
    } else if (strcmp(cmd, "dispatch_miss_info") == 0) {
        handle_dispatch_miss_info(id);
#endif
#if SONIC_REVERSE_DEBUG
    } else if (strcmp(cmd, "rdb_range") == 0) {
        handle_rdb_range(id, json);
    } else if (strcmp(cmd, "rdb_reset") == 0) {
        handle_rdb_reset(id);
    } else if (strcmp(cmd, "rdb_dump") == 0) {
        handle_rdb_dump(id, json);
    } else if (strcmp(cmd, "rdb_count") == 0) {
        handle_rdb_count(id);
    } else if (strcmp(cmd, "rdb_break") == 0) {
        handle_rdb_break(id, json);
    } else if (strcmp(cmd, "rdb_break_clear") == 0) {
        handle_rdb_break_clear(id);
    } else if (strcmp(cmd, "rdb_break_list") == 0) {
        handle_rdb_break_list(id);
    } else if (strcmp(cmd, "rdb_step") == 0) {
        handle_rdb_step(id);
    } else if (strcmp(cmd, "rdb_step_over") == 0) {
        handle_rdb_step_over(id);
    } else if (strcmp(cmd, "rdb_continue") == 0) {
        handle_rdb_continue(id);
    } else if (strcmp(cmd, "rdb_insn_break") == 0) {
        handle_rdb_insn_break(id, json);
    } else if (strcmp(cmd, "rdb_insn_break_clear") == 0) {
        handle_rdb_insn_break_clear(id);
    } else if (strcmp(cmd, "rdb_step_insn") == 0) {
        handle_rdb_step_insn(id);
    } else if (strcmp(cmd, "rdb_get_state") == 0) {
        handle_rdb_get_state(id, json);
    } else if (strcmp(cmd, "rdb_vbla_dump") == 0) {
        handle_rdb_vbla_dump(id, json);
    } else if (strcmp(cmd, "rdb_insn_counts") == 0) {
        handle_rdb_insn_counts(id);
    } else if (strcmp(cmd, "t3_range") == 0) {
        handle_t3_range(id, json);
    } else if (strcmp(cmd, "t3_reset") == 0) {
        handle_t3_reset(id);
    } else if (strcmp(cmd, "t3_dump") == 0) {
        handle_t3_dump(id, json);
    } else if (strcmp(cmd, "rdb_oracle_break") == 0) {
        handle_rdb_oracle_break(id, json);
    } else if (strcmp(cmd, "rdb_oracle_break_clear") == 0) {
        handle_rdb_oracle_break_clear(id);
    } else if (strcmp(cmd, "rdb_oracle_step_insn") == 0) {
        handle_rdb_oracle_step_insn(id);
    } else if (strcmp(cmd, "rdb_oracle_continue") == 0) {
        handle_rdb_oracle_continue(id);
    } else if (strcmp(cmd, "rdb_oracle_step_back") == 0) {
        handle_rdb_oracle_step_back(id);
    } else if (strcmp(cmd, "rdb_oracle_state") == 0) {
        handle_rdb_oracle_state(id);
#endif
    } else if (strcmp(cmd, "coverage_dump") == 0) {
#if !ENABLE_RECOMPILED_CODE
        extern int clown68000_coverage_count(void);
        extern void clown68000_coverage_dump(const char *);
        extern const char *exe_relative(const char *);
        const char *path = exe_relative("coverage.log");
        clown68000_coverage_dump(path);
        char buf[512];
        snprintf(buf, sizeof(buf),
            "{\"id\":%d,\"ok\":true,\"count\":%d,\"path\":\"%s\"}",
            id, clown68000_coverage_count(), path);
        send_response(buf);
#else
        send_err(id, "coverage_dump only available in interpreter mode");
#endif
    } else if (strcmp(cmd, "fm_trace") == 0) {
        /* {"cmd":"fm_trace","action":"on","frames":300}
         * {"cmd":"fm_trace","action":"off"}
         * {"cmd":"fm_trace"}  — returns status */
        char action[16];
        if (json_get_str(json, "action", action, sizeof(action))) {
            if (strcmp(action, "on") == 0) {
                int max_f = json_get_int(json, "frames", 300);
                if (max_f <= 0) max_f = 300;
                const char *path =
#if ENABLE_RECOMPILED_CODE
                    "fm_trace_native.log";
#else
                    "fm_trace_interp.log";
#endif
                if (s_fm_trace_file) fclose(s_fm_trace_file);
                s_fm_trace_file = fopen(path, "w");
                if (s_fm_trace_file) {
                    fprintf(s_fm_trace_file, "# frame master_cycle address value a7 ret0 ret1 ret2 ret3\n");
                    s_fm_trace_active = 1;
                    s_fm_trace_max_frames = max_f;
                    s_fm_trace_frame_count = 0;
                    /* Use cmd_server's own wall-frame counter (kept in
                     * sync by cmd_server_record_frame()). g_frame_count
                     * only advances in native builds — using it for
                     * oracle would always report start_frame=0. */
                    s_fm_trace_start_frame = (uint64_t)s_current_frame;
                    g_fm_write_trace_fn = fm_trace_callback;
                    char buf[320];
                    snprintf(buf, sizeof(buf),
                        "{\"id\":%d,\"ok\":true,\"file\":\"%s\",\"max_frames\":%d,"
                        "\"start_frame\":%llu}",
                        id, path, max_f,
                        (unsigned long long)s_fm_trace_start_frame);
                    send_response(buf);
                    fprintf(stderr, "[FM-TRACE] Started: %s (%d frames)\n", path, max_f);
                } else {
                    send_err(id, "failed to open trace file");
                }
            } else if (strcmp(action, "off") == 0) {
                if (s_fm_trace_file) { fclose(s_fm_trace_file); s_fm_trace_file = NULL; }
                s_fm_trace_active = 0;
                g_fm_write_trace_fn = NULL;
                fprintf(stderr, "[FM-TRACE] Stopped (%d frames)\n", s_fm_trace_frame_count);
                send_ok(id);
            } else {
                send_err(id, "action must be on or off");
            }
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf),
                "{\"id\":%d,\"active\":%s,\"frames\":%d}\n",
                id, s_fm_trace_active ? "true" : "false", s_fm_trace_frame_count);
            send_response(buf);
        }
    } else if (strcmp(cmd, "memory_write_log") == 0) {
        /* {"cmd":"memory_write_log","action":"on","addrs":[0xFFF001,0xFFF002],"frames":600}
         * {"cmd":"memory_write_log","action":"off"}
         * {"cmd":"memory_write_log"}  — status */
        char action[16];
        if (json_get_str(json, "action", action, sizeof(action))) {
            if (strcmp(action, "on") == 0) {
                /* Parse "addrs":[h1,h2,...] — supports 0x-hex and decimal. */
                const char *p = strstr(json, "\"addrs\"");
                if (!p) { send_err(id, "missing addrs array"); return cr; }
                p = strchr(p, '[');
                if (!p) { send_err(id, "addrs must be an array"); return cr; }
                p++;
                s_mem_write_log_watch_count = 0;
                while (*p && *p != ']' && s_mem_write_log_watch_count < MEM_WRITE_LOG_MAX_WATCH) {
                    while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n') p++;
                    if (*p == ']' || *p == '\0') break;
                    uint32_t v = (uint32_t)strtoul(p, (char **)&p, 0);  /* 0 → accept 0x */
                    s_mem_write_log_watch_lo[s_mem_write_log_watch_count] = v & 0xFFFFFFu;
                    s_mem_write_log_watch_hi[s_mem_write_log_watch_count] = v & 0xFFFFFFu;
                    s_mem_write_log_watch_count++;
                }
                if (s_mem_write_log_watch_count == 0) { send_err(id, "addrs empty"); return cr; }

                int max_f = json_get_int(json, "frames", 600);
                if (max_f <= 0) max_f = 600;
                const char *path =
#if ENABLE_RECOMPILED_CODE
                    "mem_write_log_native.log";
#else
                    "mem_write_log_oracle.log";
#endif
                if (s_mem_write_log_file) fclose(s_mem_write_log_file);
                s_mem_write_log_file = fopen(path, "w");
                if (!s_mem_write_log_file) { send_err(id, "failed to open log file"); return cr; }
                fprintf(s_mem_write_log_file,
                    "# wall_frame internal_frame game_mode address value a7 ret0 ret1 ret2 ret3 target_cycle\n");
                fprintf(s_mem_write_log_file, "# watching:");
                for (int i = 0; i < s_mem_write_log_watch_count; i++)
                    fprintf(s_mem_write_log_file, " 0x%06X", s_mem_write_log_watch_lo[i]);
                fprintf(s_mem_write_log_file, "\n");

                s_mem_write_log_active = 1;
                s_mem_write_log_max_frames = max_f;
                s_mem_write_log_frame_count = 0;
                g_mem_write_trace_fn = mem_write_log_callback;

                char buf[256];
                snprintf(buf, sizeof(buf),
                    "{\"id\":%d,\"ok\":true,\"file\":\"%s\",\"max_frames\":%d,\"addrs\":%d}",
                    id, path, max_f, s_mem_write_log_watch_count);
                send_response(buf);
                fprintf(stderr, "[MEM-WRITE-LOG] Started: %s (%d addrs, %d frames)\n",
                        path, s_mem_write_log_watch_count, max_f);
            } else if (strcmp(action, "off") == 0) {
                if (s_mem_write_log_file) { fclose(s_mem_write_log_file); s_mem_write_log_file = NULL; }
                s_mem_write_log_active = 0;
                g_mem_write_trace_fn = NULL;
                fprintf(stderr, "[MEM-WRITE-LOG] Stopped (%d frames)\n", s_mem_write_log_frame_count);
                send_ok(id);
            } else {
                send_err(id, "action must be on or off");
            }
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf),
                "{\"id\":%d,\"active\":%s,\"frames\":%d,\"addrs\":%d}",
                id, s_mem_write_log_active ? "true" : "false",
                s_mem_write_log_frame_count, s_mem_write_log_watch_count);
            send_response(buf);
        }
    } else if (strcmp(cmd, "quit") == 0) {
#if !ENABLE_RECOMPILED_CODE
        { extern int clown68000_coverage_count(void);
          extern void clown68000_coverage_dump(const char *);
          extern const char *exe_relative(const char *);
          if (clown68000_coverage_count() > 0)
              clown68000_coverage_dump(exe_relative("coverage.log")); }
#endif
        send_ok(id);
        cr.should_quit = true;
    } else {
        send_err(id, "unknown command");
    }

    return cr;
}

/* =========================================================================
 * Socket setup
 * ========================================================================= */

static void set_nonblocking(sock_t s)
{
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

/* s_current_frame is forward-declared above (near s_paused) so the
 * fm_trace handler can read it. Updated by cmd_server_record_frame(). */

/* =========================================================================
 * Public API
 * ========================================================================= */

void cmd_server_init(int port)
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen == SOCK_INVALID) {
        fprintf(stderr, "[cmd] Failed to create socket\n");
        return;
    }

    int yes = 1;
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);

    if (bind(s_listen, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "[cmd] Failed to bind port %d\n", port);
        sock_close(s_listen);
        s_listen = SOCK_INVALID;
        return;
    }

    listen(s_listen, 1);
    set_nonblocking(s_listen);

    memset(s_watchpoints, 0, sizeof(s_watchpoints));

    fprintf(stderr, "[cmd] Listening on 127.0.0.1:%d\n", port);
}

CmdResult cmd_server_poll(void)
{
    CmdResult cr = {0};
    if (s_listen == SOCK_INVALID) return cr;

    /* Accept new client if none connected */
    if (s_client == SOCK_INVALID) {
        struct sockaddr_in caddr;
        int clen = sizeof(caddr);
        sock_t c = accept(s_listen, (struct sockaddr *)&caddr, &clen);
        if (c != SOCK_INVALID) {
            s_client = c;
            set_nonblocking(s_client);
            s_recv_len = 0;
            fprintf(stderr, "[cmd] Client connected\n");
        }
    }

    if (s_client == SOCK_INVALID) return cr;

    /* Check watchpoints */
    for (int i = 0; i < MAX_WATCHPOINTS; i++) {
        if (!s_watchpoints[i].active) continue;
        uint8_t cur = emu_read8(s_watchpoints[i].addr & 0xFFFF);
        if (cur != s_watchpoints[i].prev) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "{\"watchpoint\":{\"addr\":\"0x%04X\",\"old\":%u,\"new\":%u,\"frame\":%u}}",
                s_watchpoints[i].addr & 0xFFFF,
                s_watchpoints[i].prev, cur, s_current_frame);
            send_response(buf);
            s_watchpoints[i].prev = cur;
        }
    }

    /* Try to read data */
    int space = RECV_BUF_SIZE - s_recv_len - 1;
    if (space > 0) {
        int n = recv(s_client, s_recv_buf + s_recv_len, space, 0);
        if (n > 0) {
            s_recv_len += n;
            s_recv_buf[s_recv_len] = '\0';
        } else if (n == 0) {
            fprintf(stderr, "[cmd] Client disconnected\n");
            sock_close(s_client);
            s_client = SOCK_INVALID;
            s_recv_len = 0;
            return cr;
        }
    }

    /* Process complete lines */
    char *nl;
    while ((nl = strchr(s_recv_buf, '\n')) != NULL) {
        *nl = '\0';
        if (s_recv_buf[0] != '\0') {
            CmdResult line_cr = dispatch_command(s_recv_buf, s_current_frame);
            if (line_cr.should_quit) cr.should_quit = true;
            if (line_cr.run_extra_frames > 0) cr.run_extra_frames = line_cr.run_extra_frames;
            if (line_cr.input_override) { cr.input_override = true; cr.input_keys = line_cr.input_keys; }
        }
        int consumed = (int)(nl - s_recv_buf) + 1;
        s_recv_len -= consumed;
        memmove(s_recv_buf, nl + 1, s_recv_len + 1);
    }

    return cr;
}

void cmd_server_send_frame_result(int frames_run)
{
    if (s_pending_frame_id < 0) return;

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"id\":%d,\"ok\":true,\"frames_run\":%d}",
        s_pending_frame_id, frames_run);
    send_response(buf);
    s_pending_frame_id = -1;
}

void cmd_server_record_frame(uint32_t frame_num)
{
    s_current_frame = frame_num;

    uint32_t idx = frame_num % FRAME_HISTORY_CAP;
    FrameRecord *r = &s_frame_history[idx];

    /* Wipe previous occupant fully — verify-mode fields and game_data
     * tail must not leak across reuse of a ring slot. */
    memset(r, 0, sizeof(*r));
    r->frame       = frame_num;
    r->verify_pass = -1;  /* not run */

    /* Subsystem snapshots (see frame_snapshots.c). */
    m68k_snapshot(&r->m68k);
    z80_snapshot (&r->z80,  &g_clownmdemu);
    vdp_snapshot (&r->vdp,  &g_clownmdemu);
    fm_snapshot  (&r->fm,   &g_clownmdemu);
    psg_snapshot (&r->psg,  &g_clownmdemu);
    wram_snapshot(r->wram,  &g_clownmdemu);

    /* Per-game tail. */
    if (g_game_spec.fill_frame_record)
        g_game_spec.fill_frame_record(r->game_data);
    else
        memset(r->game_data, 0, sizeof(r->game_data));

    s_history_count = frame_num + 1;
}

void cmd_server_shutdown(void)
{
    if (s_client != SOCK_INVALID) { sock_close(s_client); s_client = SOCK_INVALID; }
    if (s_listen != SOCK_INVALID) { sock_close(s_listen); s_listen = SOCK_INVALID; }
#ifdef _WIN32
    WSACleanup();
#endif
#if !ENABLE_RECOMPILED_CODE
    /* Auto-dump coverage on shutdown (interpreter mode only) */
    { extern int clown68000_coverage_count(void);
      extern void clown68000_coverage_dump(const char *);
      extern const char *exe_relative(const char *);
      int count = clown68000_coverage_count();
      if (count > 0) {
          clown68000_coverage_dump(exe_relative("coverage.log"));
          fprintf(stderr, "[cmd] Dumped %d coverage entries to coverage.log\n", count);
      }
    }
#endif
#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
    /* Dispatch-miss log per PRINCIPLES.md rule 13a — always write next to
     * the exe so the next session can find it. Empty file if no misses
     * (the principle's "is the file empty?" check still answers cleanly). */
    { extern const char *exe_relative(const char *);
      const char *path = exe_relative("dispatch_misses.log");
      FILE *f = fopen(path, "w");
      if (f) {
          fprintf(f, "# dispatch_misses.log — addresses the recompiled binary\n");
          fprintf(f, "# called via call_by_address() that have no generated function.\n");
          fprintf(f, "# Add each as `extra_func 0xADDR` to game.cfg, regenerate, rebuild.\n");
          fprintf(f, "# Total misses (any address): %u\n", (unsigned)g_miss_count_any);
          fprintf(f, "# Unique missing addresses: %d\n", g_miss_unique_count);
          for (int i = 0; i < g_miss_unique_count; i++)
              fprintf(f, "extra_func 0x%06X\n", (unsigned)g_miss_unique_addrs[i]);
          fclose(f);
          if (g_miss_unique_count > 0)
              fprintf(stderr, "[cmd] %d unique dispatch misses written to %s\n",
                      g_miss_unique_count, path);
      }
    }
#endif
    fprintf(stderr, "[cmd] Shutdown\n");
}
