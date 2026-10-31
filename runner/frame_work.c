#include "frame_work.h"
#include "engine_worker.h"
#include "video/genesis_frame_pixels.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct GenesisFrameWork {
    GenesisWorker *worker;const uint32_t *raw;uint32_t *pixels;size_t capacity;
    int stride,width,height,ready;ColorLut lut;
    uint64_t submissions,committed,stale,content_token;
};
static void frame_color(void *context)
{
    GenesisFrameWork *p=(GenesisFrameWork *)context;
    for(int y=0;y<p->height;y++)for(int x=0;x<p->width;x++){
        size_t at=(size_t)y*(size_t)p->stride+(size_t)x;
        p->pixels[at]=color_lut_map_argb(&p->lut,p->raw[at]);
    }
}
GenesisFrameWork *genesis_frame_work_create(void)
{
    GenesisWorker *worker=genesis_worker_create();if(!worker)return NULL;
    GenesisFrameWork *p=(GenesisFrameWork *)calloc(1,sizeof *p);
    if(!p){genesis_worker_destroy(worker);return NULL;}p->worker=worker;return p;
}
void genesis_frame_work_wait(GenesisFrameWork *p)
{if(p)genesis_worker_join(p->worker);}
void genesis_frame_work_begin(GenesisFrameWork *p,const uint32_t *raw,int stride,
    int width,int height,const ColorLut *lut,uint64_t content_token)
{
    if(!p)return;genesis_frame_work_wait(p);p->ready=0;
    size_t span=0;
    if(!raw || !lut || lut->passthrough || stride<=0 || stride>INT_MAX/4 ||
       !genesis_pixel_span(stride*4,width,height,&span))return;
    if(p->capacity<span){uint32_t *data=(uint32_t *)realloc(p->pixels,span);if(!data)return;p->pixels=data;p->capacity=span;}
    p->raw=raw;p->stride=stride;p->width=width;p->height=height;p->content_token=content_token;memcpy(&p->lut,lut,sizeof *lut);
    p->ready=1;++p->submissions;(void)genesis_worker_start(p->worker,frame_color,p);
}
int genesis_frame_work_take(GenesisFrameWork *p,const uint32_t *raw,int stride,
    int width,int height,const ColorLut *lut,uint64_t content_token,uint32_t *staging,int storage_height)
{
    if(!p)return 0;genesis_frame_work_wait(p);
    size_t full=0;
    if(!p->ready)return 0;
    p->ready=0;
    if(raw!=p->raw || stride!=p->stride || width!=p->width || height!=p->height || content_token!=p->content_token ||
       !lut || !staging || memcmp(lut,&p->lut,sizeof *lut) || storage_height<height ||
       stride<=0 || stride>INT_MAX/4 || !genesis_pixel_span(stride*4,stride,storage_height,&full) ||
       genesis_pixel_overlap(raw,full,staging,full)) {++p->stale;return 0;}
    for(int y=0;y<height;y++)memcpy(staging+(size_t)y*(size_t)stride,
        p->pixels+(size_t)y*(size_t)stride,(size_t)width*4u);
    ++p->committed;return 1;
}
void genesis_frame_work_destroy(GenesisFrameWork *p)
{
    if(!p)return;genesis_frame_work_wait(p);
    fprintf(stderr,"[Engine work] mode=render jobs=%llu completed=%llu committed=%llu stale=%llu scratch_bytes=%llu\n",
        (unsigned long long)p->submissions,(unsigned long long)genesis_worker_completed(p->worker),
        (unsigned long long)p->committed,(unsigned long long)p->stale,(unsigned long long)p->capacity);
    genesis_worker_destroy(p->worker);free(p->pixels);free(p);
}
