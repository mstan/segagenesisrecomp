#ifndef GENESIS_TILE_PACKET_H
#define GENESIS_TILE_PACKET_H
#include "genesis_vdp.h"

static inline unsigned gvdp_tile_packet_plane(const GVDPTileRasterRow *row,
                                              unsigned plane,unsigned x)
{
    unsigned n=row->map[plane][x];
    if(n>=row->span_count[plane])return 0;
    const GVDPTileSpan *span=&row->spans[plane][n];
    unsigned fx=((span->meta>>16)&7u)+x-(span->meta&65535u);
    if(fx>7u)return 0;
    if(span->meta&0x01000000u)fx^=7u;
    unsigned nib=(span->bits>>((7u-fx)*4u))&15u;
    return ((span->meta>>24)&0xb0u)|nib|(nib?64u:0u);
}
/* One exact sample is also enough for CPU preview consumers. They need not
 * materialize a complete raw image or synchronize with a GPU readback. */
static inline unsigned gvdp_packet_index(const GVDPTileRasterRow *packet,
                                         unsigned x,int compact)
{
    const GVDPRasterRow *r=&packet->raster;
    if(x<r->start || x>=r->start+r->count)return r->bar;
    if(!r->display)return r->backdrop;
    unsigned a=compact?gvdp_tile_packet_plane(packet,0,x):r->plane_a[x];
    unsigned b=compact?gvdp_tile_packet_plane(packet,1,x):r->plane_b[x];
    unsigned so=r->opaque[x],sh=r->high[x]&&so;
    unsigned ah=(a&0xc0u)==0xc0u,bh=(b&0xc0u)==0xc0u;
    unsigned pixel=sh?r->sprite[x]:ah?a&63u:bh?b&63u:so?r->sprite[x]:
        (a&64u)?a&63u:(b&64u)?b&63u:r->backdrop;
    if(r->shadow_mode && pixel<64u) {
        unsigned shadow=!(ah||bh||sh),hilite=0;
        if(r->shadow_op[x])shadow=1;
        if(r->hilite_op[x]){if(shadow)shadow=0;else hilite=1;}
        if(hilite)pixel+=128u;else if(shadow)pixel+=64u;
    }
    if(r->diagnostic && pixel<GVDP_HOST_PALETTE_BASE &&
       !((x-r->diag_offset)&r->diag_mask))
        pixel=(pixel&63u)+GVDP_PALETTE_SHADOW;
    return pixel;
}
#endif
