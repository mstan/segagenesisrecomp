#include "audio_native.h"

#include "audio_mix.h"
#include "../audio.h"
#include "../audio/event_queue.h"
#include "../audio/sn76489.h"
#include "../audio/ym2612.h"

#include <stdlib.h>
#include <string.h>

#define NATIVE_EVENT_CAPACITY 262144u
#define NATIVE_PSG_CAPACITY 16384u
#define NATIVE_FM_CAPACITY 4096u
#define GENESIS_MASTER_HZ 53693175u
#define GENESIS_SUBCLOCK_HZ 12500000u
#define PSG_PERIOD_NUMERATOR 40000000u
#define PSG_PERIOD_DENOMINATOR 715909u

typedef struct {
    uint64_t subclock;
    uint64_t master_clock;
    uint64_t sequence;
    uint8_t port,value;
} SegaCDNativeEvent;

struct SegaCDNativeAudio {
    SegaCDNativeEvent *event;
    size_t event_count;
    uint64_t next_sequence;
    uint64_t rendered_master_clock;
    uint64_t output_psg_samples;
    uint64_t pending_first_psg_index;
    uint64_t current_first_psg_index;
    uint32_t pending_wall_frame;
    int pending_realtime;
    int pending_valid;
    int cdda_active;
    int cdda_started;
    int owns_host_audio;
    int registered;
    int16_t pending_fm[NATIVE_FM_CAPACITY * 2u];
    int16_t pending_psg[NATIVE_PSG_CAPACITY];
    int16_t current_fm[NATIVE_FM_CAPACITY * 2u];
    int16_t current_psg[NATIVE_PSG_CAPACITY];
    size_t pending_fm_frames,pending_psg_frames;
    size_t current_fm_frames,current_psg_frames;
    SegaCDAudioMix mix;
    SegaCDNativeAudioStats stats;
};

static uint64_t subclock_to_master(uint64_t subclock,int *ok)
{
    uint64_t whole=subclock/GENESIS_SUBCLOCK_HZ;
    uint64_t part=subclock%GENESIS_SUBCLOCK_HZ;
    uint64_t tail=part*GENESIS_MASTER_HZ/GENESIS_SUBCLOCK_HZ;
    if(whole>(UINT64_MAX-tail)/GENESIS_MASTER_HZ) {
        *ok=0;
        return 0;
    }
    *ok=1;
    return whole*GENESIS_MASTER_HZ+tail;
}

static int event_compare(const void *pa,const void *pb)
{
    const SegaCDNativeEvent *a=(const SegaCDNativeEvent *)pa;
    const SegaCDNativeEvent *b=(const SegaCDNativeEvent *)pb;
    if(a->subclock!=b->subclock)return a->subclock<b->subclock?-1:1;
    return a->sequence<b->sequence?-1:(a->sequence>b->sequence?1:0);
}

static void record_result(SegaCDNativeAudio *audio,SegaCDNativeAudioResult result)
{
    if(audio && audio->stats.last_result==SEGACD_NATIVE_AUDIO_OK &&
       result!=SEGACD_NATIVE_AUDIO_OK)
        audio->stats.last_result=result;
}

static int has_fatal_error(const SegaCDNativeAudio *audio)
{
    return audio->stats.faults!=0u;
}

static void record_mix_error(SegaCDNativeAudio *audio,SegaCDAudioMixResult result)
{
    if(!audio || result==SEGACD_AUDIO_MIX_OK)return;
    audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_MIX;
    record_result(audio,SEGACD_NATIVE_AUDIO_MIX_ERROR);
}

SegaCDNativeAudio *segacd_native_audio_create(int initialize_host_audio)
{
    SegaCDNativeAudio *audio=(SegaCDNativeAudio *)calloc(1,sizeof *audio);
    if(!audio)return NULL;
    audio->event=(SegaCDNativeEvent *)malloc(sizeof(*audio->event)*NATIVE_EVENT_CAPACITY);
    if(!audio->event) {
        free(audio);
        return NULL;
    }
    ym2612_init();
    psg_init();
    segacd_audio_mix_init(&audio->mix);
    if(segacd_audio_mix_set_active(&audio->mix,SEGACD_AUDIO_SOURCE_PCM,1)!=SEGACD_AUDIO_MIX_OK ||
       segacd_audio_mix_push(&audio->mix,SEGACD_AUDIO_SOURCE_PCM,0,0,0)!=SEGACD_AUDIO_MIX_OK) {
        free(audio->event);
        free(audio);
        return NULL;
    }
    /* The CDDA device callback emits timestamped zero samples while the drive
     * is idle, so keep this stream active across transport changes. This zero
     * is the explicit initial DAC silence sample at the time-zero epoch. */
    if(segacd_audio_mix_set_active(&audio->mix,SEGACD_AUDIO_SOURCE_CDDA,1)!=SEGACD_AUDIO_MIX_OK ||
       segacd_audio_mix_push(&audio->mix,SEGACD_AUDIO_SOURCE_CDDA,0,0,0)!=SEGACD_AUDIO_MIX_OK) {
        free(audio->event);
        free(audio);
        return NULL;
    }
    audio->cdda_active=1;
    audio->cdda_started=1;
    if(segacd_audio_mix_bind_audio_extra(&audio->mix)!=SEGACD_AUDIO_MIX_OK) {
        free(audio->event);
        free(audio);
        return NULL;
    }
    audio_set_extra_source(segacd_audio_mix_audio_extra_source);
    audio->registered=1;
    if(initialize_host_audio) {
        if(audio_init((int)psg_sample_rate())!=0) {
            audio_set_extra_source(NULL);
            segacd_audio_mix_unbind_audio_extra(&audio->mix);
            free(audio->event);
            free(audio);
            return NULL;
        }
        audio->owns_host_audio=1;
    }
    record_result(audio,SEGACD_NATIVE_AUDIO_OK);
    return audio;
}

void segacd_native_audio_destroy(SegaCDNativeAudio *audio)
{
    if(!audio)return;
    if(audio->registered) {
        audio_set_extra_source(NULL);
        segacd_audio_mix_unbind_audio_extra(&audio->mix);
    }
    if(audio->owns_host_audio)audio_close();
    free(audio->event);
    free(audio);
}

SegaCDNativeAudioResult segacd_native_audio_write_checked(SegaCDNativeAudio *audio,
                                                           uint64_t subclock,
                                                           uint8_t port,uint8_t value)
{
    int ok=0;
    if(!audio)return SEGACD_NATIVE_AUDIO_INVALID;
    if(has_fatal_error(audio))return audio->stats.last_result;
    uint64_t master=subclock_to_master(subclock,&ok);
    if(!ok) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_BACKWARDS;
        ++audio->stats.backwards_writes;
        record_result(audio,SEGACD_NATIVE_AUDIO_BACKWARDS_WRITE);
        return audio->stats.last_result;
    }
    if(port>AUDIO_PORT_PSG) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_BAD_PORT;
        record_result(audio,SEGACD_NATIVE_AUDIO_BAD_PORT);
        return audio->stats.last_result;
    }
    if(master<audio->rendered_master_clock) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_BACKWARDS;
        ++audio->stats.backwards_writes;
        record_result(audio,SEGACD_NATIVE_AUDIO_BACKWARDS_WRITE);
        return audio->stats.last_result;
    }
    if(audio->event_count==NATIVE_EVENT_CAPACITY) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_OVERFLOW;
        ++audio->stats.event_overflows;
        record_result(audio,SEGACD_NATIVE_AUDIO_EVENT_OVERFLOW);
        return audio->stats.last_result;
    }
    SegaCDNativeEvent *event=&audio->event[audio->event_count++];
    event->subclock=subclock;
    event->master_clock=master;
    event->sequence=audio->next_sequence++;
    event->port=port;
    event->value=value;
    ++audio->stats.queued_events;
    record_result(audio,SEGACD_NATIVE_AUDIO_OK);
    return audio->stats.last_result;
}

void segacd_native_audio_write(void *user,uint64_t subclock,uint8_t port,uint8_t value)
{
    (void)segacd_native_audio_write_checked((SegaCDNativeAudio *)user,subclock,port,value);
}

static void apply_write(uint8_t port,uint8_t value)
{
    if(port==AUDIO_PORT_PSG) {
        psg_write(value);
        return;
    }
    switch(port) {
    case AUDIO_PORT_FM1_ADDR: ym2612_write(0,value);break;
    case AUDIO_PORT_FM1_DATA: ym2612_write(1,value);break;
    case AUDIO_PORT_FM2_ADDR: ym2612_write(2,value);break;
    case AUDIO_PORT_FM2_DATA: ym2612_write(3,value);break;
    default:break;
    }
}

static void advance_chips(uint64_t cycles)
{
    while(cycles) {
        uint32_t chunk=cycles>UINT32_MAX?UINT32_MAX:(uint32_t)cycles;
        ym2612_advance(chunk);
        psg_advance(chunk);
        cycles-=chunk;
    }
}

static SegaCDNativeAudioResult render_to_boundary(SegaCDNativeAudio *audio,uint64_t end)
{
    if(end<audio->rendered_master_clock) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_BACKWARDS;
        record_result(audio,SEGACD_NATIVE_AUDIO_BACKWARDS_WRITE);
        return audio->stats.last_result;
    }
    uint64_t new_psg=end/240u-audio->stats.rendered_psg_frames;
    uint64_t new_fm=end/1008u-audio->stats.rendered_fm_frames;
    if(new_psg>NATIVE_PSG_CAPACITY || new_fm>NATIVE_FM_CAPACITY) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_RENDER;
        record_result(audio,SEGACD_NATIVE_AUDIO_RENDER_OVERFLOW);
        return audio->stats.last_result;
    }
    qsort(audio->event,audio->event_count,sizeof(*audio->event),event_compare);
    size_t applied=0;
    while(applied<audio->event_count && audio->event[applied].master_clock<=end) {
        SegaCDNativeEvent *event=&audio->event[applied];
        if(event->master_clock<audio->rendered_master_clock) {
            audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_BACKWARDS;
            ++audio->stats.backwards_writes;
            record_result(audio,SEGACD_NATIVE_AUDIO_BACKWARDS_WRITE);
            return audio->stats.last_result;
        }
        advance_chips(event->master_clock-audio->rendered_master_clock);
        audio->rendered_master_clock=event->master_clock;
        apply_write(event->port,event->value);
        ++applied;
        ++audio->stats.applied_events;
    }
    if(applied) {
        audio->event_count-=applied;
        memmove(audio->event,audio->event+applied,audio->event_count*sizeof(*audio->event));
    }
    audio->stats.deferred_events=audio->event_count;
    advance_chips(end-audio->rendered_master_clock);
    audio->rendered_master_clock=end;

    size_t fm_available=ym2612_samples_available();
    size_t psg_available=psg_samples_available();
    if(fm_available!=new_fm || psg_available!=new_psg ||
       fm_available>NATIVE_FM_CAPACITY || psg_available>NATIVE_PSG_CAPACITY) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_RENDER;
        record_result(audio,SEGACD_NATIVE_AUDIO_RENDER_OVERFLOW);
        return audio->stats.last_result;
    }
    audio->current_fm_frames=ym2612_render(audio->current_fm,fm_available);
    audio->current_psg_frames=psg_render(audio->current_psg,psg_available);
    if(audio->current_fm_frames!=fm_available || audio->current_psg_frames!=psg_available) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_RENDER;
        record_result(audio,SEGACD_NATIVE_AUDIO_RENDER_OVERFLOW);
        return audio->stats.last_result;
    }
    audio->current_first_psg_index=audio->output_psg_samples;
    audio->output_psg_samples+=audio->current_psg_frames;
    audio->stats.output_frames=audio->output_psg_samples;
    audio->stats.rendered_fm_frames+=audio->current_fm_frames;
    audio->stats.rendered_psg_frames+=audio->current_psg_frames;
    return SEGACD_NATIVE_AUDIO_OK;
}

static SegaCDNativeAudioResult flush_pending(SegaCDNativeAudio *audio,int realtime)
{
    uint64_t first_sample_ordinal=audio->pending_first_psg_index+1u;
    if(first_sample_ordinal>UINT64_MAX/PSG_PERIOD_NUMERATOR) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_MIX;
        record_result(audio,SEGACD_NATIVE_AUDIO_MIX_ERROR);
        return audio->stats.last_result;
    }
    uint64_t numerator=first_sample_ordinal*(uint64_t)PSG_PERIOD_NUMERATOR;
    uint64_t first_subclock=numerator/PSG_PERIOD_DENOMINATOR;
    uint32_t phase=(uint32_t)(numerator%PSG_PERIOD_DENOMINATOR);
    SegaCDAudioMixResult mr=segacd_audio_mix_set_window(&audio->mix,first_subclock,phase,
        PSG_PERIOD_NUMERATOR,PSG_PERIOD_DENOMINATOR,psg_sample_rate(),audio->pending_psg_frames);
    if(mr!=SEGACD_AUDIO_MIX_OK) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_MIX;
        record_result(audio,SEGACD_NATIVE_AUDIO_MIX_ERROR);
        return audio->stats.last_result;
    }
    mr=segacd_audio_mix_check_window(&audio->mix,audio->pending_psg_frames,
                                     psg_sample_rate());
    if(mr==SEGACD_AUDIO_MIX_NEED_INPUT) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_MIX;
        ++audio->stats.mix_underflows;
        record_result(audio,SEGACD_NATIVE_AUDIO_MIX_UNDERFLOW);
        return audio->stats.last_result;
    }
    if(mr!=SEGACD_AUDIO_MIX_OK) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_MIX;
        record_result(audio,SEGACD_NATIVE_AUDIO_MIX_ERROR);
        return audio->stats.last_result;
    }
    audio_flush(audio->pending_wall_frame,realtime,audio->pending_fm,
                audio->pending_fm_frames,audio->pending_psg,audio->pending_psg_frames);
    ++audio->stats.flushed_frames;
    if(audio->mix.last_result==SEGACD_AUDIO_MIX_NEED_INPUT) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_MIX;
        ++audio->stats.mix_underflows;
        record_result(audio,SEGACD_NATIVE_AUDIO_MIX_UNDERFLOW);
        return audio->stats.last_result;
    }
    if(audio->mix.last_result!=SEGACD_AUDIO_MIX_OK) {
        audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_MIX;
        record_result(audio,SEGACD_NATIVE_AUDIO_MIX_ERROR);
        return audio->stats.last_result;
    }
    return SEGACD_NATIVE_AUDIO_OK;
}

SegaCDNativeAudioResult segacd_native_audio_finish_frame(SegaCDNativeAudio *audio,
                                                         uint64_t end_master_clock,
                                                         uint32_t wall_frame,int realtime)
{
    if(!audio)return SEGACD_NATIVE_AUDIO_INVALID;
    if(has_fatal_error(audio))return audio->stats.last_result;
    SegaCDNativeAudioResult result=render_to_boundary(audio,end_master_clock);
    if(result!=SEGACD_NATIVE_AUDIO_OK)return result;
    result=SEGACD_NATIVE_AUDIO_OK;
    if(audio->pending_valid) {
        result=flush_pending(audio,audio->pending_realtime);
        if(result!=SEGACD_NATIVE_AUDIO_OK)return result;
    }
    memcpy(audio->pending_fm,audio->current_fm,audio->current_fm_frames*2u*sizeof(int16_t));
    memcpy(audio->pending_psg,audio->current_psg,audio->current_psg_frames*sizeof(int16_t));
    audio->pending_first_psg_index=audio->current_first_psg_index;
    audio->pending_fm_frames=audio->current_fm_frames;
    audio->pending_psg_frames=audio->current_psg_frames;
    audio->pending_wall_frame=wall_frame;
    audio->pending_realtime=realtime!=0;
    audio->pending_valid=1;
    return result;
}

SegaCDNativeAudioResult segacd_native_audio_flush_pending(SegaCDNativeAudio *audio,int realtime)
{
    if(!audio)return SEGACD_NATIVE_AUDIO_INVALID;
    if(has_fatal_error(audio))return audio->stats.last_result;
    if(!audio->pending_valid)return SEGACD_NATIVE_AUDIO_OK;
    SegaCDNativeAudioResult result=flush_pending(audio,realtime);
    audio->pending_valid=0;
    return result;
}

SegaCDNativeAudioResult segacd_native_audio_set_cdda_active(SegaCDNativeAudio *audio,int active)
{
    if(!audio)return SEGACD_NATIVE_AUDIO_INVALID;
    if(has_fatal_error(audio))return audio->stats.last_result;
    SegaCDAudioMixResult result=segacd_audio_mix_set_active(&audio->mix,
        SEGACD_AUDIO_SOURCE_CDDA,active!=0);
    if(result!=SEGACD_AUDIO_MIX_OK) {
        record_mix_error(audio,result);
        return audio->stats.last_result;
    }
    if(active && segacd_audio_mix_push(&audio->mix,SEGACD_AUDIO_SOURCE_CDDA,0,0,0)!=SEGACD_AUDIO_MIX_OK) {
        record_mix_error(audio,audio->mix.last_result);
        return audio->stats.last_result;
    }
    audio->cdda_active=active!=0;
    audio->cdda_started=0;
    record_result(audio,SEGACD_NATIVE_AUDIO_OK);
    return audio->stats.last_result;
}

static void push_cdda_sample(SegaCDNativeAudio *audio,uint64_t subclock,int16_t left,int16_t right)
{
    if(!audio)return;
    if(has_fatal_error(audio))return;
    if(!audio->cdda_active) {
        if(left || right) {
            audio->stats.faults|=SEGACD_NATIVE_AUDIO_FAULT_MIX;
            record_result(audio,SEGACD_NATIVE_AUDIO_MIX_ERROR);
        }
        return;
    }
    if(!audio->cdda_started) {
        /* The earlier pending window is known silence. Re-seed both its start
         * and the instant before the first real sample, so interpolation does
         * not smear a future CDDA value across the inactive lead-in. */
        SegaCDAudioMixResult activated=segacd_audio_mix_set_active(&audio->mix,SEGACD_AUDIO_SOURCE_CDDA,1);
        if(activated!=SEGACD_AUDIO_MIX_OK) {
            record_mix_error(audio,activated);
            return;
        }
        uint64_t seed=0;
        if(audio->pending_valid) {
            uint64_t ordinal=audio->pending_first_psg_index+1u;
            if(ordinal<=UINT64_MAX/PSG_PERIOD_NUMERATOR)
                seed=(ordinal*(uint64_t)PSG_PERIOD_NUMERATOR)/PSG_PERIOD_DENOMINATOR;
        }
        if(seed<subclock) {
            SegaCDAudioMixResult seeded=segacd_audio_mix_push(&audio->mix,
                SEGACD_AUDIO_SOURCE_CDDA,seed,0,0);
            if(seeded!=SEGACD_AUDIO_MIX_OK) {
                record_mix_error(audio,seeded);
                return;
            }
        }
        if(subclock>seed+1u) {
            SegaCDAudioMixResult seeded=segacd_audio_mix_push(&audio->mix,
                SEGACD_AUDIO_SOURCE_CDDA,subclock-1u,0,0);
            if(seeded!=SEGACD_AUDIO_MIX_OK) {
                record_mix_error(audio,seeded);
                return;
            }
        }
        audio->cdda_started=1;
    }
    SegaCDAudioMixResult result=segacd_audio_mix_push(&audio->mix,
        SEGACD_AUDIO_SOURCE_CDDA,subclock,left,right);
    record_mix_error(audio,result);
}

void segacd_native_audio_pcm_sample(void *user,uint64_t subclock,int16_t left,int16_t right)
{
    SegaCDNativeAudio *audio=(SegaCDNativeAudio *)user;
    if(!audio)return;
    if(has_fatal_error(audio))return;
    record_mix_error(audio,segacd_audio_mix_push(&audio->mix,
        SEGACD_AUDIO_SOURCE_PCM,subclock,left,right));
}

void segacd_native_audio_cdda_sample(void *user,uint64_t subclock,int16_t left,int16_t right)
{
    push_cdda_sample((SegaCDNativeAudio *)user,subclock,left,right);
}

void segacd_native_audio_attach_io(SegaCDNativeAudio *audio,SegaCDIO *io)
{
    if(!io)return;
    io->pcm_timed_sink=audio?segacd_native_audio_pcm_sample:NULL;
    io->pcm_timed_user=audio;
    io->cdda_sample=audio?segacd_native_audio_cdda_sample:NULL;
    io->cdda_sample_user=audio;
}

void segacd_native_audio_get_stats(const SegaCDNativeAudio *audio,SegaCDNativeAudioStats *out)
{
    if(!out)return;
    memset(out,0,sizeof *out);
    if(audio)*out=audio->stats;
}
