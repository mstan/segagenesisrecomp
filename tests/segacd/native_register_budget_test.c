#include "native_register_budget.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
enum { VBLANK=109312, BUDGET_CHECK=255712, WATCHDOG=100 };
typedef struct State {
    uint32_t cycle,audio,seen,stalls;
    uint64_t instructions,base;
    int32_t budget;
    unsigned active,service,divisor,observable;
} State;
/* Ordinary own-backend retirement, including all state written by an empty
 * check_cycle_budget. A scaled-clock call is observable for qualification. */
static void retire(State*s,unsigned cycles,unsigned count)
{
    s->cycle+=cycles;s->audio+=cycles;s->instructions+=count;
    if(s->cycle<VBLANK)return;
    if(s->instructions-s->base>=WATCHDOG){++s->observable;return;}
    if(s->active&&!s->service&&s->cycle>=BUDGET_CHECK){
        if(s->divisor>1&&s->audio>s->seen){++s->observable;return;}
        s->stalls=0;
        if(s->audio>s->seen)s->budget-=(int32_t)(s->audio-s->seen);
        s->seen=s->audio;
        if(s->budget<=0)++s->observable;
    }
}
int main(void)
{
    const uint32_t cycles[]={0,109300,109312,255680,255700,255711,255712,255713,UINT32_MAX-64,UINT32_MAX-1,UINT32_MAX};
    const uint32_t audio[]={0,32,1000,UINT32_MAX-64,UINT32_MAX-1};
    const int32_t budgets[]={-1,0,1,4,12,64,1000};
    const uint64_t used[]={0,84,92,99,100,UINT64_MAX};
    unsigned long cases=0,hits=0,above=0;
    for(unsigned flags=0;flags<4;++flags)for(unsigned divisor=1;divisor<=2;++divisor)
    for(unsigned c=0;c<sizeof cycles/sizeof*cycles;++c)
    for(unsigned a=0;a<sizeof audio/sizeof*audio;++a)
    for(unsigned b=0;b<sizeof budgets/sizeof*budgets;++b)
    for(unsigned u=0;u<sizeof used/sizeof*used;++u)
    for(unsigned relation=0;relation<4;++relation)for(unsigned count=2;count<=8;++count){
        State before={0};before.cycle=cycles[c];before.audio=audio[a];
        before.seen=relation==0?0:relation==1?before.audio:relation==2?before.audio+10:before.audio-10;
        before.stalls=23;before.base=UINT64_MAX-3;before.instructions=before.base+used[u];
        before.budget=budgets[b];before.active=flags&1;before.service=(flags>>1)&1;before.divisor=divisor;
        unsigned total=0;for(unsigned n=0;n<count;++n)total+=4+(n%3)*2;
        int ready=genesis_native_register_budget(before.cycle,before.audio,before.instructions,
            before.base,WATCHDOG,BUDGET_CHECK,before.active,before.service,before.divisor,
            before.budget,before.seen,total,count);
        ++cases;if(!ready)continue;
        State ordinary=before,span=before;
        for(unsigned n=0;n<count;++n)retire(&ordinary,4+(n%3)*2,1);
        retire(&span,total,count);
        CHECK(!ordinary.observable&&!span.observable);
        CHECK(!memcmp(&ordinary,&span,sizeof ordinary));
        ++hits;if(before.cycle>=BUDGET_CHECK&&before.active&&!before.service)++above;
    }
    CHECK(hits&&above);
    printf("%lu budget states, %lu admitted spans, %lu above old deadline: exact watchdog/budget/seen/stalls exits pass\n",cases,hits,above);
    return 0;
}
