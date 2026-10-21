#ifndef SEGACD_SONIC_CD_WIDESCREEN_H
#define SEGACD_SONIC_CD_WIDESCREEN_H

#include "machine.h"

typedef struct SegaCDWidescreen SegaCDWidescreen;
typedef enum {
    SEGACD_WIDESCREEN_NATIVE = 0,
    SEGACD_WIDESCREEN_ORIGINAL_SCENE,
    SEGACD_WIDESCREEN_WARMING,
    SEGACD_WIDESCREEN_ACTIVE,
    SEGACD_WIDESCREEN_UNAVAILABLE
} SegaCDWidescreenState;

SegaCDWidescreen *segacd_widescreen_create(SegaCDMachine *,
                                          const SegaCDNativeProgram *);
/* Failure leaves the owner alive, with any remaining callback/module valid.
 * Retry between completed operations before closing the machine. */
int segacd_widescreen_destroy(SegaCDWidescreen *);
/* Call between completed frames. Width 320 disables the mod. No CPU executes
 * here; actual source-guarded guest routines retain object simulation. */
int segacd_widescreen_prepare(SegaCDWidescreen *, unsigned width);
SegaCDWidescreenState segacd_widescreen_state(const SegaCDWidescreen *);
/* Original-only scenes remain centered on the selected canvas. Returns an
 * immutable frame until prepare/next guest frame/destroy. */
const uint32_t *segacd_widescreen_frame(SegaCDWidescreen *, unsigned *width,
                                       unsigned *height, unsigned *pitch);
void segacd_widescreen_stats(const SegaCDWidescreen *, uint64_t *expanded_frames,
                             uint64_t *original_frames, uint64_t *scene_rejections);

#endif
