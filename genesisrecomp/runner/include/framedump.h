/*
 * framedump.h — Per-frame state snapshot and regression detection.
 *
 * Writes frame_NNNN.json to the framedump/ directory (relative to CWD)
 * after each VBlank boundary. Each file records CRC32 hashes of RAM,
 * VRAM, CRAM, VSRAM, plus VDP registers and CPU state.
 *
 * Behavior:
 *   - If no file exists for frame N: write it and continue.
 *   - If a file exists and state matches: skip write, continue.
 *   - If a file exists and state differs (regression): log the diff,
 *     delete that file and all later ones, write new file, stop dumping.
 *   - Stops automatically after FRAMEDUMP_MAX_FRAMES frames.
 *
 * First run builds the golden baseline. Subsequent runs compare against it.
 * Delete framedump/ to reset the baseline.
 */
#pragma once
#include <stdint.h>

#define FRAMEDUMP_MAX_FRAMES 100
#define FRAMEDUMP_DIR        "framedump"

/* Call once per frame, after VBlank completes and game state is stable.
 * Returns 1 if still recording, 0 if stopped (divergence or frame limit). */
int framedump_on_frame(void);

/* Returns 1 if framedump is still active (no divergence detected yet). */
int framedump_is_active(void);
