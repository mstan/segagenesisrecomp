#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "engine_worker.h"
#include "frame_work.h"
#include "video/genesis_frame_pixels.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line%d: %s; %s\n",__LINE__,#x,SDL_GetError());exit(1);}}while(0)
const uint32_t *original_prepare(const void *,const uint32_t *,uint32_t *,int,int,int,int,int,int);
typedef struct Job {unsigned count;SDL_threadID owner,observed;} Job;
static void count_job(void *context)
{Job *j=(Job *)context;++j->count;j->observed=SDL_ThreadID();}
static void delayed_job(void *context){SDL_Delay(1);count_job(context);}
static void worker_lifetime(void)
{
    Job j={0,SDL_ThreadID(),0};
    CHECK(!genesis_worker_start(NULL,count_job,&j)&&j.count==1&&j.observed==j.owner);
    GenesisWorker *w=genesis_worker_create();
#if GENESIS_ENGINE_WORKERS
    CHECK(w);
    for(unsigned i=0;i<100;i++){CHECK(genesis_worker_start(w,count_job,&j));genesis_worker_join(w);CHECK(j.count==i+2&&j.observed!=j.owner);}
    CHECK(genesis_worker_completed(w)==100);
    CHECK(genesis_worker_start(w,count_job,&j));CHECK(genesis_worker_start(w,count_job,&j));
    genesis_worker_join(w);CHECK(j.count==103&&genesis_worker_completed(w)==102);
    CHECK(genesis_worker_start(w,delayed_job,&j));genesis_worker_destroy(w);CHECK(j.count==104);
#else
    CHECK(!w);CHECK(!genesis_worker_start(w,count_job,&j)&&j.count==2);
    CHECK(genesis_worker_completed(w)==0);genesis_worker_join(w);genesis_worker_destroy(w);
#endif
}
static uint32_t random_word(uint32_t *state){*state=*state*1664525u+1013904223u;return *state;}
static void frame_ownership(void)
{
    enum {STRIDE=520,HEIGHT=480};size_t bytes=STRIDE*HEIGHT*sizeof(uint32_t);
    uint32_t *raw=(uint32_t *)malloc(bytes),*saved=(uint32_t *)malloc(bytes),
        *expected=(uint32_t *)malloc(bytes),*actual=(uint32_t *)malloc(bytes);
    CHECK(raw&&saved&&expected&&actual);uint32_t seed=919;ColorLut lut;memset(&lut,0,sizeof lut);
    for(unsigned i=0;i<512;i++)lut.table[i]=random_word(&seed);
    for(size_t i=0;i<bytes/4;i++){raw[i]=random_word(&seed);expected[i]=actual[i]=random_word(&seed);}
    memcpy(saved,raw,bytes);GenesisFrameWork *p=genesis_frame_work_create();
#if GENESIS_ENGINE_WORKERS
    CHECK(p);uint64_t token=1;const int sizes[][2]={{320,224},{511,449},{256,224},{1,1},{448,448},{320,240}};
    for(unsigned i=0;i<sizeof sizes/sizeof sizes[0];i++){
        int width=sizes[i][0],height=sizes[i][1];
        genesis_frame_work_begin(p,raw,STRIDE,width,height,&lut,token);genesis_frame_work_wait(p);
        CHECK(!memcmp(actual,expected,bytes)&&!memcmp(raw,saved,bytes));
        CHECK(genesis_frame_work_take(p,raw,STRIDE,width,height,&lut,token,actual,HEIGHT));
        CHECK(original_prepare(&lut,raw,expected,STRIDE,HEIGHT,width,height,1,0)==expected);
        CHECK(!memcmp(actual,expected,bytes)&&!memcmp(raw,saved,bytes));
        CHECK(!genesis_frame_work_take(p,raw,STRIDE,width,height,&lut,token,actual,HEIGHT));
        ++token;
    }
    /* Same pointer and geometry are insufficient after an intervening write. */
    genesis_frame_work_begin(p,raw,STRIDE,320,224,&lut,token);genesis_frame_work_wait(p);
    raw[17]^=0x204080u;++token;
    CHECK(!genesis_frame_work_take(p,raw,STRIDE,320,224,&lut,token,actual,HEIGHT));
    CHECK(!memcmp(actual,expected,bytes));memcpy(saved,raw,bytes);
    genesis_frame_work_begin(p,raw,STRIDE,320,224,&lut,token);genesis_frame_work_wait(p);
    ColorLut changed=lut;changed.table[7]^=1;
    CHECK(!genesis_frame_work_take(p,raw,STRIDE,320,224,&changed,token,actual,HEIGHT));
    CHECK(!memcmp(actual,expected,bytes));
    genesis_frame_work_begin(p,raw,STRIDE,511,449,&lut,token);genesis_frame_work_wait(p);
    CHECK(!genesis_frame_work_take(p,raw,STRIDE,320,224,&lut,token,actual,HEIGHT));
    CHECK(!memcmp(actual,expected,bytes));
    genesis_frame_work_begin(p,raw,STRIDE,320,224,&lut,token);genesis_frame_work_wait(p);
    CHECK(!genesis_frame_work_take(p,raw,STRIDE,320,224,&lut,token,raw,HEIGHT));CHECK(!memcmp(raw,saved,bytes));
    genesis_frame_work_begin(p,raw,STRIDE,320,224,&lut,token);
    CHECK(!genesis_frame_work_take(p,raw,STRIDE,320,224,&lut,token,actual,223));CHECK(!memcmp(actual,expected,bytes));
    lut.passthrough=true;genesis_frame_work_begin(p,raw,STRIDE,320,224,&lut,token);
    CHECK(!genesis_frame_work_take(p,raw,STRIDE,320,224,&lut,token,actual,HEIGHT));CHECK(!memcmp(actual,expected,bytes));
    lut.passthrough=false;genesis_frame_work_begin(p,raw,INT_MAX,320,224,&lut,token);
    CHECK(!genesis_frame_work_take(p,raw,STRIDE,320,224,&lut,token,actual,HEIGHT));
    genesis_frame_work_begin(p,raw,STRIDE,320,224,&lut,token);genesis_frame_work_destroy(p);
#else
    CHECK(!p);genesis_frame_work_begin(p,raw,STRIDE,320,224,&lut,1);genesis_frame_work_wait(p);
    CHECK(!genesis_frame_work_take(p,raw,STRIDE,320,224,&lut,1,actual,HEIGHT));
    CHECK(!memcmp(actual,expected,bytes));genesis_frame_work_destroy(p);
#endif
    free(actual);free(expected);free(saved);free(raw);
}
int main(void)
{
    SDL_SetMainReady();CHECK(SDL_Init(SDL_INIT_TIMER)==0);worker_lifetime();frame_ownership();SDL_Quit();
    puts("E18 worker ownership/fallback and independent original color/history/lifecycle checks passed");return 0;
}
