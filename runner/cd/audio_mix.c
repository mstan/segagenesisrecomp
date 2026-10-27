#include "audio_mix.h"

#include <limits.h>
#include <string.h>

#define QUEUE_MASK (SEGACD_AUDIO_MIX_QUEUE_CAPACITY - 1u)

static SegaCDAudioMix *s_audio_extra_mix;

_Static_assert((SEGACD_AUDIO_MIX_QUEUE_CAPACITY & QUEUE_MASK) == 0,
               "audio queue capacity must be a power of two");

static int valid_source(SegaCDAudioSource source)
{
    return (unsigned)source < SEGACD_AUDIO_SOURCE_COUNT;
}

static void set_result(SegaCDAudioMix *mix,SegaCDAudioMixResult result)
{
    mix->last_result=result;
}

void segacd_audio_mix_init(SegaCDAudioMix *mix)
{
    if(!mix)return;
    memset(mix,0,sizeof *mix);
    mix->last_result=SEGACD_AUDIO_MIX_OK;
}

void segacd_audio_mix_reset(SegaCDAudioMix *mix)
{
    if(!mix)return;
    uint64_t discarded[SEGACD_AUDIO_SOURCE_COUNT];
    for(unsigned i=0;i<SEGACD_AUDIO_SOURCE_COUNT;++i)
        discarded[i]=mix->source[i].discarded_frames+mix->source[i].count;
    memset(mix,0,sizeof *mix);
    for(unsigned i=0;i<SEGACD_AUDIO_SOURCE_COUNT;++i)
        mix->source[i].discarded_frames=discarded[i];
    mix->last_result=SEGACD_AUDIO_MIX_OK;
}

static void clear_queue(SegaCDAudioQueue *queue)
{
    queue->discarded_frames+=queue->count;
    queue->head=0;
    queue->count=0;
    queue->have_last_subclock=0;
}

SegaCDAudioMixResult segacd_audio_mix_set_active(SegaCDAudioMix *mix,
                                                  SegaCDAudioSource source,
                                                  int active)
{
    if(!mix || !valid_source(source))return SEGACD_AUDIO_MIX_INVALID;
    if(mix->paused) {
        set_result(mix,SEGACD_AUDIO_MIX_PAUSED);
        return mix->last_result;
    }
    SegaCDAudioQueue *queue=&mix->source[source];
    clear_queue(queue);
    queue->active=(uint8_t)(active!=0);
    set_result(mix,SEGACD_AUDIO_MIX_OK);
    return mix->last_result;
}

SegaCDAudioMixResult segacd_audio_mix_push(SegaCDAudioMix *mix,
                                           SegaCDAudioSource source,
                                           uint64_t subclock,
                                           int16_t left,int16_t right)
{
    if(!mix || !valid_source(source))return SEGACD_AUDIO_MIX_INVALID;
    if(mix->paused) {
        ++mix->paused_rejections;
        ++mix->source[source].rejected_frames;
        set_result(mix,SEGACD_AUDIO_MIX_PAUSED);
        return mix->last_result;
    }
    SegaCDAudioQueue *queue=&mix->source[source];
    if(!queue->active) {
        ++queue->rejected_frames;
        set_result(mix,SEGACD_AUDIO_MIX_NOT_ACTIVE);
        return mix->last_result;
    }
    if(queue->have_last_subclock &&
       (subclock<=queue->last_subclock || subclock-queue->last_subclock>SEGACD_AUDIO_MIX_CLOCK_HZ)) {
        mix->faults|=SEGACD_AUDIO_FAULT_TIMESTAMP;
        set_result(mix,SEGACD_AUDIO_MIX_BAD_TIMESTAMP);
        return mix->last_result;
    }
    if(queue->count==SEGACD_AUDIO_MIX_QUEUE_CAPACITY) {
        ++queue->rejected_frames;
        ++mix->overflow_events;
        mix->faults|=SEGACD_AUDIO_FAULT_OVERFLOW;
        set_result(mix,SEGACD_AUDIO_MIX_QUEUE_FULL);
        return mix->last_result;
    }
    unsigned tail=((unsigned)queue->head+queue->count)&QUEUE_MASK;
    queue->frame[tail].subclock=subclock;
    queue->frame[tail].left=left;
    queue->frame[tail].right=right;
    ++queue->count;
    queue->last_subclock=subclock;
    queue->have_last_subclock=1;
    ++queue->accepted_frames;
    set_result(mix,SEGACD_AUDIO_MIX_OK);
    return mix->last_result;
}

void segacd_audio_mix_pause(SegaCDAudioMix *mix)
{
    if(!mix)return;
    for(unsigned i=0;i<SEGACD_AUDIO_SOURCE_COUNT;++i) {
        clear_queue(&mix->source[i]);
        mix->source[i].active=0;
    }
    mix->paused=1;
    mix->window.pending=0;
    mix->next_output_valid=0;
    set_result(mix,SEGACD_AUDIO_MIX_OK);
}

void segacd_audio_mix_resume(SegaCDAudioMix *mix)
{
    if(!mix)return;
    mix->paused=0;
    mix->window.pending=0;
    mix->next_output_valid=0;
    for(unsigned i=0;i<SEGACD_AUDIO_SOURCE_COUNT;++i)
        mix->source[i].active=0;
    set_result(mix,SEGACD_AUDIO_MIX_OK);
}

void segacd_audio_mix_clear_faults(SegaCDAudioMix *mix,uint32_t mask)
{
    if(mix)mix->faults&=~mask;
}

SegaCDAudioMixResult segacd_audio_mix_set_window(SegaCDAudioMix *mix,
                                                  uint64_t first_subclock,
                                                  uint32_t first_phase,
                                                  uint32_t step_numerator,
                                                  uint32_t step_denominator,
                                                  uint32_t rate_hz,
                                                  size_t frames)
{
    if(!mix || !frames || frames>SEGACD_AUDIO_MIX_MAX_WINDOW_FRAMES ||
       !step_numerator || !step_denominator || step_denominator>1000000u ||
       first_phase>=step_denominator || !rate_hz || rate_hz>1000000u ||
       (uint64_t)step_numerator>(uint64_t)SEGACD_AUDIO_MIX_CLOCK_HZ*step_denominator) {
        if(mix) {
            mix->faults|=SEGACD_AUDIO_FAULT_WINDOW;
            set_result(mix,SEGACD_AUDIO_MIX_INVALID);
        }
        return SEGACD_AUDIO_MIX_INVALID;
    }
    /* Include the next-window anchor as well as the last rendered sample. */
    uint64_t span_numerator=(uint64_t)frames*step_numerator+first_phase;
    uint64_t span_ticks=span_numerator/step_denominator+
        (span_numerator%step_denominator!=0u);
    if(first_subclock>UINT64_MAX-span_ticks) {
        mix->faults|=SEGACD_AUDIO_FAULT_WINDOW;
        set_result(mix,SEGACD_AUDIO_MIX_INVALID);
        return SEGACD_AUDIO_MIX_INVALID;
    }
    if(mix->paused) {
        set_result(mix,SEGACD_AUDIO_MIX_PAUSED);
        return mix->last_result;
    }
    if(mix->window.pending) {
        mix->faults|=SEGACD_AUDIO_FAULT_WINDOW;
        set_result(mix,SEGACD_AUDIO_MIX_WINDOW_MISMATCH);
        return mix->last_result;
    }
    if(mix->next_output_valid &&
       (first_subclock!=mix->next_output_subclock ||
        first_phase!=mix->next_output_phase ||
        step_denominator!=mix->next_output_denominator ||
        step_numerator!=mix->next_output_step_numerator ||
        rate_hz!=mix->next_output_rate_hz)) {
        mix->faults|=SEGACD_AUDIO_FAULT_WINDOW;
        set_result(mix,SEGACD_AUDIO_MIX_WINDOW_MISMATCH);
        return mix->last_result;
    }
    mix->window.first_subclock=first_subclock;
    mix->window.first_phase=first_phase;
    mix->window.step_numerator=step_numerator;
    mix->window.step_denominator=step_denominator;
    mix->window.rate_hz=rate_hz;
    mix->window.frames=frames;
    mix->window.pending=1;
    set_result(mix,SEGACD_AUDIO_MIX_OK);
    return mix->last_result;
}

static int compare_stamp(uint64_t stamp,uint64_t clock,uint32_t phase)
{
    if(stamp<clock)return -1;
    if(stamp>clock)return 1;
    return phase? -1:0;
}

static const SegaCDAudioFrame *frame_at(const SegaCDAudioQueue *queue,unsigned index)
{
    return &queue->frame[((unsigned)queue->head+index)&QUEUE_MASK];
}

static int interpolated_sample(const SegaCDAudioQueue *queue,
                               uint64_t clock,uint32_t phase,uint32_t denominator,
                               unsigned *cursor,int16_t *left,int16_t *right)
{
    if(!queue->active)return 1;
    if(!queue->count)return 0;
    while(*cursor+1u<queue->count &&
          compare_stamp(frame_at(queue,*cursor+1u)->subclock,clock,phase)<=0)
        ++*cursor;
    const SegaCDAudioFrame *a=frame_at(queue,*cursor);
    int cmp=compare_stamp(a->subclock,clock,phase);
    if(cmp==0) {
        *left=a->left;*right=a->right;
        return 1;
    }
    if(cmp>0 || *cursor+1u>=queue->count)return 0;
    const SegaCDAudioFrame *b=frame_at(queue,*cursor+1u);
    uint64_t elapsed=(clock-a->subclock)*(uint64_t)denominator+phase;
    uint64_t span=(b->subclock-a->subclock)*(uint64_t)denominator;
    if(!span || elapsed>span)return 0;
    int64_t l_delta=(int32_t)b->left-(int32_t)a->left;
    int64_t r_delta=(int32_t)b->right-(int32_t)a->right;
    int64_t l_num=l_delta*(int64_t)elapsed;
    int64_t r_num=r_delta*(int64_t)elapsed;
    int64_t l_step=l_num>=0?(l_num+(int64_t)(span/2u))/(int64_t)span:
                   -((-l_num+(int64_t)(span/2u))/(int64_t)span);
    int64_t r_step=r_num>=0?(r_num+(int64_t)(span/2u))/(int64_t)span:
                   -((-r_num+(int64_t)(span/2u))/(int64_t)span);
    *left=(int16_t)((int32_t)a->left+(int32_t)l_step);
    *right=(int16_t)((int32_t)a->right+(int32_t)r_step);
    return 1;
}

static void advance_position(uint64_t *clock,uint32_t *phase,
                             uint32_t step_numerator,uint32_t denominator)
{
    uint64_t numerator=(uint64_t)*phase+step_numerator;
    *clock+=numerator/denominator;
    *phase=(uint32_t)(numerator%denominator);
}

static int source_window_available(const SegaCDAudioQueue *queue,
                                   const SegaCDAudioWindow *window)
{
    if(!queue->active)return 1;
    unsigned cursor=0;
    uint64_t clock=window->first_subclock;
    uint32_t phase=window->first_phase;
    for(size_t i=0;i<window->frames;++i) {
        int16_t left=0,right=0;
        if(!interpolated_sample(queue,clock,phase,window->step_denominator,
                                &cursor,&left,&right))return 0;
        advance_position(&clock,&phase,window->step_numerator,window->step_denominator);
    }
    return 1;
}

SegaCDAudioMixResult segacd_audio_mix_check_window(const SegaCDAudioMix *mix,
                                                    size_t frames,
                                                    uint32_t rate_hz)
{
    if(!mix || !mix->window.pending)return SEGACD_AUDIO_MIX_INVALID;
    if(mix->paused)return SEGACD_AUDIO_MIX_PAUSED;
    if(frames!=mix->window.frames || rate_hz!=mix->window.rate_hz)
        return SEGACD_AUDIO_MIX_WINDOW_MISMATCH;
    for(unsigned source=0;source<SEGACD_AUDIO_SOURCE_COUNT;++source)
        if(!source_window_available(&mix->source[source],&mix->window))
            return SEGACD_AUDIO_MIX_NEED_INPUT;
    return SEGACD_AUDIO_MIX_OK;
}

static int16_t saturate_s16(int32_t value)
{
    if(value>INT16_MAX)return INT16_MAX;
    if(value<INT16_MIN)return INT16_MIN;
    return (int16_t)value;
}

static int stamp_at_or_before(uint64_t stamp,uint64_t clock,uint32_t phase)
{
    return compare_stamp(stamp,clock,phase)<=0;
}

static void consume_before_window_end(SegaCDAudioQueue *queue,
                                      uint64_t end_clock,uint32_t end_phase)
{
    while(queue->count>1u &&
          stamp_at_or_before(frame_at(queue,1u)->subclock,end_clock,end_phase)) {
        queue->head=(uint16_t)(((unsigned)queue->head+1u)&QUEUE_MASK);
        --queue->count;
    }
}

SegaCDAudioMixResult segacd_audio_mix_render_window(SegaCDAudioMix *mix,
                                                    int16_t *stereo,
                                                    size_t frames,
                                                    uint32_t rate_hz)
{
    if(!mix || !stereo || !mix->window.pending) {
        if(mix) {
            mix->faults|=SEGACD_AUDIO_FAULT_WINDOW;
            set_result(mix,SEGACD_AUDIO_MIX_INVALID);
        }
        return SEGACD_AUDIO_MIX_INVALID;
    }
    SegaCDAudioWindow *window=&mix->window;
    if(mix->paused) {
        set_result(mix,SEGACD_AUDIO_MIX_PAUSED);
        return mix->last_result;
    }
    SegaCDAudioMixResult preflight=segacd_audio_mix_check_window(mix,frames,rate_hz);
    if(preflight==SEGACD_AUDIO_MIX_WINDOW_MISMATCH) {
        mix->faults|=SEGACD_AUDIO_FAULT_WINDOW;
        set_result(mix,SEGACD_AUDIO_MIX_WINDOW_MISMATCH);
        return mix->last_result;
    }
    if(preflight==SEGACD_AUDIO_MIX_NEED_INPUT) {
        mix->faults|=SEGACD_AUDIO_FAULT_UNDERFLOW;
        ++mix->underflow_blocks;
        set_result(mix,SEGACD_AUDIO_MIX_NEED_INPUT);
        return mix->last_result;
    }
    if(preflight!=SEGACD_AUDIO_MIX_OK) {
        mix->faults|=SEGACD_AUDIO_FAULT_WINDOW;
        set_result(mix,preflight);
        return mix->last_result;
    }

    uint64_t clock=window->first_subclock;
    uint32_t phase=window->first_phase;
    unsigned cursor[SEGACD_AUDIO_SOURCE_COUNT]={0,0};
    for(size_t i=0;i<frames;++i) {
        int16_t left[SEGACD_AUDIO_SOURCE_COUNT]={0,0};
        int16_t right[SEGACD_AUDIO_SOURCE_COUNT]={0,0};
        for(unsigned source=0;source<SEGACD_AUDIO_SOURCE_COUNT;++source) {
            if(!interpolated_sample(&mix->source[source],clock,phase,
                                    window->step_denominator,&cursor[source],
                                    &left[source],&right[source])) {
                /* Preflight makes this unreachable unless state is concurrently
                 * mutated; the API is emulation-thread only. */
                mix->faults|=SEGACD_AUDIO_FAULT_UNDERFLOW;
                ++mix->underflow_blocks;
                set_result(mix,SEGACD_AUDIO_MIX_NEED_INPUT);
                return mix->last_result;
            }
        }
        int32_t l=(int32_t)stereo[i*2u]+left[SEGACD_AUDIO_SOURCE_PCM]+left[SEGACD_AUDIO_SOURCE_CDDA];
        int32_t r=(int32_t)stereo[i*2u+1u]+right[SEGACD_AUDIO_SOURCE_PCM]+right[SEGACD_AUDIO_SOURCE_CDDA];
        stereo[i*2u]=saturate_s16(l);
        stereo[i*2u+1u]=saturate_s16(r);
        advance_position(&clock,&phase,window->step_numerator,window->step_denominator);
    }
    mix->next_output_subclock=clock;
    mix->next_output_phase=phase;
    mix->next_output_denominator=window->step_denominator;
    mix->next_output_step_numerator=window->step_numerator;
    mix->next_output_rate_hz=window->rate_hz;
    mix->next_output_valid=1;
    phase=window->first_phase;
    clock=window->first_subclock;
    for(size_t i=1;i<frames;++i)
        advance_position(&clock,&phase,window->step_numerator,window->step_denominator);
    for(unsigned source=0;source<SEGACD_AUDIO_SOURCE_COUNT;++source)
        consume_before_window_end(&mix->source[source],clock,phase);
    mix->rendered_frames+=frames;
    window->pending=0;
    set_result(mix,SEGACD_AUDIO_MIX_OK);
    return mix->last_result;
}

int segacd_audio_mix_next_output_position(const SegaCDAudioMix *mix,
                                          uint64_t *subclock,
                                          uint32_t *phase,
                                          uint32_t *denominator)
{
    if(!mix || !mix->next_output_valid || !subclock || !phase || !denominator)return 0;
    *subclock=mix->next_output_subclock;
    *phase=mix->next_output_phase;
    *denominator=mix->next_output_denominator;
    return 1;
}

SegaCDAudioMixResult segacd_audio_mix_bind_audio_extra(SegaCDAudioMix *mix)
{
    if(!mix)return SEGACD_AUDIO_MIX_INVALID;
    if(s_audio_extra_mix && s_audio_extra_mix!=mix)return SEGACD_AUDIO_MIX_ALREADY_BOUND;
    s_audio_extra_mix=mix;
    return SEGACD_AUDIO_MIX_OK;
}

void segacd_audio_mix_unbind_audio_extra(SegaCDAudioMix *mix)
{
    if(mix && s_audio_extra_mix==mix)s_audio_extra_mix=NULL;
}

void segacd_audio_mix_audio_extra_source(int16_t *stereo,size_t frames,double rate)
{
    SegaCDAudioMix *mix=s_audio_extra_mix;
    if(!mix)return;
    if(rate!=rate || rate<1.0 || rate>1000000.0) {
        mix->faults|=SEGACD_AUDIO_FAULT_WINDOW;
        set_result(mix,SEGACD_AUDIO_MIX_INVALID);
        mix->window.pending=0;
        return;
    }
    uint32_t rounded_rate=(uint32_t)(rate+0.5);
    SegaCDAudioMixResult result=segacd_audio_mix_render_window(mix,stereo,frames,rounded_rate);
    if(result!=SEGACD_AUDIO_MIX_OK)mix->window.pending=0;
}

void segacd_audio_mix_get_stats(const SegaCDAudioMix *mix,SegaCDAudioMixStats *out)
{
    if(!out)return;
    memset(out,0,sizeof *out);
    if(!mix)return;
    out->faults=mix->faults;
    out->rendered_frames=mix->rendered_frames;
    out->underflow_blocks=mix->underflow_blocks;
    out->overflow_events=mix->overflow_events;
    out->paused_rejections=mix->paused_rejections;
    out->pcm_queued_frames=mix->source[SEGACD_AUDIO_SOURCE_PCM].count;
    out->cdda_queued_frames=mix->source[SEGACD_AUDIO_SOURCE_CDDA].count;
    out->pcm_discarded_frames=mix->source[SEGACD_AUDIO_SOURCE_PCM].discarded_frames;
    out->cdda_discarded_frames=mix->source[SEGACD_AUDIO_SOURCE_CDDA].discarded_frames;
    out->last_result=mix->last_result;
}
