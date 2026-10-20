#include "../../runner/cd/audio_native.h"
#include "../../runner/audio.h"
#include "../../runner/audio/event_queue.h"
#include "../../runner/audio/sn76489.h"
#include "../../runner/audio/ym2612.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr,"CHECK failed at %s:%d: %s\n",__FILE__,__LINE__,#condition); \
        return 1; \
    } \
} while(0)

typedef struct {uint64_t clock;uint8_t port,value;} WriteRecord;
static WriteRecord writes[16];
static unsigned write_count;
static uint64_t fm_clock,psg_clock;
static size_t fm_samples,psg_samples;
static uint64_t fm_generated,psg_generated;
static int16_t fm_buffer[64*2],psg_buffer[256];
static size_t psg_test_limit=256u;
static AudioExtraSource extra_source;
static unsigned flush_count;
static uint32_t flushed_wall_frame;
static size_t flushed_fm_frames,flushed_psg_frames;
static int16_t flushed_stereo[256*2];

int audio_init(int rate){return rate==223721?0:-1;}
void audio_close(void){}
void audio_set_extra_source(AudioExtraSource source){extra_source=source;}
void audio_flush(uint32_t wall_frame,int realtime,const int16_t *fm,size_t fm_frames,
                 const int16_t *psg,size_t psg_frames)
{
    (void)realtime;
    flushed_wall_frame=wall_frame;
    flushed_fm_frames=fm_frames;
    flushed_psg_frames=psg_frames;
    if(fm_frames&&fm)memcpy(fm_buffer,fm,fm_frames*2u*sizeof(int16_t));
    for(size_t i=0;i<psg_frames;++i) {
        flushed_stereo[i*2u]=psg[i];
        flushed_stereo[i*2u+1u]=psg[i];
    }
    if(extra_source)extra_source(flushed_stereo,psg_frames,223721.0);
    ++flush_count;
}

void ym2612_init(void){fm_clock=0;fm_samples=0;fm_generated=0;write_count=0;}
void ym2612_advance(uint32_t cycles)
{
    fm_clock+=cycles;
    while(fm_generated<fm_clock/1008u) {
        fm_buffer[2u*fm_samples]=fm_buffer[2u*fm_samples+1u]=0;
        ++fm_samples;++fm_generated;
    }
}
void ym2612_write(uint8_t port,uint8_t value)
{
    if(write_count<16u){writes[write_count].clock=fm_clock;writes[write_count].port=port;writes[write_count].value=value;++write_count;}
}
size_t ym2612_render(int16_t *out,size_t count)
{
    size_t available=fm_samples;
    if(count>available)count=available;
    if(count)memcpy(out,fm_buffer,count*2u*sizeof(int16_t));
    if(count<fm_samples)memmove(fm_buffer,fm_buffer+count*2u,(fm_samples-count)*2u*sizeof(int16_t));
    fm_samples-=count;
    return count;
}
size_t ym2612_samples_available(void){return fm_samples;}
uint32_t ym2612_sample_rate(void){return 53267u;}

static uint8_t psg_level;
void psg_init(void){psg_clock=0;psg_samples=0;psg_generated=0;psg_level=0;}
void psg_advance(uint32_t cycles)
{
    psg_clock+=cycles;
    while(psg_generated<psg_clock/240u) {
        if(psg_samples<psg_test_limit)psg_buffer[psg_samples++]=psg_level;
        ++psg_generated;
    }
}
void psg_write(uint8_t value)
{
    if(write_count<16u){writes[write_count].clock=psg_clock;writes[write_count].port=AUDIO_PORT_PSG;writes[write_count].value=value;++write_count;}
    psg_level=(uint8_t)value;
}
size_t psg_render(int16_t *out,size_t count)
{
    if(count>psg_samples)count=psg_samples;
    if(count)memcpy(out,psg_buffer,count*sizeof(int16_t));
    if(count<psg_samples)memmove(psg_buffer,psg_buffer+count,(psg_samples-count)*sizeof(int16_t));
    psg_samples-=count;
    return count;
}
size_t psg_samples_available(void){return psg_samples;}
uint32_t psg_sample_rate(void){return 223721u;}

static int test_order_defer_and_frame_late_mix(void)
{
    SegaCDNativeAudio *audio=segacd_native_audio_create(0);
    SegaCDNativeAudioStats stats;
    flush_count=0;write_count=0;
    CHECK(audio!=NULL);
    segacd_native_audio_pcm_sample(audio,1000,100,100);
    segacd_native_audio_pcm_sample(audio,2000,200,200);
    for(uint64_t stamp=300;stamp<=2100;stamp+=300) {
        int16_t level=stamp>=600u?200:0;
        segacd_native_audio_cdda_sample(audio,stamp,level,level);
    }
    CHECK(segacd_native_audio_write_checked(audio,1000,AUDIO_PORT_PSG,0x50)==SEGACD_NATIVE_AUDIO_OK);
    CHECK(segacd_native_audio_write_checked(audio,1,AUDIO_PORT_FM1_ADDR,0x22)==SEGACD_NATIVE_AUDIO_OK);
    CHECK(segacd_native_audio_write_checked(audio,1,AUDIO_PORT_FM1_DATA,0x44)==SEGACD_NATIVE_AUDIO_OK);
    CHECK(segacd_native_audio_write_checked(audio,2,AUDIO_PORT_PSG,0x90)==SEGACD_NATIVE_AUDIO_OK);
    CHECK(segacd_native_audio_finish_frame(audio,2400,10,1)==SEGACD_NATIVE_AUDIO_OK);
    CHECK(flush_count==0u);
    CHECK(write_count==3u);
    CHECK(writes[0].clock==4u && writes[0].port==0u && writes[0].value==0x22u);
    CHECK(writes[1].clock==4u && writes[1].port==1u && writes[1].value==0x44u);
    CHECK(writes[2].clock==8u && writes[2].port==AUDIO_PORT_PSG && writes[2].value==0x90u);
    segacd_native_audio_get_stats(audio,&stats);
    CHECK(stats.applied_events==3u && stats.deferred_events==1u);
    CHECK(segacd_native_audio_finish_frame(audio,4800,11,1)==SEGACD_NATIVE_AUDIO_OK);
    CHECK(flush_count==1u && flushed_wall_frame==10u);
    CHECK(flushed_fm_frames==2u && flushed_psg_frames==10u);
    CHECK(flushed_stereo[0]==150 && flushed_stereo[1]==150);
    CHECK(flushed_stereo[18]==372 && flushed_stereo[19]==372);
    CHECK(write_count==4u && writes[3].clock==4295u && writes[3].value==0x50u);
    CHECK(segacd_native_audio_flush_pending(audio,1)==SEGACD_NATIVE_AUDIO_OK);
    CHECK(flush_count==2u && flushed_wall_frame==11u);
    segacd_native_audio_get_stats(audio,&stats);
    CHECK(stats.applied_events==4u && stats.deferred_events==0u);
    CHECK(stats.rendered_psg_frames==20u && stats.rendered_fm_frames==4u);
    CHECK(stats.mix_underflows==0u);
    CHECK(stats.backwards_writes==0u && stats.faults==0u);
    segacd_native_audio_destroy(audio);
    CHECK(extra_source==NULL);
    return 0;
}

static int test_bad_port_and_bounded_queue(void)
{
    SegaCDNativeAudio *audio=segacd_native_audio_create(0);
    SegaCDNativeAudioStats stats;
    CHECK(audio!=NULL);
    CHECK(segacd_native_audio_write_checked(audio,0,0xff,0)==SEGACD_NATIVE_AUDIO_BAD_PORT);
    CHECK(segacd_native_audio_write_checked(audio,0,AUDIO_PORT_PSG,0)==SEGACD_NATIVE_AUDIO_BAD_PORT);
    CHECK(segacd_native_audio_finish_frame(audio,2400,1,0)==SEGACD_NATIVE_AUDIO_BAD_PORT);
    segacd_native_audio_get_stats(audio,&stats);
    CHECK(stats.queued_events==0u && stats.rendered_psg_frames==0u);
    CHECK(stats.faults&SEGACD_NATIVE_AUDIO_FAULT_BAD_PORT);
    segacd_native_audio_destroy(audio);

    audio=segacd_native_audio_create(0);
    CHECK(audio!=NULL);
    for(unsigned i=0;i<262144u;++i)
        CHECK(segacd_native_audio_write_checked(audio,0,AUDIO_PORT_PSG,(uint8_t)i)==SEGACD_NATIVE_AUDIO_OK);
    CHECK(segacd_native_audio_write_checked(audio,0,AUDIO_PORT_PSG,0)==SEGACD_NATIVE_AUDIO_EVENT_OVERFLOW);
    CHECK(segacd_native_audio_write_checked(audio,0,AUDIO_PORT_PSG,0)==SEGACD_NATIVE_AUDIO_EVENT_OVERFLOW);
    CHECK(segacd_native_audio_finish_frame(audio,2400,1,0)==SEGACD_NATIVE_AUDIO_EVENT_OVERFLOW);
    segacd_native_audio_get_stats(audio,&stats);
    CHECK(stats.queued_events==262144u && stats.event_overflows==1u);
    CHECK(stats.faults&SEGACD_NATIVE_AUDIO_FAULT_OVERFLOW);
    CHECK(stats.rendered_psg_frames==0u);
    segacd_native_audio_destroy(audio);
    return 0;
}

static int test_underflow_preflight_is_terminal_and_does_not_flush(void)
{
    SegaCDNativeAudio *audio=segacd_native_audio_create(0);
    SegaCDNativeAudioStats before,after;
    CHECK(audio!=NULL);
    flush_count=0;
    CHECK(segacd_native_audio_finish_frame(audio,2400,20,0)==SEGACD_NATIVE_AUDIO_OK);
    CHECK(flush_count==0u);
    segacd_native_audio_get_stats(audio,&before);
    CHECK(segacd_native_audio_finish_frame(audio,4800,21,0)==SEGACD_NATIVE_AUDIO_MIX_UNDERFLOW);
    CHECK(flush_count==0u);
    segacd_native_audio_get_stats(audio,&after);
    CHECK(after.mix_underflows==1u);
    CHECK(after.flushed_frames==0u && (after.faults&SEGACD_NATIVE_AUDIO_FAULT_MIX));
    CHECK(after.last_result==SEGACD_NATIVE_AUDIO_MIX_UNDERFLOW);
    CHECK(segacd_native_audio_finish_frame(audio,7200,22,0)==SEGACD_NATIVE_AUDIO_MIX_UNDERFLOW);
    CHECK(segacd_native_audio_flush_pending(audio,0)==SEGACD_NATIVE_AUDIO_MIX_UNDERFLOW);
    segacd_native_audio_get_stats(audio,&before);
    CHECK(before.rendered_psg_frames==after.rendered_psg_frames);
    CHECK(before.rendered_fm_frames==after.rendered_fm_frames);
    CHECK(flush_count==0u);
    segacd_native_audio_destroy(audio);
    return 0;
}

static int test_chip_scratch_drop_is_loud(void)
{
    SegaCDNativeAudio *audio;
    SegaCDNativeAudioStats stats;
    psg_test_limit=5u;
    audio=segacd_native_audio_create(0);
    CHECK(audio!=NULL);
    CHECK(segacd_native_audio_finish_frame(audio,2400,30,0)==SEGACD_NATIVE_AUDIO_RENDER_OVERFLOW);
    segacd_native_audio_get_stats(audio,&stats);
    CHECK(stats.faults&SEGACD_NATIVE_AUDIO_FAULT_RENDER);
    CHECK(stats.rendered_psg_frames==0u && stats.rendered_fm_frames==0u);
    CHECK(segacd_native_audio_finish_frame(audio,4800,31,0)==SEGACD_NATIVE_AUDIO_RENDER_OVERFLOW);
    CHECK(psg_samples==5u);
    segacd_native_audio_destroy(audio);
    psg_test_limit=256u;
    return 0;
}

int main(void)
{
    if(test_order_defer_and_frame_late_mix())return 1;
    if(test_bad_port_and_bounded_queue())return 1;
    if(test_underflow_preflight_is_terminal_and_does_not_flush())return 1;
    if(test_chip_scratch_drop_is_loud())return 1;
    puts("Segacd native audio tests passed");
    return 0;
}
