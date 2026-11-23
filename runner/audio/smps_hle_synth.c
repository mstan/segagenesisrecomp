#include "smps_hle_synth.h"
#include <string.h>
static int range(const SmpsHleSynth *s,uint32_t at,size_t bytes)
{return at<=s->bytes && bytes<=s->bytes-at;}
static int fm(SmpsHleSynth *s,unsigned channel,unsigned reg,unsigned value)
{
    if(!s->output.fm || channel<1 || channel>6)return 0;
    unsigned index=channel-1;
    return s->output.fm(s->output.user,index/3,reg+index%3,value&255);
}
static int psg(SmpsHleSynth *s,unsigned value)
{return s->output.psg && s->output.psg(s->output.user,value&255);}
static int key(SmpsHleSynth *s,unsigned channel,int on)
{
    if(!s->output.fm || channel<1 || channel>6)return 0;
    unsigned index=channel-1;
    return s->output.fm(s->output.user,0,0x28,(on?0xF0:0)|(index%3)|(index>=3?4:0));
}
static int silence(SmpsHleSynth *s,unsigned channel,SmpsHleSynthTrack *t)
{
    t->active=0;
    if(!channel)return 1;
    return channel<7?key(s,channel,0):psg(s,(channel==9&&t->noise)?0xFF:0x9F+((channel-7)<<5));
}
static int volume(SmpsHleSynth *s,unsigned channel,SmpsHleSynthTrack *t,unsigned extra)
{
    if(channel>=7){
        unsigned value=(t->volume+extra)>>3;if(value>15)value=15;
        if(value==t->last_volume)return 1;t->last_volume=(uint8_t)value;
        return psg(s,((channel==9&&t->noise)?0xF0:0x90+((channel-7)<<5))+value);
    }
    if(!channel || !range(s,t->voice,25))return !channel;
    for(unsigned op=0;op<4;op++){
        unsigned value=s->data[t->voice+5+op];
        if(value&128){value=(value&127)+t->volume+extra;if(value>127)value=127;}
        if(!fm(s,channel,0x40+op*4,value&127))return 0;
    }
    return 1;
}
static int instrument(SmpsHleSynth *s,unsigned channel,SmpsHleSynthTrack *t)
{
    if(!range(s,t->voice,25))return 0;
    const uint8_t *voice=s->data+t->voice;
    if(!key(s,channel,0) || !fm(s,channel,0xB0,voice[0]))return 0;
    for(unsigned op=0;op<4;op++)if(!fm(s,channel,0x30+op*4,voice[1+op]))return 0;
    if(!volume(s,channel,t,0))return 0;
    for(unsigned group=0;group<4;group++)for(unsigned op=0;op<4;op++)
        if(!fm(s,channel,0x50+group*0x10+op*4,voice[9+group*4+op]))return 0;
    return fm(s,channel,0xB4,t->pan);
}
static int envelope(SmpsHleSynth *s,SmpsHleSynthTrack *t,int modulation,int *result)
{
    unsigned id=modulation?(t->modulation_control&63):t->psg_envelope;
    unsigned count=modulation?s->modulation_count:s->psg_count;
    const uint32_t *table=modulation?s->modulation_envelopes:s->psg_envelopes;
    uint8_t *index=modulation?&t->modulation_index:&t->psg_index;
    *result=0;if(!id)return 1;
    if(!table || id>count || !table[id-1])return 0;
    uint32_t start=table[id-1];
    for(unsigned n=0;n<256;n++){
        uint32_t at=start+*index;if(!range(s,at,1))return 0;
        unsigned value=s->data[at];
        if(value==0x80){*index=0;continue;}
        if(value==0x81){if(*index)--*index;else return 0;value=s->data[start+*index];}
        else if(value==0x82){if(!range(s,at+1,1))return 0;*index=s->data[at+1];continue;}
        else if(value==0x83){t->active=0;return 1;}
        else if(modulation && value==0x84){
            if(!range(s,at+1,1))return 0;t->modulation_multiplier=(uint8_t)(t->modulation_multiplier+s->data[at+1]);*index+=2;continue;
        }
        ++*index;*result=modulation?(int8_t)value*t->modulation_multiplier:(int)value*8;return 1;
    }
    return 0;
}
static int frequency(SmpsHleSynth *s,unsigned channel,SmpsHleSynthTrack *t,int delta)
{
    unsigned value=(unsigned)(t->base_frequency+t->detune+delta+t->modulation)&65535;
    if(value==t->last_frequency)return 1;t->last_frequency=(uint16_t)value;
    if(channel>=7){
        value&=1023;
        return psg(s,0x80+((channel-7)<<5)+(value&15))&&psg(s,value>>4);
    }
    return fm(s,channel,0xA4,value>>8)&&fm(s,channel,0xA0,value&255);
}
static void raw_modulation(SmpsHleSynthTrack *t)
{
    if(!(t->modulation_control&128))return;
    if(t->raw_z80){
        if(--t->raw_delay)return;++t->raw_delay;
        if(!--t->raw_speed){t->raw_speed=t->raw[1];t->modulation+=t->raw_delta;}
        if(!--t->raw_steps){t->raw_steps=t->raw[3];t->raw_delta=(int8_t)-t->raw_delta;}
    }else{
        if(t->raw_delay){--t->raw_delay;return;}
        if(--t->raw_speed)return;t->raw_speed=t->raw[1];
        if(!t->raw_steps){t->raw_steps=t->raw[3];t->raw_delta=(int8_t)-t->raw_delta;return;}
        --t->raw_steps;t->modulation+=t->raw_delta;
    }
}
static int tick(SmpsHleSynth *s,unsigned channel,SmpsHleSynthTrack *t)
{
    if(!channel || !t->active)return 1;
    raw_modulation(t);int change=0;
    if(!envelope(s,t,1,&change))return 0;
    if(!t->active)return silence(s,channel,t);
    if(!frequency(s,channel,t,change))return 0;
    if(channel>=7){
        if(!envelope(s,t,0,&change))return 0;
        if(!t->active)return silence(s,channel,t);
        return volume(s,channel,t,(unsigned)change);
    }
    return 1;
}
static uint16_t note_frequency(unsigned channel,unsigned note)
{
    static const uint16_t fm_octave[]={0x284,0x2AB,0x2D3,0x2FE,0x32D,0x35C,0x38F,0x3C5,0x3FF,0x43C,0x47C,0x4C0};
    static const uint16_t psg_notes[]={
        0x3FF,0x3FF,0x3FF,0x3FF,0x3FF,0x3FF,0x3FF,0x3FF,0x3FF,0x3F7,0x3BE,0x388,
        0x356,0x326,0x2F9,0x2CE,0x2A5,0x280,0x25C,0x23A,0x21A,0x1FB,0x1DF,0x1C4,
        0x1AB,0x193,0x17D,0x167,0x153,0x140,0x12E,0x11D,0x10D,0xFE,0xEF,0xE2,
        0xD6,0xC9,0xBE,0xB4,0xA9,0xA0,0x97,0x8F,0x87,0x7F,0x78,0x71,
        0x6B,0x65,0x5F,0x5A,0x55,0x50,0x4B,0x47,0x43,0x40,0x3C,0x39,
        0x36,0x33,0x30,0x2D,0x2B,0x28,0x26,0x24,0x22,0x20,0x1F,0x1D,
        0x1B,0x1A,0x18,0x17,0x16,0x15,0x13,0x12,0x11,0x10,0,0};
    unsigned index=(note-0x81)&255;
    if(channel>=7)return index<sizeof psg_notes/sizeof *psg_notes?psg_notes[index]:0;
    return (uint16_t)(fm_octave[index%12]|((index/12)<<11));
}
void smps_hle_synth_init(SmpsHleSynth *s,const uint8_t *data,size_t bytes,SmpsHleChipOutput output)
{
    memset(s,0,sizeof *s);s->data=data;s->bytes=bytes;s->output=output;
    for(unsigned i=0;i<SMPS_HLE_TRACKS;i++){
        s->state.track[i].pan=0xC0;s->state.track[i].last_volume=255;
        s->state.track[i].last_frequency=65535;s->state.track[i].modulation_multiplier=1;
    }
}
int smps_hle_synth_resume(SmpsHleSynth *s,unsigned channel)
{
    if(channel>=SMPS_HLE_TRACKS)return 0;
    SmpsHleSynthTrack *t=&s->state.track[channel];
    if(!channel)return 1;
    if(channel<7 && !instrument(s,channel,t))return 0;
    if(channel>=7 && t->noise && !psg(s,0xE0|(t->noise&7)))return 0;
    t->last_volume=255;t->last_frequency=65535;
    if(!t->active)return silence(s,channel,t);
    return frequency(s,channel,t,0) && volume(s,channel,t,0) && (channel>=7 || key(s,channel,1));
}
int smps_hle_synth_event(void *user,const SmpsHleEvent *event)
{
    SmpsHleSynth *s=user;if(!s || !event || event->channel>=SMPS_HLE_TRACKS)return 0;
    unsigned channel=event->channel;SmpsHleSynthTrack *t=&s->state.track[channel];
    switch(event->kind){
    case SMPS_HLE_BEGIN_TRACK:
        memset(t,0,sizeof *t);t->pan=0xC0;t->last_volume=255;t->last_frequency=65535;return 1;
    case SMPS_HLE_NOTE:
        t->active=1;t->note=(uint8_t)event->value;t->detune=(int8_t)event->argument;
        t->base_frequency=note_frequency(channel,event->value);t->last_frequency=65535;
        if(!(event->argument&(256|512))){
            t->psg_index=t->modulation_index=0;t->modulation=0;t->modulation_multiplier=0;
            t->raw_delay=t->raw[0];t->raw_speed=t->raw[1];t->raw_delta=(int8_t)t->raw[2];
            t->raw_steps=t->raw_z80?t->raw[3]:(uint8_t)(t->raw[3]/2);
            if(channel<7 && !key(s,channel,0))return 0;
        }
        if(!frequency(s,channel,t,0))return 0;
        return channel>=7 || (event->argument&256) || key(s,channel,1);
    case SMPS_HLE_REST:return silence(s,channel,t);
    case SMPS_HLE_VOICE:t->voice=event->value;t->volume=(uint8_t)event->argument;return instrument(s,channel,t);
    case SMPS_HLE_VOLUME:t->volume=(uint8_t)event->value;return volume(s,channel,t,0);
    case SMPS_HLE_PAN:
        t->pan=(uint8_t)event->value;
        if(!channel)return s->output.fm&&s->output.fm(s->output.user,1,0xB6,t->pan);
        return channel>=7 || fm(s,channel,0xB4,t->pan);
    case SMPS_HLE_NOISE:t->noise=(uint8_t)event->value;t->last_volume=255;return !event->value || psg(s,0xE0|(event->value&7));
    case SMPS_HLE_PCM:return s->output.pcm && s->output.pcm(s->output.user,event->value,t->pan);
    case SMPS_HLE_FM_REGISTER:return s->output.fm && s->output.fm(s->output.user,0,event->value,(unsigned)event->argument);
    case SMPS_HLE_ENVELOPE:t->psg_envelope=(uint8_t)event->value;t->psg_index=0;return 1;
    case SMPS_HLE_MODULATION:
        if(event->argument==14 || event->argument==33){
            t->raw[0]=(uint8_t)(event->value>>24);t->raw[1]=(uint8_t)(event->value>>16);
            t->raw[2]=(uint8_t)(event->value>>8);t->raw[3]=(uint8_t)event->value;
            t->raw_z80=event->argument==33;
            if(t->raw_z80){t->modulation_control=128;t->modulation_index=0;}else t->modulation_control|=128;
            t->raw_delay=t->raw[0];t->raw_speed=t->raw[1];t->raw_delta=(int8_t)t->raw[2];
            t->raw_steps=t->raw_z80?t->raw[3]:(uint8_t)(t->raw[3]/2);t->modulation=0;
        }else if(event->argument==34){t->modulation_control=(uint8_t)event->value;}
        else if(event->value)t->modulation_control|=128;else t->modulation_control&=127;
        return 1;
    case SMPS_HLE_SOUND_REQUEST:return s->output.request&&s->output.request(s->output.user,event->value);
    case SMPS_HLE_COMMUNICATION:s->state.communication=(uint8_t)event->value;return 1;
    case SMPS_HLE_TICK:return tick(s,channel,t);
    case SMPS_HLE_BACKUP:
        if(s->state.interrupted)return 0;
        memcpy(s->state.previous,s->state.track,sizeof s->state.track);s->state.interrupted=1;return 1;
    case SMPS_HLE_BEGIN:
        memset(s->state.track,0,sizeof s->state.track);
        if(!event->argument)s->state.interrupted=0;
        for(unsigned i=0;i<SMPS_HLE_TRACKS;i++){
            s->state.track[i].pan=0xC0;s->state.track[i].last_volume=255;
            s->state.track[i].last_frequency=65535;
        }
        return 1;
    case SMPS_HLE_RESTORE:
        if(!s->state.interrupted)return 0;
        memcpy(s->state.track,s->state.previous,sizeof s->state.track);s->state.interrupted=0;return 1;
    default:return 0;
    }
}
