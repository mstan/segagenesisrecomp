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
    queue->run_count=0;
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

static uint64_t constant_stamp(const SegaCDAudioConstant *run,unsigned index)
{
    return run->first+((uint64_t)run->phase+(uint64_t)index*run->step_numerator)/run->denominator;
}

static void advance_constant(SegaCDAudioConstant *run,unsigned count)
{
    uint64_t n=(uint64_t)run->phase+(uint64_t)count*run->step_numerator;
    run->first+=n/run->denominator;
    run->phase=(uint32_t)(n%run->denominator);
}

static SegaCDAudioFrame frame_at(const SegaCDAudioQueue *queue,unsigned index)
{
    unsigned slot=((unsigned)queue->head+index)&QUEUE_MASK;
    unsigned offset=(slot-queue->run_first)&QUEUE_MASK;
    if(offset<queue->run_count) {
        SegaCDAudioFrame frame={0};
        frame.subclock=constant_stamp(&queue->run,offset);
        frame.left=queue->run.left;frame.right=queue->run.right;
        return frame;
    }
    return queue->frame[slot];
}

int segacd_audio_mix_queue_frame(const SegaCDAudioMix *mix,SegaCDAudioSource source,
                                 unsigned index,SegaCDAudioFrame *out)
{
    if(!mix||!out||!valid_source(source)||index>=mix->source[source].count)return 0;
    *out=frame_at(&mix->source[source],index);return 1;
}

static int interpolated_sample(const SegaCDAudioQueue *queue,
                               uint64_t clock,uint32_t phase,uint32_t denominator,
                               unsigned *cursor,int16_t *left,int16_t *right)
{
    if(!queue->active)return 1;
    if(!queue->count)return 0;
    if(queue->run_count &&
       compare_stamp(queue->run.first,clock,phase)<=0 &&
       compare_stamp(constant_stamp(&queue->run,queue->run_count-1u),clock,phase)>=0) {
        /* Equal adjacent values interpolate to the same exact S16. No need to
         * visit or expand each implicit timestamp; the scalar cursor catches
         * up once if output later leaves the run. */
        *left=queue->run.left;*right=queue->run.right;return 1;
    }
    while(*cursor+1u<queue->count &&
           compare_stamp(frame_at(queue,*cursor+1u).subclock,clock,phase)<=0)
        ++*cursor;
    SegaCDAudioFrame av=frame_at(queue,*cursor);const SegaCDAudioFrame *a=&av;
    int cmp=compare_stamp(a->subclock,clock,phase);
    if(cmp==0) {
        *left=a->left;*right=a->right;
        return 1;
    }
    if(cmp>0 || *cursor+1u>=queue->count)return 0;
    SegaCDAudioFrame bv=frame_at(queue,*cursor+1u);const SegaCDAudioFrame *b=&bv;
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

static int source_constant_window(const SegaCDAudioQueue *queue,
                                   const SegaCDAudioWindow *window,int16_t *left,int16_t *right)
{
    if(!queue->active){*left=0;*right=0;return 1;}
    if(queue->run_count) {
        uint64_t n=(uint64_t)window->first_phase+
            (uint64_t)(window->frames-1u)*window->step_numerator;
        uint64_t end=window->first_subclock+n/window->step_denominator;
        uint32_t end_phase=(uint32_t)(n%window->step_denominator);
        if(compare_stamp(queue->run.first,window->first_subclock,window->first_phase)<=0 &&
           compare_stamp(constant_stamp(&queue->run,queue->run_count-1u),end,end_phase)>=0)
            {*left=queue->run.left;*right=queue->run.right;return 1;}
    }
    return 0;
}

static int source_window_available(const SegaCDAudioQueue *queue,
                                   const SegaCDAudioWindow *window)
{
    int16_t constant_left=0,constant_right=0;
    if(source_constant_window(queue,window,&constant_left,&constant_right))return 1;
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
          stamp_at_or_before(frame_at(queue,1u).subclock,end_clock,end_phase)) {
        if(queue->run_count && queue->head==queue->run_first) {
            /* Retire the same logical prefix as the scalar loop, retaining
             * the exact final anchor. At most one divide for the whole run. */
            uint64_t delta=end_clock-queue->run.first;
            uint64_t n=((delta+1u)*queue->run.denominator-1u-queue->run.phase)/queue->run.step_numerator;
            if(n>=queue->run_count)n=queue->run_count-1u;
            if(n>=queue->count)n=queue->count-1u;
            if(n) {
                advance_constant(&queue->run,(unsigned)n);
                queue->run_first=(uint16_t)((queue->run_first+n)&QUEUE_MASK);
                queue->run_count=(uint16_t)(queue->run_count-n);
                queue->head=(uint16_t)((queue->head+n)&QUEUE_MASK);
                queue->count=(uint16_t)(queue->count-n);
                continue;
            }
            /* The next physical frame is beyond the run; retire its last
             * logical sample using the ordinary boundary check above. */
            queue->run_count=0;
        }
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
    int constant[SEGACD_AUDIO_SOURCE_COUNT];
    int16_t constant_left[SEGACD_AUDIO_SOURCE_COUNT]={0,0},constant_right[SEGACD_AUDIO_SOURCE_COUNT]={0,0};
    for(unsigned source=0;source<SEGACD_AUDIO_SOURCE_COUNT;++source)
        constant[source]=source_constant_window(&mix->source[source],window,
            &constant_left[source],&constant_right[source]);
    int both_constant=constant[0]&&constant[1];
    if(both_constant) {
        int32_t l=(int32_t)constant_left[0]+constant_left[1];
        int32_t r=(int32_t)constant_right[0]+constant_right[1];
        if(l||r)for(size_t i=0;i<frames;++i) {
            stereo[i*2u]=saturate_s16((int32_t)stereo[i*2u]+l);
            stereo[i*2u+1u]=saturate_s16((int32_t)stereo[i*2u+1u]+r);
        }
        uint64_t n=(uint64_t)window->first_phase+(uint64_t)frames*window->step_numerator;
        clock+=n/window->step_denominator;phase=(uint32_t)(n%window->step_denominator);
    } else for(size_t i=0;i<frames;++i) {
        int16_t left[SEGACD_AUDIO_SOURCE_COUNT]={0,0};
        int16_t right[SEGACD_AUDIO_SOURCE_COUNT]={0,0};
        for(unsigned source=0;source<SEGACD_AUDIO_SOURCE_COUNT;++source) {
            if(constant[source]) {
                left[source]=constant_left[source];right[source]=constant_right[source];continue;
            }
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
    if(both_constant) {
        uint64_t n=(uint64_t)phase+(uint64_t)(frames-1u)*window->step_numerator;
        clock+=n/window->step_denominator;phase=(uint32_t)(n%window->step_denominator);
    } else for(size_t i=1;i<frames;++i)
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

int segacd_audio_mix_begin_span(SegaCDAudioMix *mix,SegaCDAudioSource source,
    uint64_t first,uint64_t last,size_t count,SegaCDAudioWriteGrant *g)
{
    if(!mix||!g||!valid_source(source)||!count||mix->paused)return 0;
    SegaCDAudioQueue *q=&mix->source[source];
    if(!q->active||q->count>SEGACD_AUDIO_MIX_QUEUE_CAPACITY||count>(size_t)SEGACD_AUDIO_MIX_QUEUE_CAPACITY-q->count||
       last<first||(count>1&&last==first)||last-first>SEGACD_AUDIO_MIX_CLOCK_HZ)return 0;
    if(q->have_last_subclock&&(first<=q->last_subclock||
       first-q->last_subclock>SEGACD_AUDIO_MIX_CLOCK_HZ))return 0;
    memset(g,0,sizeof *g);
    g->frame=q->frame;g->count=count;g->first=((unsigned)q->head+q->count)&QUEUE_MASK;
    g->mask=QUEUE_MASK;g->old_head=q->head;g->old_count=q->count;
    g->source=(unsigned)source;g->first_subclock=first;g->last_subclock=last;g->owner=mix;
    return 1;
}

int segacd_audio_mix_begin_constant(SegaCDAudioMix *mix,SegaCDAudioSource source,
    const SegaCDAudioConstant *run,size_t count,SegaCDAudioWriteGrant *g)
{
    if(!run||!g||!count||count>SEGACD_AUDIO_MIX_QUEUE_CAPACITY||
       !run->denominator||run->denominator>1000000u||run->phase>=run->denominator||
       run->step_numerator<run->denominator)return 0;
    uint64_t span=((uint64_t)run->phase+(uint64_t)(count-1u)*run->step_numerator)/run->denominator;
    if(run->first>UINT64_MAX-span)return 0;
    SegaCDAudioWriteGrant candidate;
    if(!segacd_audio_mix_begin_span(mix,source,run->first,run->first+span,count,&candidate))return 0;
    SegaCDAudioQueue *q=&mix->source[source];
    if(q->run_count) {
        SegaCDAudioConstant next=q->run;advance_constant(&next,q->run_count);
        if(((q->run_first+q->run_count)&QUEUE_MASK)!=candidate.first||
           next.first!=run->first||next.phase!=run->phase||
           next.step_numerator!=run->step_numerator||next.denominator!=run->denominator||
           next.left!=run->left||next.right!=run->right)return 0;
    }
    candidate.constant=*run;candidate.constant_run=1;*g=candidate;return 1;
}
SegaCDAudioMixResult segacd_audio_mix_commit_span(SegaCDAudioMix *mix,const SegaCDAudioWriteGrant *g)
{
    if(!mix||!g||g->owner!=mix||!valid_source((SegaCDAudioSource)g->source))return SEGACD_AUDIO_MIX_INVALID;
    SegaCDAudioQueue *q=&mix->source[g->source];
    if(g->constant_run) {
        SegaCDAudioWriteGrant expected;
        if(g->constant_run!=1 || !segacd_audio_mix_begin_constant(mix,(SegaCDAudioSource)g->source,
            &g->constant,g->count,&expected)||g->frame!=expected.frame||g->mask!=expected.mask||
            g->old_head!=expected.old_head||g->old_count!=expected.old_count||
            g->first!=expected.first||g->first_subclock!=expected.first_subclock||
            g->last_subclock!=expected.last_subclock)return SEGACD_AUDIO_MIX_INVALID;
        if(!q->run_count){q->run=g->constant;q->run_first=(uint16_t)g->first;}
        q->run_count=(uint16_t)(q->run_count+g->count);
        q->count=(uint16_t)(q->count+g->count);q->accepted_frames+=g->count;
        q->last_subclock=g->last_subclock;q->have_last_subclock=1;
        set_result(mix,SEGACD_AUDIO_MIX_OK);return SEGACD_AUDIO_MIX_OK;
    }
    if(mix->paused||!q->active||g->frame!=q->frame||g->mask!=QUEUE_MASK||
       q->head!=g->old_head||q->count!=g->old_count||q->count>SEGACD_AUDIO_MIX_QUEUE_CAPACITY||!g->count||
       g->count>(size_t)SEGACD_AUDIO_MIX_QUEUE_CAPACITY-q->count||
       g->first!=(((unsigned)q->head+q->count)&QUEUE_MASK)||
       segacd_audio_grant_frame(g,0)->subclock!=g->first_subclock||
       segacd_audio_grant_frame(g,g->count-1)->subclock!=g->last_subclock)
        return SEGACD_AUDIO_MIX_INVALID;
    q->count=(uint16_t)(q->count+g->count);q->accepted_frames+=g->count;
    q->last_subclock=g->last_subclock;q->have_last_subclock=1;
    set_result(mix,SEGACD_AUDIO_MIX_OK);return SEGACD_AUDIO_MIX_OK;
}
