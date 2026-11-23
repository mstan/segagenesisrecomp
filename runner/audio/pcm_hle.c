#include "pcm_hle.h"
#include <limits.h>
#include <string.h>
void pcm_hle_init(PcmHle *p,const PcmHleClip *clips,unsigned count)
{memset(p,0,sizeof *p);p->clips=clips;p->count=clips?count:0;p->state.pan=0xC0;}
int pcm_hle_play(PcmHle *p,unsigned clip,unsigned pan)
{
    if(!p || !p->clips || clip>=p->count)return 0;
    const PcmHleClip *c=&p->clips[clip];
    if(!c->samples || !c->frames || !c->rate_numerator || !c->rate_denominator || (c->loop!=UINT32_MAX && c->loop>=c->frames))return 0;
    p->state=(PcmHleState){0,clip,1,p->state.paused,(uint8_t)(pan&0xC0)};return 1;
}
void pcm_hle_pause(PcmHle *p,int paused){p->state.paused=paused!=0;}
void pcm_hle_stop(PcmHle *p){p->state.active=0;p->state.phase=0;}
static int16_t saturate(int value){return (int16_t)(value<INT16_MIN?INT16_MIN:value>INT16_MAX?INT16_MAX:value);}
int pcm_hle_mix(PcmHle *p,int16_t *stereo,size_t frames,unsigned rate)
{
    if(!p || !stereo || !rate)return 0;
    if(!p->state.active || p->state.paused)return 1;
    if(!pcm_hle_state_valid(p,&p->state))return 0;
    const PcmHleClip *c=&p->clips[p->state.clip];
    uint64_t divisor=(uint64_t)c->rate_denominator*rate;
    uint64_t step=((uint64_t)c->rate_numerator<<32)/divisor;
    if(!step)return 0;
    for(size_t i=0;i<frames;i++){
        uint64_t at=p->state.phase>>32;
        if(at>=c->frames){
            if(c->loop==UINT32_MAX){p->state.active=0;break;}
            uint64_t repeat=(uint64_t)(c->frames-c->loop)<<32;
            p->state.phase=((uint64_t)c->loop<<32)+(p->state.phase-((uint64_t)c->frames<<32))%repeat;
            at=p->state.phase>>32;
        }
        /* Waveforms retain the original unsigned DAC bytes. Interpolation
         * changes only host delivery between their native sample times. */
        unsigned next=at+1<c->frames?(unsigned)at+1:c->loop!=UINT32_MAX?c->loop:(unsigned)at;
        int a=(int)c->samples[at]-128,b=(int)c->samples[next]-128;
        int value=a*128+(int)(((int64_t)(b-a)*128*(uint32_t)p->state.phase)/INT64_C(4294967296));
        if(p->state.pan&0x80)stereo[i*2]=saturate(stereo[i*2]+value);
        if(p->state.pan&0x40)stereo[i*2+1]=saturate(stereo[i*2+1]+value);
        p->state.phase+=step;
        if((p->state.phase>>32)>=c->frames){
            if(c->loop==UINT32_MAX){p->state.active=0;break;}
            uint64_t repeat=(uint64_t)(c->frames-c->loop)<<32;
            p->state.phase=((uint64_t)c->loop<<32)+(p->state.phase-((uint64_t)c->frames<<32))%repeat;
        }
    }
    return 1;
}
int pcm_hle_state_valid(const PcmHle *p,const PcmHleState *state)
{
    if(!p || !state || state->active>1 || state->paused>1 || (state->pan&~0xC0))return 0;
    if(!state->active)return 1;
    if(state->clip>=p->count)return 0;
    const PcmHleClip *c=&p->clips[state->clip];
    return c->samples && c->frames && c->rate_numerator && c->rate_denominator &&
        (c->loop==UINT32_MAX || c->loop<c->frames) && (state->phase>>32)<=c->frames;
}
int pcm_hle_events(PcmHle *p,uint32_t cycles,uint32_t master_rate,PcmHleDac emit,void *user)
{
    if(!p || !master_rate || !emit || !pcm_hle_state_valid(p,&p->state))return 0;
    if(!p->state.active || p->state.paused)return 1;
    const PcmHleClip *clip=&p->clips[p->state.clip];
    uint64_t step=((uint64_t)clip->rate_numerator<<32)/((uint64_t)clip->rate_denominator*master_rate);
    if(!step || cycles>UINT64_MAX/step)return 0;
    uint64_t initial=p->state.phase,end=initial+step*cycles;
    uint64_t next=(initial+0xFFFFFFFFull)&~0xFFFFFFFFull;
    for(;next<end;next+=1ull<<32){
        uint64_t index=next>>32;
        if(index>=clip->frames){
            if(clip->loop==UINT32_MAX){p->state.active=0;emit(user,(uint32_t)((next-initial)/step),128);break;}
            index=clip->loop+(index-clip->loop)%(clip->frames-clip->loop);
        }
        emit(user,(uint32_t)((next-initial)/step),clip->samples[index]);
    }
    if(p->state.active){
        if((end>>32)>=clip->frames && clip->loop!=UINT32_MAX)
            end=((uint64_t)clip->loop<<32)+(end-((uint64_t)clip->loop<<32))%((uint64_t)(clip->frames-clip->loop)<<32);
        p->state.phase=end;
    }
    return 1;
}
