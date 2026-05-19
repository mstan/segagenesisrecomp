/*
 * main.c — SDL2 frontend for clownmdemu-core running Sonic the Hedgehog.
 *
 * Step 0 / Step 1  (ENABLE_RECOMPILED_CODE not defined):
 *   Plain emulator: clownmdemu drives the 68K interpreter.
 *   The SEGA logo → title screen should be visible.
 *
 * Step 2  (ENABLE_RECOMPILED_CODE defined, stub_clown68000.c linked):
 *   Recompiled code drives the 68K on a separate game thread.
 *   clownmdemu_Iterate() still renders VDP scanlines and generates audio;
 *   the stub's Clown68000_DoCycles is a no-op.
 *
 * Usage: SonicTheHedgehogRecomp <sonic.md>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "clownmdemu.h"
#include "audio.h"
#include "png_write.h"

/* =========================================================================
 * Path helper: resolve filenames relative to the exe directory.
 * This ensures savestate.bin, dispatch_misses.log, etc. always land
 * next to the exe regardless of the user's working directory.
 * ========================================================================= */

static char s_exe_dir[512] = "";

static void init_exe_dir(const char *argv0)
{
    /* Try platform API first */
#ifdef _WIN32
    extern unsigned long __stdcall GetModuleFileNameA(void*, char*, unsigned long);
    GetModuleFileNameA(NULL, s_exe_dir, sizeof(s_exe_dir) - 1);
#else
    strncpy(s_exe_dir, argv0, sizeof(s_exe_dir) - 1);
#endif
    s_exe_dir[sizeof(s_exe_dir) - 1] = '\0';
    /* Strip exe filename, keep directory with trailing slash */
    char *last_sep = strrchr(s_exe_dir, '/');
    char *last_bsep = strrchr(s_exe_dir, '\\');
    if (last_bsep > last_sep) last_sep = last_bsep;
    if (last_sep) last_sep[1] = '\0';
    else strcpy(s_exe_dir, "./");
}

/* Build a full path: exe_dir + filename */
const char *exe_relative(const char *filename)
{
    static char buf[512];
    snprintf(buf, sizeof(buf), "%s%s", s_exe_dir, filename);
    return buf;
}

#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
#include "glue.h"
#endif

#if HYBRID_RECOMPILED_CODE
#include "verify.h"
#endif

#include "cmd_server.h"
#include "game_spec.h"
#include "game_layout.h"
#if SONIC_REVERSE_DEBUG
#include "reverse_debug.h"
#endif

/* =========================================================================
 * Framebuffer and palette
 * ========================================================================= */

/* Maximum output dimensions we support. Sonic 1 uses 320×224 (H40, V28). */
#define MAX_SCREEN_WIDTH  320
#define MAX_SCREEN_HEIGHT 240

static uint32_t s_framebuf[MAX_SCREEN_WIDTH * MAX_SCREEN_HEIGHT]; /* ARGB8888 */

/* Expanded palette: 192 entries (64 CRAM × 3 brightness levels).
 * colour_updated_cb converts each entry from Genesis format to ARGB8888. */
static uint32_t s_cram[192];

static int s_screen_width  = 320;
static int s_screen_height = 224;

/* Convert a Genesis CRAM value to ARGB8888.
 *
 * Genesis CRAM format (9 significant bits):
 *   bits  3:1  = Red   (0-7)
 *   bits  7:5  = Green (0-7)
 *   bits 11:9  = Blue  (0-7)
 * Expand 3-bit components to 8-bit using × 36 (7×36 = 252 ≈ 255). */
static uint32_t md_colour_to_argb(cc_u16f colour)
{
    /* Genesis CRAM: ----BBB-GGG-RRR- (bits 1-3=R, 5-7=G, 9-11=B) */
    uint8_t r = (uint8_t)(((colour >>  1) & 7u) * 36u);
    uint8_t g = (uint8_t)(((colour >>  5) & 7u) * 36u);
    uint8_t b = (uint8_t)(((colour >>  9) & 7u) * 36u);
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* =========================================================================
 * clownmdemu callbacks
 * ========================================================================= */

static void colour_updated_cb(void *user_data,
                               cc_u16f index, cc_u16f colour)
{
    (void)user_data;
    if (index < 192)
        s_cram[index] = md_colour_to_argb(colour);
}

static void scanline_rendered_cb(void *user_data,
                                  cc_u16f scanline,
                                  const cc_u8l *pixels,
                                  cc_u16f left_boundary,
                                  cc_u16f right_boundary,
                                  cc_u16f screen_width,
                                  cc_u16f screen_height)
{
    (void)user_data;

    /* Track actual screen dimensions reported by VDP */
    s_screen_width  = (int)screen_width;
    s_screen_height = (int)screen_height;

    if ((int)scanline >= MAX_SCREEN_HEIGHT)
        return;

    uint32_t *row = s_framebuf + (int)scanline * MAX_SCREEN_WIDTH;

    /* pixels[0..count-1] are palette indices for columns
     * [left_boundary, right_boundary). */
    cc_u16f count = right_boundary - left_boundary;
    for (cc_u16f i = 0; i < count; i++) {
        int col = (int)left_boundary + (int)i;
        if (col >= MAX_SCREEN_WIDTH)
            break;
        row[col] = s_cram[pixels[i]];
    }
}

/* Scripted input: --script "start@700,right@800" */
static uint32_t s_script_start_frame = 0;  /* frame to press Start (0=disabled) */
static uint32_t s_script_right_frame = 0;  /* frame to start holding Right */
static uint32_t s_current_frame_for_input = 0;

/* TCP debug server input override (set_input command) */
static int      s_tcp_input_active = 0;
static uint8_t  s_tcp_input_keys   = 0;  /* Genesis: Up=0,Down=1,Left=2,Right=3,B=4,C=5,A=6,Start=7 */

#include "input_script.h"

static cc_bool input_requested_cb(void *user_data,
                                   cc_u8f player_id,
                                   ClownMDEmu_Button button_id)
{
    (void)user_data;
    if (player_id != 0)
        return cc_false;    /* only P1 */

    /* Scripted-script (.input file) override — fully takes priority
     * over SDL/TCP/legacy script when active. Held-button mask is
     * already in Genesis bit order. */
    if (input_script_active()) {
        uint8_t mask = input_script_held_mask();
        switch (button_id) {
            case CLOWNMDEMU_BUTTON_UP:    return (mask & 0x01) ? cc_true : cc_false;
            case CLOWNMDEMU_BUTTON_DOWN:  return (mask & 0x02) ? cc_true : cc_false;
            case CLOWNMDEMU_BUTTON_LEFT:  return (mask & 0x04) ? cc_true : cc_false;
            case CLOWNMDEMU_BUTTON_RIGHT: return (mask & 0x08) ? cc_true : cc_false;
            case CLOWNMDEMU_BUTTON_B:     return (mask & 0x10) ? cc_true : cc_false;
            case CLOWNMDEMU_BUTTON_C:     return (mask & 0x20) ? cc_true : cc_false;
            case CLOWNMDEMU_BUTTON_A:     return (mask & 0x40) ? cc_true : cc_false;
            case CLOWNMDEMU_BUTTON_START: return (mask & 0x80) ? cc_true : cc_false;
            default: return cc_false;
        }
    }

    /* Scripted inputs override keyboard when active */
    if (s_script_start_frame || s_script_right_frame) {
        uint32_t f = s_current_frame_for_input;
        if (button_id == CLOWNMDEMU_BUTTON_START) {
            /* Press Start for exactly 2 frames at the target frame */
            if (s_script_start_frame && f >= s_script_start_frame && f < s_script_start_frame + 2)
                return cc_true;
        }
        if (button_id == CLOWNMDEMU_BUTTON_RIGHT) {
            if (s_script_right_frame && f >= s_script_right_frame)
                return cc_true;
        }
        if (button_id == CLOWNMDEMU_BUTTON_A) {
            /* Press A (jump) for 2 frames, multiple attempts */
            uint32_t base = s_script_start_frame;
            if (base) {
                /* Jump at base+700, base+750, base+800 (after gameplay loads ~frame 871) */
                uint32_t jumps[] = { base+700, base+750, base+800 };
                for (int j = 0; j < 3; j++)
                    if (f >= jumps[j] && f < jumps[j] + 2)
                        return cc_true;
            }
        }
    }

    /* TCP debug server input override (set_input command).
     * Bit mapping matches Genesis: Up=0,Down=1,Left=2,Right=3,B=4,C=5,A=6,Start=7 */
    if (s_tcp_input_active) {
        cc_bool result = cc_false;
        switch (button_id) {
            case CLOWNMDEMU_BUTTON_UP:    result = (s_tcp_input_keys & 0x01) ? cc_true : cc_false; break;
            case CLOWNMDEMU_BUTTON_DOWN:  result = (s_tcp_input_keys & 0x02) ? cc_true : cc_false; break;
            case CLOWNMDEMU_BUTTON_LEFT:  result = (s_tcp_input_keys & 0x04) ? cc_true : cc_false; break;
            case CLOWNMDEMU_BUTTON_RIGHT: result = (s_tcp_input_keys & 0x08) ? cc_true : cc_false; break;
            case CLOWNMDEMU_BUTTON_B:     result = (s_tcp_input_keys & 0x10) ? cc_true : cc_false; break;
            case CLOWNMDEMU_BUTTON_C:     result = (s_tcp_input_keys & 0x20) ? cc_true : cc_false; break;
            case CLOWNMDEMU_BUTTON_A:     result = (s_tcp_input_keys & 0x40) ? cc_true : cc_false; break;
            case CLOWNMDEMU_BUTTON_START: result = (s_tcp_input_keys & 0x80) ? cc_true : cc_false; break;
            default: break;
        }
        return result;
    }

    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    switch (button_id) {
        case CLOWNMDEMU_BUTTON_UP:    return keys[SDL_SCANCODE_UP]     ? cc_true : cc_false;
        case CLOWNMDEMU_BUTTON_DOWN:  return keys[SDL_SCANCODE_DOWN]   ? cc_true : cc_false;
        case CLOWNMDEMU_BUTTON_LEFT:  return keys[SDL_SCANCODE_LEFT]   ? cc_true : cc_false;
        case CLOWNMDEMU_BUTTON_RIGHT: return keys[SDL_SCANCODE_RIGHT]  ? cc_true : cc_false;
        case CLOWNMDEMU_BUTTON_A:     return keys[SDL_SCANCODE_Z]      ? cc_true : cc_false;
        case CLOWNMDEMU_BUTTON_B:     return keys[SDL_SCANCODE_X]      ? cc_true : cc_false;
        case CLOWNMDEMU_BUTTON_C:     return keys[SDL_SCANCODE_C]      ? cc_true : cc_false;
        case CLOWNMDEMU_BUTTON_START: return keys[SDL_SCANCODE_RETURN] ? cc_true : cc_false;
        default:                      return cc_false;
    }
}

/*
 * Audio accumulation buffers.
 *
 * ClownMDEmu_Iterate() calls fm_audio_cb and psg_audio_cb multiple times
 * per video frame (once per sync point).  We accumulate all callbacks into
 * these buffers and flush once per frame so we can mix FM + PSG together.
 *
 * Sizes: ~887 FM stereo frames / video-frame and ~3729 PSG mono frames /
 * video-frame at 60 Hz NTSC.  4× headroom handles timing jitter.
 */
#define FM_ACCUM_FRAMES  4096
#define PSG_ACCUM_FRAMES 16384

static cc_s16l s_fm_accum [FM_ACCUM_FRAMES  * 2];  /* stereo */
static cc_s16l s_psg_accum[PSG_ACCUM_FRAMES];       /* mono   */
static size_t  s_fm_count  = 0;
static size_t  s_psg_count = 0;

#include "audio/mixer.h"          /* audio_mixer_drain */
#include "audio/observability.h"  /* boop detector */
extern uint32_t g_audio_cycle_counter;
extern uint64_t g_frame_count;
extern uint32_t m68k_read32(uint32_t);
extern uint8_t  m68k_read8 (uint32_t);

/* Audio backend switch for A/B diagnosis:
 *   "ours"       (default) : our cycle-stamped YM2612/PSG via audio_mixer_drain
 *   "clownmdemu"           : clownmdemu's FM/PSG via its sync callbacks
 * Select with --audio-backend=ours|clownmdemu. */
enum { AUDIO_BACKEND_OURS = 0, AUDIO_BACKEND_CLOWNMDEMU = 1 };
static int s_audio_backend = AUDIO_BACKEND_OURS;

static void fm_audio_cb(void *user_data, ClownMDEmu *clownmdemu,
                         size_t total_frames,
                         void (*generate)(ClownMDEmu*, cc_s16l*, size_t))
{
    (void)user_data;
#if ENABLE_RECOMPILED_CODE
    if (s_audio_backend == AUDIO_BACKEND_OURS) {
        /* Our mixer is the source; clownmdemu's callback is a no-op. */
        (void)clownmdemu; (void)total_frames; (void)generate;
        return;
    }
#endif
    size_t avail = FM_ACCUM_FRAMES - s_fm_count;
    if (total_frames > avail) total_frames = avail;
    if (total_frames > 0) {
        generate(clownmdemu, s_fm_accum + s_fm_count * 2, total_frames);
        s_fm_count += total_frames;
    }
}

static void psg_audio_cb(void *user_data, ClownMDEmu *clownmdemu,
                          size_t total_frames,
                          void (*generate)(ClownMDEmu*, cc_s16l*, size_t))
{
    (void)user_data;
#if ENABLE_RECOMPILED_CODE
    if (s_audio_backend == AUDIO_BACKEND_OURS) {
        (void)clownmdemu; (void)total_frames; (void)generate;
        return;
    }
#endif
    size_t avail = PSG_ACCUM_FRAMES - s_psg_count;
    if (total_frames > avail) total_frames = avail;
    if (total_frames > 0) {
        generate(clownmdemu, s_psg_accum + s_psg_count, total_frames);
        s_psg_count += total_frames;
    }
}

static void pcm_audio_cb(void *user_data, ClownMDEmu *c, size_t f,
                          void (*g)(ClownMDEmu*, cc_s16l*, size_t))
{ (void)user_data; (void)c; (void)f; (void)g; }

static void cdda_audio_cb(void *user_data, ClownMDEmu *c, size_t f,
                           void (*g)(ClownMDEmu*, cc_s16l*, size_t))
{ (void)user_data; (void)c; (void)f; (void)g; }

/* CD and save-file stubs (cartridge-only game) */
static void    cd_seeked_cb(void *u, cc_u32f s)
               { (void)u; (void)s; }
static void    cd_sector_read_cb(void *u, cc_u16l *b)
               { (void)u; (void)b; }
static cc_bool cd_track_seeked_cb(void *u, cc_u16f t, ClownMDEmu_CDDAMode m)
               { (void)u; (void)t; (void)m; return cc_false; }
static size_t  cd_audio_read_cb(void *u, cc_s16l *b, size_t f)
               { (void)u; (void)b; return (size_t)0; (void)f; }
static cc_bool save_opened_read_cb(void *u, const char *n)
               { (void)u; (void)n; return cc_false; }
static cc_s16f save_read_cb(void *u)
               { (void)u; return -1; }
static cc_bool save_opened_write_cb(void *u, const char *n)
               { (void)u; (void)n; return cc_false; }
static void    save_written_cb(void *u, cc_u8f b)
               { (void)u; (void)b; }
static void    save_closed_cb(void *u)
               { (void)u; }
static cc_bool save_removed_cb(void *u, const char *n)
               { (void)u; (void)n; return cc_false; }
static cc_bool save_size_cb(void *u, const char *n, size_t *sz)
               { (void)u; (void)n; (void)sz; return cc_false; }

/* =========================================================================
 * ROM loading
 * ========================================================================= */

/* Reads the ROM file at path.  Allocates and returns a cc_u16l[] buffer
 * (host-native 16-bit, values byte-swapped from the big-endian ROM file).
 * *out_words receives the number of 16-bit words; *raw_bytes receives the
 * raw byte buffer (for glue_init's g_rom copy); *raw_len receives byte count.
 * Caller must free() both returned pointers. */
static cc_u16l *load_rom(const char *path,
                          cc_u32l *out_words,
                          uint8_t **raw_bytes,
                          cc_u32l *raw_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror("fopen"); return NULL; }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0 || sz > 0x800000L) {
        fprintf(stderr, "ROM size out of range: %ld bytes\n", sz);
        fclose(fp);
        return NULL;
    }

    uint8_t *raw = (uint8_t *)malloc((size_t)sz);
    if (!raw) { fclose(fp); return NULL; }
    if (fread(raw, 1, (size_t)sz, fp) != (size_t)sz) {
        fprintf(stderr, "ROM read error\n");
        free(raw);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    cc_u32l words = (cc_u32l)(sz / 2);
    cc_u16l *buf  = (cc_u16l *)malloc(words * sizeof(cc_u16l));
    if (!buf) { free(raw); return NULL; }

    /* Genesis ROMs are big-endian.  Convert each 16-bit word to host order. */
    for (cc_u32l i = 0; i < words; i++)
        buf[i] = (cc_u16l)(((cc_u16l)raw[i * 2] << 8) | raw[i * 2 + 1]);

    *out_words  = words;
    *raw_bytes  = raw;
    *raw_len    = (cc_u32l)sz;
    return buf;
}

/* =========================================================================
 * ClownMDEmu instance (static storage)
 * ========================================================================= */

ClownMDEmu g_clownmdemu;

static int path_is_absolute(const char *path)
{
    if (!path || !path[0])
        return 0;
    if (path[0] == '/' || path[0] == '\\')
        return 1;
    return ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':';
}

static const char *resolve_runner_path(const char *path, char *buf, size_t buf_len)
{
    if (path_is_absolute(path))
        return path;
    snprintf(buf, buf_len, "%s", exe_relative(path));
    return buf;
}

static int runner_save_state_file(const char *path)
{
    char full_path[512];
    const char *resolved = resolve_runner_path(path, full_path, sizeof(full_path));
    FILE *sf = fopen(resolved, "wb");
    if (!sf) {
        fprintf(stderr, "[SAVE] failed to open %s\n", resolved);
        return 0;
    }

    size_t wrote = fwrite(&g_clownmdemu, 1, sizeof(g_clownmdemu), sf);
#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
    glue_save_state(sf);
#endif
    int ok = (wrote == sizeof(g_clownmdemu)) && !ferror(sf);
    fclose(sf);

    if (ok)
        fprintf(stderr, "[SAVE] saved %s\n", resolved);
    else
        fprintf(stderr, "[SAVE] failed while writing %s\n", resolved);
    return ok;
}

static int runner_load_state_file(const char *path)
{
    char full_path[512];
    const char *resolved = resolve_runner_path(path, full_path, sizeof(full_path));
    FILE *sf = fopen(resolved, "rb");
    if (!sf) {
        fprintf(stderr, "[LOAD] empty/missing %s\n", resolved);
        return 0;
    }

    const ClownMDEmu_Callbacks *cb = g_clownmdemu.callbacks;
    const cc_u16l *cart = g_clownmdemu.cartridge_buffer;
    cc_u32l cart_len = g_clownmdemu.cartridge_buffer_length;
    size_t read = fread(&g_clownmdemu, 1, sizeof(g_clownmdemu), sf);
    g_clownmdemu.callbacks = cb;
    g_clownmdemu.cartridge_buffer = cart;
    g_clownmdemu.cartridge_buffer_length = cart_len;

    if (read == sizeof(g_clownmdemu)) {
#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
        glue_load_state(sf);
#endif
    }
    int ok = (read == sizeof(g_clownmdemu)) && !ferror(sf);
    fclose(sf);

#if ENABLE_RECOMPILED_CODE
    if (ok && g_game_spec.resume_main_loop_pc)
        glue_restart_game_fiber(g_game_spec.resume_main_loop_pc);
#endif

    if (ok)
        fprintf(stderr, "[LOAD] loaded %s\n", resolved);
    else
        fprintf(stderr, "[LOAD] failed/truncated %s\n", resolved);
    return ok;
}

static uint8_t runner_ram_byte(uint32_t addr)
{
    uint16_t off = (uint16_t)(addr & 0xFFFFu);
    uint16_t word = g_clownmdemu.state.m68k.ram[off / 2u];
    return (off & 1u) ? (uint8_t)(word & 0xFFu) : (uint8_t)(word >> 8);
}

static int runner_dump_ram_file(const char *path)
{
    char full_path[512];
    const char *resolved = resolve_runner_path(path, full_path, sizeof(full_path));
    FILE *df = fopen(resolved, "wb");
    if (!df) {
        fprintf(stderr, "[RAMDUMP] failed to open %s\n", resolved);
        return 0;
    }

    uint8_t bytes[0x10000];
    for (uint32_t i = 0; i < 0x10000u; i++)
        bytes[i] = runner_ram_byte(i);

    size_t wrote = fwrite(bytes, 1, sizeof(bytes), df);
    int ok = (wrote == sizeof(bytes)) && !ferror(df);
    fclose(df);

    if (ok)
        fprintf(stderr, "[RAMDUMP] wrote %s\n", resolved);
    else
        fprintf(stderr, "[RAMDUMP] failed while writing %s\n", resolved);
    return ok;
}

static int runner_write_screenshot_file(const char *path)
{
    char full_path[512];
    const char *resolved = resolve_runner_path(path, full_path, sizeof(full_path));
    int ok = png_write_argb(resolved, s_framebuf,
                            s_screen_width, s_screen_height,
                            MAX_SCREEN_WIDTH) == 0;
    if (ok)
        fprintf(stderr, "[SCREENSHOT] wrote %s\n", resolved);
    else
        fprintf(stderr, "[SCREENSHOT] failed to write %s\n", resolved);
    return ok;
}

/* =========================================================================
 * Frame logging (main-loop version, works in ALL build modes)
 * ========================================================================= */

static FILE *s_framelog_file = NULL;

/* Dump full 64KB work RAM at a specific game frame counter for comparison */
static void check_ramdump(void)
{
    /* Align by game state: dump when in a gameplay mode (per-game
     * level_modes from g_game_layout) with the player object active
     * (obj_id byte = $01 = Sonic) and 50 frames into stable state. */
    #define EMU_BYTE_D(a) ((uint8_t)(g_clownmdemu.state.m68k.ram[((a) & 0xFFFF) / 2] >> \
                   (((a) & 1) ? 0 : 8)))
    uint8_t mode = EMU_BYTE_D(g_game_layout.game_mode_addr);
    uint8_t obj0 = EMU_BYTE_D(g_game_layout.player_object_addr);
    static uint32_t s_gameplay_frames = 0;
    /* Accept any of the per-game gameplay modes (with or without high
     * bit set, since some games flip $80 during transitions). */
    uint8_t base_mode = mode & 0x7Fu;
    bool in_gameplay = false;
    for (int i = 0; i < g_game_layout.level_mode_count; i++) {
        if (base_mode == g_game_layout.level_modes[i]) { in_gameplay = true; break; }
    }
    if (in_gameplay && obj0 == 0x01u)
        s_gameplay_frames++;
    if (s_gameplay_frames == 50) {  /* 50 frames into stable gameplay */
        static int s_dumped = 0;
        if (!s_dumped) {
            s_dumped = 1;
#if ENABLE_RECOMPILED_CODE
            const char *path = exe_relative("ramdump_native.bin");
#else
            const char *path = exe_relative("ramdump_interp.bin");
#endif
            FILE *df = fopen(path, "wb");
            if (df) {
                fwrite(g_clownmdemu.state.m68k.ram, 2, 0x8000, df);
                fclose(df);
                fprintf(stderr, "[RAMDUMP] Wrote %s (50 gameplay frames in)\n", path);
            }
        }
    }
}

static void write_framelog(uint32_t frame)
{
    if (!s_framelog_file) return;
    if (frame > 9999) return;

    /* Read from clownmdemu RAM (word-addressed, big-endian) */
    #define EMU_BYTE(addr) \
        ((uint8_t)(g_clownmdemu.state.m68k.ram[((addr) & 0xFFFF) / 2] >> \
                   (((addr) & 1) ? 0 : 8)))
    #define EMU_WORD(addr) \
        ((uint16_t)(g_clownmdemu.state.m68k.ram[((addr) & 0xFFFF) / 2]))
    #define EMU_LONG(addr) \
        (((uint32_t)EMU_WORD(addr) << 16) | EMU_WORD((addr)+2))

    /* Universal fields come from g_game_layout. Game-specific fields
     * (cnt $F628, scrl $F700, plc $F680, P1 ctrl mirrors, Sonic-1
     * object-slot offsets) keep raw addresses — they are S1-shaped
     * debug telemetry and are decorative on other games. Use the
     * FrameRecord ring + per-game fill_frame_record() for game-
     * specific debug data. */
    uint32_t player = g_game_layout.player_object_addr;
    uint32_t rdb_func = 0;
#if SONIC_REVERSE_DEBUG && ENABLE_RECOMPILED_CODE
    rdb_func = g_rdb_current_func;
#endif

    fprintf(s_framelog_file,
            "F%03u mode=%02X vbl=%02X cnt=%04X scrl=%04X plc=%04X "
            "fcnt=%08X obj0=%02X/%02X xpos=%04X ypos=%04X xvel=%04X yvel=%04X inrt=%04X "
            "rtn=%02X log=%02X/%02X phys=%02X/%02X st=%02X lk=%02X "
            "pc=%06X tc=%02X/%02X x=%04X tgt=%04X dur=%02X bg=%02X/%02X left=%02X/%02X bottom=%02X/%02X tf=%02X\n",
            frame,
            EMU_BYTE(g_game_layout.game_mode_addr),
            EMU_BYTE(g_game_layout.vint_routine_addr),
            EMU_WORD(0xF628),
            EMU_WORD(0xF700), EMU_WORD(0xF680),
            EMU_LONG(g_game_layout.vint_runcount_addr),
            EMU_BYTE(player + 0x00), EMU_BYTE(player + 0x01),
            EMU_WORD(player + 0x08),  /* player X position */
            EMU_WORD(player + 0x0C),  /* player Y position */
            EMU_WORD(player + 0x10),  /* player X velocity */
            EMU_WORD(player + 0x12),  /* player Y velocity */
            EMU_WORD(player + 0x14),  /* player inertia */
            EMU_BYTE(player + 0x24),  /* player routine     */
            EMU_BYTE(0xF602),  /* P1 held (logical) — S1-shaped */
            EMU_BYTE(0xF603),  /* P1 pressed (logical) */
            EMU_BYTE(0xF604),  /* P1 held (physical) */
            EMU_BYTE(0xF605),  /* P1 pressed (physical) */
            EMU_BYTE(player + 0x22),  /* player status */
            EMU_BYTE(g_game_layout.vint_routine_addr),
            rdb_func,
            EMU_BYTE(0xB080), EMU_BYTE(0xB0A4),
            EMU_WORD(0xB088), EMU_WORD(0xB0B0), EMU_BYTE(0xB09E),
            EMU_BYTE(0xB140), EMU_BYTE(0xB164),
            EMU_BYTE(0xB1C0), EMU_BYTE(0xB1E4),
            EMU_BYTE(0xB180), EMU_BYTE(0xB1A4),
            EMU_BYTE(0xF623));
    fflush(s_framelog_file);

    #undef EMU_BYTE
    #undef EMU_WORD
    #undef EMU_LONG
}

#if SONIC_REVERSE_DEBUG && ENABLE_RECOMPILED_CODE
/* Tier-2 park drain. Called after each ClownMDEmu_Iterate that may
 * have parked the game fiber at a block-entry breakpoint. When the
 * game fiber is parked we own the main thread and must keep
 * cmd_server polling alive until a TCP command (rdb_step/continue/
 * step_over) sets g_rdb_resume_now — at which point we SwitchToFiber
 * back. If the resumed fiber parks again, we loop. Exits when the
 * fiber yielded for any other reason (vblank / cycle budget / done).
 *
 * Safe to call unconditionally after Iterate — if the fiber didn't
 * park for a break, glue_game_yielded_for_break() is 0 and we return
 * immediately.
 */
static int s_quit_via_park_drain = 0;
static void rdb_park_drain(void)
{
    while (glue_game_yielded_for_break()) {
        SDL_Event pev;
        while (SDL_PollEvent(&pev)) {
            if (pev.type == SDL_QUIT) { s_quit_via_park_drain = 1; return; }
        }
        (void)cmd_server_poll();
        if (g_rdb_resume_now) {
            g_rdb_resume_now = 0;
            glue_resume_from_break();
        } else {
            SDL_Delay(2);
        }
    }
}
#endif

/* =========================================================================
 * main
 * ========================================================================= */

int main(int argc, char *argv[])
{
    init_exe_dir(argv[0]);

    /* --- Parse arguments --- */
    const char *rom_path = NULL;
    const char *framelog_path = NULL;
    uint32_t max_frames  = 0;   /* 0 = unlimited */
    int start_turbo      = 0;   /* --turbo: skip frame delay + audio */
    /* Debug-server port: precedence is --port > debug.ini "port" > compile-time
     * DEFAULT_DEBUG_PORT (4378 native, 4379 oracle). 0 here means "unset; use
     * the compile-time default unless debug.ini overrides it". */
#ifndef DEFAULT_DEBUG_PORT
#  define DEFAULT_DEBUG_PORT 4378
#endif
    int debug_port_cli = 0;

    /* --- Paced-native spike (option 2) ---
     * --target-fps N artificially throttles the wall-clock frame rate.
     * Default 0 = use NTSC 59.94 Hz. Non-zero forces the pacer to wait
     * until 1/N seconds have elapsed per frame. Both binaries can be
     * launched with the same --target-fps so they run in wall-clock
     * lockstep — making wall-frame N a meaningful sync key in addition
     * to the state-marker sync compare_runs.py uses by default. */
    double target_fps_cli = 0.0;

    /* --mem-write-log=ADDR1,ADDR2,...[@FRAMES]  — arm at boot so we catch
     * gm=0 writes that TCP arming misses due to startup latency. */
    const char *mem_write_log_spec = NULL;
    const char *wav_path = NULL;

    /* Headless smoke / framebuffer-hash assertion mode. When --hash-frames
     * is set, emit a [FBHASH] line every N wall frames with an FNV-1a-64
     * fingerprint of the framebuffer's active region. Used by CI / golden
     * comparison to detect regressions without a human watching the screen.
     * Pairs naturally with --max-frames; together they make the runner a
     * one-shot regression test. */
    uint32_t hash_frames = 0;

    /* --input-script PATH — load a .input scripting file (button
     * timeline + RAM assertions + EXIT). See runner/input_script.h
     * for the format spec. Combined with --max-frames + --hash-frames,
     * makes the runner a one-shot regression-test driver. */
    const char *input_script_path = NULL;

    /* Pacing mode override: --pacing fiber|accurate or --pacing=...
     * NULL = not set via CLI (debug.ini may still override; otherwise
     * compiled default in glue.c wins). */
    const char *pacing_cli = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--max-frames") == 0 && i + 1 < argc) {
            max_frames = (uint32_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "--framelog") == 0 && i + 1 < argc) {
            framelog_path = argv[++i];
        } else if (strcmp(argv[i], "--turbo") == 0) {
            start_turbo = 1;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            debug_port_cli = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--target-fps") == 0 && i + 1 < argc) {
            target_fps_cli = atof(argv[++i]);
        } else if (strcmp(argv[i], "--script-start") == 0 && i + 1 < argc) {
            s_script_start_frame = (uint32_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "--script-right") == 0 && i + 1 < argc) {
            s_script_right_frame = (uint32_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "--mem-write-log") == 0 && i + 1 < argc) {
            mem_write_log_spec = argv[++i];
        } else if (strcmp(argv[i], "--wav") == 0 && i + 1 < argc) {
            wav_path = argv[++i];
        } else if (strcmp(argv[i], "--pacing") == 0 && i + 1 < argc) {
            pacing_cli = argv[++i];
        } else if (strncmp(argv[i], "--pacing=", 9) == 0) {
            pacing_cli = argv[i] + 9;
        } else if (strcmp(argv[i], "--hash-frames") == 0 && i + 1 < argc) {
            hash_frames = (uint32_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "--input-script") == 0 && i + 1 < argc) {
            input_script_path = argv[++i];
        } else if (strncmp(argv[i], "--audio-backend=", 16) == 0) {
            const char *v = argv[i] + 16;
            if      (strcmp(v, "ours")       == 0) s_audio_backend = AUDIO_BACKEND_OURS;
            else if (strcmp(v, "clownmdemu") == 0) s_audio_backend = AUDIO_BACKEND_CLOWNMDEMU;
            else fprintf(stderr, "warning: unknown --audio-backend=%s (use ours|clownmdemu)\n", v);
            fprintf(stderr, "[audio] backend=%s\n",
                    s_audio_backend == AUDIO_BACKEND_OURS ? "ours" : "clownmdemu");
        } else if (argv[i][0] != '-') {
            rom_path = argv[i];
        }
    }
    if (!rom_path) {
#ifdef _WIN32
        static char picked_path[512] = {0};
        {
            extern int __stdcall GetOpenFileNameA(void *);
            typedef struct {
                unsigned long lStructSize; void *hwndOwner; void *hInstance;
                const char *lpstrFilter; char *lpstrCustomFilter;
                unsigned long nMaxCustFilter; unsigned long nFilterIndex;
                char *lpstrFile; unsigned long nMaxFile;
                char *lpstrFileTitle; unsigned long nMaxFileTitle;
                const char *lpstrInitialDir; const char *lpstrTitle;
                unsigned long Flags; unsigned short nFileOffset;
                unsigned short nFileExtension; const char *lpstrDefExt;
                long lCustData; void *lpfnHook; const char *lpTemplateName;
            } OPENFILENAMEA;
            OPENFILENAMEA ofn = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFilter = "Genesis/Mega Drive ROM\0*.bin;*.md;*.gen;*.smd\0All Files\0*.*\0";
            ofn.lpstrFile = picked_path;
            ofn.nMaxFile = sizeof(picked_path);
            ofn.lpstrTitle = "Select Sonic the Hedgehog ROM";
            ofn.Flags = 0x00080000 | 0x00001000;
            if (GetOpenFileNameA(&ofn))
                rom_path = picked_path;
        }
#endif
        if (!rom_path) {
            fprintf(stderr, "Usage: %s <sonic.bin>\n", argv[0]);
            return 1;
        }
    }

    /* --- Load ROM --- */
    cc_u32l rom_words = 0;
    uint8_t *rom_raw  = NULL;
    cc_u32l rom_raw_len = 0;
    cc_u16l *rom_buf  = load_rom(rom_path, &rom_words, &rom_raw, &rom_raw_len);
    if (!rom_buf) return 1;

    /* --- Load input script (if requested) before the SDL window opens
     * so any parse error fails fast without a flash of black window. */
    if (input_script_path) input_script_load(input_script_path);

    /* --- Load symbol table for crash reports ---
     * Look for `annotations_from_disasm.csv` adjacent to the ROM.
     * Falls back silently if not present — crash reports will just
     * show raw $XXXXXX addresses without names. */
    {
        extern int crash_report_load_symbols(const char *);
        char sym_path[640];
        const char *slash  = strrchr(rom_path, '/');
        const char *bslash = strrchr(rom_path, '\\');
        const char *sep    = slash > bslash ? slash : bslash;
        if (sep) {
            int dir_len = (int)(sep - rom_path) + 1;
            snprintf(sym_path, sizeof(sym_path),
                     "%.*sannotations_from_disasm.csv", dir_len, rom_path);
        } else {
            snprintf(sym_path, sizeof(sym_path), "annotations_from_disasm.csv");
        }
        int n = crash_report_load_symbols(sym_path);
        if (n > 0)
            fprintf(stderr, "[crash_report] loaded %d symbols from %s\n", n, sym_path);
    }

    /* --- SDL init --- */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        g_game_spec.display_name ? g_game_spec.display_name : "Sonic the Hedgehog",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        640, 448,   /* 2× scale: 320×224 → 640×448 */
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, MAX_SCREEN_WIDTH, MAX_SCREEN_HEIGHT);

    SDL_Texture *texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        MAX_SCREEN_WIDTH, MAX_SCREEN_HEIGHT);
    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return 1;
    }

    /* Output at PSG rate (~223721 Hz NTSC) — matches the reference mixer.
     * PSG never needs resampling; FM is upsampled to this rate. */
    audio_init(CLOWNMDEMU_PSG_SAMPLE_RATE_NTSC);

    /* --- clownmdemu init --- */
    ClownMDEmu_Constant_Initialise();

    ClownMDEmu_InitialConfiguration config;
    memset(&config, 0, sizeof(config));
    config.general.region       = CLOWNMDEMU_REGION_OVERSEAS;
    config.general.tv_standard  = CLOWNMDEMU_TV_STANDARD_NTSC;

    static const ClownMDEmu_Callbacks cbs = {
        NULL,                       /* user_data */
        colour_updated_cb,
        scanline_rendered_cb,
        input_requested_cb,
        fm_audio_cb,
        psg_audio_cb,
        pcm_audio_cb,
        cdda_audio_cb,
        cd_seeked_cb,
        cd_sector_read_cb,
        cd_track_seeked_cb,
        cd_audio_read_cb,
        save_opened_read_cb,
        save_read_cb,
        save_opened_write_cb,
        save_written_cb,
        save_closed_cb,
        save_removed_cb,
        save_size_cb,
    };

    ClownMDEmu_Initialise(&g_clownmdemu, &config, &cbs);
    ClownMDEmu_SetCartridge(&g_clownmdemu, rom_buf, rom_words);
    ClownMDEmu_HardReset(&g_clownmdemu, cc_true, cc_false);

#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
    /* Step 2 / Hybrid: initialise glue (Step 2 also starts the game thread). */
    glue_init(&g_clownmdemu, rom_raw, rom_raw_len);
#endif

    /* Audio arch overhaul: initialise our cycle-stamped YM2612 + PSG
     * instances. Safe to call on oracle too (init functions are idempotent
     * no-ops if stubs). */
    audio_mixer_init();
    audio_obs_init();

    free(rom_raw);   /* glue_init copied what it needs */

    /* TCP debug server — only if debug.ini exists next to the exe.
     * Port resolution (highest priority first):
     *   1. --port N                  command-line flag
     *   2. "port=N" in debug.ini     project-level config
     *   3. DEFAULT_DEBUG_PORT macro  compile-time default per build target
     *      (4378 native, 4379 oracle — set in CMakeLists.txt) */
    static int s_debug_enabled    = 0;
    int    debug_port_from_ini    = 0;
    double target_fps_from_ini    = 0.0;
    char   pacing_from_ini[32]    = {0};
    {
        FILE *df = fopen(exe_relative("debug.ini"), "r");
        if (df) {
            s_debug_enabled = 1;
            char ln[256];
            while (fgets(ln, sizeof(ln), df)) {
                /* tolerant key=value parser; skips blanks/comments */
                char *eq = strchr(ln, '=');
                if (!eq) continue;
                *eq = '\0';
                char *k = ln, *v = eq + 1;
                while (*k == ' ' || *k == '\t') k++;
                char *ke = k + strlen(k);
                while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
                *ke = '\0';
                while (*v == ' ' || *v == '\t') v++;
                /* Strip trailing whitespace from value too. */
                char *ve = v + strlen(v);
                while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' ||
                                  ve[-1] == '\r' || ve[-1] == '\n')) ve--;
                *ve = '\0';
                if      (strcmp(k, "port")       == 0) debug_port_from_ini = atoi(v);
                else if (strcmp(k, "target_fps") == 0) target_fps_from_ini = atof(v);
                else if (strcmp(k, "pacing")     == 0)
                    strncpy(pacing_from_ini, v, sizeof(pacing_from_ini) - 1);
            }
            fclose(df);
        }
    }
    /* --target-fps wins over debug.ini, which wins over default 59.94. */
    double target_fps = (target_fps_cli > 0.0)     ? target_fps_cli :
                        (target_fps_from_ini > 0.0) ? target_fps_from_ini :
                                                       (60000.0 / 1001.0);  /* NTSC */
    fprintf(stderr, "[pacer] target frame rate: %.4f fps\n", target_fps);
    int debug_port = debug_port_cli ? debug_port_cli :
                     (debug_port_from_ini ? debug_port_from_ini :
                      DEFAULT_DEBUG_PORT);
    if (s_debug_enabled)
        cmd_server_init(debug_port);

#if SONIC_REVERSE_DEBUG
    /* Always-on Tier 1 store ring — install before the first 68K
     * instruction so probes have full coverage from boot without any
     * arm-then-attach sequence. Per the global ring-buffer rule:
     * observation must not require timing or pre-arming. */
    { extern void rdb_autostart(void); rdb_autostart(); }
#endif

    /* Pacing resolution: --pacing wins over debug.ini, which wins over
     * the compiled default in glue.c (CYCLE_ACCURATE since the cycle-
     * tables fix made the per-instruction cycle counts exact). */
    {
        const char *chosen = pacing_cli ? pacing_cli :
                             (pacing_from_ini[0] ? pacing_from_ini : NULL);
        if (chosen) {
            if (strcmp(chosen, "accurate") == 0)
                g_pacing_mode = GLUE_PACING_CYCLE_ACCURATE;
            else if (strcmp(chosen, "fiber") == 0)
                g_pacing_mode = GLUE_PACING_FIBER_FULL;
            else
                fprintf(stderr, "[pacing] unknown mode '%s' — using compiled default\n",
                        chosen);
        }
        fprintf(stderr, "[pacing] mode=%s\n",
                g_pacing_mode == GLUE_PACING_CYCLE_ACCURATE ? "accurate" : "fiber");
    }

    /* --mem-write-log: arm the memory-write logger BEFORE the first frame.
     * Spec format: "0xFFF001,0xFFF002,0xFFF009[@FRAMES]".  Output file name
     * mirrors the TCP convention so the comparator picks it up unchanged. */
    if (mem_write_log_spec) {
        uint32_t addrs_lo[32];
        uint32_t addrs_hi[32];
        int n_addrs = 0;
        int frames = 0;
        char buf[256];
        strncpy(buf, mem_write_log_spec, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char *at = strchr(buf, '@');
        if (at) { *at = '\0'; frames = atoi(at + 1); }
        char *tok = strtok(buf, ",");
        while (tok && n_addrs < 32) {
            char *dash = strchr(tok, '-');
            addrs_lo[n_addrs] = (uint32_t)strtoul(tok, NULL, 0);
            addrs_hi[n_addrs] = dash ? (uint32_t)strtoul(dash + 1, NULL, 0)
                                     : addrs_lo[n_addrs];
            n_addrs++;
            tok = strtok(NULL, ",");
        }
        const char *path =
#if ENABLE_RECOMPILED_CODE
            exe_relative("mem_write_log_native.log");
#else
            exe_relative("mem_write_log_oracle.log");
#endif
        if (!cmd_server_mem_write_log_start_ranges(addrs_lo, addrs_hi, n_addrs, frames, path))
            fprintf(stderr, "[MEM-WRITE-LOG] failed to arm (spec=%s)\n", mem_write_log_spec);
    }

    if (wav_path) {
        extern int audio_wav_start(const char *);
        if (audio_wav_start(wav_path) != 0)
            fprintf(stderr, "[WAV] failed to open %s\n", wav_path);
    }

    if (framelog_path)
        s_framelog_file = fopen(framelog_path, "w");

#if ENABLE_RECOMPILED_CODE
    {
        extern FILE *g_yield_log_file;
        const char *yp = exe_relative("yield_log_native.log");
        g_yield_log_file = fopen(yp, "w");
        if (g_yield_log_file) {
            fprintf(g_yield_log_file, "# frame cycle_acc v_vblank_count vbla_routine\n");
        }
    }
#endif

    /* --- Save state (quick & dirty: snapshot the entire ClownMDEmu struct) --- */
    static ClownMDEmu s_savestate;
    int s_savestate_valid = 0;

    /* --- Main loop --- */
    int running = 1;
    int turbo   = start_turbo;   /* F5 toggles turbo (uncapped frame rate, no audio) */
    uint32_t frame_num = 0;
    Uint32 frame_start = SDL_GetTicks();
    const Uint32 frame_ms = 1000u / 60u;   /* ~16 ms at 60 Hz */

    while (running) {
        if (max_frames && frame_num >= max_frames) break;

        /* Poll TCP debug server */
        CmdResult cmd_cr = {0};
        if (s_debug_enabled) cmd_cr = cmd_server_poll();
        if (cmd_cr.should_quit) running = 0;
        if (cmd_cr.input_override) {
            s_tcp_input_active = 1;
            s_tcp_input_keys = cmd_cr.input_keys;
        }
        /* run_extra_frames handled after normal frame */

        /* Pause loop — hold ring buffer steady for multi-fetch tools.
         * Drain SDL events so the window stays responsive; spin on the
         * cmd server so "continue" / "quit" can break us out. */
        if (s_debug_enabled && cmd_server_is_paused()) {
            SDL_Event pev;
            while (SDL_PollEvent(&pev)) {
                if (pev.type == SDL_QUIT) { running = 0; break; }
            }
            if (!running) break;
            CmdResult pcr = cmd_server_poll();
            if (pcr.should_quit) { running = 0; break; }
            SDL_Delay(5);
            continue;   /* re-check at loop top */
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = 0;

                /* Save states: Shift+F1-F9 = save, F1-F9 = load */
                {
                    int slot = -1;
                    if (ev.key.keysym.sym >= SDLK_F1 && ev.key.keysym.sym <= SDLK_F9)
                        slot = ev.key.keysym.sym - SDLK_F1 + 1;
                    if (slot >= 1 && slot <= 9) {
                        int is_save = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
                        char slot_name[48];
#if ENABLE_RECOMPILED_CODE
                        snprintf(slot_name, sizeof(slot_name), "native_save_%d.bin", slot);
#else
                        snprintf(slot_name, sizeof(slot_name), "interp_save_%d.bin", slot);
#endif
                        if (is_save)
                            runner_save_state_file(slot_name);
                        else
                            runner_load_state_file(slot_name);
                    }
                }
#if HYBRID_RECOMPILED_CODE
                if (ev.key.keysym.sym == SDLK_F8) {
                    VerifyTogglePhase();
                }
#endif
            }
        }

        /* Tab = hold for turbo */
        { const Uint8 *ks = SDL_GetKeyboardState(NULL);
          turbo = ks[SDL_SCANCODE_TAB] ? 1 : (start_turbo ? 1 : 0); }

        /* Zero accum buffers before Iterate(): PSG_Update (and FM_OutputSamples)
         * use += to accumulate into the provided buffer, not overwrite.
         * Without this, each frame adds to the previous frame's leftovers,
         * railing to maximum amplitude within a few frames (the "drone"). */
        memset(s_fm_accum,  0, s_fm_count  * 2 * sizeof(cc_s16l));
        memset(s_psg_accum, 0, s_psg_count *     sizeof(cc_s16l));
        s_fm_count  = 0;
        s_psg_count = 0;

#if ENABLE_RECOMPILED_CODE
        /* Single-threaded fiber loop:
         * 1. Run game code until WaitForVBlank yields
         * 2. Service VBlank: run handlers (palette DMA, joypad, PLC)
         * 3. Iterate: render VDP frame using updated state, generate audio
         *
         * Scanline interleave: game code runs INSIDE Iterate via DoCycles.
         * DoCycles switches to game fiber for each scanline's worth of
         * cycles. Game code and VDP rendering interleave naturally. */
        s_current_frame_for_input = frame_num;
        { extern void glue_run_game_frame(void);
          extern void glue_service_vblank(void);
          extern void glue_reset_frame_sync(void);
          glue_reset_frame_sync();
          glue_run_game_frame();   /* prepares game fiber state */
          ClownMDEmu_Iterate(&g_clownmdemu);  /* DoCycles interleaves game */
          /* Audio arch overhaul: fill s_fm_accum + s_psg_accum from our
           * cycle-stamped mixer. Drain to NTSC wall-frame cycle count
           * (not g_audio_cycle_counter): the game fiber stops running
           * after WaitForVBlank yields, so g_audio_cycle_counter only
           * tracks ~30-50% of a wall's cycles. The YM/PSG chips must
           * still advance the full wall-frame span to generate the
           * expected ~887 FM + ~3732 PSG samples per frame. Handler
           * cycles carry accurate stamps inside [0, wall_cycles];
           * tail advance past the last event fills silence/decay
           * correctly. */
          #define NTSC_WALL_FRAME_68K_CYCLES 127856u
          if (s_audio_backend == AUDIO_BACKEND_OURS) {
              #define NTSC_WALL_FRAME_MASTER_CYCLES 895780u
              audio_mixer_drain(NTSC_WALL_FRAME_MASTER_CYCLES,
                                s_fm_accum,  FM_ACCUM_FRAMES,  &s_fm_count,
                                s_psg_accum, PSG_ACCUM_FRAMES, &s_psg_count);
          }
          /* Observability runs regardless — we want to detect boops in
           * whichever backend is providing samples. */
          audio_obs_ingest_fm ((const int16_t *)s_fm_accum,  s_fm_count);
          audio_obs_ingest_psg((const int16_t *)s_psg_accum, s_psg_count);
          audio_obs_tick_frame(g_frame_count,
                               m68k_read32(0xFFFE0C),
                               m68k_read8 (0xFFF600));
#if SONIC_REVERSE_DEBUG
          rdb_record_iterate();
          rdb_park_drain();
          if (s_quit_via_park_drain) { running = 0; break; }
#endif
          glue_service_vblank();
          glue_end_of_wall_frame(); }
#else
        s_current_frame_for_input = frame_num;
        ClownMDEmu_Iterate(&g_clownmdemu);
#if SONIC_REVERSE_DEBUG
        rdb_record_iterate();
#endif
#endif
        check_ramdump();

#if HYBRID_RECOMPILED_CODE
        { extern void glue_log_frame_state(uint64_t);
          static uint64_t hybrid_frame = 0;
          glue_log_frame_state(hybrid_frame++); }
#endif

        /* --framelog: works in ALL build modes */
        write_framelog(frame_num);

        /* Record frame state into debug server ring buffer */
        if (s_debug_enabled) {
            cmd_server_record_frame(frame_num);
            cmd_server_fm_trace_tick();
        }
        /* mem_write_log runs outside the debug gate so --mem-write-log works
         * standalone (no debug.ini required).  Both ticks are no-ops when
         * their respective trace is inactive. */
        cmd_server_mem_write_log_tick();

        /* Handle run_extra_frames from debug server */
        if (cmd_cr.run_extra_frames > 0) {
            for (int ef = 0; ef < cmd_cr.run_extra_frames; ef++) {
                frame_num++;
                s_current_frame_for_input = frame_num;
                memset(s_fm_accum,  0, sizeof(s_fm_accum));
                memset(s_psg_accum, 0, sizeof(s_psg_accum));
                s_fm_count = 0; s_psg_count = 0;
#if ENABLE_RECOMPILED_CODE
                { extern void glue_run_game_frame(void);
                  extern void glue_service_vblank(void);
                  extern void glue_reset_frame_sync(void);
                  glue_reset_frame_sync();
                  glue_run_game_frame();
                  ClownMDEmu_Iterate(&g_clownmdemu);
#if SONIC_REVERSE_DEBUG
                  rdb_record_iterate();
                  rdb_park_drain();
                  if (s_quit_via_park_drain) { running = 0; break; }
#endif
                  glue_service_vblank();
          glue_end_of_wall_frame(); }
#else
                ClownMDEmu_Iterate(&g_clownmdemu);
#if SONIC_REVERSE_DEBUG
                rdb_record_iterate();
#endif
#endif
                if (s_debug_enabled) cmd_server_record_frame(frame_num);
            }
            if (s_debug_enabled) cmd_server_send_frame_result(cmd_cr.run_extra_frames);
        }

        /* audio_flush is normally gated off in turbo (no SDL playback), but
         * WAV capture lives inside audio_flush — keep flushing while a WAV
         * is being recorded so --wav works with --turbo for headless
         * paired captures. */
        { extern int audio_wav_active(void);
          if (!turbo || audio_wav_active())
            audio_flush((const int16_t *)s_fm_accum, s_fm_count,
                        (const int16_t *)s_psg_accum, s_psg_count); }

        /* Audio queue drift monitor — log every 300 frames (~5 seconds) */
        if (s_debug_enabled && !turbo && (frame_num % 300) == 0 && frame_num > 0) {
            Uint32 qb = audio_queued_bytes();
            Uint32 one_frame_bytes = (Uint32)(s_psg_count * 2 * sizeof(int16_t));
            fprintf(stderr, "[AUDIO-Q] frame=%u  queued=%u bytes (%.1f frames worth)  psg_count=%zu  fm_count=%zu\n",
                    frame_num, qb,
                    one_frame_bytes > 0 ? (double)qb / one_frame_bytes : 0.0,
                    s_psg_count, s_fm_count);
        }

        /* --- Screenshot capture (PNG) --- */
        {
            /* Save PNG screenshots at key frames for comparison.
             * Frames: every 30 frames for first 300, then every 60 up to 900. */
            int should_save = (frame_num < 300 && (frame_num % 30) == 0) ||
                              (frame_num >= 300 && frame_num <= 900 && (frame_num % 60) == 0);
            if (should_save && s_screen_width > 0 && s_screen_height > 0) {
                char path[256];
#if ENABLE_RECOMPILED_CODE
                snprintf(path, sizeof(path), "screenshots/step2_f%04u.png", frame_num);
#else
                snprintf(path, sizeof(path), "screenshots/interp_f%04u.png", frame_num);
#endif
                png_write_argb(path, s_framebuf,
                               s_screen_width, s_screen_height,
                               MAX_SCREEN_WIDTH);
            }
        }
        frame_num++;

        /* Tick the input script (if loaded). RAM read helpers route
         * through clownmdemu's main RAM via emu_read8/16 — same
         * accessors the cmd_server uses. Auto-exit if the script
         * ran an EXIT directive. */
        if (input_script_active()) {
            extern uint8_t  m68k_read8 (uint32_t);
            extern uint16_t m68k_read16(uint32_t);
            extern void     m68k_write8 (uint32_t, uint8_t);
            extern void     m68k_write16(uint32_t, uint16_t);
            extern void     m68k_write32(uint32_t, uint32_t);
            input_script_tick(frame_num, m68k_read8, m68k_read16,
                              m68k_write8, m68k_write16, m68k_write32);
            {
                char state_path[260];
                if (input_script_take_save_state(state_path, sizeof(state_path)))
                    runner_save_state_file(state_path);
                if (input_script_take_load_state(state_path, sizeof(state_path)))
                    runner_load_state_file(state_path);
                if (input_script_take_ram_dump(state_path, sizeof(state_path)))
                    runner_dump_ram_file(state_path);
                if (input_script_take_screenshot(state_path, sizeof(state_path)))
                    runner_write_screenshot_file(state_path);
            }
            if (input_script_should_exit()) {
                fprintf(stderr, "[input_script] exiting per script directive (code=%d)\n",
                        input_script_exit_code());
                return input_script_exit_code();
            }
        }

        /* FNV-1a-64 hash of the framebuffer's active region. Emitted
         * every `hash_frames` frames when --hash-frames is set so CI /
         * golden-image comparison can detect rendering regressions
         * without a human watching the screen. Cheap (one full pass
         * over the active pixels per N frames). */
        if (hash_frames > 0 && (frame_num % hash_frames) == 0) {
            uint64_t h = 0xCBF29CE484222325ULL;
            const uint8_t *p = (const uint8_t *)s_framebuf;
            int row_bytes = s_screen_width * (int)sizeof(uint32_t);
            int stride    = MAX_SCREEN_WIDTH * (int)sizeof(uint32_t);
            for (int y = 0; y < s_screen_height; y++) {
                const uint8_t *row = p + (size_t)y * stride;
                for (int x = 0; x < row_bytes; x++) {
                    h ^= row[x];
                    h *= 0x100000001B3ULL;
                }
            }
            fprintf(stderr, "[FBHASH] frame=%u w=%d h=%d hash=0x%016llX\n",
                    frame_num, s_screen_width, s_screen_height,
                    (unsigned long long)h);
        }

        /* Upload framebuffer to GPU texture */
        SDL_UpdateTexture(texture, NULL, s_framebuf,
                          MAX_SCREEN_WIDTH * (int)sizeof(uint32_t));

        SDL_RenderClear(renderer);

        /* Only show the active area the VDP reported */
        SDL_Rect src = { 0, 0, s_screen_width, s_screen_height };
        SDL_RenderCopy(renderer, texture, &src, NULL);
        SDL_RenderPresent(renderer);

        /* NTSC frame cap.  ClownMDEmu's chip emulation runs cycles_per_frame
         * computed for 59.94 Hz (matches real NTSC Genesis: 60/1.001).
         * Pacing the runner at the same rate keeps audio sample generation
         * in lockstep with SDL playback — no slow drift between game and
         * audio. The hard cap in audio_flush handles per-frame spikes. */
        if (!turbo) {
            static Uint64 s_perf_freq = 0;
            static Uint64 s_next_frame = 0;
            if (!s_perf_freq) {
                s_perf_freq = SDL_GetPerformanceFrequency();
                s_next_frame = SDL_GetPerformanceCounter();
            }
            /* Target wall budget per frame, derived from --target-fps /
             * debug.ini target_fps. Default = NTSC 59.94 Hz. */
            s_next_frame += (Uint64)((double)s_perf_freq / target_fps);
            Uint64 now = SDL_GetPerformanceCounter();
            if (now < s_next_frame) {
                Sint64 remaining_ms = (Sint64)(s_next_frame - now) * 1000 / (Sint64)s_perf_freq;
                if (remaining_ms > 2)
                    SDL_Delay((Uint32)(remaining_ms - 1));
                while (SDL_GetPerformanceCounter() < s_next_frame)
                    ;  /* spin-wait for precision */
            } else {
                s_next_frame = now;
            }
        }
        frame_start = SDL_GetTicks();
    }

    if (max_frames)
        fprintf(stderr, "[DONE] %u frames completed\n", frame_num);

#if ENABLE_RECOMPILED_CODE
    { extern int glue_interp_total_calls(void);
      extern int glue_interp_seen_count(void);
      extern uint64_t glue_miss_count_any(void);
      fprintf(stderr, "[INTERP] hybrid_jmp/call_interpret: %d total calls, "
                      "%d unique true-miss addrs, %llu raw miss events\n",
              glue_interp_total_calls(), glue_interp_seen_count(),
              (unsigned long long)glue_miss_count_any()); }
    { extern uint64_t g_cvblank_fires_total;
      extern int g_dbg_b64_count;
      extern int g_dbg_b5e_count;
      extern int g_dbg_b88_count;
      fprintf(stderr, "[VBLA] cvblank_fires=%llu VBla_Exit=%d VBla_Music=%d loc_B88=%d\n",
              (unsigned long long)g_cvblank_fires_total,
              g_dbg_b64_count, g_dbg_b5e_count, g_dbg_b88_count); }
#endif

    /* --- Cleanup --- */
    if (s_debug_enabled) cmd_server_shutdown();
    if (s_framelog_file) fclose(s_framelog_file);
    { extern int audio_wav_active(void); extern void audio_wav_stop(void);
      if (audio_wav_active()) audio_wav_stop(); }
#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
    glue_shutdown();
#endif
    audio_close();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    free(rom_buf);
    return 0;
}
