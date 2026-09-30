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
    puts("host sprites composite behind native sprites with plane priority");
    return 0;
}
