#include "../../runner/cd/audio_mix.h"
#include "../../runner/cd/rf5c164.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr,"CHECK failed at %s:%d: %s\n",__FILE__,__LINE__,#condition); \
        return 1; \
    } \
} while(0)

typedef struct {uint64_t stamp[8];unsigned count;} TimedCapture;

static void timed_capture(void *user,uint64_t subclock,int16_t left,int16_t right)
{
    TimedCapture *capture=(TimedCapture *)user;
    if(capture->count<8u)capture->stamp[capture->count]=subclock;
    if(left==right)++capture->count;
    else capture->count=99u;
}

static int feed_linear(SegaCDAudioMix *mix,SegaCDAudioSource source)
{
    CHECK(segacd_audio_mix_set_active(mix,source,1)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(mix,source,0,0,0)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(mix,source,100,1000,1000)==SEGACD_AUDIO_MIX_OK);
    return 0;
}

static int test_linear_mix_and_transactional_underflow(void)
{
    SegaCDAudioMix mix;
    segacd_audio_mix_init(&mix);
    CHECK(feed_linear(&mix,SEGACD_AUDIO_SOURCE_PCM)==0);
    CHECK(segacd_audio_mix_set_active(&mix,SEGACD_AUDIO_SOURCE_CDDA,1)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_CDDA,0,-100,200)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_CDDA,200,-100,200)==SEGACD_AUDIO_MIX_OK);

    int16_t output[8]={7,9,7,9,7,9,7,9};
    CHECK(segacd_audio_mix_set_window(&mix,0,0,50,1,223721,4)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_render_window(&mix,output,4,223721)==SEGACD_AUDIO_MIX_NEED_INPUT);
    CHECK(output[0]==7 && output[1]==9 && output[6]==7 && output[7]==9);
    CHECK(mix.source[SEGACD_AUDIO_SOURCE_PCM].count==2u);
    CHECK(mix.faults&SEGACD_AUDIO_FAULT_UNDERFLOW);

    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,200,2000,2000)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,300,3000,3000)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_CDDA,300,-100,200)==SEGACD_AUDIO_MIX_OK);
    segacd_audio_mix_clear_faults(&mix,SEGACD_AUDIO_FAULT_UNDERFLOW);
    CHECK(segacd_audio_mix_render_window(&mix,output,4,223721)==SEGACD_AUDIO_MIX_OK);
    CHECK(output[0]==-93 && output[1]==209);
    CHECK(output[2]==407 && output[3]==709);
    CHECK(output[4]==907 && output[5]==1209);
    CHECK(output[6]==1407 && output[7]==1709);
    CHECK(mix.source[SEGACD_AUDIO_SOURCE_PCM].count==3u);
    CHECK(mix.source[SEGACD_AUDIO_SOURCE_CDDA].count==3u);
    return 0;
}

static int test_clip_and_inactive_silence(void)
{
    SegaCDAudioMix mix;
    segacd_audio_mix_init(&mix);
    CHECK(segacd_audio_mix_set_active(&mix,SEGACD_AUDIO_SOURCE_PCM,1)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_set_active(&mix,SEGACD_AUDIO_SOURCE_CDDA,1)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,0,30000,-30000)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,100,30000,-30000)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_CDDA,0,20000,-20000)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_CDDA,100,20000,-20000)==SEGACD_AUDIO_MIX_OK);
    int16_t output[4]={10000,-10000,1,2};
    CHECK(segacd_audio_mix_set_window(&mix,0,0,25,1,223721,2)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_render_window(&mix,output,2,223721)==SEGACD_AUDIO_MIX_OK);
    CHECK(output[0]==INT16_MAX && output[1]==INT16_MIN);

    segacd_audio_mix_reset(&mix);
    CHECK(segacd_audio_mix_set_active(&mix,SEGACD_AUDIO_SOURCE_PCM,0)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_set_active(&mix,SEGACD_AUDIO_SOURCE_CDDA,0)==SEGACD_AUDIO_MIX_OK);
    int16_t base[4]={123,-456,32767,-32768};
    CHECK(segacd_audio_mix_set_window(&mix,200,0,25,1,223721,2)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_render_window(&mix,base,2,223721)==SEGACD_AUDIO_MIX_OK);
    CHECK(base[0]==123 && base[1]==-456 && base[2]==32767 && base[3]==-32768);
    return 0;
}

static int test_overflow_order_pause_and_reset(void)
{
    SegaCDAudioMix mix;
    segacd_audio_mix_init(&mix);
    CHECK(segacd_audio_mix_set_active(&mix,SEGACD_AUDIO_SOURCE_PCM,1)==SEGACD_AUDIO_MIX_OK);
    for(unsigned i=0;i<SEGACD_AUDIO_MIX_QUEUE_CAPACITY;++i)
        CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,(uint64_t)i*384u,1,-1)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,((uint64_t)SEGACD_AUDIO_MIX_QUEUE_CAPACITY)*384u,2,-2)==SEGACD_AUDIO_MIX_QUEUE_FULL);
    CHECK(mix.source[SEGACD_AUDIO_SOURCE_PCM].count==SEGACD_AUDIO_MIX_QUEUE_CAPACITY);
    CHECK(mix.faults&SEGACD_AUDIO_FAULT_OVERFLOW);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,0,0,0)==SEGACD_AUDIO_MIX_BAD_TIMESTAMP);
    CHECK(mix.source[SEGACD_AUDIO_SOURCE_PCM].count==SEGACD_AUDIO_MIX_QUEUE_CAPACITY);

    segacd_audio_mix_pause(&mix);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,2000000,0,0)==SEGACD_AUDIO_MIX_PAUSED);
    CHECK(segacd_audio_mix_set_active(&mix,SEGACD_AUDIO_SOURCE_PCM,1)==SEGACD_AUDIO_MIX_PAUSED);
    segacd_audio_mix_resume(&mix);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,2000000,0,0)==SEGACD_AUDIO_MIX_NOT_ACTIVE);
    CHECK(segacd_audio_mix_set_active(&mix,SEGACD_AUDIO_SOURCE_PCM,1)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,2000000,0,0)==SEGACD_AUDIO_MIX_OK);
    CHECK(mix.source[SEGACD_AUDIO_SOURCE_PCM].discarded_frames==SEGACD_AUDIO_MIX_QUEUE_CAPACITY);
    return 0;
}

static int test_audio_extra_adapter_and_rational_window(void)
{
    SegaCDAudioMix mix;
    segacd_audio_mix_init(&mix);
    CHECK(segacd_audio_mix_set_active(&mix,SEGACD_AUDIO_SOURCE_PCM,1)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,1000,100,200)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,1100,200,300)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,1200,300,400)==SEGACD_AUDIO_MIX_OK);
    int16_t out[4]={0,0,0,0};
    CHECK(segacd_audio_mix_bind_audio_extra(&mix)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_set_window(&mix,1000,0,40000000,715909,223721,2)==SEGACD_AUDIO_MIX_OK);
    segacd_audio_mix_audio_extra_source(out,2,223721.0);
    CHECK(mix.last_result==SEGACD_AUDIO_MIX_OK);
    CHECK(out[0]==100 && out[1]==200);
    CHECK(out[2]>=155 && out[2]<=156 && out[3]>=255 && out[3]<=256);
    segacd_audio_mix_unbind_audio_extra(&mix);
    return 0;
}

static int test_rational_window_continuity(void)
{
    SegaCDAudioMix mix;
    uint64_t next_clock=0;
    uint32_t next_phase=0,next_denominator=0;
    int16_t output[4]={1,2,3,4};
    segacd_audio_mix_init(&mix);

    CHECK(segacd_audio_mix_set_window(&mix,1000,0,3,2,223721,2)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_set_window(&mix,2000,0,3,2,223721,2)==SEGACD_AUDIO_MIX_WINDOW_MISMATCH);
    CHECK(mix.window.pending);
    CHECK(segacd_audio_mix_render_window(&mix,output,2,223721)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_next_output_position(&mix,&next_clock,&next_phase,&next_denominator));
    CHECK(next_clock==1003u && next_phase==0u && next_denominator==2u);

    CHECK(segacd_audio_mix_set_window(&mix,next_clock,next_phase,3,2,223721,2)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_render_window(&mix,output,2,223721)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_next_output_position(&mix,&next_clock,&next_phase,&next_denominator));
    CHECK(next_clock==1006u && next_phase==0u && next_denominator==2u);
    CHECK(segacd_audio_mix_set_window(&mix,1007,0,3,2,223721,2)==SEGACD_AUDIO_MIX_WINDOW_MISMATCH);
    CHECK(mix.faults&SEGACD_AUDIO_FAULT_WINDOW);

    segacd_audio_mix_reset(&mix);
    CHECK(!segacd_audio_mix_next_output_position(&mix,&next_clock,&next_phase,&next_denominator));
    return 0;
}

static int test_audio_extra_underflow_is_reported(void)
{
    SegaCDAudioMix mix;
    int16_t output[4]={11,-12,13,-14};
    segacd_audio_mix_init(&mix);
    CHECK(segacd_audio_mix_set_active(&mix,SEGACD_AUDIO_SOURCE_CDDA,1)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_bind_audio_extra(&mix)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_set_window(&mix,500,0,40000000,715909,223721,2)==SEGACD_AUDIO_MIX_OK);
    segacd_audio_mix_audio_extra_source(output,2,223721.0);
    CHECK(mix.last_result==SEGACD_AUDIO_MIX_NEED_INPUT);
    CHECK(mix.faults&SEGACD_AUDIO_FAULT_UNDERFLOW);
    CHECK(output[0]==11 && output[1]==-12 && output[2]==13 && output[3]==-14);
    CHECK(!mix.window.pending);
    segacd_audio_mix_unbind_audio_extra(&mix);
    return 0;
}

static int test_window_preflight_is_read_only(void)
{
    SegaCDAudioMix mix;
    int16_t output[4]={31,-32,33,-34};
    segacd_audio_mix_init(&mix);
    CHECK(segacd_audio_mix_set_active(&mix,SEGACD_AUDIO_SOURCE_PCM,1)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_push(&mix,SEGACD_AUDIO_SOURCE_PCM,0,100,200)==SEGACD_AUDIO_MIX_OK);
    CHECK(segacd_audio_mix_set_window(&mix,50,0,50,1,223721,2)==SEGACD_AUDIO_MIX_OK);
    SegaCDAudioQueue queue_before=mix.source[SEGACD_AUDIO_SOURCE_PCM];
    uint32_t faults_before=mix.faults;
    uint64_t underflows_before=mix.underflow_blocks;
    SegaCDAudioMixResult last_before=mix.last_result;
    CHECK(segacd_audio_mix_check_window(&mix,2,223721)==SEGACD_AUDIO_MIX_NEED_INPUT);
    CHECK(mix.source[SEGACD_AUDIO_SOURCE_PCM].head==queue_before.head);
    CHECK(mix.source[SEGACD_AUDIO_SOURCE_PCM].count==queue_before.count);
    CHECK(mix.source[SEGACD_AUDIO_SOURCE_PCM].last_subclock==queue_before.last_subclock);
    CHECK(mix.faults==faults_before && mix.underflow_blocks==underflows_before);
    CHECK(mix.last_result==last_before && mix.window.pending);
    CHECK(output[0]==31 && output[1]==-32 && output[2]==33 && output[3]==-34);
    CHECK(segacd_audio_mix_render_window(&mix,output,2,223721)==SEGACD_AUDIO_MIX_NEED_INPUT);
    CHECK(mix.underflow_blocks==underflows_before+1u);
    CHECK(mix.source[SEGACD_AUDIO_SOURCE_PCM].count==queue_before.count);
    CHECK(output[0]==31 && output[1]==-32 && output[2]==33 && output[3]==-34);
    return 0;
}

static int test_exact_rf5c164_timestamps(void)
{
    Rf5c164 pcm;
    TimedCapture capture={{0},0};
    rf5c164_init(&pcm);
    rf5c164_advance_timed(&pcm,100,383,timed_capture,&capture);
    CHECK(capture.count==0u);
    rf5c164_advance_timed(&pcm,483,1,timed_capture,&capture);
    CHECK(capture.count==1u && capture.stamp[0]==484u);
    rf5c164_advance_timed(&pcm,484,768,timed_capture,&capture);
    CHECK(capture.count==3u);
    CHECK(capture.stamp[1]==868u && capture.stamp[2]==1252u);
    return 0;
}

int main(void)
{
    if(test_linear_mix_and_transactional_underflow())return 1;
    if(test_clip_and_inactive_silence())return 1;
    if(test_overflow_order_pause_and_reset())return 1;
    if(test_audio_extra_adapter_and_rational_window())return 1;
    if(test_rational_window_continuity())return 1;
    if(test_audio_extra_underflow_is_reported())return 1;
    if(test_window_preflight_is_read_only())return 1;
    if(test_exact_rf5c164_timestamps())return 1;
    puts("Segacd audio mix tests passed");
    return 0;
}
