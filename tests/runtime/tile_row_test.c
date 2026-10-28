#include "video/genesis_tile_row.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t seed=0x9e3779b9;
static uint32_t random_u32(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
static uint8_t reference_pixel(const uint8_t *v, uint16_t attr, int x, int y)
{
    int fx=x&7,fy=y&7;
    if(attr&0x800)fx^=7;
    if(attr&0x1000)fy^=7;
    unsigned addr=(attr&2047u)*32u+(unsigned)fy*4u+(unsigned)fx/2u;
    unsigned nib=(fx&1)?v[addr]&15:v[addr]>>4;
    return nib?(uint8_t)(((attr>>9)&48)|nib):0;
}
int main(void)
{
    static uint8_t vram[65536];
    for(unsigned i=0;i<sizeof vram;++i)vram[i]=(uint8_t)random_u32();
    for(unsigned test=0;test<20000;++test) {
        uint16_t attr=(uint16_t)random_u32();
        int x=(int)(random_u32()&65535)-32768,y=(int)(random_u32()&65535)-32768;
        GenesisTileRow row;gtile_row_begin(&row,vram,attr,x,y);
        for (int flip=0;flip<2;++flip) {
            uint8_t metadata=(uint8_t)(((attr>>9)&48)|((attr>>8)&128));
            uint64_t packed=gtile_packed_row(row.bits,metadata,flip);
            uint8_t decoded[8];memcpy(decoded,&packed,sizeof decoded);
            for (unsigned pixel=0;pixel<8;++pixel) {
                unsigned nib=(row.bits>>(28-((pixel^(flip?7u:0u))*4)))&15u;
                unsigned expected=metadata|nib|(nib?64u:0u);
                if(decoded[pixel]!=expected) {
                    fprintf(stderr,"packed row mismatch test=%u flip=%d pixel=%u\n",test,flip,pixel);return 1;
                }
            }
        }
        int count=8-(x&7);
        for(int i=0;i<count;++i) {
            if(gtile_row_pixel(&row)!=reference_pixel(vram,attr,x+i,y)) {
                fprintf(stderr,"pattern-row mismatch test=%u x=%d y=%d attr=%04x pixel=%d\n",test,x,y,attr,i);return 1;
            }
        }
        if(row.remaining!=0)return 1;
    }
    for (unsigned nib=0;nib<16;++nib) {
        uint32_t bits=nib*UINT32_C(0x11111111);
        uint64_t packed=gtile_packed_row(bits,0xb0,0);
        uint8_t decoded[8];memcpy(decoded,&packed,sizeof decoded);
        for (unsigned i=0;i<8;++i)
            if(decoded[i]!=(uint8_t)(0xb0u|nib|(nib?64u:0u)))return 1;
    }
    puts("tile rows: 20000 random patterns, native packed rows, flips, palettes, partial/negative coordinates matched");
    return 0;
}
