/* Full device-state/Word RAM differential against the frozen pre-E14 device.
 * This is an execution oracle, not a second copy of the new cursor formula. */
#include "segacd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u, case %u: %s\n",(unsigned)__LINE__,case_id,#x);exit(1);}}while(0)
void stamp_reference_reset(SegaCDStamp *);
void stamp_reference_latch(SegaCDStamp *,unsigned,uint16_t);
void stamp_reference_start(SegaCDStamp *,uint16_t);
int stamp_reference_advance(SegaCDStamp *,uint8_t *,unsigned,uint32_t);
static SegaCDStamp a,b,saved;
static uint8_t ra[0x40000],rb[0x40000],snapshot[0x40000];
static uint32_t rng=0xE14CDAFEu;
static unsigned case_id;
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void same(void){CHECK(!memcmp(&a,&b,sizeof a));CHECK(!memcmp(ra,rb,sizeof ra));}
static void latch(unsigned reg,uint16_t v){segacd_stamp_latch(&a,reg,v);stamp_reference_latch(&b,reg,v);}
static void advance(unsigned priority,uint32_t clocks){int x=segacd_stamp_advance(&a,ra,priority,clocks);int y=stamp_reference_advance(&b,rb,priority,clocks);CHECK(x==y);same();}
static void put(unsigned off,unsigned value){ra[off&0x3FFFFu]=rb[off&0x3FFFFu]=(uint8_t)(value>>8);ra[(off+1)&0x3FFFFu]=rb[(off+1)&0x3FFFFu]=(uint8_t)value;}
static void begin(unsigned control,unsigned width,unsigned height,unsigned dest,unsigned vector)
{
    segacd_stamp_reset(&a);stamp_reference_reset(&b);
    latch(0,(uint16_t)control);latch(1,(uint16_t)random32());
    latch(2,(uint16_t)random32());latch(3,(uint16_t)(dest>>2));
    latch(4,(uint16_t)random32());latch(5,(uint16_t)width);latch(6,(uint16_t)height);
    segacd_stamp_start(&a,(uint16_t)(vector>>2));stamp_reference_start(&b,(uint16_t)(vector>>2));same();
}
static void random_jobs(void)
{
    for(case_id=0;case_id<2048;++case_id){
        for(unsigned i=0;i<sizeof ra;++i)ra[i]=(uint8_t)random32();
        memcpy(rb,ra,sizeof ra);
        unsigned vector=random32()&0x3FFF8u,dest=(case_id&3u)==0?vector:random32()&0x3FFE0u;
        unsigned width=case_id<512?case_id:random32()&511u,height=case_id%9u;
        begin(case_id&7u,width,height,dest,vector);
        /* Latched register writes must not alter a running job. */
        for(unsigned reg=0;reg<8;++reg)latch(reg,(uint16_t)random32());
        unsigned steps=0;
        while(a.busy){
            unsigned priority=random32()%5u;
            unsigned clocks=steps%4u==0?0u:steps%4u==1?1u:random32()%(a.line_clocks*2u+1u);
            advance(priority,clocks);
            /* Live RAM/priority changes between device slices; save and resume
             * using only current-build device state, with no retained pointers. */
            if((steps++%3u)==0){unsigned off=random32()&0x3FFFFu;ra[off]=rb[off]=(uint8_t)random32();}
            if(steps==2){saved=a;memcpy(snapshot,ra,sizeof ra);advance(priority,1);a=saved;b=saved;memcpy(ra,snapshot,sizeof ra);memcpy(rb,snapshot,sizeof rb);same();}
            CHECK(steps<2000);
        }
        advance(3,UINT32_MAX);
    }
}
static void feedback_jobs(void)
{
    /* Output aliases map words, art, its own trace data and the NEXT line's
     * trace data. Coordinate increments cross cells/maps and wrap negatively. */
    for(unsigned kind=0;kind<4;++kind)for(unsigned control=0;control<8;++control)
    for(unsigned priority=0;priority<4;++priority)for(unsigned offset=0;offset<8;++offset){
        ++case_id;for(unsigned i=0;i<sizeof ra;++i)ra[i]=(uint8_t)(i*37u+(i>>9));memcpy(rb,ra,sizeof ra);
        begin(control,511,3,kind==0?0x4000u:kind==1?0x80u:kind==2?0x6000u:0x6008u,0x6000);
        /* Set active geometry through a fresh start, not implementation-private cursors. */
        latch(1,0x1000);latch(2,31);latch(4,(uint16_t)offset);
        segacd_stamp_start(&a,0x1800);stamp_reference_start(&b,0x1800);
        for(unsigned i=0;i<3;++i){put(0x6000+8*i,0);put(0x6002+8*i,0);put(0x6004+8*i,i==1?0xF800u:0x0C00u);put(0x6006+8*i,0x0400);}
        put(0x4000,1u|((offset&7u)<<13));advance(priority,a.line_clocks*3u);
    }
    /* Maximum supported dimensions / output wrap, and zero dimensions. */
    for(unsigned i=0;i<8;++i){++case_id;memset(ra,0xAF,sizeof ra);memcpy(rb,ra,sizeof ra);begin(i,511,255,0x3FFE0,0x3FFF8);advance(i&3u,UINT32_MAX);}
}
static SegaCD cd,control_cd,restored;
static uint8_t bios[SEGACD_BIOS_BYTES];
static void ownership_and_snapshot(void)
{
    CHECK(segacd_init(&cd,bios,sizeof bios));cd.memory_mode=2;
    for(unsigned i=0;i<sizeof cd.word;++i)cd.word[i]=(uint8_t)(i*37u);
    for(unsigned reg=0;reg<7;++reg)segacd_stamp_latch(&cd.stamp,reg,(uint16_t)(reg==5?31u:reg==6?9u:reg==3?0x4000u:reg==1?0x1000u:0u));
    segacd_stamp_start(&cd.stamp,0x1800);control_cd=cd;
    segacd_advance(&cd,154);CHECK(cd.stamp.line==0 && cd.stamp.remaining_clocks==1);
    for(unsigned mode=1;mode<=5;mode+=4){SegaCDStamp before=cd.stamp;uint8_t word[0x40000];memcpy(word,cd.word,sizeof word);cd.memory_mode=(uint8_t)mode;segacd_advance(&cd,500);CHECK(!memcmp(&before,&cd.stamp,sizeof before));CHECK(!memcmp(word,cd.word,sizeof word));}
    cd.memory_mode=2;restored=cd;
    segacd_advance(&cd,1241);for(unsigned i=0;i<1241;++i)segacd_advance(&restored,1);
    CHECK(!cd.stamp.busy && !memcmp(&cd,&restored,sizeof cd));
    segacd_advance(&control_cd,1395);CHECK(!memcmp(&cd.stamp,&control_cd.stamp,sizeof cd.stamp));CHECK(!memcmp(cd.word,control_cd.word,sizeof cd.word));
}
int main(void){random_jobs();feedback_jobs();ownership_and_snapshot();printf("Stamp line jobs: %u cases, full state/RAM, live alias feedback, split clocks, priority, ownership and current-build snapshots pass\n",case_id);return 0;}
