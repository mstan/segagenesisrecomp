#ifndef GENESIS_RASTER_FRAME_H
#define GENESIS_RASTER_FRAME_H
#include <stdint.h>
#include <string.h>
#include "genesis_tile_packet.h"
#include "genesis_dac.h"

#define GENESIS_RASTER_ROWS (GVDP_MAX_HEIGHT*2)
typedef struct GenesisRasterRow {
    GVDPTileRasterRow packet;
    uint16_t cram[GVDP_CRAM_ENTRIES];
} GenesisRasterRow;
/* Two slots per row let a rejected new capture leave the prior owned image
 * intact. No packet copying or per-row allocation is required. */
typedef struct GenesisRasterFrame {
    GenesisRasterRow rows[2][GENESIS_RASTER_ROWS];
    uint8_t slot[GENESIS_RASTER_ROWS],pending[GENESIS_RASTER_ROWS];
    uint8_t captured[GENESIS_RASTER_ROWS];
    uint32_t *raw;
    int stride,compact,active,last_width;
    uint64_t generation,captures,resolves;
} GenesisRasterFrame;

static inline uint32_t genesis_raster_frame_color(const GenesisRasterRow *r,
                                                  unsigned x,int compact)
{
    unsigned index=gvdp_packet_index(&r->packet,x,compact);
    return index<GVDP_TOTAL_PALETTE?
        genesis_dac_cram_to_argb(r->cram[index&63u],(int)(index>>6)):0xff000000u;
}
static inline void genesis_raster_frame_resolve_row(GenesisRasterFrame *f,unsigned y)
{
    if(!f || y>=GENESIS_RASTER_ROWS || !f->pending[y] || !f->raw)return;
    const GenesisRasterRow *r=&f->rows[f->slot[y]][y];
    uint32_t *out=f->raw+(size_t)y*(size_t)f->stride;
    for(unsigned x=0;x<r->packet.raster.width;x++)
        out[x]=genesis_raster_frame_color(r,x,f->compact);
    f->pending[y]=0;++f->resolves;
}
static inline void genesis_raster_frame_resolve(GenesisRasterFrame *f)
{
    if(f)for(unsigned y=0;y<GENESIS_RASTER_ROWS;y++)genesis_raster_frame_resolve_row(f,y);
}
static inline void genesis_raster_frame_abort(GenesisRasterFrame *f)
{
    if(!f)return;
    genesis_raster_frame_resolve(f);f->active=0;
    memset(f->captured,0,sizeof f->captured);
}
static inline void genesis_raster_frame_detach(GenesisRasterFrame *f)
{
    if(!f)return;
    genesis_raster_frame_abort(f);f->raw=NULL;f->stride=0;
}
static inline int genesis_raster_frame_begin(GenesisRasterFrame *f,uint32_t *raw,
                                              int stride,uint64_t generation)
{
    if(!f || !raw || stride<GVDP_MAX_WIDTH ||
       (size_t)stride>SIZE_MAX/(GENESIS_RASTER_ROWS*sizeof(uint32_t)))return 0;
    if(f->raw!=raw || f->stride!=stride || f->generation!=generation)
        genesis_raster_frame_abort(f);
    f->raw=raw;f->stride=stride;f->generation=generation;f->active=1;
    memset(f->captured,0,sizeof f->captured);return 1;
}
static inline int genesis_raster_frame_capture(GenesisRasterFrame *f,GVDP *v,int y)
{
    if(!f || !f->active || !v || y<0 || y>=GENESIS_RASTER_ROWS)return 0;
    int width=gvdp_active_width(v);
    if(width<=0 || width>GVDP_MAX_WIDTH || width>f->stride)return 0;
    unsigned slot=f->slot[y]^1u;
    GenesisRasterRow *next=&f->rows[slot][y];
    int count=f->compact?gvdp_capture_tile_scanline(v,y,&next->packet):
                         gvdp_capture_scanline(v,y,&next->packet.raster);
    if(count<=0)return 0;
    /* Materialize an older wider row before replacing its pending tail. The
     * same rule preserves full raw/linear-filter history across mode changes. */
    if(f->pending[y] && f->rows[f->slot[y]][y].packet.raster.width>(unsigned)count)
        genesis_raster_frame_resolve_row(f,(unsigned)y);
    memcpy(next->cram,v->cram,sizeof next->cram);
    f->slot[y]=(uint8_t)slot;f->pending[y]=f->captured[y]=1;
    f->last_width=count;++f->captures;return count;
}
static inline uint32_t genesis_raster_frame_sample(const GenesisRasterFrame *f,
                                                   unsigned x,unsigned y)
{
    if(!f || !f->raw || y>=GENESIS_RASTER_ROWS || x>=(unsigned)f->stride)return 0xff000000u;
    const GenesisRasterRow *r=&f->rows[f->slot[y]][y];
    if(f->pending[y] && x<r->packet.raster.width)
        return genesis_raster_frame_color(r,x,f->compact);
    return f->raw[(size_t)y*(size_t)f->stride+x];
}
#endif
