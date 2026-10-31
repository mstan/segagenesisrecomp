/* Independent original426 headers; no candidate helper in this translation unit. */
#include "pixel_reference/color_lut.h"
#include <string.h>
int original_inverse(uint32_t value){return genesis_dac_nearest_normal_idx(value);}
void original_color_row(const void *context,const void *source,void *destination,int count)
{
    const ColorLut *lut=(const ColorLut *)context;
    const uint32_t *src=(const uint32_t *)source;
    uint32_t *dst=(uint32_t *)destination;
    for(int x=0;x<count;++x)dst[x]=color_lut_map_argb(lut,src[x]);
}
/* Original main sequence: color-active-only, otherwise UI copies FULLstorage. */
const uint32_t *original_prepare(const void *lut,const uint32_t *raw,uint32_t *stage,
    int stride,int storage_h,int width,int height,int color,int ui)
{
    const uint32_t *src=raw;
    if(color){for(int y=0;y<height;++y)original_color_row(lut,raw+(size_t)y*stride,stage+(size_t)y*stride,width);src=stage;}
    if(ui && src!=stage){memcpy(stage,src,(size_t)stride*storage_h*sizeof(uint32_t));src=stage;}
    return src;
}
