/*
 * main_runner.c — SDL2 main loop for the Genesis recompiler runner.
 *
 * Responsibilities:
 *   - Parse command-line args (ROM path, --script, --game flags)
 *   - Initialize SDL2 window (320×224 or 256×224 depending on VDP mode)
 *   - Call runtime_init(), then game_on_init()
 *   - Drive the frame loop: call func_VBLANK() at 60Hz, render via vdp_render_frame()
 *   - Forward input to g_controller1_buttons
 *   - Auto-save screenshots to C:/temp/ every 60 frames
 *   - Handle save state hotkeys (F5=turbo, F6=save, F7=load)
 *
 * TODO: implement SDL2 setup, audio mixing (YM2612 + PSG), input mapping
 */
#include "genesis_runtime.h"
#include "game_extras.h"
#include "vdp.h"
#include "ym2612.h"
#include "psg.h"
#include "logger.h"
#include "launcher.h"
#include "input_script.h"
#include "savestate.h"
#include "crc32.h"
#include "framedump.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GENESIS_SCREEN_W 320
#define GENESIS_SCREEN_H 224
#define GENESIS_FPS      60

static uint32_t s_framebuf[GENESIS_SCREEN_W * GENESIS_SCREEN_H];

/* ---- Forward declarations ---- */
static void handle_input(SDL_Event *ev);
static void run_frame(void);
static void save_screenshot(uint64_t frame);

int main(int argc, char *argv[]) {
    /* Parse args */
    const char *rom_path    = NULL;
    const char *script_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--script") == 0 && i+1 < argc) script_path = argv[++i];
        else if (argv[i][0] != '-') rom_path = argv[i];
        else { int r = game_handle_arg(argv[i], i+1 < argc ? argv[i+1] : NULL);
               if (r && i+1 < argc) i++; }
    }

    /* ROM discovery (file dialog if not on CLI) */
    char rom_buf[512];
    if (!rom_path) {
        if (!launcher_pick_rom(rom_buf, sizeof(rom_buf))) {
            fprintf(stderr, "No ROM selected.\n");
            return 1;
        }
        rom_path = rom_buf;
    }

    /* Load ROM into g_rom[] */
    if (!launcher_load_rom(rom_path)) {
        fprintf(stderr, "Failed to load ROM: %s\n", rom_path);
        return 1;
    }

    /* CRC32 verification */
    uint32_t expected = game_get_expected_crc32();
    if (expected) {
        /* TODO: compute CRC32 over g_rom[0x200..rom_size] (skipping vector table)
         * uint32_t actual = crc32_compute(g_rom + 0x200, rom_size - 0x200);
         * if (actual != expected) { warn; } */
    }

    /* SDL2 init */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    char title[128];
    snprintf(title, sizeof(title), "%s — GenesisRecomp", game_get_name());

    SDL_Window   *window   = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        GENESIS_SCREEN_W * 2, GENESIS_SCREEN_H * 2,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_MINIMIZED);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture  *texture  = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        GENESIS_SCREEN_W, GENESIS_SCREEN_H);

    /* Runtime + game init */
    runtime_init();
    game_on_init();

    /* Input script */
    if (script_path) input_script_load(script_path);

    /* Load input script if provided */
    int turbo = 0;

    /* Initial entry */
    game_call_entry_point();

    /* Main loop */
    int running = 1;
    uint32_t last_ticks = SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            else handle_input(&ev);
        }

        /* Frame timing: 60Hz = ~16.67ms per frame */
        uint32_t now = SDL_GetTicks();
        if (!turbo && (now - last_ticks) < (1000u / GENESIS_FPS)) {
            SDL_Delay(1);
            continue;
        }
        last_ticks = now;

        game_on_frame(g_frame_count);

        /* Run one frame */
        run_frame();

        game_post_vblank(g_frame_count);
        framedump_on_frame();
        g_frame_count++;

        /* Render */
        fprintf(stderr, "[MAIN] frame %llu: pre-render\n", (unsigned long long)g_frame_count);
        vdp_render_frame(s_framebuf);
        fprintf(stderr, "[MAIN] frame %llu: post-render\n", (unsigned long long)g_frame_count);
        SDL_UpdateTexture(texture, NULL, s_framebuf, GENESIS_SCREEN_W * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        /* Auto screenshot every 60 frames */
        if (g_frame_count % 60 == 0) save_screenshot(g_frame_count);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

static void handle_input(SDL_Event *ev) {
    if (ev->type != SDL_KEYDOWN && ev->type != SDL_KEYUP) return;
    uint8_t mask = 0;
    switch (ev->key.keysym.scancode) {
    case SDL_SCANCODE_UP:     mask = 0x01; break;
    case SDL_SCANCODE_DOWN:   mask = 0x02; break;
    case SDL_SCANCODE_LEFT:   mask = 0x04; break;
    case SDL_SCANCODE_RIGHT:  mask = 0x08; break;
    case SDL_SCANCODE_Z:      mask = 0x40; break;  /* A button */
    case SDL_SCANCODE_X:      mask = 0x10; break;  /* B button */
    case SDL_SCANCODE_C:      mask = 0x20; break;  /* C button */
    case SDL_SCANCODE_RETURN: mask = 0x80; break;  /* Start   */
    default: return;
    }
    if (ev->type == SDL_KEYDOWN)
        g_controller1_buttons |= mask;
    else
        g_controller1_buttons &= (uint8_t)~mask;
}

static void run_frame(void) {
    /* Signal the game thread to service HBlank (×224) + VBlank on its own stack.
     * Blocks until the game thread completes the interrupt handlers.
     * This eliminates g_ram races: all RAM access happens on one thread at a time. */
    runtime_request_vblank();
}

static void save_screenshot(uint64_t frame) {
    char path[64];
    snprintf(path, sizeof(path), "C:/temp/genesis_shot_%04llu.bmp",
             (unsigned long long)frame);
    SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(
        s_framebuf,
        GENESIS_SCREEN_W, GENESIS_SCREEN_H,
        32, GENESIS_SCREEN_W * 4,
        0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
    if (surf) {
        SDL_SaveBMP(surf, path);
        SDL_FreeSurface(surf);
    }
}
