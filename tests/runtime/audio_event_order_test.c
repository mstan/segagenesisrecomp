#include "audio/event_order.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
typedef struct {AudioEvent e;uint32_t idx;} OriginalEvent;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"event order line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int compare(const void *pa,const void *pb)
{
    const OriginalEvent *a=pa,*b=pb;
    if(a->e.cycle_stamp!=b->e.cycle_stamp)return a->e.cycle_stamp<b->e.cycle_stamp?-1:1;
    return a->idx<b->idx?-1:a->idx>b->idx?1:0;
}
static void reference(OriginalEvent *events,size_t n,int pairguard)
{
    if(pairguard)for(size_t i=0;i<n;++i) {
        unsigned p=events[i].e.port;
        if(p==AUDIO_PORT_FM1_ADDR||p==AUDIO_PORT_FM2_ADDR)for(size_t j=i+1;j<n;++j) {
            unsigned q=events[j].e.port;
            if(q==p+1u){events[i].e.cycle_stamp=events[j].e.cycle_stamp;break;}
            if(q==p)break;
        }
    }
    qsort(events,n,sizeof *events,compare);
}
static uint32_t seed=0x23567121;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void check(const AudioEvent *source,size_t n,int pairguard)
{
    AudioEvent *actual=malloc((n+1)*sizeof *actual),*scratch=malloc((n+1)*sizeof *scratch);
    OriginalEvent *old=malloc((n+1)*sizeof *old);CHECK(actual&&scratch&&old);
    for(size_t i=0;i<n;++i){actual[i]=source[i];old[i].e=source[i];old[i].idx=(uint32_t)i;}
    reference(old,n,pairguard);audio_event_order(actual,scratch,n,pairguard);
    for(size_t i=0;i<n;++i)CHECK(actual[i].cycle_stamp==old[i].e.cycle_stamp&&actual[i].port==old[i].e.port&&actual[i].value==old[i].e.value);
    free(actual);free(scratch);free(old);
}
int main(void)
{
    AudioEvent *events=calloc(262144,sizeof *events);CHECK(events);
    audio_event_order(NULL,NULL,0,1);
    for(unsigned trial=0;trial<1200;++trial) {
        size_t n=rnd()%2049u;
        for(size_t i=0;i<n;++i) {
            events[i].port=(uint8_t)(rnd()%5u);events[i].value=(uint8_t)rnd();
            events[i].cycle_stamp=trial%3u==0?rnd()%11u:trial%3u==1?(uint32_t)i/2u:rnd();
        }
        check(events,n,0);check(events,n,1);
    }
    /* Complete giant-handler capacity, equal keys, backwards axes, relatches,
     * orphan ADDRESS before a long PSG run, and more than two sorted runs. */
    for(unsigned mode=0;mode<5;++mode) {
        for(unsigned i=0;i<262144;++i) {
            events[i].port=(uint8_t)(mode==3?(i?AUDIO_PORT_PSG:AUDIO_PORT_FM1_ADDR):i%5u);
            events[i].value=(uint8_t)i;
            events[i].cycle_stamp=mode==0?i:mode==1?UINT32_MAX-i:mode==2?7u:mode==3?i:i%9701u;
        }
        check(events,262144,0);check(events,262144,1);
    }
    free(events);puts("audio timeline: stable old-order parity, FM pairs/relatches/orphans, wrapped stamps and full262144-event bursts PASS");return 0;
}
