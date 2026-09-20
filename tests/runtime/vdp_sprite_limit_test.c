/* Crowded scanlines must retain complete sprites when the host opts in. */
#include "genesis_vdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static GVDP v;
static void word(unsigned a,unsigned n) { v.vram[a]=n>>8; v.vram[a+1]=n; }
static void scene(unsigned count,unsigned size)
{
    gvdp_init(&v);
    v.reg[1]=64; v.reg[2]=0x30; v.reg[4]=7; v.reg[5]=0x7C;
    v.reg[12]=1; v.reg[13]=0x3F;
    memset(v.vram+32,0x11,128);
    for (unsigned i=0;i<count;++i) {
        unsigned a=0xF800+i*8;
        word(a,128); v.vram[a+2]=size; v.vram[a+3]=i+1<count?i+1:0;
        word(a+4,0x8001); word(a+6,128+i*8);
    }
}
int main(void)
{
    uint8_t row[320];
    scene(21,0); gvdp_set_unlimited_sprites(0);
    gvdp_render_scanline(&v,0,row); CHECK(row[160]==0 && v.sprite_overflow);
    gvdp_set_unlimited_sprites(1); v.sprite_overflow=0;
    gvdp_render_scanline(&v,0,row); CHECK(row[160]==1 && !v.sprite_overflow);
    /* Eleven 32px sprites exhaust the pixel budget before the count limit. */
    scene(11,12); gvdp_set_unlimited_sprites(0);
    gvdp_render_scanline(&v,0,row); CHECK(row[108]==0 && v.sprite_overflow);
    gvdp_set_unlimited_sprites(1); v.sprite_overflow=0;
    gvdp_render_scanline(&v,0,row); CHECK(row[108]==1 && !v.sprite_overflow);
    /* Intentional sprite masks (including native split-screen) still work. */
    word(0xF80E,0); gvdp_render_scanline(&v,0,row); CHECK(row[108]==0);
    gvdp_set_unlimited_sprites(0);
    puts("sprite count/pixel limits lifted; intentional masks preserved");
    return 0;
}
