// launcher_gui.h — RmlUi pre-boot launcher for segagenesisrecomp games.
//
// Shown in its own SDL/OpenGL window before the recompiled game boots, modeled
// structurally on the nesrecomp / snesrecomp / psxrecomp launchers. The user
// picks/verifies a ROM (Genesis header + CRC32/SHA-1), assigns per-player input
// devices, REBINDS individual keyboard + gamepad buttons (3- or 6-button),
// manages the SRAM save, tunes display/audio + the widescreen toggle, then
// presses PLAY. Display/audio choices are written back into the caller-owned
// Settings; controller bindings are written straight into the live g_input_map.

#pragma once

#include <cstddef>
#include <cstdint>

struct SDL_Window;

namespace genesis_launcher {

enum class Result {
    Launch,       // user pressed PLAY — boot out_rom_path with the edited settings
    Quit,         // user closed the window — caller should exit
    Unavailable,  // launcher could not initialise (assets/GL) — boot as if skipped
};

// Editable display/audio/launcher settings (controller bindings live in
// g_input_map, edited in place). Mirrors AppConfig.
struct Settings {
    int  window_scale     = 2;
    int  fullscreen       = 0;
    bool linear_filter    = false;
    bool widescreen       = false;
    int  widescreen_cells = 8;
    int  volume           = 100;
    bool skip_launcher    = false;
};

// Static facts about the game being configured.
struct GameInfo {
    const char* name             = nullptr;  // "Sonic the Hedgehog"
    const char* short_name       = nullptr;  // "Sonic3" — selects img/boxart-<short>.png
    const char* region           = nullptr;  // fallback region label
    uint32_t    expected_crc     = 0;        // CRC32 over the raw ROM
    bool        has_expected_crc = false;
    uint32_t    expected_size    = 0;        // bytes (0 = skip the size check)
    bool        uses_sram        = false;    // battery-backed cart -> SAVE RAM panel
    bool        widescreen_supported = false;
};

// Run the launcher loop to completion. `gl_context` is an SDL_GLContext (void*)
// already current on `window`. `io` is seeded with the effective settings and,
// on Result::Launch, updated in place. `assets_dir` holds launcher.rml / fonts /
// img. On Result::Launch, `out_rom_path` receives the ROM to boot. `initial_rom`
// may be a cached path (rom.cfg) to pre-populate the dashboard, or null/empty.
Result run(SDL_Window* window, void* gl_context,
           Settings& io, const GameInfo& game,
           const char* assets_dir, const char* initial_rom,
           char* out_rom_path, size_t out_rom_path_len);

} // namespace genesis_launcher
