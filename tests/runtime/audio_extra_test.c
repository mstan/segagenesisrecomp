/* Exercise the production mixer, generated WAV tap and callback lifecycle
 * under a dummy SDL device; no speaker output or game data is needed. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned calls;
static void add(int16_t *out,size_t n,double rate)
{
    CHECK(rate==223721.0);calls++;
    for(size_t i=0;i<n*2;i++){int v=out[i]+1000;out[i]=(int16_t)(v>32767?32767:v);}
}
static void replace(int16_t *out,size_t n,double rate)
{CHECK(rate==223721.0);calls++;for(size_t i=0;i<n*2;i++)out[i]=-1234;}
int main(int argc,char **argv)
{
    CHECK(argc==2);CHECK(!SDL_setenv("SDL_AUDIODRIVER","dummy",1));CHECK(!SDL_Init(SDL_INIT_AUDIO));
    int16_t psg[64],fm[128],samples[64*2*5];
    for(unsigned i=0;i<64;i++){psg[i]=(int16_t)(i*8);fm[i*2]=100;fm[i*2+1]=-100;}
    CHECK(!audio_init(223721));audio_set_master_volume(0);audio_set_playback_enabled(0);
    CHECK(!audio_wav_start(argv[1]));
    audio_flush(0,0,fm,64,psg,64);
    audio_set_extra_source(add);audio_flush(1,0,fm,64,psg,64);CHECK(calls==1);
    audio_set_extra_source(NULL);audio_flush(2,0,fm,64,psg,64);CHECK(calls==1);
    audio_set_extra_source(replace);audio_flush(3,0,fm,64,psg,64);CHECK(calls==2);
    audio_close();CHECK(!audio_init(223721));audio_set_playback_enabled(0);
    audio_flush(4,0,fm,64,psg,64);CHECK(calls==2);audio_wav_stop();audio_close();
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(!fseek(f,44,SEEK_SET));
    CHECK(fread(samples,sizeof samples,1,f)==1);CHECK(fgetc(f)==EOF);fclose(f);
    for(unsigned i=0;i<64;i++) {
        CHECK(samples[i*2]==(int)i+100 && samples[i*2+1]==(int)i-100);
        CHECK(samples[128+i*2]==samples[i*2]+1000);
        CHECK(samples[128+i*2+1]==samples[i*2+1]+1000);
    }
    CHECK(!memcmp(samples,samples+256,256));CHECK(!memcmp(samples,samples+512,256));
    for(unsigned i=384;i<512;i++)CHECK(samples[i]==-1234);
    SDL_Quit();remove(argv[1]);puts("Extra audio: base PCM, pre-volume WAV, registration/replacement/unregister/close passed");return 0;
}
