#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>
extern "C" {
#include "audio/mixer.h"
#include "audio/event_queue.h"
#include "audio/ym2612.h"
#include "audio/sn76489.h"
#include "engine_worker.h"
void ref_audio_mixer_init(void);
void ref_audio_mixer_drain(uint32_t,int16_t *,size_t,size_t *,int16_t *,size_t,size_t *);
void ref_audio_event_push(uint32_t,uint8_t,uint8_t);
size_t ref_audio_event_rb_save(void *,size_t);
int ref_audio_event_rb_load(const void *,size_t);
size_t ref_ym2612_rb_save(void *,size_t);
int ref_ym2612_rb_load(const void *,size_t);
size_t ref_psg_rb_save(void *,size_t);
int ref_psg_rb_load(const void *,size_t);
unsigned long g_snd_frame=0,g_snd_mc=0;
unsigned g_snd_pcz=0,g_snd_line=0;
void snd_trace_chip(int,uint8_t,uint8_t){ }
void fm_shadow_init(void){ }
void fm_shadow_advance(uint32_t){ }
void fm_shadow_write(uint8_t,uint8_t){ }
void fm_shadow_verify_and_substitute(int16_t *,size_t){ }
static char *reference_trace;
char *ref_getenv(const char *name)
{return !std::strcmp(name,"GENESIS_AUDIO_APPLIEDDUMP")?reference_trace:std::getenv(name);}
}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"Audio worker oracle line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static uint32_t random_state=0x293adb19u;
static uint32_t rnd(){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void environment(const char *name,const char *value)
{
#ifdef _WIN32
    CHECK(_putenv_s(name,value)==0);
#else
    CHECK(setenv(name,value,1)==0);
#endif
}
static void execute(AudioMixerJob fm,void *fm_context,AudioMixerJob psg,void *psg_context,void *worker)
{
    CHECK(genesis_worker_start((GenesisWorker *)worker,psg,psg_context));
    fm(fm_context);genesis_worker_join((GenesisWorker *)worker);
}
using Save=size_t (*)(void *,size_t);
using Load=int (*)(const void *,size_t);
static std::vector<uint8_t> snapshot(Save save)
{size_t n=save(nullptr,0);CHECK(n);std::vector<uint8_t> data(n);CHECK(save(data.data(),n)==n);return data;}
static void parity()
{
    CHECK(snapshot(ym2612_rb_save)==snapshot(ref_ym2612_rb_save));
    CHECK(snapshot(psg_rb_save)==snapshot(ref_psg_rb_save));
    CHECK(snapshot(audio_event_rb_save)==snapshot(ref_audio_event_rb_save));
}
static void push(uint32_t stamp,uint8_t port,uint8_t value)
{audio_event_push(stamp,port,value);ref_audio_event_push(stamp,port,value);}
static void restore_both(Save save,Save original,Load load,Load original_load)
{
    auto a=snapshot(save),b=snapshot(original);CHECK(a==b);
    CHECK(load(a.data(),a.size())&&original_load(b.data(),b.size()));
}
int main(int argc,char **argv)
{
    CHECK(argc==3);reference_trace=argv[1];environment("GENESIS_AUDIO_APPLIEDDUMP",argv[2]);
    environment("GENESIS_AUDIO_SHADOW","0");
    SDL_SetMainReady();CHECK(SDL_Init(SDL_INIT_TIMER)==0);
    GenesisWorker *worker=genesis_worker_create();CHECK(worker);
    audio_mixer_init();ref_audio_mixer_init();audio_mixer_set_executor(execute,worker);
    constexpr uint32_t end=895780u;
    /* Realistic large deferred DAC burst, including an address/data pair near
     * the frame split; ports retain exact original generic event semantics. */
    for(uint32_t i=0;i<60000;i++){
        push(i*2000u,AUDIO_PORT_FM1_ADDR,0x2a);
        push(i*2000u+1u,AUDIO_PORT_FM1_DATA,(uint8_t)i);
    }
    push(end-1,AUDIO_PORT_FM2_ADDR,0xb4);push(end+1,AUDIO_PORT_FM2_DATA,0xc0);
    std::vector<int16_t> af(8192,-1337),bf(8192,-1337),ap(16384,-2337),bp(16384,-2337);
    for(unsigned frame=0;frame<160;frame++){
        g_snd_frame=frame;
        for(unsigned i=0;i<80;i++)push(rnd()%(end*2u),(uint8_t)(rnd()%5u),(uint8_t)rnd());
        size_t fm_cap=frame%4==0?0:frame%4==1?31:frame%4==2?1024:4096;
        size_t psg_cap=frame%4==0?127:frame%4==1?0:frame%4==2?4096:16384;
        bool discard_fm=frame%13==0,discard_psg=frame%17==0;
        size_t fa=999,fb=999,pa=999,pb=999;
        std::fill(af.begin(),af.end(),-1337);bf=af;std::fill(ap.begin(),ap.end(),-2337);bp=ap;
        audio_mixer_drain(end,discard_fm?nullptr:af.data(),fm_cap,&fa,discard_psg?nullptr:ap.data(),psg_cap,&pa);
        ref_audio_mixer_drain(end,discard_fm?nullptr:bf.data(),fm_cap,&fb,discard_psg?nullptr:bp.data(),psg_cap,&pb);
        CHECK(fa==fb&&pa==pb&&af==bf&&ap==bp);parity();
        if(frame%19==0){
            restore_both(ym2612_rb_save,ref_ym2612_rb_save,ym2612_rb_load,ref_ym2612_rb_load);
            restore_both(psg_rb_save,ref_psg_rb_save,psg_rb_load,ref_psg_rb_load);
            restore_both(audio_event_rb_save,ref_audio_event_rb_save,audio_event_rb_load,ref_audio_event_rb_load);parity();
        }
    }
    CHECK(genesis_worker_completed(worker)==160);
    /* The original API permits overlapping output arrays. These arms MUST
     * retain serial FM-then-PSG writes, including an overflowing declared cap. */
    for(unsigned i=0;i<4;i++){
        g_snd_frame=160+i;std::vector<int16_t> a(32768,-317),b=a;
        push(17,AUDIO_PORT_PSG,0x80);push(19,AUDIO_PORT_PSG,0x0f);
        size_t fa=0,fb=0,pa=0,pb=0;unsigned offset=i==0?0:i==1?1:17;
        size_t cap=i==3?std::numeric_limits<size_t>::max():512;
        audio_mixer_drain(end,a.data(),cap,&fa,a.data()+offset,1024,&pa);
        ref_audio_mixer_drain(end,b.data(),cap,&fb,b.data()+offset,1024,&pb);
        CHECK(fa==fb&&pa==pb&&a==b);parity();CHECK(genesis_worker_completed(worker)==160);
    }
    audio_mixer_set_executor(nullptr,nullptr);genesis_worker_destroy(worker);
    std::fflush(nullptr);std::ifstream a(argv[1],std::ios::binary),b(argv[2],std::ios::binary);
    CHECK(a.good()&&b.good());std::string x((std::istreambuf_iterator<char>(a)),{}),y((std::istreambuf_iterator<char>(b)),{});
    CHECK(!x.empty()&&x==y);SDL_Quit();
    std::puts("E18 real threaded mixer versus frozen original: samples, complete FM/PSG/queue states, deferred pairs/caps/null outputs, restore and merged applied diagnostics exact");
    return 0;
}
