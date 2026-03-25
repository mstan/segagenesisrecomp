/*
 * game_extras.h — Game-specific hook interface for the Genesis runner.
 *
 * Each game provides exactly one extras.c that implements these functions.
 * The runner calls them at the appropriate points in the main loop.
 *
 * For games with no special behavior, provide empty implementations.
 * See sonicthehedgehog/extras.c for the stub template.
 */
#pragma once
#include <stdint.h>

/* Human-readable game name, shown in the window title */
const char *game_get_name(void);

/* Called once after ROM is loaded and runtime_init() completes */
void game_on_init(void);

/* Called every VBlank, before the VBlank handler runs */
void game_on_frame(uint64_t frame_count);

/* Called every VBlank, after the VBlank handler runs (VDP state is up-to-date) */
void game_post_vblank(uint64_t frame_count);

/* Called every H-blank */
void game_on_hblank(uint32_t line);

/*
 * Handle a game-specific CLI argument.
 * key = "--flag", val = next argv (may be NULL if no following arg).
 * Returns 1 if the key was consumed (runner should skip val too if used),
 * 0 if not recognized.
 */
int game_handle_arg(const char *key, const char *val);

/* One-line usage string for game-specific args, or NULL if none */
const char *game_arg_usage(void);

/*
 * Expected CRC32 of the ROM file (raw binary, no header).
 * The launcher checks this before starting the game and re-prompts if wrong.
 * Return 0 to skip verification.
 */
uint32_t game_get_expected_crc32(void);

/*
 * Called when call_by_address has no entry for the given address.
 * The game can handle the call and return 1,
 * or return 0 to fall through to the dispatch miss log.
 */
int game_dispatch_override(uint32_t addr);

/* ---- Recompiled entry points ---- */
/* Each game's extras.c must implement these to route into the
 * generated recompiled functions (func_XXXXXX). The runner never
 * links against generated/ code directly. */

/* Called once at startup (replaces Reset_Spin / EntryPoint) */
void game_call_entry_point(void);

/* Called every VBlank (IRQ6 handler) */
void game_call_vblank(void);

/* Called every HBlank (IRQ4 handler) */
void game_call_hblank(void);
