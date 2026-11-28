#pragma once
#include "sim_step.h"
#include <stddef.h>
#include <stdint.h>

struct RecompLauncherCModProvider;
/* Always available in production. The settings default is enabled. */
typedef struct GenesisReplayView {
  int drawable_w, drawable_h, custom_on, custom_aspect;
  int widescreen, cells, pacing;
} GenesisReplayView;
typedef void (*GenesisReplayViewFn)(GenesisReplayView *, int apply);
int genesis_replay_mods(const struct RecompLauncherCModProvider *,
                        const char *playback);
int genesis_replay_init(const char *directory, const char *playback,
                        uint32_t rom_crc, size_t rom_bytes, void *sram,
                        size_t sram_bytes, int sram_loaded,
                        const char *settings, GenesisReplayViewFn);
int genesis_replay_boot_sram(const char *playback, void *sram, size_t size,
                             int *loaded);
void genesis_replay_shutdown(void);
int genesis_replay_playing(void);
int genesis_replay_failed(void);
int genesis_replay_finished(void);
const char *genesis_replay_path(void);
void genesis_replay_viewport(int *width, int *height);
/* Called only by the common tick and snapshot paths. */
int genesis_replay_begin(GenesisSimInput *);
void genesis_replay_end(void);
uint64_t genesis_replay_snapshot_saved(void);
void genesis_replay_snapshot_loaded(uint64_t token);
void genesis_replay_file_loaded(const char *path);
void genesis_replay_save_requested(void);
void genesis_replay_sram_flushed(void);
/* Hashable snapshot sections, excluding all native execution addresses. */
int genesis_rb_replay_parts(const void *blob, size_t size, const void *parts[4],
                            size_t sizes[4]);
size_t genesis_rb_save_untracked(void *dst, size_t cap);
