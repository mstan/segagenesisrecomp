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
SegaCDCddaResult ref_segacd_cdda_push_raw_sector(SegaCDCdda *,uint64_t,int32_t,const uint8_t *);
SegaCDCddaResult ref_segacd_cdda_advance_to(SegaCDCdda *,uint64_t,SegaCDCddaSample,void *,uint64_t *);
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
static void compare(void)
{CHECK(a.fatal==b.fatal);CHECK(!memcmp(&a.mix,&b.mix,sizeof a.mix));}
int main(void)
{
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
        SegaCDAudioOwnedSink owned={sample,&b,&b,begin,commit};
        Rf5c164TimedSink sink=sample;
        if(k%29u==0){sink=opaque_sample;a.pcm=&pa;b.pcm=&pb;}
        ref_rf5c164_advance_timed(&pa,start,clocks,sink,&a);
        rf5c164_advance_owned(&pb,start,clocks,sink,&b,&owned);
        CHECK(!memcmp(&pa,&pb,sizeof pa));compare();
        if(sink!=sample)CHECK(!b.attempts);
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
        SegaCDAudioOwnedSink owned={sample,&b,&b,begin,commit};
        SegaCDCddaSample sink=sample;
        if(k%29u==0){sink=opaque_sample;a.cdda=&da;b.cdda=&db;}
        uint64_t emitted_a=123,emitted_b=123,target=start+clocks[k%11u];
        uint64_t *ea=k%101u==0?&da.phase:&emitted_a,*eb=k%101u==0?&db.phase:&emitted_b;
        SegaCDCddaResult ra=ref_segacd_cdda_advance_to(&da,target,sink,&a,ea);
        SegaCDCddaResult rb=segacd_cdda_advance_owned(&db,target,sink,&b,eb,&owned);
        CHECK(ra==rb&&*ea==*eb);CHECK(!memcmp(&da,&db,sizeof da));compare();
        if(sink!=sample)CHECK(!b.attempts);
        grants+=b.grants;
    }
    CHECK(grants>100u);
    printf("Owned PCM/CDDA spans: frozen complete state/stamps/PCM, fader/FIFO/underflow, queue prefix/faults and stale/reentrant callbacks PASS (%u owned jobs)\n",grants);
    return 0;
}
