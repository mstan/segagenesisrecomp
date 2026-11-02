#include "game_video.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static int viewport_on,requested,selected,order,width_calls,owner_calls;
static int eager_calls,lazy_calls,overlay_calls,fetch_calls,native_width,seen_width;
static uint32_t original[512],output[1026];
static int configure(const char *m){(void)m;CHECK(0);return 0;}
static int enabled(void){return viewport_on;}
static int width(int dw,int dh,int nw,int nh)
{(void)dw;(void)dh;(void)nw;(void)nh;CHECK(order==0);order=1;++width_calls;return requested;}
static int owner(void){CHECK(order==viewport_on);order=2;++owner_calls;return selected;}
static const uint32_t *fetch(void *ctx){CHECK(ctx==original);++fetch_calls;return original;}
static void eager(const struct GVDP *v,int line,const uint32_t *pixels,int nw,uint32_t *out,int w)
{
    (void)v;CHECK(line==37&&nw==native_width);++eager_calls;seen_width=w;
    for(int x=0;x<w;++x)out[x]=x<nw?pixels[x]:0x44556677u;
}
static void lazy(const struct GVDP *v,int line,int nw,uint32_t *out,int w,GameVideoNativeRow get,void *ctx)
{
    ++lazy_calls;eager(v,line,get(ctx),nw,out,w);
}
static void complete(const struct GVDP *v,int line,int nw,uint32_t *out,int w,GameVideoNativeRow get,void *ctx)
{
    (void)v;(void)get;(void)ctx;CHECK(line==37&&nw==native_width);++lazy_calls;seen_width=w;
    for(int x=0;x<w;++x)out[x]=0x55667788u;
}
static void overlay(const struct GVDP *v,int line,uint32_t *out,int w)
{(void)v;CHECK(line==37&&w>0);++overlay_calls;out[w-1]^=0x1234u;}
static void reset(int view,int own,int requested_width)
{
    viewport_on=view;selected=own;requested=requested_width;order=0;
    width_calls=owner_calls=eager_calls=lazy_calls=overlay_calls=fetch_calls=seen_width=0;
    for(int i=0;i<512;++i)original[i]=(uint32_t)i*0x10203u+0xff000000u;
    for(int i=0;i<1026;++i)output[i]=0xdeadbeefu;
}
int main(void)
{
    const GameVideo modern={configure,enabled,width,eager,overlay,lazy,owner};
    /* Deliberately preserve positional legacy initialization/source defaults. */
    const GameVideo legacy={configure,enabled,width,eager,overlay,NULL,NULL};
    static const int geometries[]={256,320,336,384,464,512};
    unsigned cases=0;
    for(unsigned n=0;n<sizeof geometries/sizeof *geometries;++n)
    for(int view=0;view<2;++view)for(int own=0;own<2;++own)
    for(int requested_width=0;requested_width<=1024;requested_width+=256) {
        reset(view,own,requested_width);native_width=geometries[n];
        GameVideoFrame plan=game_video_prepare_frame(&modern,1920,1080,320,224);
        CHECK(width_calls==view&&owner_calls==1);
        CHECK(plan.requested_width==(view?requested_width:0));
        CHECK(plan.viewport==(view&&requested_width>=320));
        CHECK((plan.owner!=NULL)==own);
        int expected_width=own&&plan.viewport?requested_width:native_width;
        int got=game_video_draw_row(&plan,NULL,37,native_width,output+1,1024,fetch,original);
        CHECK(got==expected_width&&overlay_calls==1&&fetch_calls==1);
        CHECK(lazy_calls==own&&eager_calls==own);
        CHECK(output[0]==0xdeadbeefu&&output[expected_width+1]==0xdeadbeefu);
        for(int x=0;x<expected_width;++x) {
            uint32_t value=x<native_width?original[x]:0x44556677u;
            if(x==expected_width-1)value^=0x1234u;
            CHECK(output[x+1]==value);
        }
        /* Host output failure must not alter the settled simulation request. */
        int request=plan.requested_width,policy=plan.viewport;
        game_video_native_fallback(&plan);
        CHECK(!plan.owner&&!plan.width&&plan.requested_width==request&&plan.viewport==policy);
        ++cases;
    }
    reset(1,0,768);native_width=320;
    GameVideoFrame plan=game_video_prepare_frame(&legacy,0,0,320,224);
    CHECK(plan.owner==&legacy&&plan.width==768&&width_calls==1&&!owner_calls);
    CHECK(game_video_draw_row(&plan,NULL,37,320,output+1,1024,fetch,original)==768);
    CHECK(eager_calls==1&&lazy_calls==0&&overlay_calls==1&&fetch_calls==1);
    /* An oversized local canvas falls back with a truthful native width and
     * no stale advertised columns, without touching viewport/simulation. */
    reset(1,1,1024);plan=game_video_prepare_frame(&modern,0,0,320,224);
    CHECK(game_video_draw_row(&plan,NULL,37,320,output+1,512,fetch,original)==320);
    CHECK(!eager_calls&&!lazy_calls&&overlay_calls==1&&fetch_calls==1&&plan.viewport);
    CHECK(output[321]==0xdeadbeefu);
    /* A complete native producer needs no viewport callbacks or eager pixels. */
    reset(0,1,0);native_width=256;
    GameVideo native_only={0};native_only.renderer_enabled=owner;native_only.scanline_lazy=complete;
    plan=game_video_prepare_frame(&native_only,0,0,320,224);
    CHECK(plan.owner==&native_only&&!plan.width&&!plan.viewport&&!game_video_has_viewport(&native_only));
    CHECK(game_video_draw_row(&plan,NULL,37,256,output+1,512,fetch,original)==256);
    CHECK(lazy_calls==1&&!eager_calls&&!fetch_calls);
    reset(0,1,0);GameVideo incomplete={0};incomplete.renderer_enabled=owner;incomplete.overlay_scanline=overlay;
    plan=game_video_prepare_frame(&incomplete,0,0,320,224);CHECK(!plan.owner);
    CHECK(game_video_draw_row(&plan,NULL,37,256,output+1,512,fetch,original)==256);
    CHECK(overlay_calls==1&&fetch_calls==1);
    plan=game_video_prepare_frame(NULL,0,0,320,224);CHECK(!plan.owner&&!plan.adapter);
    CHECK(game_video_draw_row(&plan,NULL,37,512,output+1,256,fetch,original)==0);
    printf("Game video frame: %u geometry/view/ownership cases, legacy/eager/lazy/overlay/complete-native/fallback contracts pass\n",cases);
    return 0;
}
