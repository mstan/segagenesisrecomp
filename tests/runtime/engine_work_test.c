#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "engine_worker.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line%d: %s; %s\n",__LINE__,#x,SDL_GetError());exit(1);}}while(0)

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
int main(void)
{
    SDL_SetMainReady();CHECK(SDL_Init(SDL_INIT_TIMER)==0);worker_lifetime();SDL_Quit();
    puts("E18 worker ownership, join, destruction and synchronous fallback checks passed");return 0;
}
