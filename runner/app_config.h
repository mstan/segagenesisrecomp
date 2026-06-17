/*
 * app_config.h — persistent launcher/runtime settings (settings.ini) + the
 * last-ROM pointer (rom.cfg), both stored next to the executable.
 *
 * settings.ini is a simple [section] key=value file (same flavour as debug.ini).
 * It carries the video/audio/launcher knobs the launcher edits AND the per-player
 * controller bindings (which live in g_input_map). Load seeds both; save writes
 * both back. Absent fields keep their defaults, so a hand-trimmed or first-run
 * file is fine.
 */
#ifndef RUNNER_APP_CONFIG_H
#define RUNNER_APP_CONFIG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AppConfig {
    /* video */
    int window_scale;       /* integer scale, 1..N (display window) */
    int fullscreen;         /* 0 windowed, 1 borderless-desktop */
    int linear_filter;      /* 0 nearest, 1 bilinear */
    int widescreen;         /* 0 = 4:3, 1 = 16:9 (only if the game is ws_capable) */
    int widescreen_cells;   /* extra 8px cells per side in widescreen */
    /* audio */
    int volume;             /* 0..100 */
    /* launcher */
    int skip_launcher;      /* 1 = boot straight to the game next time */
} AppConfig;

extern AppConfig g_app_config;

/* Reset g_app_config to defaults (does NOT touch g_input_map). */
void app_config_defaults(void);

/* Load settings.ini at `path` into g_app_config + g_input_map. Missing file is
 * not an error (defaults stand). Returns 1 if a file was read, 0 otherwise. */
int  app_config_load(const char *path);

/* Write g_app_config + g_input_map to settings.ini at `path`. Returns 1 on
 * success. */
int  app_config_save(const char *path);

/* rom.cfg: one line holding the absolute path of the last ROM played, used by
 * skip-launcher to boot straight in. */
int  rom_cfg_read(const char *path, char *out, size_t out_len);   /* 1 if read */
void rom_cfg_write(const char *path, const char *rom_path);

#ifdef __cplusplus
}
#endif

#endif /* RUNNER_APP_CONFIG_H */
