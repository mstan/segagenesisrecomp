#ifndef GENESIS_GPU_RASTER_H
#define GENESIS_GPU_RASTER_H
#include <stdint.h>
#include "genesis_vdp.h"
struct SDL_Renderer;
struct SDL_Texture;
typedef struct GenesisGpuRaster GenesisGpuRaster;
GenesisGpuRaster *genesis_gpu_raster_create(struct SDL_Renderer *renderer,
    void (*before_replace)(void *),void *user);
void genesis_gpu_raster_destroy(GenesisGpuRaster *gpu);
int genesis_gpu_raster_begin(GenesisGpuRaster *gpu,uint32_t *raw,int stride,uint64_t generation,int eligible);
int genesis_gpu_raster_capture(GenesisGpuRaster *gpu,GVDP *vdp,int line);
void genesis_gpu_raster_resolve(GenesisGpuRaster *gpu);
void genesis_gpu_raster_invalidate(GenesisGpuRaster *gpu);
struct SDL_Texture *genesis_gpu_raster_draw(GenesisGpuRaster *gpu);
struct SDL_Texture *genesis_gpu_raster_last_texture(GenesisGpuRaster *gpu);
int genesis_gpu_raster_preview(void *gpu,uint32_t *out,int width,int height,int source_width,int source_height);
#endif
