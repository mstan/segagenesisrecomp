#include "cdda.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
typedef struct {uint64_t at;int16_t l,r;} Frame;
typedef struct {Frame *frame;size_t count,capacity;} Trace;
static Frame records_a[44120],records_b[44120];
static void sink(void*u,uint64_t at,int16_t l,int16_t r){Trace*t=u;CHECK(t->count<t->capacity);t->frame[t->count++]=(Frame){at,l,r};}
static uint64_t ceil_div(uint64_t n,uint64_t d){return n/d+(n%d!=0);}
static int16_t decode(uint16_t x){int32_t n=x<0x8000u?(int32_t)x:(int32_t)x-65536;return(int16_t)n;}
static uint16_t pattern_left(unsigned n){return(uint16_t)(n%32768u);}
static uint16_t pattern_right(unsigned n){return(uint16_t)(0x10000u-((n%30000u)+1u));}
static void make_sector(uint8_t raw[2352],unsigned sector){for(unsigned j=0;j<SEGACD_CDDA_SECTOR_FRAMES;++j){unsigned n=sector*SEGACD_CDDA_SECTOR_FRAMES+j;uint16_t l=pattern_left(n),r=pattern_right(n);raw[j*4]=(uint8_t)l;raw[j*4+1]=(uint8_t)(l>>8);raw[j*4+2]=(uint8_t)r;raw[j*4+3]=(uint8_t)(r>>8);}}
static SegaCDCddaResult advance_split(SegaCDCdda*s,uint64_t target,Trace*t,int split){if(!split)return segacd_cdda_advance_to(s,target,sink,t,NULL);static const unsigned chunks[]={1,17,101,283,384,1009,31};unsigned i=0;while(s->clock_now<target){uint64_t n=chunks[i++%7],end=s->clock_now+n;if(end>target)end=target;SegaCDCddaResult r=segacd_cdda_advance_to(s,end,sink,t,NULL);if(r!=SEGACD_CDDA_OK)return r;}return SEGACD_CDDA_OK;}
static void run_trace(unsigned quant_phase,uint64_t base,int split,Trace*t,SegaCDCdda*out)
{
 SegaCDCdda s;uint8_t raw[2352];segacd_cdda_reset(&s,base,0x4000);segacd_cdda_play(&s);make_sector(raw,0);CHECK(segacd_cdda_push_raw_sector(&s,base,0,raw)==SEGACD_CDDA_OK);
 for(unsigned k=1;k<=75;++k){uint64_t q=(uint64_t)quant_phase*25u;uint64_t event=base+ceil_div((uint64_t)k*SEGACD_CDDA_CLOCK+q,75u)-ceil_div(q,75u);CHECK(event>s.clock_now);uint64_t before=s.delivered_frames;CHECK(advance_split(&s,event,t,split)==SEGACD_CDDA_OK);uint64_t elapsed=event-base;CHECK(s.delivered_frames==elapsed*SEGACD_CDDA_RATE/SEGACD_CDDA_CLOCK);CHECK(s.delivered_frames<=((uint64_t)k*SEGACD_CDDA_SECTOR_FRAMES));CHECK(s.delivered_frames-before<=SEGACD_CDDA_SECTOR_FRAMES+1u);make_sector(raw,k);CHECK(segacd_cdda_push_raw_sector(&s,event,(int32_t)k,raw)==SEGACD_CDDA_OK);}
 *out=s;
}
static void test_rate_phases_and_splits(void)
{
 for(unsigned q=0;q<3;++q){Trace a={records_a,0,44120},b={records_b,0,44120};SegaCDCdda sa,sb;uint64_t base=1000u+q;
  run_trace(q,base,0,&a,&sa);run_trace(q,base,1,&b,&sb);
  CHECK(a.count==44100&&b.count==44100);CHECK(sa.total_sample_ticks==44100&&sb.total_sample_ticks==44100);
  CHECK(sa.delivered_frames==44100&&sb.delivered_frames==44100&&!sa.underflow_frames&&!sb.underflow_frames);
  CHECK(sa.sectors_accepted==76&&sa.fifo_count==1&&sa.fifo_frame==0);
  for(size_t n=0;n<a.count;++n){CHECK(a.frame[n].at==b.frame[n].at);CHECK(a.frame[n].l==b.frame[n].l);CHECK(a.frame[n].r==b.frame[n].r);}
  CHECK(memcmp(&sa,&sb,sizeof sa)==0);
  for(unsigned n=0;n<44100;++n){CHECK(a.frame[n].at==base+ceil_div((uint64_t)(n+1)*SEGACD_CDDA_CLOCK,SEGACD_CDDA_RATE));CHECK(a.frame[n].l==decode(pattern_left(n)));CHECK(a.frame[n].r==decode(pattern_right(n)));}
  CHECK(a.frame[587].l==decode(pattern_left(587)));CHECK(a.frame[588].l==decode(pattern_left(588)));
 }
}
static void test_extremes_fader_and_first_period(void)
{
 SegaCDCdda s;Trace t={records_a,0,44120};uint8_t raw[2352]={0};uint64_t frames=99;
 segacd_cdda_reset(&s,12345,0x4000);segacd_cdda_play(&s);raw[0]=0;raw[1]=0x80;raw[2]=0xFF;raw[3]=0x7F;
 CHECK(segacd_cdda_push_raw_sector(&s,12345,7,raw)==SEGACD_CDDA_OK);
 CHECK(segacd_cdda_set_fader_target_raw(&s,0x0000)==SEGACD_CDDA_OK);CHECK(s.fader_raw_target==0&&segacd_fader_target_raw(&s.fader)==0&&segacd_fader_current_raw(&s.fader)==0x4000);
 CHECK(segacd_cdda_advance_to(&s,12628,sink,&t,&frames)==SEGACD_CDDA_OK&&frames==0&&t.count==0);
 CHECK(segacd_cdda_advance_to(&s,12629,sink,&t,&frames)==SEGACD_CDDA_OK&&frames==1&&t.count==1);
 CHECK(t.frame[0].at==12629&&t.frame[0].l==INT16_MIN&&t.frame[0].r==INT16_MAX);
 CHECK(segacd_fader_current_raw(&s.fader)==0x3FF0&&s.total_sample_ticks==1&&s.delivered_frames==1);
}
static void test_queue_pause_seek_reset_and_underflow(void)
{
 SegaCDCdda s;Trace t={records_a,0,44120};uint8_t a[2352],b[2352];uint64_t frames;
 segacd_cdda_reset(&s,1000,0x4000);segacd_cdda_play(&s);make_sector(a,0);make_sector(b,9);
 CHECK(segacd_cdda_push_raw_sector(&s,1000,0,a)==SEGACD_CDDA_OK);
 CHECK(segacd_cdda_push_raw_sector(&s,1000,1,b)==SEGACD_CDDA_OK);
 segacd_cdda_play(&s);CHECK(s.fifo_count==2&&s.source_anchored); /* PLAY while already PLAYING is idempotent. */
 CHECK(segacd_cdda_push_raw_sector(&s,1000,2,a)==SEGACD_CDDA_OVERFLOW&&s.fifo_count==2&&s.sectors_accepted==2&&(s.status&SEGACD_CDDA_STATUS_OVERFLOW));
 segacd_cdda_reset(&s,1000,0x4000);segacd_cdda_play(&s);CHECK(segacd_cdda_push_raw_sector(&s,1000,0,a)==SEGACD_CDDA_OK);
 CHECK(segacd_cdda_advance_to(&s,1100,sink,&t,&frames)==SEGACD_CDDA_OK&&frames==0);
 uint64_t ticks=s.total_sample_ticks,delivered=s.delivered_frames,sectors=s.sectors_accepted;
 segacd_cdda_pause(&s);CHECK(s.fifo_count==0&&!s.source_anchored&&s.transport==SEGACD_CDDA_PAUSED);
 CHECK(segacd_cdda_advance_to(&s,12000,sink,&t,&frames)==SEGACD_CDDA_OK);
 CHECK(t.count>0&&s.total_sample_ticks>ticks&&s.delivered_frames>delivered&&s.sectors_accepted==sectors);
 for(size_t n=0;n<t.count;++n)CHECK(t.frame[n].l==0&&t.frame[n].r==0);
 segacd_cdda_play(&s);make_sector(b,20);uint64_t base=s.clock_now;
 CHECK(segacd_cdda_push_raw_sector(&s,base,20,b)==SEGACD_CDDA_OK);
 CHECK(segacd_cdda_advance_to(&s,base+283,sink,&t,&frames)==SEGACD_CDDA_OK&&frames==0);
 CHECK(segacd_cdda_advance_to(&s,base+284,sink,&t,&frames)==SEGACD_CDDA_OK&&frames==1);
 CHECK(t.frame[t.count-1].l==decode(pattern_left(20u*588u)));
 segacd_cdda_seek(&s);CHECK(!s.fifo_count&&!s.source_anchored&&s.delivered_frames>delivered);
 base=s.clock_now;make_sector(a,30);CHECK(segacd_cdda_push_raw_sector(&s,base,30,a)==SEGACD_CDDA_OK);
 CHECK(segacd_cdda_advance_to(&s,base+284,sink,&t,&frames)==SEGACD_CDDA_OK&&frames==1);
 CHECK(t.frame[t.count-1].l==decode(pattern_left(30u*588u)));
 segacd_cdda_clear_for_data(&s);CHECK(!s.fifo_count&&!s.source_anchored);
 base=s.clock_now;make_sector(b,40);CHECK(segacd_cdda_push_raw_sector(&s,base,40,b)==SEGACD_CDDA_OK);
 CHECK(segacd_cdda_advance_to(&s,base+284,sink,&t,&frames)==SEGACD_CDDA_OK&&frames==1);
 CHECK(t.frame[t.count-1].l==decode(pattern_left(40u*588u)));
 segacd_cdda_stop(&s);CHECK(s.transport==SEGACD_CDDA_STOPPED&&!s.fifo_count);
 ticks=s.total_sample_ticks;segacd_cdda_reset(&s,777,0x4000);
 CHECK(s.clock_now==777&&s.epoch_origin==777&&!s.total_sample_ticks&&!s.delivered_frames&&!s.underflow_frames&&!s.silence_frames&&!s.sectors_accepted);
 CHECK(ticks>0);

 Trace under={records_b,0,44120};segacd_cdda_reset(&s,2000,0x4000);segacd_cdda_play(&s);make_sector(a,0);
 CHECK(segacd_cdda_push_raw_sector(&s,2000,0,a)==SEGACD_CDDA_OK);
 CHECK(segacd_cdda_advance_to(&s,2000+167000,sink,&under,&frames)==SEGACD_CDDA_UNDERFLOW);
 CHECK(under.count==588&&frames==588&&s.underflow_frames>0&&(s.status&SEGACD_CDDA_STATUS_UNDERFLOW));
 CHECK(s.total_sample_ticks>under.count); /* missing frames clocked, never emitted as fake silence */
}
static void test_unsupported_modes_and_fader_flags(void)
{
 SegaCDCdda s;Trace t={records_a,0,4};uint8_t raw[2352]={0};uint64_t frames=99;
 segacd_cdda_reset(&s,0,0x4000);segacd_cdda_play(&s);
 CHECK(segacd_cdda_set_modes(&s,1,0)==SEGACD_CDDA_UNSUPPORTED);
 CHECK(s.status&SEGACD_CDDA_STATUS_DEEMPHASIS_UNSUPPORTED);
 CHECK(segacd_cdda_advance_to(&s,100,sink,&t,&frames)==SEGACD_CDDA_UNSUPPORTED&&s.clock_now==0&&t.count==0);
 segacd_cdda_reset(&s,0,0x4000);CHECK(segacd_cdda_set_modes(&s,0,1)==SEGACD_CDDA_UNSUPPORTED);
 CHECK(s.status&SEGACD_CDDA_STATUS_DOUBLE_SPEED_UNSUPPORTED);
 segacd_cdda_reset(&s,0,0x4000);CHECK(segacd_cdda_set_fader_target_raw(&s,0x4002)==SEGACD_CDDA_UNSUPPORTED);
 CHECK(s.status&SEGACD_CDDA_STATUS_DOUBLE_SPEED_UNSUPPORTED);
 CHECK(segacd_cdda_set_fader_target_raw(&s,0x4004)==SEGACD_CDDA_UNSUPPORTED);
 CHECK(s.status&SEGACD_CDDA_STATUS_DEEMPHASIS_UNSUPPORTED);
 segacd_cdda_reset(&s,0,0x4000);segacd_cdda_stop(&s);
 CHECK(segacd_cdda_push_raw_sector(&s,0,0,raw)==SEGACD_CDDA_NOT_PLAYING);
}
int main(void)
{
 test_rate_phases_and_splits();test_extremes_fader_and_first_period();
 test_queue_pause_seek_reset_and_underflow();test_unsupported_modes_and_fader_flags();
 puts("CDDA stream rational cadence, queue, transport, fader and unsupported-mode tests passed");return 0;
}
