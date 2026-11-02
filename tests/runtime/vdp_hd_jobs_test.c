#include "genesis_raster_jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line%d: %s\n",__LINE__,#c);return 1;}}while(0)
static GVDP device;
static uint32_t expected[GVDP_HD_ROWS][GVDP_MAX_WIDTH],actual[GVDP_HD_ROWS][GVDP_MAX_WIDTH];
static uint32_t random_state=0x13572468u;
static uint8_t private_art[2][0x10000],private_lines[2048],extended_lines[2048];
static uint32_t random32(void)
{random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void fill(unsigned mode)
{
    /* Reuse the established renderer corpus's random VRAM/register model. */
    memset(&device,0,sizeof device);
    for(unsigned i=0;i<GVDP_VRAM_SIZE;++i)device.vram[i]=(uint8_t)random32();
    for(unsigned i=0;i<GVDP_VSRAM_ENTRIES;++i)device.vsram[i]=(uint16_t)random32();
    device.reg[1]=(uint8_t)(0x40|((mode&8)?8:0));
    device.reg[2]=(uint8_t)((random32()&7)<<3);device.reg[3]=(uint8_t)(random32()&0x3e);
    device.reg[4]=(uint8_t)(random32()&7);device.reg[5]=(uint8_t)(random32()&0x7f);
    device.reg[7]=(uint8_t)(random32()&63);device.reg[11]=(uint8_t)(mode&7);
    device.reg[12]=(uint8_t)((mode&1)|((mode&2)?6:0)|((mode&4)?8:0));
    device.reg[13]=(uint8_t)(random32()&63);device.reg[16]=(uint8_t)(random32()&0x33);
    device.reg[17]=(uint8_t)(random32()&0x9f);device.reg[18]=(uint8_t)(random32()&0x9f);
}
static void no_host_pixels(void *u,const GVDP *v,const GVDPSpriteLayer *layer)
{(void)u;(void)v;(void)layer;}
static void host_pixels(void *u,const GVDP *v,const GVDPSpriteLayer *layer)
{(void)u;(void)v;layer->index[0]=5;layer->opaque[0]=1;layer->high[0]=1;}
int main(void)
{
    const GVDPRasterFrame *held=NULL;
    uint32_t held_pixel=0;
    for(unsigned i=0;i<sizeof private_art;++i)((uint8_t *)private_art)[i]=(uint8_t)random32();
    for(unsigned i=0;i<2048;++i){private_lines[i]=(uint8_t)(i%4);extended_lines[i]=(uint8_t)((i+1)%4);}
    gvdp_hd_enable(1);
    for(unsigned frame=0;frame<32;++frame) {
        fill(frame);
        if(frame==24){CHECK(gvdp_set_sprite_patterns_bank(0,private_art[0],sizeof private_art[0],private_lines));CHECK(gvdp_set_sprite_patterns_bank(1,private_art[1],sizeof private_art[1],private_lines));}
        if(frame==28)gvdp_set_ext_tiles(extended_lines);
        gvdp_set_ws_extra((frame&1)?31:0);gvdp_set_ws_canvas((frame&2)?511:0);
        gvdp_set_ws_bar_black((frame>>2)&1);gvdp_set_bgdiag((frame>>3)&1);
        gvdp_set_host_sprites(frame&16?no_host_pixels:NULL,NULL);
        unsigned width=0,height=(unsigned)gvdp_output_height(&device);
        for(unsigned y=0;y<height;++y) {
            /* Changes bypass the port API, as do whole-machine restores. The
             * observed content, not an incomplete write-hook list, is identity. */
            device.vram[random32()&65535]=(uint8_t)random32();
            device.vsram[random32()%40]=(uint16_t)random32();
            uint8_t indices[GVDP_MAX_WIDTH];
            width=(unsigned)gvdp_render_scanline(&device,(int)y,indices);
            uint32_t colors[256];
            for(unsigned i=0;i<256;++i)colors[i]=0xff000000u|((i*0x71419u+y*0x149u+frame*0x163u)&0xffffffu);
            gvdp_hd_bind_palette(y,colors,256);
            for(unsigned x=0;x<width;++x)expected[y][x]=colors[indices[x]];
        }
        const GVDPRasterFrame *f=gvdp_hd_acquire();CHECK(f&&f->valid&&f->height==height&&f->width==width);
        memset(&device,0xDA,sizeof device);
        CHECK(gvdp_hd_render_argb(f,&actual[0][0],GVDP_MAX_WIDTH));
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
            if(actual[y][x]!=expected[y][x]) {
                fprintf(stderr,"frame%u row%u col%u job%08x native%08x spans%u\n",frame,y,x,actual[y][x],expected[y][x],f->span_count);return 1;
            }
        if(held){CHECK(gvdp_hd_render_argb(held,&actual[0][0],GVDP_MAX_WIDTH));CHECK(actual[0][0]==held_pixel);gvdp_hd_release(held);}
        held=f;held_pixel=expected[0][0];
    }
    /* A frontend overlay can invalidate the last row after end_row and before
     * its palette binding. No subsequent row may revoke a stale publication. */
    fill(0);device.reg[1]=0;gvdp_set_ws_extra(0);gvdp_set_ws_canvas(0);
    gvdp_set_bgdiag(0);gvdp_set_host_sprites(NULL,NULL);
    for(unsigned y=0;y<224;++y) {
        uint8_t indices[GVDP_MAX_WIDTH];uint32_t palette[256]={0};
        gvdp_render_scanline(&device,(int)y,indices);
        if(y==223)gvdp_hd_ineligible();
        gvdp_hd_bind_palette(y,palette,256);
    }
    CHECK(!gvdp_hd_acquire());
    CHECK(gvdp_hd_render_argb(held,&actual[0][0],GVDP_MAX_WIDTH));CHECK(actual[0][0]==held_pixel);
    gvdp_reset(&device);CHECK(!gvdp_hd_acquire());
    CHECK(gvdp_hd_render_argb(held,&actual[0][0],GVDP_MAX_WIDTH));CHECK(actual[0][0]==held_pixel);
    gvdp_hd_release(held);
    gvdp_set_sprite_patterns(NULL,0,NULL);gvdp_set_sprite_patterns_bank(1,NULL,0,NULL);gvdp_set_ext_tiles(NULL);
    /* Keeping every lease cannot overwrite one or block guest progress. */
    const GVDPRasterFrame *leases[3];
    fill(1);device.reg[1]=0;gvdp_set_ws_extra(0);gvdp_set_ws_canvas(0);gvdp_set_bgdiag(0);gvdp_set_host_sprites(NULL,NULL);
    uint8_t row[GVDP_MAX_WIDTH];uint32_t palette[256]={0};
    for(unsigned f=0;f<4;++f) {
        for(unsigned y=0;y<224;++y){gvdp_render_scanline(&device,(int)y,row);gvdp_hd_bind_palette(y,palette,256);}
        if(f<3){leases[f]=gvdp_hd_acquire();CHECK(leases[f]);}else CHECK(!gvdp_hd_acquire());
    }
    for(unsigned i=0;i<3;++i)gvdp_hd_release(leases[i]);
    fill(1);gvdp_set_ws_extra(0);gvdp_set_ws_canvas(0);gvdp_set_bgdiag(0);
    gvdp_set_host_sprites(host_pixels,NULL);
    for(unsigned y=0;y<224;++y){gvdp_render_scanline(&device,(int)y,row);gvdp_hd_bind_palette(y,palette,256);}
    CHECK(!gvdp_hd_acquire()); /* Arbitrary custom drawing is not misrepresented. */
    /* Explicit immutable presentation capture must retain that same HD veto. */
    GVDPRasterRow captured;
    for(unsigned y=0;y<224;++y){
        CHECK(gvdp_capture_presented_scanline(&device,(int)y,&captured)>0);
        gvdp_hd_bind_palette(y,palette,256);
    }
    CHECK(!gvdp_hd_acquire());
    gvdp_set_host_sprites(NULL,NULL);gvdp_hd_enable(0);
    puts("32 complete native/job frames exact: H32/H40, IM2, window/scroll, shadow/highlight, odd canvas, direct writes, palettes, private sources/colors, immutable leases/reset/backpressure and custom fallback");
    return 0;
}
