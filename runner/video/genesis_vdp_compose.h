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

/* Normalize each byte's arbitrary nonzero value to one. All contributing
 * shifts sum to seven, so no adjacent lane can reach another lane's bit0. */
static inline uint64_t gvdp_compose_boolean(uint64_t flags)
{
    flags|=flags>>4;flags|=flags>>2;flags|=flags>>1;
    return flags&UINT64_C(0x0101010101010101);
}

/* Complete eight-lane compositor shared by retained rows and direct producers.
 * Inputs and result use native byte order, preserving output order on either
 * endian host. Zero/one sprite metadata is the evaluator's existing contract. */
static inline uint64_t gvdp_compose_packed(uint64_t a,uint64_t b,uint64_t s,
    uint64_t so,uint64_t high,uint64_t shadow_op,uint64_t hilite_op,
    uint8_t backdrop,int shadow_mode)
{
    const uint64_t one=UINT64_C(0x0101010101010101);
    const uint64_t colors=UINT64_C(0x3f3f3f3f3f3f3f3f);
    uint64_t ah=((a&(a<<1))>>7)&one;
    uint64_t bh=((b&(b<<1))>>7)&one;
    uint64_t sh=high&so;
    uint64_t pixel=(uint64_t)backdrop*one;
    pixel=gvdp_compose_select(pixel,b&colors,(b>>6)&one);
    pixel=gvdp_compose_select(pixel,a&colors,(a>>6)&one);
    pixel=gvdp_compose_select(pixel,s,so);
    pixel=gvdp_compose_select(pixel,b&colors,bh);
    pixel=gvdp_compose_select(pixel,a&colors,ah);
    pixel=gvdp_compose_select(pixel,s,sh);
    if(shadow_mode) {
        uint64_t normal=(~(pixel|(pixel<<1))>>7)&one;
        uint64_t shadow=(one^(ah|bh|sh))|shadow_op;
        uint64_t shade=((shadow&(one^hilite_op))<<6)|(((one^shadow)&hilite_op)<<7);
        pixel+=shade&(normal*UINT64_C(255));
    }
    return pixel;
}

static inline void gvdp_compose_span(uint8_t *out,const uint8_t *plane_a,
    const uint8_t *plane_b,const uint8_t *sprite,const uint8_t *opaque,
    const uint8_t *high,const uint8_t *shadow_op,const uint8_t *hilite_op,
    int count,uint8_t backdrop,int shadow_mode)
{
    while(count>=8) {
        uint64_t pixel=gvdp_compose_packed(gvdp_compose_load(plane_a),
            gvdp_compose_load(plane_b),gvdp_compose_load(sprite),
            gvdp_compose_load(opaque),gvdp_compose_load(high),
            gvdp_compose_load(shadow_op),gvdp_compose_load(hilite_op),
            backdrop,shadow_mode);
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
