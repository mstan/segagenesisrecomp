#ifndef GENESIS_VDP_COMPOSE_H
#define GENESIS_VDP_COMPOSE_H
#include <stdint.h>
#include <string.h>

/* Eight independent byte lanes, using only portable unsigned arithmetic.
 * Plane bytes carry index[5:0], opacity[6], priority[7]. Sprite flags are
 * normalized zero/one bytes from the existing evaluator. Native-endian loads
 * and stores preserve lane order on either byte order; no ISA is required. */
static inline uint64_t gvdp_compose_load(const uint8_t *p)
{ uint64_t v; memcpy(&v,p,sizeof v); return v; }

static inline uint64_t gvdp_compose_select(uint64_t under,uint64_t over,uint64_t flags)
{ return under ^ ((under ^ over) & (flags * UINT64_C(255))); }

static inline void gvdp_compose_span(uint8_t *out,const uint8_t *plane_a,
    const uint8_t *plane_b,const uint8_t *sprite,const uint8_t *opaque,
    const uint8_t *high,const uint8_t *shadow_op,const uint8_t *hilite_op,
    int count,uint8_t backdrop,int shadow_mode)
{
    const uint64_t one=UINT64_C(0x0101010101010101);
    const uint64_t colors=UINT64_C(0x3f3f3f3f3f3f3f3f);
    while(count>=8) {
        uint64_t a=gvdp_compose_load(plane_a),b=gvdp_compose_load(plane_b);
        uint64_t s=gvdp_compose_load(sprite),so=gvdp_compose_load(opaque);
        uint64_t ah=((a&(a<<1))>>7)&one;
        uint64_t bh=((b&(b<<1))>>7)&one;
        uint64_t sh=gvdp_compose_load(high)&so;
        uint64_t pixel=(uint64_t)backdrop*one;
        pixel=gvdp_compose_select(pixel,b&colors,(b>>6)&one);
        pixel=gvdp_compose_select(pixel,a&colors,(a>>6)&one);
        pixel=gvdp_compose_select(pixel,s,so);
        pixel=gvdp_compose_select(pixel,b&colors,bh);
        pixel=gvdp_compose_select(pixel,a&colors,ah);
        pixel=gvdp_compose_select(pixel,s,sh);
        if(shadow_mode) {
            uint64_t normal=(~(pixel|(pixel<<1))>>7)&one;
            uint64_t shadow=(one^(ah|bh|sh))|gvdp_compose_load(shadow_op);
            uint64_t hilite=gvdp_compose_load(hilite_op);
            uint64_t shade=((shadow&(one^hilite))<<6)|(((one^shadow)&hilite)<<7);
            pixel+=shade&(normal*UINT64_C(255));
        }
        memcpy(out,&pixel,sizeof pixel);
        out+=8;plane_a+=8;plane_b+=8;sprite+=8;opaque+=8;high+=8;
        shadow_op+=8;hilite_op+=8;count-=8;
    }
    while(count--) {
        unsigned a=*plane_a++,b=*plane_b++,s=*sprite++;
        unsigned so=*opaque++,sh=*high++&&so;
        unsigned ah=(a&0xc0u)==0xc0u,bh=(b&0xc0u)==0xc0u;
        unsigned pixel=sh?s:ah?a&63u:bh?b&63u:so?s:
                       (a&64u)?a&63u:(b&64u)?b&63u:backdrop;
        if(shadow_mode && pixel<64u) {
            unsigned shadow=!(ah||bh||sh),hilite=0;
            if(*shadow_op)shadow=1;
            if(*hilite_op){if(shadow)shadow=0;else hilite=1;}
            if(hilite)pixel+=128u;else if(shadow)pixel+=64u;
        }
        ++shadow_op;++hilite_op;*out++=(uint8_t)pixel;
    }
}
#endif
