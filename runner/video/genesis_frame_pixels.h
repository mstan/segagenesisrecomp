/* Synchronous final-pixel ownership, shared by native conversion and host
 * color/UI/upload preparation. Borrowed raw input is never modified/retained.
 * Scratch belongs to the caller; copy_width/copy_height declare the complete
 * raw storage extent, including inactive filtering/history pixels. Metadata
 * and writable output must not alias any of that storage. */
#ifndef GENESIS_FRAME_PIXELS_H
#define GENESIS_FRAME_PIXELS_H
#include "genesis_pixel_policy.h"
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
typedef struct GenesisPixelView { const void *pixels;int pitch_bytes,writable; } GenesisPixelView;
typedef void (*GenesisPixelTransform)(void *user,const void *src,void *dst,int count);
static inline int genesis_pixel_span(int pitch,int width,int height,size_t *span)
{
    if(width<=0 || height<=0 || width>INT_MAX/4 || pitch<width*4)return 0;
    size_t row=(size_t)width*4u;
    if((size_t)(height-1)>(SIZE_MAX-row)/(size_t)pitch)return 0;
    *span=(size_t)(height-1)*(size_t)pitch+row;return 1;
}
static inline int genesis_pixel_overlap(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;
}
static inline int genesis_frame_pixels(GenesisPixelView *view,const void *raw,int raw_pitch,
    void *scratch,size_t scratch_bytes,int scratch_pitch,int width,int height,
    int copy_width,int copy_height,int need_writable,GenesisPixelTransform transform,void *user)
{
    size_t raw_span,dst_span;
    if(!view || !raw || width<=0 || height<=0 || copy_width<width || copy_height<height ||
       !genesis_pixel_span(raw_pitch,copy_width,copy_height,&raw_span) ||
       genesis_pixel_overlap(raw,raw_span,view,sizeof *view))return 0;
    /* Preserve packed fallback for unusual byte pitches on every SDL backend. */
    if(!need_writable && !transform && !(raw_pitch&3)) {
        view->pixels=raw;view->pitch_bytes=raw_pitch;view->writable=0;return 1;
    }
    int out_w=transform?width:copy_width,out_h=transform?height:copy_height;
    if(!scratch ||
       !genesis_pixel_span(scratch_pitch,out_w,out_h,&dst_span) || dst_span>scratch_bytes ||
       genesis_pixel_overlap(raw,raw_span,scratch,dst_span) ||
       genesis_pixel_overlap(scratch,dst_span,view,sizeof *view))return 0;
    for(int y=0;y<out_h;++y) {
        const uint8_t *src=(const uint8_t *)raw+(size_t)y*(size_t)raw_pitch;
        uint8_t *dst=(uint8_t *)scratch+(size_t)y*(size_t)scratch_pitch;
        if(transform)transform(user,src,dst,out_w);
        else memcpy(dst,src,(size_t)out_w*4u);
    }
    view->pixels=scratch;view->pitch_bytes=scratch_pitch;view->writable=1;return 1;
}
static inline void genesis_pixels_expand(uint32_t *out,const uint8_t *indices,
    int count,const uint32_t *palette,unsigned colors,uint32_t fallback)
{
    for(int x=0;x<count;++x){unsigned i=indices[x];out[x]=i<colors?palette[i]:fallback;}
}
#endif
