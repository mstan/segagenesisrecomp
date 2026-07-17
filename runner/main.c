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
#ifndef _WIN32
#include <unistd.h>   /* readlink, ssize_t — exe-dir resolution + popen picker */
#endif

#include <SDL2/SDL.h>

#if OWN_BACKEND
#include "backend_decls.h"   /* own decls — native builds have no clownmdemu paths */
#else
#include "clownmdemu.h"
#endif
#include "genesis_clocks.h"
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
    /* Inside an AppImage /proc/self/exe is the read-only squashfs mount and
     * argv0 may be relative; $APPIMAGE (exported by the AppImage runtime) is
     * the .AppImage's path, so config/saves anchor next to it. Order:
     * $APPIMAGE, then /proc/self/exe, then argv0. */
    s_exe_dir[0] = '\0';
    {
        const char *appimg = getenv("APPIMAGE");
        if (appimg && appimg[0]) {
            strncpy(s_exe_dir, appimg, sizeof(s_exe_dir) - 1);
        } else {
            ssize_t r = readlink("/proc/self/exe", s_exe_dir, sizeof(s_exe_dir) - 1);
            if (r > 0) s_exe_dir[r] = '\0';
            else if (argv0) strncpy(s_exe_dir, argv0, sizeof(s_exe_dir) - 1);
        }
    }
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

#ifndef _WIN32
/* Run one shell-wrapped native file chooser; read the selected path from its
 * stdout. Each command is gated on `command -v <tool>` so an absent tool
 * prints nothing and we fall through. Returns 1 and fills `out` only on a
 * real selection (the tool printed a path and exited 0). */
static int run_picker_cmd(const char *cmd, char *out, size_t max_len)
{
    FILE *p = popen(cmd, "r");
    if (!p) return 0;
    char buf[1024];
    buf[0] = '\0';
    char *got = fgets(buf, sizeof(buf), p);
    int rc = pclose(p);
    if (!got) return 0;
    size_t n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    if (rc != 0 || n == 0 || n >= max_len) return 0;
    memcpy(out, buf, n + 1);
    return 1;
}
#endif

#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
#include "glue.h"
#endif

#if HYBRID_RECOMPILED_CODE
#include "verify.h"
#endif

#include "cmd_server.h"
#include "game_spec.h"
#include "game_layout.h"
#include "gamepad.h"
#include "input_map.h"
#include "app_config.h"
#if GENESIS_LAUNCHER
#include "launcher_capi.h"
#endif
#if RECOMP_LAUNCHER
/* Shared recomp-ui launcher C ABI. The header is supplied through the game
 * target's include dirs (recomp-ui/src) via recomp_ui.cmake — the engine repo
 * itself does NOT vendor recomp-ui, so this is only reachable when a game opts
 * in by defining RECOMP_LAUNCHER. Mutually exclusive with GENESIS_LAUNCHER. */
#include "recomp_launcher.h"
#endif
#if SONIC_REVERSE_DEBUG
#include "reverse_debug.h"
#endif

/* =========================================================================
 * Framebuffer and palette
 * ========================================================================= */

/* Maximum output dimensions we support. 2P Sonic 2 uses interlace
 * double-resolution mode, which reports 320x448 active pixels. The width
 * ceiling is 512 (= H40 320 + up to 96px of widescreen margin per side) so
 * the opt-in 16:9 path fits the same buffers; authentic 4:3 only ever fills
 * the leftmost 320/256 columns. Must match GVDP_MAX_WIDTH. */
#define MAX_SCREEN_WIDTH  512
#define MAX_SCREEN_HEIGHT 480

static uint32_t s_framebuf[MAX_SCREEN_WIDTH * MAX_SCREEN_HEIGHT]; /* ARGB8888 */

/* ── Present-time screen-color LUT (verified-enhancement video) ──────────
 * Opt-in via GENESIS_SCREEN={raw,crt,trinitron,composite,linear}; default
 * raw=passthrough. Applied to a COPY of s_framebuf at SDL-upload time only —
 * NEVER to s_framebuf itself, so [FBHASH], PNG dumps, and any verify path stay
 * defined on the raw VDP output and remain byte-identical with the screen off.
 * See video/color_lut.h. */
#include "video/color_lut.h"
#include "video/genesis_dac.h"  /* authentic Genesis output-DAC color ladder */
static uint32_t s_present_buf[MAX_SCREEN_WIDTH * MAX_SCREEN_HEIGHT]; /* present copy */
static ColorLut s_color_lut;
static int      s_color_lut_on = 0;   /* 0 = raw passthrough (default) */

static void color_lut_setup(void)
{
    ScreenKind k = SCREEN_RAW;
    const char *env = getenv("GENESIS_SCREEN");
    if (env && env[0] && screen_kind_from_name(env, &k) && k != SCREEN_RAW) {
        color_lut_build(&s_color_lut, k, -1.0);
        s_color_lut_on = 1;
        fprintf(stderr, "[VIDEO-SCREEN] present-time color model '%s' ENABLED "
                "(present-only; raw VDP output unchanged + still the oracle)\n",
                env);
    } else {
        color_lut_build(&s_color_lut, SCREEN_RAW, -1.0);  /* passthrough */
        s_color_lut_on = 0;
    }
}

/* ── Widescreen (16:9) opt-in ────────────────────────────────────────────
 * User toggle via GENESIS_WIDESCREEN={1,on,yes,true} (default off) and
 * GENESIS_WIDESCREEN_COLUMNS=<cells/side> (default 8). The toggle only
 * ARMS widescreen; whether a given frame actually renders wide is decided
 * per frame in widescreen_update_for_frame() — it requires the game to be
 * ws_capable (per-game [widescreen] config) AND in an eligible game mode,
 * otherwise the authentic 4:3 view is pillarboxed inside the wider buffer.
 * With the toggle off, ws_extra stays 0 and output is byte-identical 4:3. */
static int s_ws_user_on    = 0;   /* user requested widescreen */
static int s_ws_user_cells = 8;   /* requested extra 8px cells per side */
static int s_ws_bgdiag_on  = 0;   /* GENESIS_WS_BGDIAG: Plane B parallax diagnostic */
static int s_ws_bar_black  = 1;   /* pillarbox bars: black (default); GENESIS_WS_BARS=backdrop for seamless */

static void widescreen_setup(void)
{
    /* Seed from persisted settings (launcher / settings.ini). The env vars
     * below still override, so dev/CI can force widescreen without a config. */
    if (g_app_config.widescreen) s_ws_user_on = 1;
    if (g_app_config.widescreen_cells > 0) s_ws_user_cells = g_app_config.widescreen_cells;

    const char *env = getenv("GENESIS_WIDESCREEN");
    if (env && (env[0] == '1' || env[0] == 'y' || env[0] == 'Y' ||
                env[0] == 't' || env[0] == 'T' ||
                ((env[0] == 'o' || env[0] == 'O') &&
                 (env[1] == 'n' || env[1] == 'N'))))
        s_ws_user_on = 1;
    const char *cols = getenv("GENESIS_WIDESCREEN_COLUMNS");
    if (cols && cols[0]) { int c = atoi(cols); if (c > 0) s_ws_user_cells = c; }
    const char *bg = getenv("GENESIS_WS_BGDIAG");
    s_ws_bgdiag_on = (bg && bg[0] && bg[0] != '0');
    if (s_ws_bgdiag_on)
        fprintf(stderr, "[VIDEO-WIDESCREEN] Plane B diagnostic ON (constant hscroll + wrap marker)\n");
    /* Pillarbox bar colour for the always-16:9 window: black (default) or the
     * per-scanline backdrop (seamless). Only matters on non-gameplay
     * (pillarboxed) frames. */
    const char *bars = getenv("GENESIS_WS_BARS");
    if (bars && bars[0]) {
        if (strcmp(bars, "black") == 0)         s_ws_bar_black = 1;
        else if (strcmp(bars, "backdrop") == 0) s_ws_bar_black = 0;
        else fprintf(stderr, "[VIDEO-WIDESCREEN] unknown GENESIS_WS_BARS=%s "
                     "(use black|backdrop); using black\n", bars);
    }
    if (s_ws_user_on)
        fprintf(stderr, "[VIDEO-WIDESCREEN] requested ON (%d cells/side, %s bars); "
                "16:9 window always (gameplay widened, other screens pillarboxed); "
                "content widening active only for widescreen-capable games in "
                "gameplay modes\n",
                s_ws_user_cells, s_ws_bar_black ? "black" : "backdrop");
}

/* Widescreen window/output geometry (valid after widescreen_setup()). */
#define WS_ASPECT_W 16
#define WS_ASPECT_H 9
/* True while the 16:9 presentation is active: user asked for it AND the game
 * supports widescreen content. */
static int ws_armed(void) { return s_ws_user_on && g_game_layout.ws_capable; }
/* Width (px) of the fixed widescreen output canvas the VDP emits every frame. */
static int ws_canvas_w(void) { return 320 + 2 * s_ws_user_cells * 8; }
/* Height (px) that makes that canvas width a true 16:9 frame. The canvas is
 * physically wider (2:1 at 8 cells), so the present path scales the authentic
 * 224-line image into this height to fill the 16:9 window edge-to-edge —
 * gameplay full-bleed, menus pillarboxed inside it (see gvdp pillarbox). */
static int ws_canvas_h(void) { return ws_canvas_w() * WS_ASPECT_H / WS_ASPECT_W; }

/* Expanded palette: 192 entries (64 CRAM × 3 brightness levels).
 * colour_updated_cb converts each entry from Genesis format to ARGB8888. */
static uint32_t s_cram[192];

static int s_screen_width  = 320;
static int s_screen_height = 224;

typedef enum InterlaceDisplayMode {
    INTERLACE_DISPLAY_TV,
    INTERLACE_DISPLAY_RAW,
} InterlaceDisplayMode;

static InterlaceDisplayMode s_interlace_display_mode = INTERLACE_DISPLAY_TV;

static void set_interlace_display_mode(const char *value)
{
    if (!value || !*value)
        return;

    if (strcmp(value, "tv") == 0 || strcmp(value, "original") == 0 ||
        strcmp(value, "squash") == 0) {
        s_interlace_display_mode = INTERLACE_DISPLAY_TV;
    } else if (strcmp(value, "raw") == 0 || strcmp(value, "full") == 0 ||
               strcmp(value, "expanded") == 0) {
        s_interlace_display_mode = INTERLACE_DISPLAY_RAW;
    } else {
        fprintf(stderr,
                "[display] unknown interlace_display=%s (use tv|raw); using tv\n",
                value);
        s_interlace_display_mode = INTERLACE_DISPLAY_TV;
    }
}

static int display_logical_height(void)
{
    if (s_screen_height > 240 && s_interlace_display_mode == INTERLACE_DISPLAY_TV)
        return (s_screen_height + 1) / 2;
    return s_screen_height;
}

static void update_render_logical_size(SDL_Renderer *renderer)
{
    /* Widescreen: present on a true 16:9 logical canvas (canvas_w × 16:9 height)
     * so the content fills the 16:9 window with no SDL letterbox. The authentic
     * 224-line frame scales into the 16:9 height; gameplay is full-bleed and
     * non-gameplay screens carry their pillarbox bars within the canvas. */
    if (ws_armed()) {
        SDL_RenderSetLogicalSize(renderer, ws_canvas_w(), ws_canvas_h());
        return;
    }
    SDL_RenderSetLogicalSize(renderer, s_screen_width, display_logical_height());
}

/* Convert a Genesis CRAM value to ARGB8888.
 *
 * Genesis CRAM format (9 significant bits):
 *   bits  3:1  = Red   (0-7)
 *   bits  7:5  = Green (0-7)
 *   bits 11:9  = Blue  (0-7)
 * Expand 3-bit components to 8-bit via the authentic nonlinear Genesis DAC
 * ladder (genesis_dac.h), matching real hardware / the BlastEm oracle. */
static uint32_t md_colour_to_argb(cc_u16f colour)
{
    /* Genesis CRAM: ----BBB-GGG-RRR- (bits 1-3=R, 5-7=G, 9-11=B) */
    return genesis_dac_cram_to_argb((uint16_t)colour, GENESIS_DAC_NORMAL);
}

#if PERMISSIVE_VDP
#include "vdp_integration.h"   /* clean-room VDP shadow seam (toggle build) */
#endif

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

#if PERMISSIVE_VDP
    /* Shadow VDP: substitute our clean-room renderer's output for this line. */
    {
        int w = gvdp_render_substitute((int)scanline, row, MAX_SCREEN_WIDTH);
        if (w > 0) s_screen_width = w;
        return;
    }
#endif

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

#if OWN_BACKEND
#include "genesis_machine.h"
/* Own-backend scanline sink: copy our VDP's rendered ARGB row to the framebuf. */
static void own_scanline_sink(void *u, int line, const uint32_t *argb, int width)
{
    (void)u;
    /* `width` already includes any widescreen margins (gvdp_render_scanline
     * returns w + 2*extra). Track it so the present path scales to the wider
     * image, mirroring the clownmdemu scanline_rendered_cb. */
    s_screen_width = width;
    if (line < 0 || line >= MAX_SCREEN_HEIGHT) return;
    uint32_t *row = s_framebuf + line * MAX_SCREEN_WIDTH;
    int n = width < MAX_SCREEN_WIDTH ? width : MAX_SCREEN_WIDTH;
    for (int x = 0; x < n; x++) row[x] = argb[x];
}

/* Decide the widescreen margin for the frame about to run, then (a) tell the
 * VDP how many extra columns to render and (b) write the same per-side margin
 * (in pixels) to the per-game RAM word so the recompiled 68K widens its own
 * object-cull / tile-load bounds to match. extra==0 ⇒ authentic 4:3 and a
 * vanilla game frame. Called immediately before machine_run_frame(), where the
 * game executes (scanline-interleaved) and the VDP renders. */
static void widescreen_update_for_frame(void)
{
    extern uint8_t  m68k_read8 (uint32_t);
    extern uint16_t m68k_read16(uint32_t);
    extern void     m68k_write16(uint32_t, uint16_t);
    extern int      g_ws_margin;   /* runtime widening signal (defined in glue.c) */
    int extra_px = 0;
    if (s_ws_user_on && g_game_layout.ws_capable) {
        int cells = s_ws_user_cells;
        if (g_game_layout.ws_max_extra_cells > 0 &&
            cells > g_game_layout.ws_max_extra_cells)
            cells = g_game_layout.ws_max_extra_cells;

        /* Eligible-mode gate: EXACT game-mode match (NO masking). Prefer the
         * [widescreen] eligible_modes list, else the gameplay level_modes. */
        int eligible = 1;
        if (g_game_layout.game_mode_addr) {
            const uint8_t *modes = g_game_layout.ws_eligible_mode_count
                ? g_game_layout.ws_eligible_modes : g_game_layout.level_modes;
            int n = g_game_layout.ws_eligible_mode_count
                ? g_game_layout.ws_eligible_mode_count : g_game_layout.level_mode_count;
            if (n > 0) {
                uint8_t mode = m68k_read8(g_game_layout.game_mode_addr);
                eligible = 0;
                for (int i = 0; i < n; i++)
                    if (modes[i] == mode) { eligible = 1; break; }
            }
        }
        /* Require the level to have actually started (excludes the title card /
         * act transitions that share the gameplay game-mode). */
        if (eligible && g_game_layout.ws_level_started_addr &&
            m68k_read8(g_game_layout.ws_level_started_addr) == 0)
            eligible = 0;
        /* Never widen 2-player split-screen. */
        if (eligible && g_game_layout.ws_two_player_addr &&
            m68k_read16(g_game_layout.ws_two_player_addr) != 0)
            eligible = 0;

        if (eligible) extra_px = cells * 8;
    }

    gvdp_set_ws_extra(extra_px);
    gvdp_set_bgdiag(s_ws_bgdiag_on);

    /* Arm the fixed 16:9 output canvas: while widescreen is on for a capable
     * game, EVERY frame emits the full canvas width — gameplay fills it with
     * widened content (extra_px>0), other screens center the authentic view and
     * pillarbox the sides (extra_px==0) — so the window never resizes. The width
     * matches the window the engine opened ((320 + 2*cells*8) * 2). 0 disarms
     * (authentic 4:3, byte-identical). Decoupled from extra_px on purpose. */
    int canvas_w = (s_ws_user_on && g_game_layout.ws_capable)
                       ? (320 + 2 * s_ws_user_cells * 8) : 0;
    gvdp_set_ws_canvas(canvas_w);
    gvdp_set_ws_bar_black(s_ws_bar_black);

    /* Post-patch widening layer (recompile-the-original-ROM approach): the
     * recompiler's [[widescreen_site]] injection reads g_ws_margin in the
     * generated C to widen object-cull / tile-load / ring bounds. 0 => no-op
     * (4:3). This supersedes the legacy disasm RAM-word path below. */
    g_ws_margin = extra_px;

    /* Legacy disasm-based path: write the Widescreen_extra RAM word the
     * (patched-disasm) recompiled 68K read. A ROM recompiled unmodified never
     * reads it; harmless to keep during the migration. */
    if (g_game_layout.ws_extra_ram_addr)
        m68k_write16(g_game_layout.ws_extra_ram_addr, (uint16_t)extra_px);

    /* On the frame widescreen first turns on (extra 0 -> nonzero), force one
     * full-screen tile redraw so the just-revealed side margins are filled.
     * The level's own initial fill ran earlier (during the title card / load)
     * while extra was still gated to 0, so it only covered the 4:3 area. */
    {
        static int s_prev_ws_extra = 0;
        if (extra_px > 0 && s_prev_ws_extra == 0 && g_game_layout.ws_redraw_flag_addr) {
            extern void m68k_write8(uint32_t, uint8_t);
            m68k_write8(g_game_layout.ws_redraw_flag_addr, 1);
        }
        s_prev_ws_extra = extra_px;
    }
}
#endif

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
    if (player_id > 1)
        return cc_false;    /* P1 + P2 */

    /* Dev-driven sources ((.input scripts, --script-* flags, TCP set_input)
     * MERGE with the live keyboard/gamepad instead of replacing it: a button
     * held by ANY source reads as held. Unattended runs stay deterministic
     * (nobody typing ⇒ identical stream), but a human can always grab the
     * controls mid-script — previously a script parked on WAIT_RAM16 or a
     * finished TCP probe locked the keyboard out entirely. Sources can only
     * ADD buttons, never mask a live press. P1-only (dev/regression tooling). */
    if (player_id == 0 && input_script_active()) {
        uint8_t mask = input_script_held_mask();
        switch (button_id) {
            case CLOWNMDEMU_BUTTON_UP:    if (mask & 0x01) return cc_true; break;
            case CLOWNMDEMU_BUTTON_DOWN:  if (mask & 0x02) return cc_true; break;
            case CLOWNMDEMU_BUTTON_LEFT:  if (mask & 0x04) return cc_true; break;
            case CLOWNMDEMU_BUTTON_RIGHT: if (mask & 0x08) return cc_true; break;
            case CLOWNMDEMU_BUTTON_B:     if (mask & 0x10) return cc_true; break;
            case CLOWNMDEMU_BUTTON_C:     if (mask & 0x20) return cc_true; break;
            case CLOWNMDEMU_BUTTON_A:     if (mask & 0x40) return cc_true; break;
            case CLOWNMDEMU_BUTTON_START: if (mask & 0x80) return cc_true; break;
            default: break;
        }
    }

    /* Scripted inputs override keyboard when active (P1 only) */
    if (player_id == 0 && (s_script_start_frame || s_script_right_frame)) {
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

    /* TCP debug server input (set_input command) — additive, same merge rule
     * as the script sources above. `set_input keys=off` clears it entirely.
     * Bit mapping matches Genesis: Up=0,Down=1,Left=2,Right=3,B=4,C=5,A=6,Start=7 */
    if (player_id == 0 && s_tcp_input_active) {
        switch (button_id) {
            case CLOWNMDEMU_BUTTON_UP:    if (s_tcp_input_keys & 0x01) return cc_true; break;
            case CLOWNMDEMU_BUTTON_DOWN:  if (s_tcp_input_keys & 0x02) return cc_true; break;
            case CLOWNMDEMU_BUTTON_LEFT:  if (s_tcp_input_keys & 0x04) return cc_true; break;
            case CLOWNMDEMU_BUTTON_RIGHT: if (s_tcp_input_keys & 0x08) return cc_true; break;
            case CLOWNMDEMU_BUTTON_B:     if (s_tcp_input_keys & 0x10) return cc_true; break;
            case CLOWNMDEMU_BUTTON_C:     if (s_tcp_input_keys & 0x20) return cc_true; break;
            case CLOWNMDEMU_BUTTON_A:     if (s_tcp_input_keys & 0x40) return cc_true; break;
            case CLOWNMDEMU_BUTTON_START: if (s_tcp_input_keys & 0x80) return cc_true; break;
            default: break;
        }
    }

    /* Live input via the rebindable per-player map (keyboard + that player's
     * gamepad, device-gated; see input_map.c). Both players resolve here. The
     * 8 standard buttons map 1:1 onto GenesisButton; 6-button extras (X/Y/Z/
     * Mode) are not expressible through the ClownMDEmu_Button callback and are
     * OR'd in directly by the own-backend pad push. */
    uint16_t mask = input_current_mask((int)player_id);
    GenesisButton gb;
    switch (button_id) {
        case CLOWNMDEMU_BUTTON_UP:    gb = GB_UP;    break;
        case CLOWNMDEMU_BUTTON_DOWN:  gb = GB_DOWN;  break;
        case CLOWNMDEMU_BUTTON_LEFT:  gb = GB_LEFT;  break;
        case CLOWNMDEMU_BUTTON_RIGHT: gb = GB_RIGHT; break;
        case CLOWNMDEMU_BUTTON_A:     gb = GB_A;     break;
        case CLOWNMDEMU_BUTTON_B:     gb = GB_B;     break;
        case CLOWNMDEMU_BUTTON_C:     gb = GB_C;     break;
        case CLOWNMDEMU_BUTTON_START: gb = GB_START; break;
        default:                      return cc_false;
    }
    return (mask & input_button_bit(gb)) ? cc_true : cc_false;
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

/* Tier-3 audio localization: dump the mixer's DETERMINISTIC pre-resample output
 * (before the DRC resampler + host audio sink) so a boop can be pinned to the
 * mixer vs the resampler, and to FM vs PSG, and diffed across mixer-logic
 * toggles. Env GENESIS_AUDIO_PREDRC=<prefix> => <prefix>.fm.s16 (interleaved
 * stereo) + <prefix>.psg.s16 (mono), raw int16 LE. No-op unless the env is set,
 * so it's harmless in every build. */
static void dump_predrc(const cc_s16l *fm, size_t fm_frames,
                        const cc_s16l *psg, size_t psg_frames) {
    static FILE *ff = NULL, *pf = NULL;
    static int inited = 0;
    if (!inited) {
        inited = 1;
        const char *pfx = getenv("GENESIS_AUDIO_PREDRC");
        if (pfx && *pfx) {
            char p[512];
            snprintf(p, sizeof p, "%s.fm.s16",  pfx); ff = fopen(p, "wb");
            snprintf(p, sizeof p, "%s.psg.s16", pfx); pf = fopen(p, "wb");
            fprintf(stderr, "[PREDRC] dumping pre-resample mixer output to %s.{fm,psg}.s16\n", pfx);
        }
    }
    if (ff && fm  && fm_frames)  fwrite(fm,  sizeof(cc_s16l), fm_frames * 2, ff);
    if (pf && psg && psg_frames) fwrite(psg, sizeof(cc_s16l), psg_frames,    pf);
}

#include "audio/mixer.h"          /* audio_mixer_drain */
#ifdef GENESIS_COSIM
#include "cosim.h"                 /* cosim_init / cosim_frame_checkpoint */
#endif
#include "audio/observability.h"  /* boop detector */
#include "audio/event_queue.h"    /* audio_event_queue_reset (save-state load) */
#include "audio/ym2612.h"         /* ym2612_save/load_state */
#include "audio/sn76489.h"        /* psg_save/load_state */
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

#if !OWN_BACKEND
/* clownmdemu sync/CD/save callbacks — referenced only by the (gated)
 * ClownMDEmu_Initialise callbacks table. The own backend's audio comes from
 * audio_mixer_drain and its persistence from the bus SRAM accessors. */
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
#endif /* !OWN_BACKEND */

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
 * ClownMDEmu instance (static storage; oracle/hybrid builds only)
 * ========================================================================= */

#if !OWN_BACKEND
ClownMDEmu g_clownmdemu;
#endif

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

#if OWN_BACKEND
/* Own-backend save container: versioned magic, then the glue blob (M68K regs,
 * frame count, cycle accumulators, V-int latch), WRAM, the whole machine
 * (VDP + bus incl. Z80 RAM/SRAM + the full embedded superzazu Z80 core), and
 * both audio chips. Raw-struct format, private to a build — same convention as
 * the clownmdemu-path states (which dumped g_clownmdemu raw); the magic keeps
 * the formats from being confused. GROWNS2 = the declown-headers machine
 * layout (superzazu z80 embedded in g_machine, no side ext blob); GROWNS1
 * saves are rejected with a clear message rather than misloaded. */
static const char OWN_SAVE_MAGIC[8] = "GROWNS2\0";

int runner_save_state_file(const char *path)
{
    char full_path[512];
    const char *resolved = resolve_runner_path(path, full_path, sizeof(full_path));
    FILE *sf = fopen(resolved, "wb");
    if (!sf) {
        fprintf(stderr, "[SAVE] failed to open %s\n", resolved);
        return 0;
    }

    extern uint8_t g_ram[0x010000];
    int ok = fwrite(OWN_SAVE_MAGIC, 1, sizeof(OWN_SAVE_MAGIC), sf) == sizeof(OWN_SAVE_MAGIC);
    glue_save_state(sf);
    ok = ok && fwrite(g_ram, 1, 0x10000, sf) == 0x10000;
    ok = ok && machine_save_state(sf);
    ok = ok && ym2612_save_state(sf);
    ok = ok && psg_save_state(sf);
    ok = ok && !ferror(sf);
    fclose(sf);

    if (ok)
        fprintf(stderr, "[SAVE] saved %s\n", resolved);
    else
        fprintf(stderr, "[SAVE] failed while writing %s\n", resolved);
    return ok;
}

int runner_load_state_file(const char *path)
{
    char full_path[512];
    const char *resolved = resolve_runner_path(path, full_path, sizeof(full_path));
    FILE *sf = fopen(resolved, "rb");
    if (!sf) {
        fprintf(stderr, "[LOAD] empty/missing %s\n", resolved);
        return 0;
    }

    char magic[8];
    if (fread(magic, 1, sizeof(magic), sf) != sizeof(magic) ||
        memcmp(magic, OWN_SAVE_MAGIC, sizeof(magic)) != 0) {
        fprintf(stderr, "[LOAD] %s is not an own-backend save (old format?)\n", resolved);
        fclose(sf);
        return 0;
    }

    extern uint8_t g_ram[0x010000];
    int ok = 1;
    glue_load_state(sf);
    ok = ok && fread(g_ram, 1, 0x10000, sf) == 0x10000;
    ok = ok && machine_load_state(sf);
    ok = ok && ym2612_load_state(sf);
    ok = ok && psg_load_state(sf);
    ok = ok && !ferror(sf);
    fclose(sf);

    /* Drop any chip writes queued by the frame the save interrupted; the
     * restored chips already contain their effect. */
    audio_event_queue_reset();

    if (ok) {
        /* Mode-aware resume: re-enter at the restored Game_Mode's per-frame
         * loop top (moment-in-time) when the game maps one; otherwise fall
         * back to the outer dispatcher (mode-handler re-entry / reload). */
        uint32_t resume_pc = g_game_spec.resume_main_loop_pc;
        if (g_game_spec.save_resume_pc && g_game_layout.game_mode_addr) {
            uint32_t pc = g_game_spec.save_resume_pc(
                m68k_read8(g_game_layout.game_mode_addr));
            if (pc) resume_pc = pc;
        }
        if (resume_pc)
            glue_restart_game_fiber(resume_pc);
    }

    if (ok)
        fprintf(stderr, "[LOAD] loaded %s\n", resolved);
    else
        fprintf(stderr, "[LOAD] failed/truncated %s\n", resolved);
    return ok;
}
#else /* !OWN_BACKEND — clownmdemu-path save states (unchanged) */
int runner_save_state_file(const char *path)
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

int runner_load_state_file(const char *path)
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
    if (ok) {
        /* Mode-aware resume (see the own-backend path above). */
        uint32_t resume_pc = g_game_spec.resume_main_loop_pc;
        if (g_game_spec.save_resume_pc && g_game_layout.game_mode_addr) {
            uint32_t pc = g_game_spec.save_resume_pc(
                m68k_read8(g_game_layout.game_mode_addr));
            if (pc) resume_pc = pc;
        }
        if (resume_pc)
            glue_restart_game_fiber(resume_pc);
    }
#endif

    if (ok)
        fprintf(stderr, "[LOAD] loaded %s\n", resolved);
    else
        fprintf(stderr, "[LOAD] failed/truncated %s\n", resolved);
    return ok;
}
#endif /* OWN_BACKEND */

/* =========================================================================
 * Battery-backed cartridge SRAM persistence (e.g. Sonic 3 save slots).
 *
 * ClownMDEmu holds cartridge SRAM in g_clownmdemu.state.external_ram.buffer
 * but never writes it to disk: its save_file_* callbacks drive Mega-CD
 * backup RAM only (bus-sub-m68k.c), not cartridge SRAM. So the runner owns
 * SRAM persistence — load the .srm at boot, auto-flush it shortly after the
 * game writes a save, and flush once more on exit.
 *
 * Fully game-agnostic: cartridges without battery SRAM report size==0 /
 * non_volatile==false (Sonic 1 & 2), so every entry point below no-ops for
 * them. Nothing here knows a game-specific address — the SRAM geometry comes
 * from ClownMDEmu's parse of the ROM header (SetUpExternalRAM).
 * ========================================================================= */

static char     s_sram_path[512] = "";
static int      s_sram_active    = 0;  /* battery SRAM present this run */
static uint64_t s_sram_hash      = 0;  /* last-seen content hash */
static int      s_sram_dirty     = 0;  /* content changed since last flush */
static uint32_t s_sram_dirty_at  = 0;  /* frame the pending change was seen */

/* SRAM storage source: the own backend keeps cartridge SRAM in the own bus
 * (header-parsed geometry, genesis_bus.c); the clownmdemu path keeps it in
 * g_clownmdemu.state.external_ram. The persistence layer below is otherwise
 * identical for both. */
#if OWN_BACKEND
static unsigned char *sram_buf(void)  { return gbus_sram_buffer(&g_machine.bus); }
static size_t         sram_size(void) { return (size_t)gbus_sram_size(&g_machine.bus); }
static int            sram_battery_present(void) { return sram_size() != 0; }
#else
static unsigned char *sram_buf(void)  { return g_clownmdemu.state.external_ram.buffer; }
static size_t         sram_size(void) { return (size_t)g_clownmdemu.state.external_ram.size; }
static int            sram_battery_present(void)
{
    return g_clownmdemu.state.external_ram.non_volatile &&
           g_clownmdemu.state.external_ram.size != 0;
}
#endif

static uint64_t sram_content_hash(void)
{
    const unsigned char *buf = sram_buf();
    size_t n = sram_size();
    uint64_t h = 0xCBF29CE484222325ULL;  /* FNV-1a-64, as in [FBHASH] */
    for (size_t i = 0; i < n; i++) { h ^= buf[i]; h *= 0x100000001B3ULL; }
    return h;
}

/* FNV-1a-64 over the active region of s_framebuf (the verified raw VDP output,
 * never the present-time color copy). Shared by the fixed-frame [FBHASH] path
 * and the state-anchored [MODEHASH] path. */
static uint64_t framebuf_active_hash(void)
{
    uint64_t h = 0xCBF29CE484222325ULL;
    const uint8_t *p = (const uint8_t *)s_framebuf;
    int row_bytes = s_screen_width * (int)sizeof(uint32_t);
    int stride    = MAX_SCREEN_WIDTH * (int)sizeof(uint32_t);
    for (int y = 0; y < s_screen_height; y++) {
        const uint8_t *row = p + (size_t)y * stride;
        for (int x = 0; x < row_bytes; x++) { h ^= row[x]; h *= 0x100000001B3ULL; }
    }
    return h;
}

static void runner_sram_flush(void)
{
    if (!s_sram_active) return;
    FILE *f = fopen(s_sram_path, "wb");
    if (!f) { fprintf(stderr, "[SRAM] flush failed to open %s\n", s_sram_path); return; }
    size_t n = sram_size();
    size_t wrote = fwrite(sram_buf(), 1, n, f);
    fclose(f);
    s_sram_dirty = 0;
    fprintf(stderr, "[SRAM] flushed %zu bytes to %s\n", wrote, s_sram_path);
}

/* Resolve <rom-basename>.srm next to the exe, then load it (if present) into
 * the cartridge SRAM buffer. Called once, right after ClownMDEmu_HardReset
 * (which runs SetUpExternalRAM and so has populated size / non_volatile). */
static void runner_sram_init_and_load(const char *rom_path)
{
    if (!sram_battery_present())
        return;  /* no battery save on this cartridge */
    s_sram_active = 1;

    const char *base = rom_path;
    const char *s1 = strrchr(rom_path, '/');
    const char *s2 = strrchr(rom_path, '\\');
    const char *sep = (s2 > s1) ? s2 : s1;
    if (sep) base = sep + 1;

    char name[256];
    snprintf(name, sizeof(name), "%s", base);
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
    strncat(name, ".srm", sizeof(name) - strlen(name) - 1);
    snprintf(s_sram_path, sizeof(s_sram_path), "%s", exe_relative(name));

    FILE *f = fopen(s_sram_path, "rb");
    if (f) {
        size_t n = sram_size();
        /* Validate the file SIZE before loading. A .srm whose length doesn't
         * match this cart's SRAM size is truncated, oversized, or from another
         * cart; splicing it in leaves partial/foreign bytes in SRAM and the game
         * can boot into a corrupt save state. Reject -> start fresh (the buffer
         * keeps its power-on contents). Game-agnostic: only the size is checked
         * here — per-save semantic validity (slot checksums, etc.) is the game's
         * own responsibility, so a correctly-sized but game-"bad" save still
         * loads (the game must handle that). */
        fseek(f, 0, SEEK_END);
        long fsz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (fsz != (long)n) {
            fprintf(stderr, "[SRAM] IGNORING %s — file %ld B != cart SRAM %zu B "
                    "(truncated/oversized/foreign); starting fresh\n",
                    s_sram_path, fsz, n);
        } else {
            size_t got = fread(sram_buf(), 1, n, f);
            fprintf(stderr, "[SRAM] loaded %zu/%zu bytes from %s\n", got, n, s_sram_path);
        }
        fclose(f);
    } else {
        fprintf(stderr, "[SRAM] no save file yet (%s) — starting fresh\n", s_sram_path);
    }
    s_sram_hash  = sram_content_hash();
    s_sram_dirty = 0;
}

/* Per-frame: detect a change to SRAM and flush a short while after it settles.
 * The delay coalesces the game's multi-frame save burst into one write and
 * still persists well before a manual taskkill (the standard relaunch path,
 * PRINCIPLES #24). Hashing the buffer is cheap and has no per-frame I/O. */
static void runner_sram_autosave_tick(uint32_t frame_num)
{
    if (!s_sram_active) return;
    uint64_t h = sram_content_hash();
    if (h != s_sram_hash) {
        s_sram_hash     = h;
        s_sram_dirty    = 1;
        s_sram_dirty_at = frame_num;
    }
    if (s_sram_dirty && (frame_num - s_sram_dirty_at) >= 30)
        runner_sram_flush();
}

static uint8_t runner_ram_byte(uint32_t addr)
{
    uint16_t off = (uint16_t)(addr & 0xFFFFu);
#if OWN_BACKEND
    extern uint8_t g_ram[0x010000];   /* own bus's authoritative WRAM */
    return g_ram[off];
#else
    uint16_t word = g_clownmdemu.state.m68k.ram[off / 2u];
    return (off & 1u) ? (uint8_t)(word & 0xFFu) : (uint8_t)(word >> 8);
#endif
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

int runner_write_screenshot_file(const char *path)
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
#if OWN_BACKEND
    extern uint8_t g_ram[0x10000];
    #define EMU_BYTE_D(a) (g_ram[(a) & 0xFFFF])
#else
    #define EMU_BYTE_D(a) ((uint8_t)(g_clownmdemu.state.m68k.ram[((a) & 0xFFFF) / 2] >> \
                   (((a) & 1) ? 0 : 8)))
#endif
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
#if OWN_BACKEND
                fwrite(g_ram, 1, 0x10000, df);
#else
                fwrite(g_clownmdemu.state.m68k.ram, 2, 0x8000, df);
#endif
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

    /* Read work RAM as big-endian. The two backends keep WRAM in different
     * buffers: clownmdemu in a word array (state.m68k.ram), the own backend
     * in our byte array (g_ram). Reading the wrong one yields a stale/blank
     * log — which is why the own-backend framelog must source g_ram so the
     * two logs are directly diffable (clean-room A/B against the oracle). */
#if OWN_BACKEND
    extern uint8_t g_ram[0x010000];
    #define EMU_BYTE(addr) ((uint8_t)g_ram[(addr) & 0xFFFF])
    #define EMU_WORD(addr) ((uint16_t)((g_ram[(addr) & 0xFFFF] << 8) | \
                                        g_ram[((addr) + 1) & 0xFFFF]))
    #define EMU_LONG(addr) (((uint32_t)EMU_WORD(addr) << 16) | EMU_WORD((addr)+2))
#else
    #define EMU_BYTE(addr) \
        ((uint8_t)(g_clownmdemu.state.m68k.ram[((addr) & 0xFFFF) / 2] >> \
                   (((addr) & 1) ? 0 : 8)))
    #define EMU_WORD(addr) \
        ((uint16_t)(g_clownmdemu.state.m68k.ram[((addr) & 0xFFFF) / 2]))
    #define EMU_LONG(addr) \
        (((uint32_t)EMU_WORD(addr) << 16) | EMU_WORD((addr)+2))
#endif

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

    /* Game-agnostic divergence signal: FNV-1a-64 over the full 64 KB of work
     * RAM, sourced from the backend's authoritative WRAM (the EMU_* macros pick
     * g_ram vs clownmdemu ram per build), so a native vs oracle framelog diff
     * pinpoints the first frame at which 68K memory state diverges — without
     * relying on any per-game address. The Sonic-shaped fields below stay for
     * S1 telemetry; on other games read `wh` (and DUMP_RAM at the divergent
     * frame to localise the bytes). */
    uint64_t wh = 0xCBF29CE484222325ULL;
    for (uint32_t a = 0; a < 0x10000u; a++) {
        wh ^= (uint64_t)EMU_BYTE(a);
        wh *= 0x100000001B3ULL;
    }

    fprintf(s_framelog_file,
            "F%03u wh=%016llX mode=%02X vbl=%02X cnt=%04X scrl=%04X plc=%04X "
            "fcnt=%08X obj0=%02X/%02X xpos=%04X ypos=%04X xvel=%04X yvel=%04X inrt=%04X "
            "rtn=%02X log=%02X/%02X phys=%02X/%02X st=%02X lk=%02X "
            "pc=%06X tc=%02X/%02X x=%04X tgt=%04X dur=%02X bg=%02X/%02X left=%02X/%02X bottom=%02X/%02X tf=%02X\n",
            frame,
            (unsigned long long)wh,
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
    input_map_init_defaults();   /* today's mapping; app_config_load may override */

    /* --- Parse arguments --- */
    const char *rom_path = NULL;
    const char *framelog_path = NULL;
    uint32_t max_frames  = 0;   /* 0 = unlimited */
    int start_turbo      = 0;   /* --turbo: skip frame delay + audio */
    uint32_t snd_dump_frame = 0; /* [SND-TRACE] headless auto-dump of both rings at this frame (0=off, use F12) */
    uint32_t snd_dump_vint  = 0; /* [CHIP-TRACE] dump chip_ring when vint_runcount hits N (0=off) */
    int      snd_dump_done  = 0;
    /* Debug-server port: precedence is --port > debug.ini "port" > compile-time
     * DEFAULT_DEBUG_PORT (4378 native, 4379 oracle). 0 here means "unset; use
     * the compile-time default unless debug.ini overrides it". */
#ifndef DEFAULT_DEBUG_PORT
#  define DEFAULT_DEBUG_PORT 4378
#endif
    int debug_port_cli = 0;

    /* Launcher gate: --launcher forces the GUI on (even if skip_launcher is
     * set), --no-launcher / GENESIS_NO_LAUNCHER forces it off (headless). */
    int force_launcher = 0;
    int no_launcher    = 0;

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

    /* --exec-coverage-out PATH — oracle build only. At exit, dump the
     * always-on executed-PC coverage bitmaps (ROM + WRAM) to a binary
     * file. This is the discovery runtime oracle's guaranteed-code
     * positive set; tools/rka decode it into address lists. No-op on
     * native (the interpreter that feeds the coverage isn't running). */
    const char *exec_cov_out = NULL;

    /* Headless smoke / framebuffer-hash assertion mode. When --hash-frames
     * is set, emit a [FBHASH] line every N wall frames with an FNV-1a-64
     * fingerprint of the framebuffer's active region. Used by CI / golden
     * comparison to detect regressions without a human watching the screen.
     * Pairs naturally with --max-frames; together they make the runner a
     * one-shot regression test. */
    uint32_t hash_frames = 0;
    int      hash_on_mode = 0;   /* --hash-on-mode: hash on Game_Mode transitions (pacing-invariant) */

    /* --input-script PATH — load a .input scripting file (button
     * timeline + RAM assertions + EXIT). See runner/input_script.h
     * for the format spec. Combined with --max-frames + --hash-frames,
     * makes the runner a one-shot regression-test driver. */
    const char *input_script_path = NULL;

    /* Pacing mode override: --pacing fiber|accurate or --pacing=...
     * NULL = not set via CLI (debug.ini may still override; otherwise
     * compiled default in glue.c wins). */
    const char *pacing_cli = NULL;
    const char *interlace_display_cli = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--max-frames") == 0 && i + 1 < argc) {
            max_frames = (uint32_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "--framelog") == 0 && i + 1 < argc) {
            framelog_path = argv[++i];
        } else if (strcmp(argv[i], "--turbo") == 0) {
            start_turbo = 1;
        } else if (strcmp(argv[i], "--launcher") == 0) {
            force_launcher = 1;
        } else if (strcmp(argv[i], "--no-launcher") == 0) {
            no_launcher = 1;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            debug_port_cli = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--target-fps") == 0 && i + 1 < argc) {
            target_fps_cli = atof(argv[++i]);
        } else if (strcmp(argv[i], "--snd-dump-frame") == 0 && i + 1 < argc) {
            snd_dump_frame = (uint32_t)atol(argv[++i]);   /* [SND-TRACE] */
        } else if (strcmp(argv[i], "--snd-dump-vint") == 0 && i + 1 < argc) {
            snd_dump_vint = (uint32_t)atol(argv[++i]);    /* [CHIP-TRACE] dump when
                vint_runcount ($FFFE0C) hits N — the cross-backend sync key, so
                native & oracle dump at the SAME logical sound-driver state
                (frame numbers misalign across backends). */
        } else if (strcmp(argv[i], "--psg-vol-div") == 0 && i + 1 < argc) {
            extern void audio_set_psg_vol_div(int);       /* [SND-TRACE] PSG balance knob */
            int d = atoi(argv[++i]); audio_set_psg_vol_div(d);
            fprintf(stderr, "[audio] PSG vol div = %d\n", d);
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
        } else if (strcmp(argv[i], "--interlace-display") == 0 && i + 1 < argc) {
            interlace_display_cli = argv[++i];
        } else if (strncmp(argv[i], "--interlace-display=", 20) == 0) {
            interlace_display_cli = argv[i] + 20;
        } else if (strcmp(argv[i], "--hash-frames") == 0 && i + 1 < argc) {
            hash_frames = (uint32_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "--hash-on-mode") == 0) {
            hash_on_mode = 1;
        } else if (strcmp(argv[i], "--input-script") == 0 && i + 1 < argc) {
            input_script_path = argv[++i];
        } else if (strcmp(argv[i], "--exec-coverage-out") == 0 && i + 1 < argc) {
            exec_cov_out = argv[++i];
        } else if (strncmp(argv[i], "--audio-backend=", 16) == 0) {
            const char *v = argv[i] + 16;
            if      (strcmp(v, "ours")       == 0) s_audio_backend = AUDIO_BACKEND_OURS;
#if !OWN_BACKEND
            /* The clownmdemu audio backend is a dev/oracle-only A/B path; it is
             * absent from the own-backend (release) build, so neither it nor its
             * name is compiled in here. */
            else if (strcmp(v, "clownmdemu") == 0) s_audio_backend = AUDIO_BACKEND_CLOWNMDEMU;
            else fprintf(stderr, "warning: unknown --audio-backend=%s (use ours|clownmdemu)\n", v);
#else
            else fprintf(stderr, "warning: unknown --audio-backend=%s (only 'ours' on this build)\n", v);
#endif
            fprintf(stderr, "[audio] backend=%s\n",
#if OWN_BACKEND
                    "ours");   /* only backend in the release build */
#else
                    s_audio_backend == AUDIO_BACKEND_OURS ? "ours" : "clownmdemu");
#endif
        } else if (argv[i][0] != '-') {
            rom_path = argv[i];
        }
    }

    /* ---- Settings + pre-boot launcher ------------------------------------
     * settings.ini (display/audio/launcher + controller bindings) and rom.cfg
     * (last ROM) live next to the exe. Loaded unconditionally so the persisted
     * knobs apply even when the launcher is skipped. */
    char settings_ini[600], rom_cfg_path[600];
    snprintf(settings_ini, sizeof settings_ini, "%s", exe_relative("settings.ini"));
    /* rom.cfg is per-game (keyed by short_name) so multiple game exes sharing one
     * output directory — the Sonic3AndKnucklesRecomp repo's 3 modes — each
     * remember their own last ROM. settings.ini stays shared (one set of
     * controls/video for the repo). */
    {
        char rc[80];
        snprintf(rc, sizeof rc, "rom-%s.cfg",
                 g_game_spec.short_name ? g_game_spec.short_name : "game");
        snprintf(rom_cfg_path, sizeof rom_cfg_path, "%s", exe_relative(rc));
    }
    app_config_defaults();
    app_config_load(settings_ini);   /* also seeds g_input_map controller bindings */

    {
        /* The GUI launcher only runs for a plain double-click: no positional
         * ROM and no automation/headless flag (preserves every scripted dev /
         * CI invocation in CLAUDE.md). */
        int positional_rom = (rom_path != NULL);
        int automation = (max_frames || hash_frames || hash_on_mode || input_script_path ||
                          snd_dump_frame || snd_dump_vint || target_fps_cli != 0.0 ||
                          mem_write_log_spec || wav_path || s_script_start_frame ||
                          s_script_right_frame || debug_port_cli || start_turbo ||
                          no_launcher || getenv("GENESIS_NO_LAUNCHER") != NULL);
#if RECOMP_LAUNCHER || GENESIS_LAUNCHER
        if (!positional_rom && !automation) {
            static char cached_rom[600];
            int booted_cached = 0;
            if (g_app_config.skip_launcher && !force_launcher &&
                rom_cfg_read(rom_cfg_path, cached_rom, sizeof cached_rom)) {
                FILE *probe = fopen(cached_rom, "rb");
                if (probe) { fclose(probe); rom_path = cached_rom; booted_cached = 1; }
            }
            if (!booted_cached) {
                char initial_rom[600] = "";
                rom_cfg_read(rom_cfg_path, initial_rom, sizeof initial_rom);
                char ltitle[200];
                snprintf(ltitle, sizeof ltitle, "%s — Sega Genesis Launcher",
                         g_game_spec.display_name ? g_game_spec.display_name : "Genesis");
                static char picked[600] = "";
                int lr = 2;   /* default UNAVAILABLE -> legacy picker below */

#if RECOMP_LAUNCHER
                /* ---- shared recomp-ui (Dear ImGui) launcher ------------------
                 * The new cross-console launcher (recomp_launcher.h, linked via
                 * the game target + recomp_ui.cmake). It persists key/pad rebinds
                 * straight into the engine's own settings.ini [input.pN] through
                 * its genesis bridge; the display/audio/device knobs round-trip
                 * through the settings struct below. */
                {
                    RecompLauncherCSettings ls;
                    memset(&ls, 0, sizeof ls);
                    ls.output_method    = 2;   /* OpenGL (matches the launcher) */
                    ls.window_scale     = g_app_config.window_scale;
                    ls.fullscreen       = g_app_config.fullscreen;
                    ls.linear_filter    = g_app_config.linear_filter;
                    ls.widescreen       = g_app_config.widescreen;
                    ls.widescreen_cells = g_app_config.widescreen_cells;
                    ls.enable_audio     = 1;
                    ls.volume           = g_app_config.volume;
                    ls.skip_launcher    = g_app_config.skip_launcher;
                    for (int p = 0; p < 2; p++) {
                        int dev = g_input_map.p[p].device;
                        ls.player_src[p] = (dev == INPUT_DEV_NONE)    ? 0
                                         : (dev & INPUT_DEV_KEYBOARD) ? 1 : 2;
                        ls.pad_mode[p]   = (g_input_map.p[p].pad_type == PAD_6BUTTON) ? 1 : 0;
                        ls.deadzone[p]   = g_input_map.p[p].deadzone_pct;
                    }

                    RecompLauncherCGameInfo gi;
                    memset(&gi, 0, sizeof gi);
                    gi.name                 = g_game_spec.display_name;
                    gi.region               = "NTSC-U (USA)";
                    gi.expected_crc         = g_game_spec.expected_rom_crc32;
                    gi.has_expected_crc     = g_game_spec.expected_rom_crc32 != 0;
                    gi.widescreen_supported = g_game_layout.ws_capable;
                    gi.num_players          = 2;   /* both controller ports configurable */
                    gi.platform             = "SEGA GENESIS";  /* infers the genesis profile */
                    gi.theme                = "genesis";
                    gi.rom_noun             = "ROM";
                    gi.pad_mode_supported   = 1;   /* 3-Button / 6-Button selector */
                    gi.pad_mode_selectable  = 1;
                    /* Point the launcher's bind bridge at the engine's OWN
                     * settings.ini (not a separate keybinds.ini) so key/pad
                     * rebinds land where app_config_load() reads them. */
                    gi.keybinds_path        = settings_ini;
                    /* SRAM panel: the engine names its battery file
                     * <rom-basename>.srm next to the exe (runner_sram_init_and_load,
                     * ~main.c:1024). Mirror that from the current ROM so the
                     * launcher's Import/Clear act on the real save file. */
                    static char sram_path[600];
                    if (g_game_spec.sram_start != 0 && initial_rom[0]) {
                        const char *b = initial_rom;
                        const char *s1 = strrchr(initial_rom, '/');
                        const char *s2 = strrchr(initial_rom, '\\');
                        const char *sep = (s2 > s1) ? s2 : s1;
                        if (sep) b = sep + 1;
                        char nm[256]; snprintf(nm, sizeof nm, "%s", b);
                        char *dot = strrchr(nm, '.'); if (dot) *dot = '\0';
                        strncat(nm, ".srm", sizeof(nm) - strlen(nm) - 1);
                        snprintf(sram_path, sizeof sram_path, "%s", exe_relative(nm));
                        gi.sram_path = sram_path;
                    }
                    /* Per-mode box art: a repo that builds several game modes
                     * into ONE exe dir (Sonic3AndKnucklesRecomp) sets g_game_spec.boxart
                     * so each mode shows its own; NULL keeps the shared boxart.tga. */
                    static char boxart_rel[128];
                    if (g_game_spec.boxart && g_game_spec.boxart[0]) {
                        snprintf(boxart_rel, sizeof boxart_rel, "assets/img/%s", g_game_spec.boxart);
                        gi.boxart_path = boxart_rel;
                    }

                    char assets_dir[600];
                    snprintf(assets_dir, sizeof assets_dir, "%sassets", s_exe_dir);
                    lr = recomp_launcher_run_window(ltitle, &ls, &gi, assets_dir,
                                                    initial_rom, picked, sizeof picked);
                    if (lr == 0) {                  /* PLAY */
                        if (picked[0]) rom_path = picked;
                        /* Ordering matters. Re-load settings.ini FIRST so the
                         * key/pad rebinds the launcher just wrote reach
                         * g_input_map; THEN copy this session's display/audio +
                         * device/pad_type/deadzone over the top; THEN persist all
                         * of it together (app_config_save writes g_input_map too). */
                        app_config_load(settings_ini);
                        g_app_config.window_scale     = ls.window_scale;
                        g_app_config.fullscreen       = ls.fullscreen;
                        g_app_config.linear_filter    = ls.linear_filter;
                        g_app_config.widescreen       = ls.widescreen;
                        g_app_config.widescreen_cells = ls.widescreen_cells;
                        g_app_config.volume           = ls.volume;
                        g_app_config.skip_launcher    = ls.skip_launcher;
                        for (int p = 0; p < 2; p++) {
                            int src = ls.player_src[p];
                            g_input_map.p[p].device       = (src == 1) ? INPUT_DEV_KEYBOARD
                                                          : (src == 2) ? INPUT_DEV_GAMEPAD
                                                          : INPUT_DEV_NONE;
                            g_input_map.p[p].pad_type     = (ls.pad_mode[p] == 1) ? PAD_6BUTTON : PAD_3BUTTON;
                            g_input_map.p[p].deadzone_pct = ls.deadzone[p];
                        }
                        app_config_save(settings_ini);
                        if (rom_path) rom_cfg_write(rom_cfg_path, rom_path);
                    }
                }
#elif GENESIS_LAUNCHER
                /* ---- legacy RmlUi launcher (fallback during transition) ------
                 * Unchanged from before recomp-ui: edits g_input_map directly and
                 * round-trips only display/audio/skip through the settings struct. */
                {
                    GenesisLauncherCSettings ls;
                    ls.window_scale     = g_app_config.window_scale;
                    ls.fullscreen       = g_app_config.fullscreen;
                    ls.linear_filter    = g_app_config.linear_filter;
                    ls.widescreen       = g_app_config.widescreen;
                    ls.widescreen_cells = g_app_config.widescreen_cells;
                    ls.volume           = g_app_config.volume;
                    ls.skip_launcher    = g_app_config.skip_launcher;

                    GenesisLauncherCGameInfo gi;
                    memset(&gi, 0, sizeof gi);
                    gi.name                 = g_game_spec.display_name;
                    gi.short_name           = g_game_spec.short_name;
                    gi.region               = "NTSC-U (USA)";
                    gi.expected_crc         = g_game_spec.expected_rom_crc32;
                    gi.has_expected_crc     = g_game_spec.expected_rom_crc32 != 0;
                    gi.expected_size        = g_game_spec.expected_rom_size;
                    gi.uses_sram            = g_game_spec.sram_start != 0;
                    gi.widescreen_supported = g_game_layout.ws_capable;

                    char assets_dir[600];
                    snprintf(assets_dir, sizeof assets_dir, "%slauncher", s_exe_dir);
                    lr = genesis_launcher_run_window(ltitle, &ls, &gi, assets_dir,
                                                     initial_rom, picked, sizeof picked);
                    if (lr == 0) {                  /* PLAY */
                        if (picked[0]) rom_path = picked;
                        g_app_config.window_scale     = ls.window_scale;
                        g_app_config.fullscreen       = ls.fullscreen;
                        g_app_config.linear_filter    = ls.linear_filter;
                        g_app_config.widescreen       = ls.widescreen;
                        g_app_config.widescreen_cells = ls.widescreen_cells;
                        g_app_config.volume           = ls.volume;
                        g_app_config.skip_launcher    = ls.skip_launcher;
                        app_config_save(settings_ini);    /* persists g_input_map too */
                        if (rom_path) rom_cfg_write(rom_cfg_path, rom_path);
                    }
                }
#endif
                if (lr == 1) return 0;   /* user closed the launcher */
                /* lr == 2 (unavailable) -> fall through to the legacy picker. */
            }
        }
#else
        (void)positional_rom; (void)automation; (void)force_launcher;
#endif
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
#else
        /* Native graphical chooser, preference order; each gated on
         * `command -v` so an absent tool falls through. No new link deps. */
        static char picked_path[1024] = {0};
        static const char *const pickers[] = {
            "command -v zenity >/dev/null 2>&1 && "
            "zenity --file-selection --title='Select Genesis/Mega Drive ROM' "
            "--file-filter='Genesis ROMs | *.bin *.md *.gen *.smd *.BIN *.MD *.GEN *.SMD' "
            "--file-filter='All files | *' 2>/dev/null",
            "command -v kdialog >/dev/null 2>&1 && "
            "kdialog --getopenfilename \"${HOME:-/}\" "
            "'*.bin *.md *.gen *.smd|Genesis/Mega Drive ROMs' 2>/dev/null",
            "command -v qarma >/dev/null 2>&1 && "
            "qarma --file-selection --title='Select Genesis/Mega Drive ROM' 2>/dev/null",
            "command -v osascript >/dev/null 2>&1 && "
            "osascript -e 'POSIX path of (choose file with prompt \"Select Genesis ROM\")' "
            "2>/dev/null",
        };
        for (size_t i = 0; i < sizeof(pickers) / sizeof(pickers[0]); i++)
            if (run_picker_cmd(pickers[i], picked_path, sizeof(picked_path))) {
                rom_path = picked_path;
                break;
            }
#endif
        if (!rom_path) {
            fprintf(stderr, "Usage: %s <sonic.bin>  "
                    "(or install zenity/kdialog for a file picker)\n", argv[0]);
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

    /* --- SDL init ---
     * GAMECONTROLLER enables XInput (Xbox pads) and HID gamepads via SDL's
     * controller database. Init is non-fatal if no controller is attached. */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS
                 | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    gamepad_init();

    /* Build-type-aware window title so the RECOMPILED (native) window is
     * visually distinguishable from the INTERPRETER (oracle) window when both
     * run side by side — otherwise both show the bare game name and can't be
     * told apart. */
#if defined(SONIC_ORACLE_BUILD)
    const char *build_tag = "INTERPRETER - oracle";
#elif ENABLE_RECOMPILED_CODE
    const char *build_tag = "RECOMPILED - native";
#elif HYBRID_RECOMPILED_CODE
    const char *build_tag = "HYBRID";
#else
    const char *build_tag = "interpreter";
#endif
    char window_title[256];
    {
        const char *base = g_game_spec.display_name ? g_game_spec.display_name
                                                    : "Sonic the Hedgehog";
        if (debug_port_cli)
            snprintf(window_title, sizeof window_title, "%s  [ %s ]  port %d",
                     base, build_tag, debug_port_cli);
        else
            snprintf(window_title, sizeof window_title, "%s  [ %s ]",
                     base, build_tag);
    }

    /* Resolve the widescreen request (env: GENESIS_WIDESCREEN / _COLUMNS /
     * _BARS) BEFORE sizing the window — ws_armed()/ws_canvas_w() below depend
     * on it. Only reads env + the compile-time g_game_layout, so it is safe to
     * run this early. */
    widescreen_setup();

    /* 2× scale: 320×224 → 640×448 in authentic 4:3. When widescreen is armed
     * for a capable game, open a TRUE 16:9 window (canvas_w × 2 wide, 16:9
     * tall) so the launcher window is shaped 16:9 from the start. The 16:9
     * logical size (see update_render_logical_size) makes the content fill it —
     * gameplay full-bleed, menus pillarboxed inside. ws_armed() requires
     * widescreen_setup() to have run, so that call is hoisted above this. */
    int win_scale = g_app_config.window_scale;
    if (win_scale < 1) win_scale = 1; else if (win_scale > 8) win_scale = 8;
    int win_w = 320 * win_scale, win_h = 224 * win_scale;   /* scale 2 = 640x448 (4:3) */
    if (ws_armed()) {
        win_w = ws_canvas_w() * win_scale;
        win_h = win_w * WS_ASPECT_H / WS_ASPECT_W;   /* true 16:9 */
    }
    Uint32 win_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (g_app_config.fullscreen) win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_Window *window = SDL_CreateWindow(
        window_title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
        win_flags);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    fprintf(stderr, "[VIDEO] window %dx%d (%s)\n", win_w, win_h,
            ws_armed() ? "16:9 widescreen" : "4:3");

    /* Texture scaling filter (settings.ini / launcher): nearest vs bilinear. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, g_app_config.linear_filter ? "1" : "0");

    /* PRESENTVSYNC aligns each present to the display's vblank, which kills
     * the scroll tearing visible without it (notably on macOS/Metal). The
     * manual frame pacer still bounds the rate, so on a 60/120 Hz display the
     * two simply settle on whichever is slower. Fall back to no-vsync if the
     * driver can't provide it. */
    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    }
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    update_render_logical_size(renderer);

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
    audio_init(GENESIS_PSG_SAMPLE_RATE_NTSC);
    audio_set_master_volume(g_app_config.volume);   /* settings.ini / launcher */

    /* --- clownmdemu init (debug/oracle backend only) ---
     * The own backend uses machine_init() below and links ZERO clownmdemu
     * code, so its entire emulator lifecycle (Constant_Initialise / Initialise
     * / SetCartridge / HardReset) is skipped. ROM still reaches g_rom via
     * glue_init() further down, which is backend-independent. */
#if !OWN_BACKEND
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
#endif /* !OWN_BACKEND */
#if OWN_BACKEND
    machine_init();   /* clean-room own backend (VDP + bus + Z80) */
#endif

    /* Battery-backed cartridge SRAM (Sonic 3 save slots): HardReset has run
     * SetUpExternalRAM, so size / non_volatile are now valid. Load the .srm
     * if one exists; a no-op for cartridges without battery save. */
    /* (battery-SRAM init moved below glue_init — the own backend parses SRAM
     * geometry from g_rom, which glue_init populates.) */

#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
    /* Step 2 / Hybrid: initialise glue (Step 2 also starts the game thread).
     * The own backend has no clownmdemu instance — glue keeps s_emu NULL and
     * routes everything through g_machine. */
#if OWN_BACKEND
    glue_init(NULL, rom_raw, rom_raw_len);
#else
    glue_init(&g_clownmdemu, rom_raw, rom_raw_len);
#endif
#endif

    /* Audio arch overhaul: initialise our cycle-stamped YM2612 + PSG
     * instances. Safe to call on oracle too (init functions are idempotent
     * no-ops if stubs). */
    audio_mixer_init();
    audio_obs_init();
    color_lut_setup();   /* present-time screen-color LUT (default raw passthrough) */
    /* widescreen_setup() was hoisted above window creation (it sizes the 16:9
     * window); nothing else here depends on it. */

#if OWN_BACKEND
    gbus_sram_setup(&g_machine.bus);   /* g_rom is populated now (glue_init) */
    /* Per-game SRAM override for lock-on carts whose header carries no "RA"
     * marker (S3&K combined, standalone S&K). No-op when the header already
     * declared SRAM or the spec leaves sram_start at 0. */
    gbus_sram_set_geometry(&g_machine.bus,
                           g_game_spec.sram_start, g_game_spec.sram_end);
#endif
    runner_sram_init_and_load(rom_path);

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
    char   interlace_display_from_ini[32] = {0};
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
                else if (strcmp(k, "interlace_display") == 0)
                    strncpy(interlace_display_from_ini, v, sizeof(interlace_display_from_ini) - 1);
            }
            fclose(df);
        }
    }
    {
        const char *chosen = interlace_display_cli ? interlace_display_cli :
            (interlace_display_from_ini[0] ? interlace_display_from_ini : NULL);
        if (chosen)
            set_interlace_display_mode(chosen);
        fprintf(stderr, "[display] interlace_display=%s\n",
                s_interlace_display_mode == INTERLACE_DISPLAY_RAW ? "raw" : "tv");
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

#ifdef GENESIS_COSIM
    /* Differential co-simulation: bring up the lockstep TCP server + checkpoint
     * machinery BEFORE the first 68K instruction runs (env GENESIS_COSIM_PORT /
     * _STRIDE / _CLOCK). Only built into the genesis-cosim target. */
    cosim_init();
#endif

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

    /* --- Save state (quick & dirty: snapshot the entire ClownMDEmu struct).
     * Oracle/hybrid only; the own backend has the GROWNS file states. --- */
#if !OWN_BACKEND
    static ClownMDEmu s_savestate;
    int s_savestate_valid = 0;
#endif

    /* --- Main loop --- */
    int running = 1;
    int turbo   = start_turbo;   /* F5 toggles turbo (uncapped frame rate, no audio) */
    uint32_t frame_num = 0;
    int      mode_prev = -1;     /* --hash-on-mode: last Game_Mode seen (-1 = none yet) */
    uint32_t mode_seq  = 0;      /* --hash-on-mode: transition sequence counter */
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
                gamepad_handle_event(&pev);
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
            gamepad_handle_event(&ev);
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = 0;

                /* Fullscreen toggle: F11, Alt+Enter, or Cmd/Ctrl+F.
                 * FULLSCREEN_DESKTOP keeps the desktop resolution and lets
                 * SDL_RenderSetLogicalSize letterbox the 320×224 image. */
                {
                    SDL_Keymod mod = ev.key.keysym.mod;
                    int alt_enter = (ev.key.keysym.sym == SDLK_RETURN) &&
                                    (mod & KMOD_ALT);
                    int cmd_f = (ev.key.keysym.sym == SDLK_f) &&
                                (mod & (KMOD_GUI | KMOD_CTRL));
                    if (ev.key.keysym.sym == SDLK_F11 || alt_enter || cmd_f) {
                        Uint32 is_fs = SDL_GetWindowFlags(window) &
                                       SDL_WINDOW_FULLSCREEN_DESKTOP;
                        SDL_SetWindowFullscreen(window,
                            is_fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                        update_render_logical_size(renderer);
                        continue;   /* don't also treat Enter/F-key as a save slot */
                    }
                }

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
                /* [SND-TRACE] F12 = dump the sound-command lifecycle ring AND
                 * the [CHIP-TRACE] FM/PSG register-write stream, coincidentally
                 * so the two snapshots line up for native-vs-oracle A/B. */
                if (ev.key.keysym.sym == SDLK_F12) {
                    /* chip_ring = shared stream (both builds); snd_ring = own only. */
                    { extern void chip_trace_dump(const char *path); chip_trace_dump("chip_ring.txt"); }
#if OWN_BACKEND
                    { extern void snd_trace_dump(const char *path); snd_trace_dump("snd_ring.txt"); }
#endif
                }
            }
        }

        /* Controller shoulder taps: LB = quicksave slot 1, RB = quickload
         * slot 1. Edge-triggered inside gamepad_handle_event so a single
         * press fires once even at 60 Hz polling. */
        {
            int save_slot = gamepad_consume_quicksave();
            int load_slot = gamepad_consume_quickload();
            if (save_slot || load_slot) {
                int slot = save_slot ? save_slot : load_slot;
                char slot_name[48];
#if ENABLE_RECOMPILED_CODE
                snprintf(slot_name, sizeof(slot_name), "native_save_%d.bin", slot);
#else
                snprintf(slot_name, sizeof(slot_name), "interp_save_%d.bin", slot);
#endif
                if (save_slot) runner_save_state_file(slot_name);
                else           runner_load_state_file(slot_name);
            }
        }

        /* Tab OR controller Back = hold for turbo */
        { const Uint8 *ks = SDL_GetKeyboardState(NULL);
          int held = ks[SDL_SCANCODE_TAB] || gamepad_turbo_held();
          turbo = held ? 1 : (start_turbo ? 1 : 0); }

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
#if OWN_BACKEND
          {
              /* Own-backend input: build each port's pad mask from the same
               * sources clownmdemu queries (keyboard / gamepad / .input
               * script / TCP) via input_requested_cb, and hand it to our bus
               * before running the frame. The recompiled ReadJoypads reads it
               * back through the controller protocol in gbus pad_read().
               *
               * The 8 standard buttons route through input_requested_cb so the
               * P1 dev overrides (.input / TCP / scripted) still drive the game;
               * 6-button extras (X/Y/Z/Mode) — which the 8-button callback can't
               * express — are OR'd straight from the per-player input map. */
              static const ClownMDEmu_Button own_btns[8] = {
                  CLOWNMDEMU_BUTTON_UP,   CLOWNMDEMU_BUTTON_DOWN,
                  CLOWNMDEMU_BUTTON_LEFT, CLOWNMDEMU_BUTTON_RIGHT,
                  CLOWNMDEMU_BUTTON_B,    CLOWNMDEMU_BUTTON_C,
                  CLOWNMDEMU_BUTTON_A,    CLOWNMDEMU_BUTTON_START };
              static const uint16_t own_bits[8] = {
                  GPAD_UP, GPAD_DOWN, GPAD_LEFT, GPAD_RIGHT,
                  GPAD_B,  GPAD_C,    GPAD_A,    GPAD_START };
              for (int port = 0; port < 2; port++) {
                  machine_set_pad_type(port, g_input_map.p[port].pad_type);
                  uint16_t pad_mask = 0;
                  for (int b = 0; b < 8; b++)
                      if (input_requested_cb(NULL, (cc_u8f)port, own_btns[b]))
                          pad_mask |= own_bits[b];
                  if (g_input_map.p[port].pad_type == PAD_6BUTTON)
                      pad_mask |= input_current_mask(port) &
                                  (GPAD_X | GPAD_Y | GPAD_Z | GPAD_MODE);
                  machine_set_pad(port, pad_mask);
              }
          }
          { extern unsigned long g_snd_vint;       /* [CHIP-TRACE] cross-backend sync stamp */
            g_snd_vint = (unsigned long)m68k_read32(0xFFFE0C); }
          widescreen_update_for_frame();   /* set VDP margin + game RAM word */
          machine_run_frame(own_scanline_sink, NULL);
          s_screen_width  = gvdp_active_width(&g_machine.vdp);
          /* Output height doubles in interlace mode 2 (S2 2P split-screen);
           * the existing interlace display modes (tv squash / raw) take over
           * from here, same as the clownmdemu path. */
          s_screen_height = gvdp_output_height(&g_machine.vdp);
#else
          ClownMDEmu_Iterate(&g_clownmdemu);  /* DoCycles interleaves game */
#endif
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
              /* Both backends now deliver chip writes through the cycle-
               * stamped event queue; the mixer sorts by stamp, advances the
               * chips between writes, and tail-advances to the wall-frame
               * end. (The own backend's old per-scanline live advance is
               * gone — it collapsed the 68K V-int handler's whole driver
               * tick onto one chip cycle; see genesis_machine.c.) */
              audio_mixer_drain(NTSC_WALL_FRAME_MASTER_CYCLES,
                                s_fm_accum,  FM_ACCUM_FRAMES,  &s_fm_count,
                                s_psg_accum, PSG_ACCUM_FRAMES, &s_psg_count);
          }
          /* Tier-3: deterministic pre-resample mixer capture (env-gated). */
          dump_predrc(s_fm_accum, s_fm_count, s_psg_accum, s_psg_count);
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
          glue_end_of_wall_frame();
#ifdef GENESIS_COSIM
          /* Differential co-sim FRAME checkpoint. Own-backend: master_cycle is the
           * ruler. Oracle (pairing #2): no g_machine — use the wall-frame number
           * (both backends run one frame per checkpoint; the chain folds the
           * ordinal, so the clock value is report-only). */
#if OWN_BACKEND
          cosim_frame_checkpoint(g_machine.master_cycle);
#else
          cosim_frame_checkpoint((uint64_t)frame_num);
#endif
#endif
          }
#else
        s_current_frame_for_input = frame_num;
        /* [CHIP-TRACE] stamp the oracle's FM/PSG write stream with the current
         * wall frame so its chip_ring.txt aligns with the own-backend dump.
         * (Writes happen inside Iterate; g_snd_line is set per-write in the
         * audio_event_push tap.) */
        { extern unsigned long g_snd_frame, g_snd_vint;
          g_snd_frame = (unsigned long)frame_num;
          g_snd_vint  = (unsigned long)m68k_read32(0xFFFE0C); }  /* [CHIP-TRACE] sync stamp */
        ClownMDEmu_Iterate(&g_clownmdemu);
#if SONIC_REVERSE_DEBUG
        rdb_record_iterate();
#endif
#ifdef GENESIS_COSIM
        /* Pairing #2 (oracle) FRAME checkpoint: ClownMDEmu_Iterate ran one full
         * wall frame. No g_machine here — key on the wall-frame number (the chain
         * folds the ordinal, so the clock value is report-only). */
        cosim_frame_checkpoint((uint64_t)frame_num);
#endif
#endif
        check_ramdump();

#ifdef GEN_DEV_TRACE
        /* [POLL-DIAG] measure how often the 256-poll bounded fallback fires
         * (own-backend vs oracle). Dev builds only. */
        {
            extern unsigned long g_z80poll_fallback_hits, g_z80poll_yields;
            if ((frame_num % 120u) == 0)
                fprintf(stderr, "[POLL-DIAG] frame=%u fallback_hits=%lu yields=%lu\n",
                        (unsigned)frame_num, g_z80poll_fallback_hits, g_z80poll_yields);
        }
#endif

#if HYBRID_RECOMPILED_CODE
        { extern void glue_log_frame_state(uint64_t);
          static uint64_t hybrid_frame = 0;
          glue_log_frame_state(hybrid_frame++); }
#endif

        /* --framelog: works in ALL build modes */
        write_framelog(frame_num);

        /* [SND-TRACE] headless auto-dump: when --snd-dump-frame N is set, dump
         * both always-on rings once at frame N (no F12 needed for agent-driven
         * captures). Strip with the rest of the [SND-TRACE] diagnostics. */
        if (snd_dump_frame && frame_num == snd_dump_frame) {
            /* chip_ring is the SHARED dev-only stream (chip_trace.c) — both the
             * own backend and the oracle capture it, so dump in BOTH builds for
             * the native-vs-oracle stream diff. */
            { extern void chip_trace_dump(const char *path); chip_trace_dump("chip_ring.txt"); }
#if OWN_BACKEND   /* snd_ring (SndEvt) + z80_ram dump are own-backend only (genesis_machine.c) */
            { extern void snd_trace_dump(const char *path); snd_trace_dump("snd_ring.txt"); }
            { extern void z80_ram_dump(const char *path); z80_ram_dump("z80_ram.bin"); }
#endif
            fprintf(stderr, "[CHIP-TRACE] auto-dumped chip_ring at frame %u\n", (unsigned)frame_num);
        }

        /* [CHIP-TRACE] vint-triggered dump: fire once when vint_runcount reaches
         * the target. vint ($FFFE0C, big-endian longword) is the cross-backend
         * sync key — both builds reach a given vint at the SAME logical sound
         * state (unlike wall frame, which misaligns across native/oracle). */
        if (snd_dump_vint && !snd_dump_done) {
            uint32_t vint = m68k_read32(0xFFFE0C);
            if (vint >= snd_dump_vint) {
                { extern void chip_trace_dump(const char *path); chip_trace_dump("chip_ring.txt"); }
                snd_dump_done = 1;
                fprintf(stderr, "[CHIP-TRACE] auto-dumped chip_ring at vint %u (frame %u)\n",
                        (unsigned)vint, (unsigned)frame_num);
            }
        }

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
#if OWN_BACKEND
                  widescreen_update_for_frame();   /* set VDP margin + game RAM word */
                  machine_run_frame(own_scanline_sink, NULL);
#else
                  ClownMDEmu_Iterate(&g_clownmdemu);
#endif
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
            audio_flush(frame_num, !turbo,
                        (const int16_t *)s_fm_accum, s_fm_count,
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
                if (s_sram_dirty) runner_sram_flush();
                return input_script_exit_code();
            }
        }

        /* FNV-1a-64 hash of the framebuffer's active region. Emitted
         * every `hash_frames` frames when --hash-frames is set so CI /
         * golden-image comparison can detect rendering regressions
         * without a human watching the screen. NOTE: fixed-frame sampling is
         * drift-fragile — any timing change slides content past the sample
         * points (see GENESIS_ACCURACY_BURNDOWN.md item 8). Prefer
         * --hash-on-mode for a pacing-invariant regression guard. */
        if (hash_frames > 0 && (frame_num % hash_frames) == 0) {
            fprintf(stderr, "[FBHASH] frame=%u w=%d h=%d hash=0x%016llX\n",
                    frame_num, s_screen_width, s_screen_height,
                    (unsigned long long)framebuf_active_hash());
        }

        /* State-anchored hash: emit a framebuffer hash on each Game_Mode
         * TRANSITION rather than at fixed wall frames. The transition sequence
         * is deterministic; only its wall-frame timing drifts, so anchoring to
         * "the game reached state X" makes the guard immune to pacing drift.
         * Tag carries the sequence index + the mode so the harness compares
         * like-for-like (seq, mode), never absolute frame. */
        if (hash_on_mode && g_game_layout.game_mode_addr) {
            int mode = (int)m68k_read8(g_game_layout.game_mode_addr);
            if (mode != mode_prev) {
                fprintf(stderr,
                        "[MODEHASH] seq=%u mode=0x%02X frame=%u w=%d h=%d hash=0x%016llX\n",
                        mode_seq, (unsigned)mode, frame_num,
                        s_screen_width, s_screen_height,
                        (unsigned long long)framebuf_active_hash());
                mode_prev = mode;
                mode_seq++;
            }
        }

        /* Persist battery SRAM to disk shortly after the game writes a save. */
        runner_sram_autosave_tick(frame_num);

        /* Upload framebuffer to GPU texture. When a present-time color model
         * is enabled, transform a COPY into s_present_buf and upload that —
         * s_framebuf (the verified/hashed raw VDP output) is never modified. */
        const uint32_t *present_src = s_framebuf;
        if (s_color_lut_on) {
            color_lut_map_frame(&s_color_lut, s_framebuf, s_present_buf,
                                s_screen_width, s_screen_height,
                                MAX_SCREEN_WIDTH);
            present_src = s_present_buf;
        }
        SDL_UpdateTexture(texture, NULL, present_src,
                          MAX_SCREEN_WIDTH * (int)sizeof(uint32_t));

        update_render_logical_size(renderer);
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

    /* --- Discovery runtime oracle: dump executed-PC coverage --- */
#if SONIC_REVERSE_DEBUG && defined(SONIC_ORACLE_BUILD)
    if (exec_cov_out) {
        extern void oracle_exec_coverage_write(const char *path);
        oracle_exec_coverage_write(exec_cov_out);
    }
#else
    if (exec_cov_out)
        fprintf(stderr, "[EXECCOV] --exec-coverage-out requires the oracle "
                        "build (interpreter coverage); ignored\n");
#endif

    /* --- Cleanup --- */
    if (s_sram_dirty) runner_sram_flush();  /* final flush of any pending save */
    if (s_debug_enabled) cmd_server_shutdown();
    if (s_framelog_file) fclose(s_framelog_file);
    { extern int audio_wav_active(void); extern void audio_wav_stop(void);
      if (audio_wav_active()) audio_wav_stop(); }
#if ENABLE_RECOMPILED_CODE || HYBRID_RECOMPILED_CODE
    glue_shutdown();
#endif
    audio_close();
    SDL_DestroyTexture(texture);
    gamepad_shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    free(rom_buf);
    return 0;
}
