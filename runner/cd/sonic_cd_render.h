#ifndef SEGACD_SONIC_CD_RENDER_H
#define SEGACD_SONIC_CD_RENDER_H

#include "sonic_cd_scene.h"
#include "sonic_cd_limits.h"
#include "sonic_cd_world.h"
#include "../video/genesis_vdp.h"

/* Presentation only. Inputs are an immutable original level/RAM snapshot,
 * the scene actually published to the native SAT, and the live raster VDP.
 * This renderer neither updates the guest nor stretches a native frame. */
typedef struct {
    const SegaCDWorldMap *world;
    const SegaCDScene *scene;
    unsigned width;
    uint8_t *sprite_line; /* caller-owned scratch: width bytes */
    size_t sprite_line_bytes;
} SegaCDRender;

/* Returns zero when this frame uses a display/level mode that has not been
 * qualified for reconstruction, or a source lookup is invalid. The host must
 * discard reconstruction for the whole frame on zero. Early display-mode
 * rejection leaves output/scratch untouched; a late invalid source lookup
 * may leave a partial working row, which must never be presented. */
int segacd_render_scanline(const SegaCDRender *render, const GVDP *vdp,
                           unsigned line, uint32_t *argb, size_t pixels);

#endif
