// launcher_capi.h — C-callable entry point for the RmlUi Genesis launcher.
//
// main.c (C) can't speak the C++ genesis_launcher::run() API directly, so this
// shim wraps it: it creates its own SDL/GL window, runs the launcher, maps a
// plain-C settings struct in/out, and tears the window down — leaving main.c to
// seed/read the struct and pick up the chosen ROM path. Controller bindings are
// NOT in this struct: the launcher edits the live g_input_map (input_map.h)
// directly, and main.c persists it via app_config_save().

#ifndef GENESIS_LAUNCHER_CAPI_H
#define GENESIS_LAUNCHER_CAPI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mirrors genesis_launcher::Settings as plain C. Maps 1:1 onto AppConfig.
typedef struct GenesisLauncherCSettings {
    int window_scale;        // 1..N integer scale
    int fullscreen;          // 0 windowed, 1 borderless-desktop
    int linear_filter;       // bool: bilinear vs nearest
    int widescreen;          // bool: 16:9 (only meaningful if widescreen_supported)
    int widescreen_cells;    // extra 8px cells per side in widescreen
    int volume;              // 0..100
    int skip_launcher;       // bool: boot straight to the game next time
} GenesisLauncherCSettings;

typedef struct GenesisLauncherCGameInfo {
    const char* name;            // "Sonic the Hedgehog"
    const char* short_name;      // "Sonic3" — selects img/boxart-<short>.png (lowercased)
    const char* region;          // fallback region label if the ROM header lacks one
    uint32_t    expected_crc;    // CRC32 of the raw ROM (No-Intro style)
    int         has_expected_crc;
    uint32_t    expected_size;   // expected ROM size in bytes (0 = skip)
    int         uses_sram;       // show the SAVE RAM panel (battery-backed cart)
    int         widescreen_supported;  // gate the Settings -> Widescreen toggle
} GenesisLauncherCGameInfo;

// Returns: 0 = LAUNCH (boot out_rom_path with the edited *io + g_input_map),
//          1 = QUIT (caller should exit),
//          2 = UNAVAILABLE (assets/GL failed — caller boots as if skipped).
int genesis_launcher_run_window(const char* window_title,
                                GenesisLauncherCSettings* io,
                                const GenesisLauncherCGameInfo* game,
                                const char* assets_dir,
                                const char* initial_rom,
                                char* out_rom_path, size_t out_rom_path_len);

#ifdef __cplusplus
}
#endif

#endif // GENESIS_LAUNCHER_CAPI_H
