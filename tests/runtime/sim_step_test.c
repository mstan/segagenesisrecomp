#include "sim_step.h"
#include "game_spec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
static int phase,accepted=1,continue_tick=1,pre_calls,post_calls,state;
static void pre(uint64_t tick)
{
    CHECK(phase==1 && tick==genesis_sim_tick_count());
    CHECK(genesis_sim_pad(0)==0x31);phase=2;pre_calls++;state+=7;
}
static void post(uint64_t tick)
{CHECK(phase==8 && tick==genesis_sim_tick_count());phase=9;post_calls++;state+=11;}
const GameSpec g_game_spec={.on_frame_pre=pre,.on_frame_post=post};
int genesis_replay_begin(GenesisSimInput *input)
{CHECK(!phase);input->pad[0]=0x31;phase=1;return accepted;}
void genesis_replay_end(void){CHECK(phase==9);phase=10;}
void glue_reset_frame_sync(void){CHECK(phase==2);phase=3;}
void glue_run_game_frame(void){CHECK(phase==3);phase=4;}
void machine_set_pad_type(int p,int type){(void)p;(void)type;CHECK(phase==4);}
void machine_set_pad(int p,uint16_t buttons){(void)p;(void)buttons;CHECK(phase==4);}
void glue_sched_frame_begin(void){CHECK(phase==4);phase=5;}
void machine_run_frame_ex(GenesisScanlineSink sink,void *user,const GenesisScanlineProducer *producer)
{(void)sink;(void)user;(void)producer;CHECK(phase==5);phase=6;state*=3;}
void audio_mixer_drain(uint32_t cycles,int16_t *fm,size_t fc,size_t *fn,
                       int16_t *psg,size_t pc,size_t *pn)
{(void)fm;(void)fc;(void)psg;(void)pc;CHECK(phase==6 && cycles==895780);*fn=*pn=0;phase=7;}
void glue_service_vblank(void){CHECK(phase==7);}
void glue_end_of_wall_frame(void){CHECK(phase==7);phase=8;}
static int stop(void *ctx){(void)ctx;return continue_tick;}
int main(void)
{
    GenesisSimInput input={0};
    CHECK(genesis_sim_step(&input,NULL,NULL));CHECK(state==32 && pre_calls==1 && post_calls==1);
    CHECK(genesis_sim_tick_count()==1 && phase==10);
    /* Restored state and sealed input reproduce the same transition. */
    phase=state=0;genesis_sim_set_tick_count(0);
    CHECK(genesis_sim_step(&input,NULL,NULL));CHECK(state==32 && pre_calls==2 && post_calls==2);
    phase=0;accepted=0;
    CHECK(!genesis_sim_step(&input,NULL,NULL));CHECK(pre_calls==2 && post_calls==2);
    phase=0;accepted=1;continue_tick=0;
    GenesisSimHooks hooks={.post_drain=stop};
    CHECK(!genesis_sim_step(&input,NULL,&hooks));CHECK(pre_calls==3 && post_calls==2);
    CHECK(genesis_sim_tick_count()==1);
    puts("Sealed tick game callbacks, replay parity, rejection and incomplete tick PASS");
    return 0;
}
