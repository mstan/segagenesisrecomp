#include "audio_mix.h"
#include "rf5c164.h"
#include "cdda.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"source span line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
void ref_rf5c164_init(Rf5c164 *);
void ref_rf5c164_write(Rf5c164 *,unsigned,uint8_t);
void ref_rf5c164_advance_timed(Rf5c164 *,uint64_t,uint32_t,Rf5c164TimedSink,void *);
void ref_segacd_cdda_reset(SegaCDCdda *,uint64_t,uint16_t);
void ref_segacd_cdda_play(SegaCDCdda *);
void ref_segacd_cdda_pause(SegaCDCdda *);
void ref_segacd_cdda_seek(SegaCDCdda *);
SegaCDCddaResult ref_segacd_cdda_set_fader_target_raw(SegaCDCdda *,uint16_t);
SegaCDCddaResult ref_segacd_cdda_push_raw_sector(SegaCDCdda *,uint64_t,int32_t,const uint8_t *);
SegaCDCddaResult ref_segacd_cdda_advance_to(SegaCDCdda *,uint64_t,SegaCDCddaSample,void *,uint64_t *);
void ref_mix_init(SegaCDAudioMix *);
SegaCDAudioMixResult ref_mix_set_active(SegaCDAudioMix *,SegaCDAudioSource,int);
SegaCDAudioMixResult ref_mix_push(SegaCDAudioMix *,SegaCDAudioSource,uint64_t,int16_t,int16_t);
SegaCDAudioMixResult ref_mix_set_window(SegaCDAudioMix *,uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,size_t);
SegaCDAudioMixResult ref_mix_check_window(const SegaCDAudioMix *,size_t,uint32_t);
SegaCDAudioMixResult ref_mix_render_window(SegaCDAudioMix *,int16_t *,size_t,uint32_t);
typedef struct {
    SegaCDAudioMix mix;
    SegaCDAudioSource source;
    Rf5c164 *pcm;
    SegaCDCdda *cdda;
    unsigned calls,grants,attempts;
    int fatal,decline;
} Capture;
static Capture a,b;
static Rf5c164 pa,pb;
static SegaCDCdda da,db;
static uint32_t random_state=0x928ad71u;
static uint32_t rnd(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void sample(void *user,uint64_t at,int16_t left,int16_t right)
{
    Capture *c=user;++c->calls;
    if(!c->fatal)c->fatal=segacd_audio_mix_push(&c->mix,c->source,at,left,right)!=SEGACD_AUDIO_MIX_OK;
}
static void opaque_sample(void *user,uint64_t at,int16_t left,int16_t right)
{
    Capture *c=user;sample(user,at,left,right);
    if(c->pcm){c->pcm->enabled^=1;c->pcm->channel[0].env^=7;}
    if(c->cdda){c->cdda->fader.target_field^=4;if(c->calls==3)segacd_cdda_pause(c->cdda);}
}
static int begin(void *user,uint64_t first,uint64_t last,size_t count,SegaCDAudioWriteGrant *grant)
{
    Capture *c=user;++c->attempts;
    return !c->decline&&!c->fatal&&segacd_audio_mix_begin_span(&c->mix,c->source,first,last,count,grant);
}
static int begin_constant(void *user,const SegaCDAudioConstant *run,size_t count,SegaCDAudioWriteGrant *grant)
{
    Capture *c=user;++c->attempts;
    return !c->decline&&!c->fatal&&segacd_audio_mix_begin_constant(&c->mix,c->source,run,count,grant);
}
static void commit(void *user,const SegaCDAudioWriteGrant *grant)
{
    Capture *c=user;CHECK(segacd_audio_mix_commit_span(&c->mix,grant)==SEGACD_AUDIO_MIX_OK);++c->grants;
}
static void prepare(Capture *c,SegaCDAudioSource source,uint64_t start,unsigned mode,unsigned head)
{
    memset(c,0,sizeof *c);c->source=source;segacd_audio_mix_init(&c->mix);
    CHECK(segacd_audio_mix_set_active(&c->mix,source,1)==SEGACD_AUDIO_MIX_OK);
    c->mix.source[source].head=(uint16_t)head;
    unsigned fill=mode==1?4095u:mode==2?4096u:1u;
    for(unsigned i=0;i<fill;++i)CHECK(segacd_audio_mix_push(&c->mix,source,start-fill+i,123,-321)==SEGACD_AUDIO_MIX_OK);
    if(mode==3)segacd_audio_mix_pause(&c->mix);
    if(mode==4)CHECK(segacd_audio_mix_set_active(&c->mix,source,0)==SEGACD_AUDIO_MIX_OK);
    if(mode==5)c->mix.source[source].last_subclock=start+1000000;
    if(mode==6)c->decline=1;
}
static void compare_mix(const SegaCDAudioMix *old,const SegaCDAudioMix *candidate)
{
    SegaCDAudioMix x=*old,y=*candidate;
    for(unsigned source=0;source<SEGACD_AUDIO_SOURCE_COUNT;++source) {
        CHECK(x.source[source].count==y.source[source].count);
        CHECK(x.source[source].head==y.source[source].head);
        for(unsigned i=0;i<x.source[source].count;++i) {
            SegaCDAudioFrame expected=old->source[source].frame[(old->source[source].head+i)&4095u],actual;
            CHECK(segacd_audio_mix_queue_frame(candidate,(SegaCDAudioSource)source,i,&actual));
            CHECK(expected.subclock==actual.subclock&&expected.left==actual.left&&expected.right==actual.right);
        }
        /* Physical host storage can differ. Compare ALL logical frames above
         * and every remaining result/cursor/counter/future-window field here. */
        memset(x.source[source].frame,0,sizeof x.source[source].frame);
        memset(y.source[source].frame,0,sizeof y.source[source].frame);
        memset(&x.source[source].run,0,sizeof x.source[source].run);
        memset(&y.source[source].run,0,sizeof y.source[source].run);
        x.source[source].run_first=x.source[source].run_count=0;
        y.source[source].run_first=y.source[source].run_count=0;
    }
    CHECK(!memcmp(&x,&y,sizeof x));
}
static void compare(void)
{CHECK(a.fatal==b.fatal);compare_mix(&a.mix,&b.mix);}

static void reference_run(SegaCDAudioMix *mix,SegaCDAudioSource source,
                           const SegaCDAudioConstant *run,unsigned count)
{
    for(unsigned i=0;i<count;++i) {
        uint64_t at=run->first+((uint64_t)run->phase+(uint64_t)i*run->step_numerator)/run->denominator;
        CHECK(ref_mix_push(mix,source,at,run->left,run->right)==SEGACD_AUDIO_MIX_OK);
    }
}

static void test_constant_mix(void)
{
    for(unsigned mode=0;mode<8;++mode) {
        ref_mix_init(&a.mix);segacd_audio_mix_init(&b.mix);
        for(unsigned source=0;source<2;++source) {
            SegaCDAudioSource id=(SegaCDAudioSource)source;
            CHECK(ref_mix_set_active(&a.mix,id,1)==SEGACD_AUDIO_MIX_OK);
            CHECK(segacd_audio_mix_set_active(&b.mix,id,1)==SEGACD_AUDIO_MIX_OK);
            a.mix.source[source].head=b.mix.source[source].head=(uint16_t)(4090u+source);
            CHECK(ref_mix_push(&a.mix,id,0,19,-37)==SEGACD_AUDIO_MIX_OK);
            CHECK(segacd_audio_mix_push(&b.mix,id,0,19,-37)==SEGACD_AUDIO_MIX_OK);
            int16_t l=(int16_t)(mode&1?32767:-32768),r=(int16_t)(mode&2?-30000:30000);
            if(source){l=(int16_t)(l>0?-32768:32767);r=(int16_t)-r;}
            if(mode==0)l=r=0;
            SegaCDAudioConstant run={1200u+source*200u,source?12345u:0u,
                source?12500000u:384u,source?44100u:1u,l,r};
            SegaCDAudioWriteGrant grant;
            reference_run(&a.mix,id,&run,144);
            CHECK(segacd_audio_mix_begin_constant(&b.mix,id,&run,144,&grant));
            CHECK(segacd_audio_mix_commit_span(&b.mix,&grant)==SEGACD_AUDIO_MIX_OK);
            CHECK(b.mix.source[source].run_count==144);
            for(unsigned i=144;i<148;++i) {
                uint64_t at=run.first+((uint64_t)run.phase+(uint64_t)i*run.step_numerator)/run.denominator;
                int16_t v=(int16_t)(i*701+mode*199);
                CHECK(ref_mix_push(&a.mix,id,at,v,(int16_t)-v)==SEGACD_AUDIO_MIX_OK);
                CHECK(segacd_audio_mix_push(&b.mix,id,at,v,(int16_t)-v)==SEGACD_AUDIO_MIX_OK);
            }
        }
        compare_mix(&a.mix,&b.mix);
        uint64_t clock=0;uint32_t phase=(mode*113123u)%715909u;
        for(unsigned window=0;window<16;++window) {
            CHECK(ref_mix_set_window(&a.mix,clock,phase,40000000u,715909u,223721u,64)==SEGACD_AUDIO_MIX_OK);
            CHECK(segacd_audio_mix_set_window(&b.mix,clock,phase,40000000u,715909u,223721u,64)==SEGACD_AUDIO_MIX_OK);
            int16_t x[128],y[128],original[128];
            for(unsigned i=0;i<128;++i)x[i]=y[i]=original[i]=(int16_t)(i*7919+window*41);
            CHECK(ref_mix_check_window(&a.mix,64,223721u)==segacd_audio_mix_check_window(&b.mix,64,223721u));
            SegaCDAudioMixResult expected=ref_mix_render_window(&a.mix,x,64,223721u);
            CHECK(segacd_audio_mix_render_window(&b.mix,y,64,223721u)==expected);
            CHECK(!memcmp(x,y,sizeof x));compare_mix(&a.mix,&b.mix);
            if(expected==SEGACD_AUDIO_MIX_NEED_INPUT) {
                CHECK(!memcmp(x,original,sizeof x));
                for(unsigned source=0;source<2;++source) {
                    uint64_t next=a.mix.source[source].last_subclock+100000u;
                    CHECK(ref_mix_push(&a.mix,(SegaCDAudioSource)source,next,12345,-23456)==SEGACD_AUDIO_MIX_OK);
                    CHECK(segacd_audio_mix_push(&b.mix,(SegaCDAudioSource)source,next,12345,-23456)==SEGACD_AUDIO_MIX_OK);
                }
                CHECK(ref_mix_render_window(&a.mix,x,64,223721u)==SEGACD_AUDIO_MIX_OK);
                CHECK(segacd_audio_mix_render_window(&b.mix,y,64,223721u)==SEGACD_AUDIO_MIX_OK);
                CHECK(!memcmp(x,y,sizeof x));compare_mix(&a.mix,&b.mix);
            } else CHECK(expected==SEGACD_AUDIO_MIX_OK);
            clock=a.mix.next_output_subclock;phase=a.mix.next_output_phase;
        }
    }
    /* Full logical capacity, wrapped storage, refusal transaction, algebraic
     * retirement, exact phase extension, then an audible future transition. */
    ref_mix_init(&a.mix);segacd_audio_mix_init(&b.mix);
    CHECK(ref_mix_set_active(&a.mix,SEGACD_AUDIO_SOURCE_PCM,1)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_set_active(&b.mix,SEGACD_AUDIO_SOURCE_PCM,1)==SEGACD_AUDIO_MIX_OK);
    a.mix.source[0].head=b.mix.source[0].head=4094;
    SegaCDAudioConstant run={0,0,2,1,1234,-4321};SegaCDAudioWriteGrant grant;
    reference_run(&a.mix,SEGACD_AUDIO_SOURCE_PCM,&run,4096);
    CHECK(segacd_audio_mix_begin_constant(&b.mix,SEGACD_AUDIO_SOURCE_PCM,&run,4096,&grant));
    CHECK(segacd_audio_mix_commit_span(&b.mix,&grant)==SEGACD_AUDIO_MIX_OK);compare_mix(&a.mix,&b.mix);
    static SegaCDAudioMix saved;saved=b.mix;
    run.first=8192;
    CHECK(!segacd_audio_mix_begin_constant(&b.mix,SEGACD_AUDIO_SOURCE_PCM,&run,1,&grant));
    CHECK(!memcmp(&saved,&b.mix,sizeof saved));
    CHECK(ref_mix_push(&a.mix,SEGACD_AUDIO_SOURCE_PCM,8192,0,0)==SEGACD_AUDIO_MIX_QUEUE_FULL);
    CHECK(segacd_audio_mix_push(&b.mix,SEGACD_AUDIO_SOURCE_PCM,8192,0,0)==SEGACD_AUDIO_MIX_QUEUE_FULL);compare_mix(&a.mix,&b.mix);
    CHECK(ref_mix_set_window(&a.mix,0,0,1,1,1000000u,4096)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_set_window(&b.mix,0,0,1,1,1000000u,4096)==SEGACD_AUDIO_MIX_OK);
    static int16_t x[32768],y[32768];memset(x,0,sizeof x);memset(y,0,sizeof y);
    CHECK(ref_mix_render_window(&a.mix,x,4096,1000000u)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_render_window(&b.mix,y,4096,1000000u)==SEGACD_AUDIO_MIX_OK);
    CHECK(!memcmp(x,y,sizeof x));compare_mix(&a.mix,&b.mix);
    reference_run(&a.mix,SEGACD_AUDIO_SOURCE_PCM,&run,1024);
    CHECK(segacd_audio_mix_begin_constant(&b.mix,SEGACD_AUDIO_SOURCE_PCM,&run,1024,&grant));
    CHECK(segacd_audio_mix_commit_span(&b.mix,&grant)==SEGACD_AUDIO_MIX_OK);compare_mix(&a.mix,&b.mix);
    CHECK(ref_mix_push(&a.mix,SEGACD_AUDIO_SOURCE_PCM,10240,700,-800)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&b.mix,SEGACD_AUDIO_SOURCE_PCM,10240,700,-800)==SEGACD_AUDIO_MIX_OK);
    CHECK(ref_mix_push(&a.mix,SEGACD_AUDIO_SOURCE_PCM,16384,-1900,2300)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&b.mix,SEGACD_AUDIO_SOURCE_PCM,16384,-1900,2300)==SEGACD_AUDIO_MIX_OK);
    CHECK(ref_mix_set_window(&a.mix,4096,0,1,1,1000000u,8192)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_set_window(&b.mix,4096,0,1,1,1000000u,8192)==SEGACD_AUDIO_MIX_OK);
    memset(x,0,sizeof x);memset(y,0,sizeof y);
    CHECK(ref_mix_render_window(&a.mix,x,8192,1000000u)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_render_window(&b.mix,y,8192,1000000u)==SEGACD_AUDIO_MIX_OK);
    CHECK(!memcmp(x,y,sizeof x));compare_mix(&a.mix,&b.mix);CHECK(!b.mix.source[0].run_count);
    run.first=16386;
    reference_run(&a.mix,SEGACD_AUDIO_SOURCE_PCM,&run,32);
    CHECK(segacd_audio_mix_begin_constant(&b.mix,SEGACD_AUDIO_SOURCE_PCM,&run,32,&grant));
    CHECK(segacd_audio_mix_commit_span(&b.mix,&grant)==SEGACD_AUDIO_MIX_OK);compare_mix(&a.mix,&b.mix);
    CHECK(ref_mix_push(&a.mix,SEGACD_AUDIO_SOURCE_PCM,30000,-10000,20000)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&b.mix,SEGACD_AUDIO_SOURCE_PCM,30000,-10000,20000)==SEGACD_AUDIO_MIX_OK);
    CHECK(ref_mix_set_window(&a.mix,12288,0,1,1,1000000u,8192)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_set_window(&b.mix,12288,0,1,1,1000000u,8192)==SEGACD_AUDIO_MIX_OK);
    memset(x,0,sizeof x);memset(y,0,sizeof y);
    CHECK(ref_mix_render_window(&a.mix,x,8192,1000000u)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_render_window(&b.mix,y,8192,1000000u)==SEGACD_AUDIO_MIX_OK);
    CHECK(!memcmp(x,y,sizeof x));compare_mix(&a.mix,&b.mix);
    puts("Constant source queue: frozen logical state/timestamps, mixed clipping, future transition, underflow, wrap/capacity/retirement PASS");
}

static void test_constant_devices(void)
{
    SegaCDAudioOwnedSink owned={sample,&b,&b,begin,commit,begin_constant};
    ref_rf5c164_init(&pa);rf5c164_init(&pb);
    memset(pa.wave,255,sizeof pa.wave);memset(pb.wave,255,sizeof pb.wave);
    pa.wave[5]=pb.wave[5]=0xC0;
    pa.enabled=pb.enabled=1;pa.phase=pb.phase=383;
    pa.channel[0].on=pb.channel[0].on=1;
    pa.channel[0].env=pb.channel[0].env=255;
    pa.channel[0].pan=pb.channel[0].pan=255;
    pa.channel[0].lsl=pb.channel[0].lsl=5;
    uint64_t clock=100000;
    prepare(&a,SEGACD_AUDIO_SOURCE_PCM,clock,0,4093);
    prepare(&b,SEGACD_AUDIO_SOURCE_PCM,clock,0,4093);
    ref_rf5c164_advance_timed(&pa,clock,384u*37u,sample,&a);
    rf5c164_advance_owned(&pb,clock,384u*37u,sample,&b,&owned);
    CHECK(!memcmp(&pa,&pb,sizeof pa));compare();CHECK(b.mix.source[0].run_count==37);
    clock+=384u*37u;
    /* Muting a moving voice must NOT freeze its authoritative address. */
    pa.channel[0].fdl=pb.channel[0].fdl=127;
    pa.channel[0].env=pb.channel[0].env=0;
    uint32_t old_address=pa.channel[0].address;
    ref_rf5c164_advance_timed(&pa,clock,384u*100u,sample,&a);
    rf5c164_advance_owned(&pb,clock,384u*100u,sample,&b,&owned);
    CHECK(!memcmp(&pa,&pb,sizeof pa));compare();CHECK(pa.channel[0].address!=old_address);
    clock+=384u*100u;pa.channel[0].env=pb.channel[0].env=255;
    ref_rf5c164_advance_timed(&pa,clock,384u*43u,sample,&a);
    rf5c164_advance_owned(&pb,clock,384u*43u,sample,&b,&owned);
    CHECK(!memcmp(&pa,&pb,sizeof pa));compare();

    uint8_t sector[2352],zero[2352]={0};
    for(unsigned i=0;i<588;++i) {
        sector[i*4]=0xE8;sector[i*4+1]=3; /* +1000 */
        sector[i*4+2]=0x30;sector[i*4+3]=0xF8; /* -2000 */
    }
    ref_segacd_cdda_reset(&da,100000,0x4000);segacd_cdda_reset(&db,100000,0x4000);
    ref_segacd_cdda_play(&da);segacd_cdda_play(&db);
    CHECK(ref_segacd_cdda_push_raw_sector(&da,da.clock_now,1,sector)==segacd_cdda_push_raw_sector(&db,db.clock_now,1,sector));
    CHECK(ref_segacd_cdda_push_raw_sector(&da,da.clock_now,2,sector)==segacd_cdda_push_raw_sector(&db,db.clock_now,2,sector));
    prepare(&a,SEGACD_AUDIO_SOURCE_CDDA,da.clock_now,0,4092);
    prepare(&b,SEGACD_AUDIO_SOURCE_CDDA,db.clock_now,0,4092);
    for(unsigned pass=0;pass<3;++pass) {
        uint64_t target=da.clock_now+(pass==0?200000u:pass==1?400000u:100000u),ea=0,eb=0;
        uint16_t fader=da.fader.current_field;
        CHECK(ref_segacd_cdda_advance_to(&da,target,sample,&a,&ea)==segacd_cdda_advance_owned(&db,target,sample,&b,&eb,&owned));
        CHECK(ea==eb&&!memcmp(&da,&db,sizeof da));compare();
        if(pass==0)CHECK(b.mix.source[1].run_count>600);
        if(pass==2)CHECK(!ea&&da.fader.current_field==fader); /* no fabricated underflow silence */
    }
    CHECK(ref_segacd_cdda_push_raw_sector(&da,da.clock_now,3,zero)==segacd_cdda_push_raw_sector(&db,db.clock_now,3,zero));
    da.fader.current_field=db.fader.current_field=100;
    da.fader.target_field=db.fader.target_field=1100;
    prepare(&a,SEGACD_AUDIO_SOURCE_CDDA,da.clock_now,0,4091);
    prepare(&b,SEGACD_AUDIO_SOURCE_CDDA,db.clock_now,0,4091);
    uint64_t ea=0,eb=0,target=da.clock_now+100000;
    CHECK(ref_segacd_cdda_advance_to(&da,target,sample,&a,&ea)==segacd_cdda_advance_owned(&db,target,sample,&b,&eb,&owned));
    CHECK(ea==eb&&!memcmp(&da,&db,sizeof da));compare();CHECK(b.mix.source[1].run_count>300);
    CHECK(da.fader.current_field>100); /* silent raw data still ramps the fader */
    CHECK(ref_segacd_cdda_push_raw_sector(&da,da.clock_now,4,sector)==segacd_cdda_push_raw_sector(&db,db.clock_now,4,sector));
    target=da.clock_now+200000;
    CHECK(ref_segacd_cdda_advance_to(&da,target,sample,&a,&ea)==segacd_cdda_advance_owned(&db,target,sample,&b,&eb,&owned));
    CHECK(ea==eb&&!memcmp(&da,&db,sizeof da));compare();
    puts("Constant devices: nonzero PCM/loop, muted moving voices, nonzero/zero CDDA, FIFO/underflow/fader and future audible state PASS");
}
int main(void)
{
    test_constant_mix();
    test_constant_devices();
    unsigned grants=0;
    ref_rf5c164_init(&pa);rf5c164_init(&pb);
    for(unsigned i=0;i<65536;++i)pa.wave[i]=pb.wave[i]=(uint8_t)rnd();
    for(unsigned k=0;k<1024;++k) {
        for(unsigned j=0;j<4;++j){unsigned reg=rnd()%9u;uint8_t value=(uint8_t)rnd();ref_rf5c164_write(&pa,reg,value);rf5c164_write(&pb,reg,value);}
        if(k%17u==0) {
            pa.enabled=pb.enabled=1;
            for(unsigned j=0;j<8;++j){pa.channel[j].on=pb.channel[j].on=1;pa.channel[j].env=pb.channel[j].env=255;pa.channel[j].pan=pb.channel[j].pan=255;}
        }
        uint64_t start=k&&k%256u==0?UINT64_MAX-1000u:100000u+(uint64_t)k*100000u;
        pa.phase=pb.phase=(uint16_t)(rnd()%384u);
        uint32_t clocks=k<4?k:500u+rnd()%100000u;
        unsigned mode=k%31u==0?(k/31u)%7u:0u,head=rnd()&4095u;
        prepare(&a,SEGACD_AUDIO_SOURCE_PCM,start,mode,head);prepare(&b,SEGACD_AUDIO_SOURCE_PCM,start,mode,head);
        SegaCDAudioOwnedSink owned={sample,&b,&b,begin,commit,begin_constant};
        if(k%43u==0)owned.legacy_user=&a;
        Rf5c164TimedSink sink=sample;
        if(k%29u==0){sink=opaque_sample;a.pcm=&pa;b.pcm=&pb;}
        ref_rf5c164_advance_timed(&pa,start,clocks,sink,&a);
        rf5c164_advance_owned(&pb,start,clocks,sink,&b,&owned);
        CHECK(!memcmp(&pa,&pb,sizeof pa));compare();
        if(sink!=sample||owned.legacy_user!=&b)CHECK(!b.attempts);
        grants+=b.grants;
    }
    static const unsigned fields[]={0,1,2,3,4,5,6,7,8,1023,1024,1025,1027,1028,2047,65535};
    static const uint32_t clocks[]={0,1,282,283,284,567,568,1000,100000,340000,500000};
    uint8_t sector[2352],sector2[2352];
    for(unsigned i=0;i<2352;++i)sector[i]=(uint8_t)rnd();
    static const int16_t extremes[]={-32768,32767,-128,128,-1,1,0,0,-129,129,-32767,32766};
    for(unsigned i=0;i<sizeof extremes/sizeof extremes[0];++i) {
        uint16_t value=(uint16_t)extremes[i];sector[2u*i]=(uint8_t)value;sector[2u*i+1u]=(uint8_t)(value>>8);
    }
    for(unsigned i=0;i<2352;++i)sector2[i]=(uint8_t)(sector[i]^0xA5u);
    for(unsigned k=0;k<1024;++k) {
        uint64_t start=200000u+(uint64_t)k*100000u;
        ref_segacd_cdda_reset(&da,start,0x4000);segacd_cdda_reset(&db,start,0x4000);
        unsigned transport=(k/256u)%4u;
        if(transport>=2u){ref_segacd_cdda_play(&da);segacd_cdda_play(&db);}
        if(transport==3u) {
            CHECK(ref_segacd_cdda_push_raw_sector(&da,start,19,sector)==SEGACD_CDDA_OK);
            CHECK(segacd_cdda_push_raw_sector(&db,start,19,sector)==SEGACD_CDDA_OK);
            if(k&16u) {
                CHECK(ref_segacd_cdda_push_raw_sector(&da,start,20,sector2)==SEGACD_CDDA_OK);
                CHECK(segacd_cdda_push_raw_sector(&db,start,20,sector2)==SEGACD_CDDA_OK);
            }
        }
        if(transport==1u){ref_segacd_cdda_pause(&da);segacd_cdda_pause(&db);}
        da.phase=db.phase=rnd()%SEGACD_CDDA_CLOCK;
        da.fader.current_field=db.fader.current_field=(uint16_t)fields[k%16u];
        da.fader.target_field=db.fader.target_field=(uint16_t)fields[(k/16u)%16u];
        if(k%37u==0)da.status=db.status=SEGACD_CDDA_STATUS_DEEMPHASIS_UNSUPPORTED;
        unsigned mode=k%31u==0?(k/31u)%7u:0u,head=rnd()&4095u;
        prepare(&a,SEGACD_AUDIO_SOURCE_CDDA,start,mode,head);prepare(&b,SEGACD_AUDIO_SOURCE_CDDA,start,mode,head);
        SegaCDAudioOwnedSink owned={sample,&b,&b,begin,commit,begin_constant};
        if(k%43u==0)owned.legacy_user=&a;
        SegaCDCddaSample sink=sample;
        if(k%29u==0){sink=opaque_sample;a.cdda=&da;b.cdda=&db;}
        uint64_t emitted_a=123,emitted_b=123,target=start+clocks[k%11u];
        uint64_t *ea=k%101u==0?&da.phase:&emitted_a,*eb=k%101u==0?&db.phase:&emitted_b;
        SegaCDCddaResult ra=ref_segacd_cdda_advance_to(&da,target,sink,&a,ea);
        SegaCDCddaResult rb=segacd_cdda_advance_owned(&db,target,sink,&b,eb,&owned);
        CHECK(ra==rb&&*ea==*eb);CHECK(!memcmp(&da,&db,sizeof da));compare();
        if(sink!=sample||owned.legacy_user!=&b)CHECK(!b.attempts);
        grants+=b.grants;
    }
    /* Reuse the same state and queue through partial advances and real
     * sector/fader/transport discontinuities, including FIFO ring reuse. */
    ref_segacd_cdda_reset(&da,1000000u,0x4000);segacd_cdda_reset(&db,1000000u,0x4000);
    ref_segacd_cdda_play(&da);segacd_cdda_play(&db);
    CHECK(ref_segacd_cdda_push_raw_sector(&da,da.clock_now,40,sector)==SEGACD_CDDA_OK);
    CHECK(segacd_cdda_push_raw_sector(&db,db.clock_now,40,sector)==SEGACD_CDDA_OK);
    CHECK(ref_segacd_cdda_push_raw_sector(&da,da.clock_now,41,sector2)==SEGACD_CDDA_OK);
    CHECK(segacd_cdda_push_raw_sector(&db,db.clock_now,41,sector2)==SEGACD_CDDA_OK);
    prepare(&a,SEGACD_AUDIO_SOURCE_CDDA,1000000u,0,4090);prepare(&b,SEGACD_AUDIO_SOURCE_CDDA,1000000u,0,4090);
    for(unsigned k=0;k<20;++k) {
        if(k==5||k==13) {
            uint16_t raw=k==5?0x3ff0u:0;
            CHECK(ref_segacd_cdda_set_fader_target_raw(&da,raw)==segacd_cdda_set_fader_target_raw(&db,raw));
        }
        if(k==10){ref_segacd_cdda_pause(&da);segacd_cdda_pause(&db);}
        if(k==11||k==19){ref_segacd_cdda_play(&da);segacd_cdda_play(&db);}
        if(k==16){ref_segacd_cdda_seek(&da);segacd_cdda_seek(&db);}
        if(k==8||k==11||k==15||k==17||k==19)
            CHECK(ref_segacd_cdda_push_raw_sector(&da,da.clock_now,(int32_t)(50+k),sector2)==
                  segacd_cdda_push_raw_sector(&db,db.clock_now,(int32_t)(50+k),sector2));
        SegaCDAudioOwnedSink owned={sample,&b,&b,begin,commit,begin_constant};
        SegaCDCddaSample sink=sample;unsigned attempts=b.attempts;
        if(k==18){sink=opaque_sample;a.cdda=&da;b.cdda=&db;a.calls=b.calls=0;}
        uint64_t ea=0,eb=0,target=da.clock_now+20000u+k*1000u;
        CHECK(ref_segacd_cdda_advance_to(&da,target,sink,&a,&ea)==segacd_cdda_advance_owned(&db,target,sink,&b,&eb,&owned));
        CHECK(ea==eb&&!memcmp(&da,&db,sizeof da));compare();
        if(k==18)CHECK(b.attempts==attempts);
    }
    CHECK(grants>100u);
    /* Real machines also service between adjacent sample ticks. A one-clock
     * interval crossing one tick must publish through ownership, not remain a
     * fixture-only multi-sample optimization. */
    ref_rf5c164_init(&pa);rf5c164_init(&pb);pa.phase=pb.phase=383;
    prepare(&a,SEGACD_AUDIO_SOURCE_PCM,100000,0,4095);prepare(&b,SEGACD_AUDIO_SOURCE_PCM,100000,0,4095);
    SegaCDAudioOwnedSink single={sample,&b,&b,begin,commit,begin_constant};
    ref_rf5c164_advance_timed(&pa,100000,1,sample,&a);
    rf5c164_advance_owned(&pb,100000,1,sample,&b,&single);
    CHECK(!memcmp(&pa,&pb,sizeof pa));compare();CHECK(b.grants==1&&!b.calls);grants+=b.grants;
    ref_segacd_cdda_reset(&da,100000,0x4000);segacd_cdda_reset(&db,100000,0x4000);
    ref_segacd_cdda_play(&da);segacd_cdda_play(&db);
    CHECK(ref_segacd_cdda_push_raw_sector(&da,100000,7,sector)==segacd_cdda_push_raw_sector(&db,100000,7,sector));
    da.phase=db.phase=SEGACD_CDDA_CLOCK-1;
    prepare(&a,SEGACD_AUDIO_SOURCE_CDDA,100000,0,4095);prepare(&b,SEGACD_AUDIO_SOURCE_CDDA,100000,0,4095);
    CHECK(ref_segacd_cdda_advance_to(&da,100001,sample,&a,NULL)==segacd_cdda_advance_owned(&db,100001,sample,&b,NULL,&single));
    CHECK(!memcmp(&da,&db,sizeof da));compare();CHECK(b.grants==1&&!b.calls);grants+=b.grants;
    printf("Owned PCM/CDDA spans: frozen complete state/stamps/PCM, fader/FIFO/underflow, queue prefix/faults and stale/reentrant callbacks PASS (%u owned jobs)\n",grants);
    return 0;
}
