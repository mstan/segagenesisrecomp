#include "segacd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"CHECK failed %s:%d: %s\n",__FILE__,__LINE__,#c);exit(1);} } while(0)
void reference_segacd_advance_io(SegaCD *,uint32_t,const SegaCDIO *);
static uint8_t bios[SEGACD_BIOS_BYTES];
static SegaCD before,actual,reference;
static unsigned cases;

static void check_phase(unsigned phase,uint32_t clocks)
{
    SegaCDDma a,b;
    memset(&a,0xa5,sizeof a);a.active=0;a.phase=(uint8_t)phase;
    b=a;
    SegaCDDmaResult result=segacd_dma_advance(&before,&b,clocks);
    segacd_dma_idle_clock(&a,clocks);
    CHECK(a.phase==(uint8_t)(((uint64_t)phase+clocks)%6u));
    CHECK(result==a.last_result&&memcmp(&a,&b,sizeof a)==0);
    ++cases;
}
static void test_phase_domain(void)
{
    static const uint32_t clocks[]={0,1,2,3,4,5,6,7,11,12,383,384,385,
        0x7fffffffu,0x80000000u,0xffffff00u,0xffffff01u,0xffffff04u,
        0xfffffffau,0xfffffffbu,0xfffffffcu,0xfffffffdu,0xfffffffeu,0xffffffffu};
    for(unsigned phase=0;phase<256;++phase) {
        for(unsigned n=0;n<sizeof clocks/sizeof clocks[0];++n)check_phase(phase,clocks[n]);
        /* Both sides of each phase-dependent unsigned carry threshold. */
        if(phase){check_phase(phase,UINT32_MAX-phase);check_phase(phase,UINT32_MAX-phase+1u);}
    }
    uint32_t random=0x6843a2c1u;
    for(unsigned n=0;n<65536;++n) {
        random=random*1664525u+1013904223u;
        check_phase((random>>16)&255u,random);
    }
}
typedef struct {
    uint64_t clock,words,waits;
    uint32_t address;
    int16_t left,right;
    uint16_t fifo;
    uint8_t phase,active,kind;
} Event;
typedef struct {SegaCD *cd;Event events[128];unsigned count,action,source,done;} Observer;
static void sample(Observer *o,unsigned kind,uint64_t clock,int16_t left,int16_t right)
{
    SegaCD *s=o->cd;CHECK(o->count<128);
    Event *e=&o->events[o->count++];
    e->clock=clock;e->words=s->dma.words;e->waits=s->dma.waits;e->address=s->dma.address;
    e->left=left;e->right=right;e->fifo=s->cdc.fifo_count;
    e->phase=s->dma.phase;e->active=s->dma.active;e->kind=(uint8_t)kind;
    if(kind!=o->source||o->done)return;
    o->done=1;
    if(o->action==1) {
        s->dma.phase=5;CHECK(segacd_dma_begin(s,&s->dma)==SEGACD_DMA_OK);
    } else if(o->action==2) {
        s->dma.active=0;s->dma.phase=255;s->dma.last_result=SEGACD_DMA_WAIT;
        s->dma.words=0x12345678;s->dma.waits=0x12345679;
    } else if(o->action==3) {
        s->dma.active=0;s->dma.phase=4;s->dma.last_result=SEGACD_DMA_DISABLED;
    }
}
static void timed_pcm(void *u,uint64_t t,int16_t l,int16_t r){sample(u,1,t,l,r);}
static void legacy_pcm(void *u,int16_t l,int16_t r){sample(u,2,0,l,r);}
static void cdda(void *u,uint64_t t,int16_t l,int16_t r){sample(u,3,t,l,r);}
static void setup(unsigned dest,unsigned mode,unsigned active)
{
    CHECK(segacd_init(&before,bios,sizeof bios));
    before.cdc_destination=(uint8_t)dest;before.cdc.ifctrl=0x42;
    before.cdc.dbc=31;before.cdc.dac=16376;
    for(unsigned n=0;n<32;++n)before.cdc.ram[(16376+n)&16383u]=(uint8_t)(0x40+n);
    before.cdc_dma_address=0xffffu;
    CHECK(segacd_dma_begin(&before,&before.dma)==SEGACD_DMA_OK);
    segacd_cdc_address(&before.cdc,6);segacd_cdc_write(&before.cdc,1);
    before.dma.active=(uint8_t)active;before.dma.phase=3;
    before.memory_mode=(uint8_t)mode;before.write_protect=1;before.pcm.bank=7;
    before.irq_mask=0x3e;before.timer_reload=1;before.pcm.phase=383;
    /* A one-clock stamp completion competes with DMA/device callbacks. */
    before.stamp.busy=1;before.stamp.remaining_clocks=1;
}
static void compare_device(unsigned action,unsigned source,int split)
{
    actual=reference=before;
    Observer a={0},b={0};a.cd=&actual;b.cd=&reference;
    a.action=b.action=action;a.source=b.source=source;
    SegaCDIO ia={0},ib={0};
    ia.pcm_sink=ib.pcm_sink=legacy_pcm;ia.pcm_user=&a;ib.pcm_user=&b;
    if(source!=2){ia.pcm_timed_sink=ib.pcm_timed_sink=timed_pcm;ia.pcm_timed_user=&a;ib.pcm_timed_user=&b;}
    ia.cdda_sample=ib.cdda_sample=cdda;ia.cdda_sample_user=&a;ib.cdda_sample_user=&b;
    static const uint32_t deltas[]={0,1,2,3,5,6,17,349,1,383,384,385,1536};
    if(split)for(unsigned n=0;n<sizeof deltas/sizeof deltas[0];++n){
        segacd_advance_io(&actual,deltas[n],&ia);reference_segacd_advance_io(&reference,deltas[n],&ib);
        CHECK(memcmp(&actual,&reference,sizeof actual)==0);
    } else {
        segacd_advance_io(&actual,3072,&ia);reference_segacd_advance_io(&reference,3072,&ib);
    }
    CHECK(a.count==b.count&&a.count>0&&a.done&&b.done);
    CHECK(memcmp(a.events,b.events,sizeof a.events)==0);
    CHECK(memcmp(&actual,&reference,sizeof actual)==0);
    if(action==1)CHECK(actual.dma.words>0||actual.dma.waits>0);
    ++cases;
}
static void test_device_boundaries(void)
{
    static const unsigned modes[]={0,1,4,5};
    for(unsigned dest=4;dest<=7;++dest)if(dest!=6)
        for(unsigned m=0;m<4;++m)for(unsigned source=1;source<=3;++source)
            for(unsigned action=0;action<=3;++action)for(unsigned split=0;split<2;++split) {
                setup(dest,modes[m],action==3||action==0);
                compare_device(action,source,(int)split);
            }
}
int main(void)
{
    test_phase_domain();test_device_boundaries();
    printf("Inactive DMA spans: %u full-state/phase cases, PCM/CDDA callback activation and active/WAIT/protected/wrap ordering pass\n",cases);
    return 0;
}
