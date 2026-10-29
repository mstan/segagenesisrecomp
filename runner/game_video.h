#pragma once
#include <stdint.h>
#include <string.h>

struct GVDP;
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
    /* Requested logical viewport width (0 means native geometry). Without an
     * independent renderer qualifier, 0 also keeps legacy native rendering. */
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
    /* Optional complete-row ownership, independent of viewport/expansion mode.
     * Pure presentation policy, sampled after width() at frame preparation.
     * The producer must handle native geometry too, using native_row for any
     * unsupported row. NULL retains the legacy enabled/width qualification.
     * This does not enable scene hooks, change simulation or configure a mode. */
    int (*renderer_enabled)(void);
} GameVideo;

/* Host-local derived frame plan, never deterministic machine/snapshot state.
 * A zero width means actual native row geometry, not an absent row owner. */
typedef struct GameVideoFrame {
    const GameVideo *adapter,*owner;
    int requested_width,width;
    int viewport; /* qualified simulation/view policy, not local row ownership */
} GameVideoFrame;

static inline int game_video_has_viewport(const GameVideo *video)
{
    return video && video->configure && video->enabled && video->width;
}

static inline int game_video_viewport_enabled(const GameVideo *video)
{
    return video && video->enabled && video->enabled();
}

static inline GameVideoFrame game_video_prepare_frame(const GameVideo *video,
    int drawable_w,int drawable_h,int native_w,int native_h)
{
    GameVideoFrame frame={video,NULL,0,0,0};
    if(!video || native_w<=0 || native_h<=0)return frame;
    /* Width may publish simulation state. Always do it before local ownership
     * or texture eligibility, including when the selected renderer declines. */
    if(video->enabled && video->enabled() && video->width)
        frame.requested_width=video->width(drawable_w,drawable_h,native_w,native_h);
    int valid_width=frame.requested_width>=native_w;
    frame.viewport=valid_width;
    int selected=video->renderer_enabled ? video->renderer_enabled() : valid_width;
    if(selected && (video->scanline || video->scanline_lazy)) {
        frame.owner=video;
        frame.width=valid_width ? frame.requested_width : 0;
    }
    return frame;
}

/* A local resource failure affects image ownership only, never configure(),
 * enabled(), requested simulation width, or an agreed session profile. */
static inline void game_video_native_fallback(GameVideoFrame *frame)
{
    frame->owner=NULL;frame->width=0;
}

static inline int game_video_output_width(const GameVideoFrame *frame,int native_w)
{
    return frame->owner && frame->width>0 ? frame->width : native_w;
}

/* Synchronous original-row provider has the same current-row memoization
 * contract as GameVideoNativeRow. Returns the actual initialized row width.
 * An oversized local plan falls back to the complete original row; it never
 * advertises stale columns. The caller supplies capacity for native_w pixels. */
static inline int game_video_draw_row(const GameVideoFrame *frame,
    const struct GVDP *vdp,int line,int native_w,uint32_t *out,int capacity,
    GameVideoNativeRow native_row,void *native_ctx)
{
    int width=game_video_output_width(frame,native_w);
    const GameVideo *owner=frame->owner;
    if(width>capacity || width<=0){owner=NULL;width=native_w;}
    if(native_w<=0 || native_w>capacity || !out || !native_row)return 0;
    if(owner && owner->scanline_lazy)
        owner->scanline_lazy(vdp,line,native_w,out,width,native_row,native_ctx);
    else if(owner && owner->scanline)
        owner->scanline(vdp,line,native_row(native_ctx),native_w,out,width);
    else memcpy(out,native_row(native_ctx),(size_t)native_w*sizeof *out);
    if(frame->adapter && frame->adapter->overlay_scanline)
        frame->adapter->overlay_scanline(vdp,line,out,width);
    return width;
}
