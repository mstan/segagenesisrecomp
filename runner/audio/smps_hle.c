#include "smps_hle.h"
#include <stdio.h>
#include <string.h>

static int fail(SmpsHle *s,const char *message,uint32_t cursor)
{
    s->state.failed=1;
    snprintf(s->error,sizeof s->error,"%s at sequence offset %08X",message,cursor);
    return 0;
}
static int span(const SmpsHle *s,uint32_t offset,size_t bytes)
{return offset<=s->bytes && bytes<=s->bytes-offset;}
static unsigned word(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
static int send(SmpsHle *s,SmpsHleEventKind kind,unsigned channel,uint32_t value,int argument)
{
    SmpsHleEvent event={kind,channel,value,argument};
    return s->emit && s->emit(s->user,&event)?1:fail(s,"Unsupported host sound output",value);
}
static int read(SmpsHle *s,SmpsHleTrack *t,unsigned *value)
{
    if(!span(s,t->cursor,1))return fail(s,"Sequence read outside immutable data",t->cursor);
    *value=s->data[t->cursor++];return 1;
}
static int rest(SmpsHle *s,unsigned channel,SmpsHleTrack *t)
{t->gate_remaining=0;return send(s,SMPS_HLE_REST,channel,0,0);}
static int voice(SmpsHle *s,unsigned channel,SmpsHleTrack *t)
{
    if(channel==0 || channel>=7)return 1;
    uint32_t offset=t->voice_base+25u*t->voice;
    if(!span(s,offset,25))return fail(s,"Voice outside immutable data",offset);
    return send(s,SMPS_HLE_VOICE,channel,offset,t->volume);
}
void smps_hle_init(SmpsHle *s,const uint8_t *data,size_t bytes,SmpsHleEmit emit,void *user)
{memset(s,0,sizeof *s);s->data=data;s->bytes=bytes;s->emit=emit;s->user=user;}
int smps_hle_stop(SmpsHle *s)
{
    int ok=1;
    for(unsigned c=0;c<SMPS_HLE_TRACKS;c++)if(s->state.music.track[c].active)
        if(!rest(s,c,&s->state.music.track[c]))ok=0;
    memset(&s->state.music,0,sizeof s->state.music);
    s->state.interrupted=0;return ok;
}
int smps_hle_music(SmpsHle *s,uint32_t offset,int flags)
{
    int interrupt_previous=(flags&SMPS_HLE_INTERRUPT)!=0;
    if(flags&~3 || ((flags&SMPS_HLE_ONE_PASS) && !interrupt_previous))return fail(s,"Invalid host song flags",offset);
    if(s->state.failed || !s->data || !span(s,offset,8))return fail(s,"Missing host music data",offset);
    unsigned fm=s->data[offset+2],psg=s->data[offset+3];
    if(!fm || fm>7 || psg>3 || !span(s,offset,8+fm*4+psg*6))return fail(s,"Invalid host music header",offset);
    SmpsHleClock music={0};music.tempo=s->data[offset+7];
    uint32_t voices=offset+word(s->data+offset);
    for(unsigned i=0;i<fm+psg;i++){
        unsigned channel=i<fm?i:7+i-fm;
        unsigned at=offset+8+(i<fm?i*4:fm*4+(i-fm)*6);
        SmpsHleTrack *t=&music.track[channel];
        t->cursor=offset+word(s->data+at);t->voice_base=voices;
        if(!span(s,t->cursor,1))return fail(s,"Channel outside immutable data",t->cursor);
        t->active=1;t->remaining=1;t->duration=1;
        t->divider=s->data[offset+6]?s->data[offset+6]:1;
        t->transpose=(int8_t)s->data[at+2];t->volume=s->data[at+3];t->pan=0xC0;
        if(i>=fm){t->envelope=s->data[at+5];t->modulation_control=s->data[at+4];}
    }
    if(interrupt_previous && !s->state.interrupted){
        if(!send(s,SMPS_HLE_BACKUP,0,0,0))return 0;
        s->state.previous=s->state.music;s->state.previous_song=s->state.song;s->state.interrupted=1;
    }
    if(!interrupt_previous)s->state.interrupted=0;
    for(unsigned c=0;c<SMPS_HLE_TRACKS;c++)if(s->state.music.track[c].active && !rest(s,c,&s->state.music.track[c]))return 0;
    s->state.music=music;s->state.song=offset;
    s->state.restore_on_loop=(flags&SMPS_HLE_ONE_PASS)!=0;
    s->state.lead_channel=fm>1?1:psg?7:0;
    if(!send(s,SMPS_HLE_BEGIN,0,0,interrupt_previous))return 0;
    for(unsigned c=0;c<SMPS_HLE_TRACKS;c++){
        SmpsHleTrack *t=&s->state.music.track[c];if(!t->active)continue;
        if(c && c<7){if(!voice(s,c,t))return 0;}
        else if(c>=7){
            if(!send(s,SMPS_HLE_VOLUME,c,t->volume,0) || !send(s,SMPS_HLE_ENVELOPE,c,t->envelope,0) ||
               !send(s,SMPS_HLE_MODULATION,c,t->modulation_control,34))return 0;
        }
    }
    return 1;
}
void smps_hle_pause(SmpsHle *s,int paused){s->state.music.paused=paused!=0;}
int smps_hle_effect(SmpsHle *s,uint32_t offset,int continuous)
{
    if(s->state.failed || !span(s,offset,4))return fail(s,"Missing host effect data",offset);
    unsigned count=s->data[offset+3],divider=s->data[offset+2];
    if(!count || count>6 || !divider || !span(s,offset,4+count*6))return fail(s,"Invalid host effect header",offset);
    for(unsigned i=0;i<count;i++){
        unsigned at=offset+4+i*6,hardware=s->data[at+1],channel;
        if(hardware&128){
            if(hardware!=0x80 && hardware!=0xA0 && hardware!=0xC0)return fail(s,"Unknown PSG effect channel",at);
            channel=7+(hardware-0x80)/32;
        }else{
            if(hardware!=2 && hardware!=4 && hardware!=5 && hardware!=6)return fail(s,"Unknown FM effect channel",at);
            channel=(hardware&3)+1+(hardware&4?3:0);
        }
        SmpsHleTrack *old=&s->state.music.track[channel];
        if(continuous && old->active && old->effect==offset){old->continuous=1;continue;}
        SmpsHleTrack track={0};track.active=1;track.remaining=track.duration=1;track.effect=offset;
        track.cursor=offset+word(s->data+at+2);track.voice_base=offset+word(s->data+offset);
        track.divider=(uint8_t)divider;track.transpose=(int8_t)s->data[at+4];track.volume=s->data[at+5];track.pan=0xC0;
        if(!span(s,track.cursor,1))return fail(s,"Effect outside immutable data",track.cursor);
        s->state.music.track[channel]=track;
        if(!send(s,SMPS_HLE_BEGIN_TRACK,channel,0,0) || !rest(s,channel,&s->state.music.track[channel]) || !voice(s,channel,&s->state.music.track[channel]) ||
           !send(s,SMPS_HLE_VOLUME,channel,track.volume,0) || !send(s,SMPS_HLE_PAN,channel,track.pan,0) ||
           !send(s,SMPS_HLE_ENVELOPE,channel,0,0) || !send(s,SMPS_HLE_MODULATION,channel,0,34))return 0;
    }
    return 1;
}
static int restore(SmpsHle *s)
{
    if(!s->state.interrupted)return fail(s,"Restore without interrupted music",s->state.song);
    for(unsigned c=0;c<SMPS_HLE_TRACKS;c++)if(s->state.music.track[c].active && !rest(s,c,&s->state.music.track[c]))return 0;
    s->state.music=s->state.previous;s->state.song=s->state.previous_song;
    s->state.interrupted=0;s->state.restore_on_loop=0;
    if(!send(s,SMPS_HLE_RESTORE,0,0,0))return 0;
    for(unsigned c=0;c<SMPS_HLE_TRACKS;c++){
        SmpsHleTrack *t=&s->state.music.track[c];if(!t->active)continue;
        if(!voice(s,c,t) || !send(s,SMPS_HLE_PAN,c,t->pan,0))return 0;
        if(t->note>0x80 && !send(s,c?SMPS_HLE_NOTE:SMPS_HLE_PCM,c,t->note+t->transpose,(uint8_t)t->detune|512))return 0;
    }
    return 1;
}
static int branch(SmpsHle *s,SmpsHleTrack *t,int call)
{
    unsigned hi,lo;if(!read(s,t,&hi)||!read(s,t,&lo))return 0;
    /* The normalized format retains the source command contracts: calls
     * are relative to the return point, jumps/loops to its preceding byte. */
    int64_t target=(int64_t)t->cursor-(call?0:1)+(int16_t)(hi<<8|lo);
    if(target<0 || target>=s->bytes)return fail(s,"Branch outside immutable data",t->cursor);
    t->cursor=(uint32_t)target;return 1;
}
static int command(SmpsHle *s,unsigned channel,SmpsHleTrack *t,unsigned op)
{
    unsigned a,b,c,d;
    switch(op){
    case 0: if(!read(s,t,&a))return 0;t->pan=(uint8_t)(a|(t->pan&0x37));return send(s,SMPS_HLE_PAN,channel,t->pan,0);
    case 1: if(!read(s,t,&a))return 0;t->detune=(int8_t)a;return 1;
    case 2: if(!read(s,t,&a))return 0;return send(s,SMPS_HLE_COMMUNICATION,channel,a,0);
    case 3: if(!t->depth)return fail(s,"Return without sequence call",t->cursor);t->cursor=t->returns[--t->depth];return 1;
    case 4: return restore(s)?2:0;
    case 5: if(!read(s,t,&a)||!a)return fail(s,"Invalid timing divider",t->cursor);t->divider=(uint8_t)a;return 1;
    case 6: case 12:
        if(!read(s,t,&a))return 0;t->volume=(uint8_t)(t->volume+(int8_t)a);
        return send(s,SMPS_HLE_VOLUME,channel,t->volume,0);
    case 8: case 29:
        if(!read(s,t,&a))return 0;t->gate=(uint16_t)(op==29?a*t->divider:a);return 1;
    case 9: if(!read(s,t,&a))return 0;t->transpose=(int8_t)(t->transpose+(int8_t)a);return 1;
    case 10: if(!read(s,t,&a))return 0;s->state.music.tempo=(uint8_t)a;return 1;
    case 11:
        if(!read(s,t,&a)||!a)return fail(s,"Invalid timing divider",t->cursor);
        for(unsigned i=0;i<SMPS_HLE_TRACKS;i++)s->state.music.track[i].divider=(uint8_t)a;return 1;
    case 13: if(!read(s,t,&a))return 0;t->voice=(uint8_t)a;return voice(s,channel,t);
    case 16: case 24: t->active=0;return rest(s,channel,t);
    case 17: if(!read(s,t,&a))return 0;return send(s,SMPS_HLE_NOISE,channel,a,0);
    case 19: if(!read(s,t,&a))return 0;t->envelope=(uint8_t)a;return send(s,SMPS_HLE_ENVELOPE,channel,a,0);
    case 20: {
        uint32_t previous=t->cursor;if(!branch(s,t,0))return 0;
        if(s->state.restore_on_loop && channel==s->state.lead_channel && !t->depth && t->cursor<previous)
            return restore(s)?2:0;
        return 1;
    }
    case 21:
        if(!read(s,t,&a)||!read(s,t,&b))return 0;
        if(a>=SMPS_HLE_LOOPS)return fail(s,"Loop slot outside host state",t->cursor);
        if(!t->loops[a])t->loops[a]=(uint8_t)b;
        if(--t->loops[a])return branch(s,t,0);
        if(!span(s,t->cursor,2))return fail(s,"Loop read outside immutable data",t->cursor);
        t->cursor+=2;return 1;
    case 22:
        if(t->depth>=SMPS_HLE_STACK)return fail(s,"Sequence call stack exhausted",t->cursor);
        t->returns[t->depth++]=t->cursor+2;return branch(s,t,1);
    case 25: if(!read(s,t,&a))return 0;return send(s,SMPS_HLE_PCM,channel,a,0);
    case 26: if(!read(s,t,&a))return 0;return send(s,SMPS_HLE_SOUND_REQUEST,channel,a,0);
    case 27: if(!read(s,t,&a))return 0;t->transpose=(int8_t)a;return 1;
    case 28: if(!read(s,t,&a))return 0;t->volume=(uint8_t)a;return send(s,SMPS_HLE_VOLUME,channel,a,0);
    case 31: return 1; /* legacy push-request latch has no audio output */
    case 30:
        if(t->continuous){t->continuous=0;return branch(s,t,0);}
        if(!span(s,t->cursor,2))return fail(s,"Continuous effect outside immutable data",t->cursor);
        t->cursor+=2;return 1;
    case 32:
        if(!read(s,t,&a)||!read(s,t,&b))return 0;
        return send(s,SMPS_HLE_FM_REGISTER,0,a,(int)b);
    case 14: case 33:
        if(!read(s,t,&a)||!read(s,t,&b)||!read(s,t,&c)||!read(s,t,&d))return 0;
        return send(s,SMPS_HLE_MODULATION,channel,a<<24|b<<16|c<<8|d,(int)op);
    case 15: case 18: return send(s,SMPS_HLE_MODULATION,channel,op==15,0);
    case 34: if(!read(s,t,&a))return 0;t->modulation_control=(uint8_t)a;return send(s,SMPS_HLE_MODULATION,channel,a,34);
    default: return fail(s,"Unsupported host SMPS command",t->cursor-1);
    }
}
int smps_hle_frame(SmpsHle *s)
{
    if(s->state.failed)return 0;
    if(s->state.music.paused)return 1;
    unsigned elapsed=s->state.music.accumulator+s->state.music.tempo;
    s->state.music.accumulator=(uint8_t)elapsed;
    /* Tempo carry delays the sequence cursor. Note gates and modulation
     * still run on the sound clock while the current note is held. */
    int hold_duration = elapsed > 255;
    for(unsigned channel=0;channel<SMPS_HLE_TRACKS;channel++){
        SmpsHleTrack *t=&s->state.music.track[channel];if(!t->active)continue;
        if(t->gate_remaining && !--t->gate_remaining && !rest(s,channel,t))return 0;
        if(hold_duration || (t->remaining && --t->remaining)){
            if(!send(s,SMPS_HLE_TICK,channel,0,0))return 0;
            continue;
        }
        t->tie=0;unsigned steps=0;
        for(;steps<4096 && t->active;steps++){
            unsigned op;if(!read(s,t,&op))return 0;
            if(op==0xFE){t->tie=1;continue;}
            if(op==0xFF){
                unsigned command_id;if(!read(s,t,&command_id))return 0;
                int result=command(s,channel,t,command_id);if(!result)return 0;
                if(result==2)return 1;continue;
            }
            /* Prepared DAC IDs occupy the full byte range below the two
             * stream markers; melodic note data is bounded separately. */
            if(op>=0xE0 && channel)return fail(s,"Invalid normalized melodic note",t->cursor-1);
            if(op>=0x80){
                t->note=(uint8_t)op;
                if(span(s,t->cursor,1) && s->data[t->cursor]<0x80){
                    unsigned duration=s->data[t->cursor++];t->duration=(uint16_t)((duration?duration:256u)*t->divider);
                }
            }else t->duration=(uint16_t)((op?op:256u)*t->divider);
            t->remaining=t->duration;t->gate_remaining=t->gate;
            if(t->note<=0x80){if(!rest(s,channel,t))return 0;}
            else if(!send(s,channel?SMPS_HLE_NOTE:SMPS_HLE_PCM,channel,
                    channel?(unsigned)((int)t->note+t->transpose):t->note,(uint8_t)t->detune|(t->tie?256:0)))return 0;
            if(!send(s,SMPS_HLE_TICK,channel,0,0))return 0;
            break;
        }
        if(steps==4096)return fail(s,"Sequence did not advance its sound clock",t->cursor);
    }
    return 1;
}
int smps_hle_state_valid(const SmpsHle *s,const SmpsHleState *state)
{
    if(!state || state->failed>1 || state->interrupted>1 || state->restore_on_loop>1 ||
       state->lead_channel>=SMPS_HLE_TRACKS || (state->restore_on_loop && !state->interrupted))return 0;
    const SmpsHleClock *clocks[2]={&state->music,&state->previous};
    for(unsigned c=0;c<2;c++){
        if(clocks[c]->paused>1)return 0;
        for(unsigned i=0;i<SMPS_HLE_TRACKS;i++){
            const SmpsHleTrack *t=&clocks[c]->track[i];
            if(t->active>1 || t->depth>SMPS_HLE_STACK || (t->active && (!t->divider || !span(s,t->cursor,1))))return 0;
            for(unsigned n=0;n<t->depth;n++)if(!span(s,t->returns[n],1))return 0;
        }
    }
    return 1;
}
