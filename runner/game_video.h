#pragma once
#include <stdint.h>

struct GVDP;
struct GVDPRasterRow;
/* Synchronous, memoized original image for the current row. Valid only during
 * scanline_lazy; callers that fully draw the row need not request it. */
typedef const uint32_t *(*GameVideoNativeRow)(void *ctx);

/* Optional game-owned presentation. It reads the live scene at the same
 * scanline as the native VDP pass; the scanline/overlay callbacks must not
 * advance or mutate the guest. Output storage belongs to the runner and can
 * grow beyond VDP dimensions.
 *
 * CORRECTED 2026-09-25: "presentation only" does not hold for the MODE. A
 * game may key simulation on it -- Sonic 2 activates objects across the
 * requested width and raises the main CPU divisor while it is on -- so the
 * configured mode (and a window-derived width) is session configuration
 * online: runner/netplay/genesis_netplay.h, GenesisSessionConfig.video. */
typedef struct GameVideo {
    /* off, fit, stage, or a positive W:H ratio. Return 0 for invalid input. */
    int (*configure)(const char *mode);
    int (*enabled)(void);
    /* Requested logical width (0 disables custom rendering this frame). */
    int (*width)(int drawable_w, int drawable_h, int native_w, int native_h);
    void (*scanline)(const struct GVDP *vdp, int line,
                     const uint32_t *native, int native_w,
                     uint32_t *out, int width);
    /* Additive game UI/actors after native OR custom rendering. Independent
     * of aspect mode; must not advance simulation or mutate the guest. */
    void (*overlay_scanline)(const struct GVDP *vdp, int line,
                             uint32_t *out, int width);
    /* Optional complete-row producer. Sprite status has already been evaluated.
     * Request original pixels through native_row only at fallback sites. The
     * legacy scanline callback remains supported for existing game adapters. */
    void (*scanline_lazy)(const struct GVDP *vdp, int line, int native_w,
                          uint32_t *out, int width,
                          GameVideoNativeRow native_row, void *native_ctx);
    /* Request immutable native plane/sprite inputs before evaluation. Only
     * requested rows pay for plane capture. Presentation providers are consumed
     * synchronously into owned pixels/masks/colors. NULL raster retains the
     * ordinary image for unsupported captures. Capture or fallback evaluates
     * device-visible sprite status exactly once. */
    int (*raster_enabled)(const struct GVDP *vdp);
    /* Complete-row producer with optional presented layers. Appended to keep
     * existing lazy producers' signatures and initializers compatible. */
    void (*scanline_presented)(const struct GVDP *vdp, int line, int native_w,
                          uint32_t *out, int width,
                          GameVideoNativeRow native_row, void *native_ctx,
                          const struct GVDPRasterRow *raster);
} GameVideo;
