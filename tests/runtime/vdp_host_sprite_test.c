/* Host sprites join the sprite layer behind every SAT sprite and obey plane
 * priority, without touching the hardware overflow/collision flags. */
#include "genesis_vdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static GVDP v;
static int calls, high;
static void word(unsigned a,unsigned n) { v.vram[a]=n>>8; v.vram[a+1]=n; }
static void draw(void *user, const GVDP *vdp, const GVDPSpriteLayer *layer)
{
    CHECK(user==&calls && vdp==&v && layer->total==320 && layer->offset==0);
    ++calls;
    for (int x=0;x<32;++x) {
        if (layer->opaque[x]) continue;
        layer->index[x]=2; layer->opaque[x]=1; layer->high[x]=(uint8_t)high;
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
    gvdp_set_host_sprites(NULL,NULL);
    gvdp_render_scanline(&v,0,row); CHECK(calls==2 && row[12]==0);
    puts("host sprites composite behind native sprites with plane priority");
    return 0;
}
