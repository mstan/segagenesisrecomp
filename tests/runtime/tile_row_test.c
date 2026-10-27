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
        int count=8-(x&7);
        for(int i=0;i<count;++i) {
            if(gtile_row_pixel(&row)!=reference_pixel(vram,attr,x+i,y)) {
                fprintf(stderr,"pattern-row mismatch test=%u x=%d y=%d attr=%04x pixel=%d\n",test,x,y,attr,i);return 1;
            }
        }
        if(row.remaining!=0)return 1;
    }
    puts("tile rows: 20000 random patterns, flips, palettes, partial/negative coordinates matched");
    return 0;
}
