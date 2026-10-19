/* Host sprites join the sprite layer behind every SAT sprite and obey plane
 * priority, without touching the hardware overflow/collision flags. */
#include "genesis_vdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static GVDP v;
static int calls, high, color = 2;
static void word(unsigned a,unsigned n) { v.vram[a]=n>>8; v.vram[a+1]=n; }
static void draw(void *user, const GVDP *vdp, const GVDPSpriteLayer *layer)
{
    CHECK(user==&calls && vdp==&v && layer->total==320 && layer->offset==0);
    ++calls;
    for (int x=0;x<32;++x) {
        if (layer->opaque[x]) continue;
        layer->index[x]=(uint8_t)color; layer->opaque[x]=1; layer->high[x]=(uint8_t)high;
    }
}
int main(void)
{
    uint8_t row[320];
    gvdp_init(&v);
    v.reg[1]=64; v.reg[2]=0x30; v.reg[4]=7; v.reg[5]=0x7C;
    v.reg[12]=1; v.reg[13]=0x3F;
    memset(v.vram+32,0x11,128);
    word(0xF800,128); v.vram[0xF802]=0; v.vram[0xF803]=0;  /* one 8x8 native sprite */
    word(0xF804,0x0001); word(0xF806,128);
    word(0xC004,0xA002);                                  /* plane A x=16..23: high, palette 1 */
    gvdp_render_scanline(&v,0,row);
    CHECK(row[4]==1 && row[12]==0 && row[20]==17);
    gvdp_set_host_sprites(draw,&calls);
    gvdp_render_scanline(&v,0,row);
    CHECK(calls==1);
    CHECK(row[4]==1);          /* native sprite keeps precedence */
    CHECK(row[12]==2);         /* host pixel fills an empty sprite column */
    CHECK(row[20]==17);        /* low-priority host pixel stays behind a high plane tile */
    high=1; gvdp_render_scanline(&v,0,row);
    CHECK(row[20]==2);         /* high-priority host pixel wins, like hardware */
    CHECK(!v.sprite_overflow && !v.sprite_collision);
    const uint32_t colors[] = { 0xFFFF1020u, 0xFF12AB34u };
    CHECK(!gvdp_host_palette());
    CHECK(gvdp_set_host_palette(colors, 2));
    CHECK(gvdp_host_palette()[0] == colors[0] && gvdp_host_palette()[1] == colors[1]);
    CHECK(gvdp_host_palette()[GVDP_HOST_PALETTE_SIZE-1] == 0xFF000000u);
    CHECK(!gvdp_set_host_palette(colors, GVDP_HOST_PALETTE_SIZE+1));
    CHECK(gvdp_host_palette()[0] == colors[0]);
    color=GVDP_HOST_PALETTE_BASE+1;
    high=0; v.reg[12]|=8; /* Host colors must not wrap in shadow/highlight mode. */
    gvdp_render_scanline(&v,0,row);
    CHECK(row[4]==1+GVDP_PALETTE_SHADOW && row[12]==color && row[20]==17);
    CHECK(gvdp_host_palette()[row[12]-GVDP_HOST_PALETTE_BASE] == colors[1]);
    high=1; gvdp_render_scanline(&v,0,row); CHECK(row[20]==color);
    uint32_t full_palette[GVDP_HOST_PALETTE_SIZE];
    for (unsigned i=0;i<GVDP_HOST_PALETTE_SIZE;++i) full_palette[i]=0xFF000000u | i;
    CHECK(gvdp_set_host_palette(full_palette,GVDP_HOST_PALETTE_SIZE));
    color=255; high=0; gvdp_render_scanline(&v,0,row);
    CHECK(row[12]==255 && gvdp_host_palette()[255-GVDP_HOST_PALETTE_BASE]==full_palette[GVDP_HOST_PALETTE_SIZE-1]);
    CHECK(v.cram[1]==0 && !v.sprite_overflow && !v.sprite_collision);
    CHECK(gvdp_set_host_palette(NULL,0) && !gvdp_host_palette());
    CHECK(gvdp_set_host_palette(colors,2));
    gvdp_set_host_sprites(NULL,NULL);
    CHECK(!gvdp_host_palette());
    v.reg[12]&=~8;
    gvdp_render_scanline(&v,0,row); CHECK(calls==5 && row[12]==0);
    uint8_t ext_tiles[2048]={0};
    uint16_t top[GVDP_EXT_LINES*16]={0}, bottom[GVDP_EXT_LINES*16]={0};
    top[1]=0xE;bottom[1]=0xE00;ext_tiles[1]=1;
    CHECK(!gvdp_ext_active() && !gvdp_ext_line(1));
    gvdp_set_ext_colors(top,bottom,8);gvdp_set_ext_tiles(ext_tiles);
    gvdp_render_scanline(&v,0,row);CHECK(row[4]==GVDP_HOST_PALETTE_BASE+1);
    CHECK(gvdp_ext_color(7,1,1)==0xE && gvdp_ext_color(8,1,1)==0xE00);
    CHECK(v.cram[1]==0 && !v.sprite_overflow && !v.sprite_collision);
    gvdp_set_ext_tiles(NULL);gvdp_render_scanline(&v,0,row);CHECK(row[4]==1);
    /* An explicit marker selects private art without touching VRAM or plane
     * pixels, at native width as well as the game's custom renderer width. */
    uint8_t patterns[64]={0},pattern_lines[2]={0,1};
    memset(patterns+32,0x33,32);
    CHECK(!gvdp_set_sprite_patterns(patterns,63,pattern_lines));
    CHECK(gvdp_set_sprite_patterns(patterns,sizeof patterns,pattern_lines));
    unsigned bytes=99;CHECK(!gvdp_sprite_patterns(0,&bytes) && bytes==0);
    CHECK(gvdp_sprite_patterns(0x80,&bytes)==patterns && bytes==64);
    uint8_t second[64],second_lines[2]={2,2};memset(second,0x44,sizeof second);
    CHECK(!gvdp_set_sprite_patterns_bank(2,second,sizeof second,second_lines));
    CHECK(gvdp_set_sprite_patterns_bank(1,second,sizeof second,second_lines));
    CHECK(gvdp_sprite_patterns(0xC0,&bytes)==second && bytes==64);
    CHECK(gvdp_sprite_pattern_line(0xC0,1)==2 && gvdp_sprite_pattern_line(0x80,1)==1);
    gvdp_render_scanline(&v,0,row);CHECK(row[4]==1);
    v.vram[0xF802]=0x80;gvdp_render_scanline(&v,0,row);
    CHECK(row[4]==GVDP_HOST_PALETTE_BASE+3 && row[20]==17);
    CHECK(v.vram[32]==0x11 && !v.sprite_overflow && !v.sprite_collision);
    word(0xF804,0x2001);gvdp_render_scanline(&v,0,row);CHECK(row[4]==19);
    word(0xF804,0x0002);gvdp_render_scanline(&v,0,row);CHECK(row[4]==0);
    word(0xF804,1);CHECK(gvdp_set_sprite_patterns(NULL,0,NULL));
    CHECK(gvdp_sprite_patterns(0xC0,&bytes)==second && bytes==64);
    v.vram[0xF802]=0xC0;gvdp_render_scanline(&v,0,row);
    CHECK(row[4]==GVDP_HOST_PALETTE_BASE+16+4 && v.vram[32]==0x11);
    CHECK(gvdp_set_sprite_patterns_bank(1,NULL,0,NULL));v.vram[0xF802]=0x80;
    gvdp_render_scanline(&v,0,row);CHECK(row[4]==1);
    puts("host sprites composite behind native sprites with plane priority");
    return 0;
}
