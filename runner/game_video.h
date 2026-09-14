#pragma once
#include <stdint.h>

struct GVDP;

/* Optional game-owned presentation. It reads the live scene at the same
 * scanline as the native VDP pass; it must not advance or mutate the guest.
 * Output storage belongs to the runner and can grow beyond VDP dimensions. */
typedef struct GameVideo {
    /* off, fit, stage, or a positive W:H ratio. Return 0 for invalid input. */
    int (*configure)(const char *mode);
    int (*enabled)(void);
    /* Requested logical width (0 disables custom rendering this frame). */
    int (*width)(int drawable_w, int drawable_h, int native_w, int native_h);
    void (*scanline)(const struct GVDP *vdp, int line,
                     const uint32_t *native, int native_w,
                     uint32_t *out, int width);
} GameVideo;
