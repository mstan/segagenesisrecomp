#include "smps_hle_synth.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct Output {unsigned latch,period,last[4],psg_latch,fm_keys;} Output;
static int fm(void *u,unsigned p,unsigned r,unsigned v)
{
    Output *out=u;
    if(p==0 && r==0x28)++out->fm_keys;
    return p<2 && r<256 && v<256;
}
static int psg(void *u,unsigned v)
{
    Output *o=u;if(v>255)return 0;
    if(v&128){
        o->psg_latch=v;
        if((v&0x70)==0)o->latch=v&15;
        if(v&0x10)o->last[(v>>5)&3]=v&15;
    }else o->period=o->latch|((v&63)<<4);
    return 1;
}
static int send(SmpsHleSynth *s,SmpsHleEventKind kind,unsigned value,int arg)
{SmpsHleEvent event={kind,7,value,arg};return smps_hle_synth_event(s,&event);}
static int noise_handoff(SmpsHleSynth *s, Output *out)
{
    SmpsHleEvent noise={SMPS_HLE_NOISE,9,0xE7,0};
    SmpsHleEvent vol={SMPS_HLE_VOLUME,9,24,0};
    SmpsHleEvent takeover={SMPS_HLE_BEGIN_TRACK,9,0,0};
    CHECK(smps_hle_synth_event(s,&noise));
    CHECK(out->last[2]==15 && out->psg_latch==0xE7);
    CHECK(smps_hle_synth_event(s,&vol));
    CHECK(out->last[3]==3);
    CHECK(smps_hle_synth_event(s,&takeover));
    CHECK(out->last[2]==15 && out->last[3]==15);
    return 0;
}
static int clock_output(void *u,const SmpsHleEvent *event)
{
    unsigned *events=u;
    if(event->channel==7 && event->kind==SMPS_HLE_TICK)++events[0];
    if(event->channel==7 && event->kind==SMPS_HLE_REST)++events[1];
    return 1;
}
static int tempo_carry(void)
{
    /* One DAC track and a gated PSG note. The first carry falls on its
     * second sound tick and must expire the gate without moving the cursor. */
    const uint8_t song[]={0,0,1,1,0,0,1,128,
        0,18,0,0,0,20,0,0,0,0,
        0xFF,16,0xFF,8,1,0x91,8,0xFF,16};
    unsigned events[2]={0};SmpsHle hle;
    smps_hle_init(&hle,song,sizeof song,clock_output,events);
    CHECK(smps_hle_music(&hle,0,0));
    CHECK(smps_hle_frame(&hle));
    CHECK(events[0]==1 && events[1]==0);
    const unsigned cursor=hle.state.music.track[7].cursor;
    CHECK(smps_hle_frame(&hle));
    CHECK(events[0]==2 && events[1]==1);
    CHECK(hle.state.music.track[7].cursor==cursor && hle.state.music.track[7].remaining==8);
    return 0;
}
static int tied_voice(SmpsHleSynth *s, Output *out)
{
    SmpsHleEvent note={SMPS_HLE_NOTE,1,0x91,0};
    SmpsHleEvent voice={SMPS_HLE_VOICE,1,25,0};
    CHECK(smps_hle_synth_event(s,&note));
    const unsigned before=out->fm_keys;
    CHECK(smps_hle_synth_event(s,&voice));
    note.value=0x94;
    note.argument=256;
    CHECK(smps_hle_synth_event(s,&note));
    CHECK(out->fm_keys==before);
    return 0;
}
int main(void)
{
    uint8_t data[256]={0};const uint8_t a[]={0x84,1,2,4,6,0x81},b[]={0x84,1,10,20,30,0x81},loop[]={0x84,1,2,0x80};
    memcpy(data+128,a,sizeof a);memcpy(data+144,b,sizeof b);memcpy(data+160,loop,sizeof loop);
    const uint32_t envelopes[]={128,144,160};Output output={0};SmpsHleSynth synth;
    SmpsHleChipOutput callbacks={fm,psg,NULL,NULL,&output};smps_hle_synth_init(&synth,data,sizeof data,callbacks);
    synth.modulation_envelopes=envelopes;synth.modulation_count=3;
    CHECK(send(&synth,SMPS_HLE_BEGIN,0,0));CHECK(send(&synth,SMPS_HLE_NOTE,0xA0,0));unsigned base=output.period;
    CHECK(send(&synth,SMPS_HLE_MODULATION,1,34));CHECK(send(&synth,SMPS_HLE_TICK,0,0));CHECK(output.period==base+2);
    CHECK(send(&synth,SMPS_HLE_MODULATION,2,34));CHECK(send(&synth,SMPS_HLE_TICK,0,0));CHECK(output.period==base+20);
    CHECK(send(&synth,SMPS_HLE_NOTE,0xA0,0));CHECK(send(&synth,SMPS_HLE_TICK,0,0));CHECK(output.period==base+10);
    CHECK(send(&synth,SMPS_HLE_MODULATION,3,34));CHECK(send(&synth,SMPS_HLE_NOTE,0xA0,0));
    for(unsigned n=1;n<=3;n++){CHECK(send(&synth,SMPS_HLE_TICK,0,0));CHECK(output.period==base+2*n);}
    CHECK(send(&synth,SMPS_HLE_NOTE,0xA0,0));CHECK(send(&synth,SMPS_HLE_TICK,0,0));CHECK(output.period==base+2);
    CHECK(send(&synth,SMPS_HLE_MODULATION,2,34));CHECK(send(&synth,SMPS_HLE_MODULATION,0x01010102,33));
    CHECK(send(&synth,SMPS_HLE_TICK,0,0));CHECK(output.period==base+1);
    CHECK(send(&synth,SMPS_HLE_MODULATION,2,34));CHECK(send(&synth,SMPS_HLE_NOTE,0xA0,0));
    CHECK(send(&synth,SMPS_HLE_MODULATION,0x01010102,14));CHECK(send(&synth,SMPS_HLE_TICK,0,0));CHECK(output.period==base+10);
    CHECK(send(&synth,SMPS_HLE_TICK,0,0));CHECK(output.period==base+21);
    CHECK(noise_handoff(&synth,&output)==0);
    CHECK(tempo_carry()==0);
    CHECK(tied_voice(&synth,&output)==0);
    puts("PASS PSG modulation/noise, tempo-carry note gates, and tied FM voice changes");return 0;
}
