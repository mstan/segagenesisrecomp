#include "../../runner/video/genesis_raster_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int head_gvdp_render_scanline(GVDP *,int,uint8_t *);
void head_gvdp_set_ws_extra(int);
void head_gvdp_set_ws_canvas(int);
void head_gvdp_set_ws_bar_black(int);
void head_gvdp_set_bgdiag(int);
void head_gvdp_set_host_sprites(GVDPHostSprites,void *);
uint32_t gpu_frozen_dac(uint16_t,int);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"raster frame %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t seed=0x7195882u;
static uint32_t rnd(void){seed=seed*1664525u+1013904223u;return seed;}
static unsigned callbacks;
static void private_sprites(void *u,const GVDP *v,const GVDPSpriteLayer *l)
{(void)u;(void)v;(void)l;++callbacks;}
static void setup(GVDP *v)
{
    memset(v,0,sizeof *v);
    for(unsigned i=0;i<GVDP_VRAM_SIZE;i++)v->vram[i]=(uint8_t)(rnd()>>24);
    for(unsigned i=0;i<GVDP_VSRAM_ENTRIES;i++)v->vsram[i]=(uint16_t)rnd();
    v->reg[1]=0x40;v->reg[2]=0x20;v->reg[3]=0x30;v->reg[4]=7;v->reg[5]=0x70;
    v->reg[11]=7;v->reg[12]=1;v->reg[13]=0x3e;v->reg[16]=0x33;
    gvdp_set_ws_extra(0);head_gvdp_set_ws_extra(0);gvdp_set_ws_canvas(0);head_gvdp_set_ws_canvas(0);
    gvdp_set_ws_bar_black(0);head_gvdp_set_ws_bar_black(0);gvdp_set_bgdiag(0);head_gvdp_set_bgdiag(0);
    gvdp_set_host_sprites(NULL,NULL);head_gvdp_set_host_sprites(NULL,NULL);
}
static void render_frame(GenesisRasterFrame *f,GVDP *v,uint32_t *expected,int height)
{
    GVDP reference=*v;
    for(int y=0;y<height;y++) {
        for(unsigned c=0;c<64;c++)v->cram[c]=reference.cram[c]=(uint16_t)rnd();
        unsigned address=((unsigned)y*119u)&65535u;
        v->vram[address]=reference.vram[address]=(uint8_t)rnd();
        uint8_t indices[GVDP_MAX_WIDTH];int width=head_gvdp_render_scanline(&reference,y,indices);
        CHECK(genesis_raster_frame_capture(f,v,y)==width);
        CHECK(!memcmp(v,&reference,sizeof reference));
        for(int x=0;x<width;x++)expected[(size_t)y*GVDP_MAX_WIDTH+x]=indices[x]<192?
            gpu_frozen_dac(reference.cram[indices[x]&63],indices[x]>>6):0xff000000u;
    }
    memset(v->vram,0xd2,sizeof v->vram);memset(v->cram,0xa7,sizeof v->cram);
    /* All samples, including inactive rows and old right tails, are exact
     * without requiring full canonical materialization. */
    for(unsigned y=0;y<GENESIS_RASTER_ROWS;y++)for(unsigned x=0;x<GVDP_MAX_WIDTH;x++)
        CHECK(genesis_raster_frame_sample(f,x,y)==expected[(size_t)y*GVDP_MAX_WIDTH+x]);
}
int main(void)
{
    size_t size=(size_t)GVDP_MAX_WIDTH*GENESIS_RASTER_ROWS*sizeof(uint32_t);
    GenesisRasterFrame *f=(GenesisRasterFrame *)calloc(1,sizeof *f);
    uint32_t *raw=(uint32_t *)malloc(size),*expected=(uint32_t *)malloc(size);CHECK(f&&raw&&expected);
    for(int compact=0;compact<2;compact++) {
        memset(f,0,sizeof *f);f->compact=compact;
        for(size_t i=0;i<size/4;i++)raw[i]=expected[i]=0xff000000u|((uint32_t)i*71u&0xffffffu);
        GVDP v;setup(&v);CHECK(genesis_raster_frame_begin(f,raw,GVDP_MAX_WIDTH,1));
        render_frame(f,&v,expected,224);CHECK(memcmp(raw,expected,size));
        /* Leave the wider frame pending, then narrow only some rows. */
        setup(&v);v.reg[12]=0;CHECK(genesis_raster_frame_begin(f,raw,GVDP_MAX_WIDTH,1));
        render_frame(f,&v,expected,113);CHECK(f->resolves>=113);
        genesis_raster_frame_resolve(f);CHECK(!memcmp(raw,expected,size));
        setup(&v);v.reg[12]=15;CHECK(genesis_raster_frame_begin(f,raw,GVDP_MAX_WIDTH,1));
        render_frame(f,&v,expected,448);
        setup(&v);CHECK(genesis_raster_frame_begin(f,raw,GVDP_MAX_WIDTH,1));
        render_frame(f,&v,expected,224);
        /* A restore invalidates queues but retains last-image padding. */
        CHECK(genesis_raster_frame_begin(f,raw,GVDP_MAX_WIDTH,2));CHECK(!memcmp(raw,expected,size));
        for(unsigned y=0;y<GENESIS_RASTER_ROWS;y++)CHECK(!f->pending[y]&&!f->captured[y]);
        setup(&v);render_frame(f,&v,expected,1);GVDP before=v;
        gvdp_set_host_sprites(private_sprites,NULL);callbacks=0;
        CHECK(genesis_raster_frame_capture(f,&v,0)==0);CHECK(callbacks==0&&!memcmp(&v,&before,sizeof v));
        for(unsigned x=0;x<GVDP_MAX_WIDTH;x++)CHECK(genesis_raster_frame_sample(f,x,0)==expected[x]);
        gvdp_set_host_sprites(NULL,NULL);genesis_raster_frame_abort(f);CHECK(!memcmp(raw,expected,size));
        CHECK(!genesis_raster_frame_capture(f,&v,0));
        CHECK(!genesis_raster_frame_begin(f,NULL,GVDP_MAX_WIDTH,3));
        CHECK(!genesis_raster_frame_begin(f,raw,GVDP_MAX_WIDTH-1,3));
        genesis_raster_frame_detach(f);CHECK(!f->raw&&!f->active);
    }
    free(expected);free(raw);free(f);
    puts("expanded/compact owned frame, palette, full history, restore, rejected capture, abort and sparse reads exact");return 0;
}
