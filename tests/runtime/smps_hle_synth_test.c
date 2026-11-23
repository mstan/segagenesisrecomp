#include "smps_hle_synth.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct Output {unsigned latch,period;} Output;
static int fm(void *u,unsigned p,unsigned r,unsigned v){(void)u;return p<2&&r<256&&v<256;}
static int psg(void *u,unsigned v)
{
    Output *o=u;if(v>255)return 0;
    if(v&128){if((v&0x70)==0)o->latch=v&15;}
    else o->period=o->latch|((v&63)<<4);
    return 1;
}
static int send(SmpsHleSynth *s,SmpsHleEventKind kind,unsigned value,int arg)
{SmpsHleEvent event={kind,7,value,arg};return smps_hle_synth_event(s,&event);}
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
    puts("PASS host PSG output: envelope cursor, accumulated sensitivity, note reset, Z80 raw reset and 68k composition");return 0;
}
