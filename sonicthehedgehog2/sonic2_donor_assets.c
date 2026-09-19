#include "sonic2_donor_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* See docs/SONIC2_DONOR_PORT.md. These describe verified donor revisions,
 * never offsets in the Sonic 2 host image. Callers must verify donor identity. */
const S2DonorLayout s2_amy_171_layout = {253,0x8B8C0,0x8D6CE,0x60000,0x29E2,8};
const S2DonorLayout s2_sk_knuckles_layout = {251,0x14A8D6,0x14BD0A,0x1200E0,0xA8AFC,6};
static unsigned word(const uint8_t *p) { return (unsigned)p[0]*256+p[1]; }
static int inside(size_t size,size_t offset,size_t length) { return offset<=size && length<=size-offset; }
void s2_donor_free(S2DonorBank *bank)
{
    if (!bank) return;
    for (unsigned i=0;i<S2_DONOR_MAX_FRAMES;++i) free(bank->frames[i].pixels);
    memset(bank,0,sizeof *bank);
}
int s2_donor_decode(const uint8_t *rom,size_t size,const S2DonorLayout *l,
                    S2DonorBank *out,char *error,size_t error_size)
{
    if (error && error_size) error[0]=0;
    if (!rom || !l || !out || !l->frames || l->frames>S2_DONOR_MAX_FRAMES ||
        (l->mapping_stride!=6 && l->mapping_stride!=8) ||
        !inside(size,l->mappings,l->frames*2) || !inside(size,l->dplc,l->frames*2) ||
        !inside(size,l->palette,32)) {
        if (error && error_size) snprintf(error,error_size,"Invalid donor asset layout");
        return 0;
    }
    /* Build transactionally; never destroy a previously loaded bank on error.
     * Caller initializes *out to zero before its first decode. */
    S2DonorBank *bank=(S2DonorBank *)calloc(1,sizeof *bank);
    if (!bank) return 0;
    bank->count=l->frames;
    for (unsigned c=0;c<16;++c) bank->palette[c]=(uint16_t)word(rom+l->palette+c*2);
    unsigned frame=0;
    for (;frame<l->frames;++frame) {
        size_t mp=(size_t)l->mappings+word(rom+l->mappings+frame*2);
        size_t dp=(size_t)l->dplc+word(rom+l->dplc+frame*2);
        if (!inside(size,mp,2) || !inside(size,dp,2)) goto bad;
        unsigned pieces=word(rom+mp), cues=word(rom+dp);
        mp+=2; dp+=2;
        if (pieces>S2_DONOR_MAX_PIECES || cues>64 ||
            !inside(size,mp,pieces*l->mapping_stride) || !inside(size,dp,cues*2)) goto bad;
        uint8_t tiles[1024*32]; unsigned tile_count=0;
        for (unsigned cue=0;cue<cues;++cue) {
            unsigned code=word(rom+dp+cue*2), count=(code>>12)+1;
            size_t source=(size_t)l->art+(code&4095)*32;
            if (tile_count+count>1024 || !inside(size,source,count*32)) goto bad;
            memcpy(tiles+tile_count*32,rom+source,count*32);
            tile_count+=count;
        }
        if (!pieces) continue;
        int minx=32767,miny=32767,maxx=-32768,maxy=-32768;
        for (unsigned piece=0;piece<pieces;++piece) {
            const uint8_t *p=rom+mp+piece*l->mapping_stride;
            int y=(int8_t)p[0],x=(int16_t)word(p+l->mapping_stride-2);
            unsigned w=((p[1]>>2)&3)+1,h=(p[1]&3)+1,attr=word(p+2);
            if ((attr&2047)+w*h>tile_count || (attr&0x6000)) goto bad;
            if (x<minx) minx=x;
            if (y<miny) miny=y;
            if (x+(int)w*8>maxx) maxx=x+w*8;
            if (y+(int)h*8>maxy) maxy=y+h*8;
        }
        if (minx < -128 || miny < -128 || maxx>128 || maxy>128 || maxx<=minx || maxy<=miny) goto bad;
        S2DonorFrame *f=&bank->frames[frame];
        f->x=(int16_t)minx; f->y=(int16_t)miny;
        f->width=(uint16_t)(maxx-minx); f->height=(uint16_t)(maxy-miny);
        f->pixels=(uint8_t *)calloc((size_t)f->width*f->height,1);
        if (!f->pixels) goto bad;
        /* Native SAT priority: earlier pieces win overlapping nonzero pixels. */
        for (unsigned piece=0;piece<pieces;++piece) {
            const uint8_t *p=rom+mp+piece*l->mapping_stride;
            int y=(int8_t)p[0],x=(int16_t)word(p+l->mapping_stride-2);
            unsigned w=(((p[1]>>2)&3)+1)*8,h=((p[1]&3)+1)*8,attr=word(p+2);
            for (unsigned py=0;py<h;++py) for (unsigned px=0;px<w;++px) {
                unsigned sx=attr&0x0800?w-1-px:px,sy=attr&0x1000?h-1-py:py;
                unsigned tile=(attr&2047)+(sx/8)*(h/8)+sy/8;
                unsigned byte=tiles[tile*32+(sy%8)*4+(sx%8)/2];
                unsigned nibble=sx&1?byte&15:byte>>4;
                size_t dest=(size_t)(y-miny+py)*f->width+x-minx+px;
                if (!f->pixels[dest]) f->pixels[dest]=(uint8_t)nibble;
            }
        }
    }
    s2_donor_free(out); *out=*bank; free(bank);
    return 1;
bad:
    if (error && error_size) snprintf(error,error_size,"Invalid mapping or DPLC at donor frame %u",frame);
    s2_donor_free(bank); free(bank); return 0;
}
