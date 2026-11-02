#ifndef GENESIS_HD_GPU_H
#define GENESIS_HD_GPU_H

#include <stdint.h>
#include <stddef.h>
#include <SDL2/SDL.h>
#include "color_lut.h"

#ifdef __cplusplus
extern "C" {
#endif

struct GVDPRasterFrame;
typedef struct GenesisHdGpu GenesisHdGpu;
typedef struct GenesisHdGpuMetrics {
    uint64_t frames, uploaded_bytes, source_upload_bytes, job_upload_bytes;
    uint64_t gpu_upload_ns, gpu_draw_ns, gpu_samples;
    uint64_t cpu_prepare_ticks;
    unsigned requested_scale, output_width, output_height;
    uint64_t target_bytes;
    int timestamps_available;
} GenesisHdGpuMetrics;

/* Settings are presentation-only. Environment override is useful to existing
 * automated runs; invalid values leave the configured scale unchanged. */
unsigned genesis_hd_scale(unsigned configured);
int genesis_hd_prefer_renderer(unsigned scale);
SDL_Renderer *genesis_hd_create_renderer(SDL_Window *, unsigned flags, unsigned scale);
GenesisHdGpu *genesis_hd_gpu_create(SDL_Renderer *, SDL_Texture *activation);
void genesis_hd_gpu_destroy(GenesisHdGpu *);
/* Returns a borrowed SDL target texture on success; NULL keeps the ordinary
 * exact native presentation path. No guest/framebuffer ownership is taken. */
SDL_Texture *genesis_hd_gpu_render(GenesisHdGpu *,
    const struct GVDPRasterFrame *, unsigned scale, const ColorLut *);
void genesis_hd_gpu_invalidate(GenesisHdGpu *);
void genesis_hd_gpu_metrics(const GenesisHdGpu *, GenesisHdGpuMetrics *);
int genesis_hd_gpu_readback(GenesisHdGpu *, void *argb, size_t pitch_bytes);

#ifdef __cplusplus
}
#endif
#endif
