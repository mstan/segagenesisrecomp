/* Exercise the actual production publication with a paused dummy callback.
 * Private inclusion avoids adding a runtime-only inspection API. */
#define SDL_MAIN_HANDLED
#include "../../runner/audio.c"
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static void add_source(int16_t *out,size_t count,double rate)
{
    CHECK(rate==223721.0);
    for(size_t i=0;i<count*2;++i) {
        int v=out[i]+1234;
        out[i]=(int16_t)(v>32767?32767:v);
    }
}

int main(int argc,char **argv)
{
    CHECK(argc==2);
    CHECK(!SDL_setenv("SDL_AUDIODRIVER","dummy",1));
    CHECK(!SDL_Init(SDL_INIT_AUDIO)); CHECK(!audio_init(223721));
    SDL_PauseAudioDevice(s_dev,1);
    int16_t fm[194],psg[251],original[502],gained[502],wav[502];
    for(unsigned i=0;i<194;++i)fm[i]=(int16_t)(i*3767-32768);
    for(unsigned i=0;i<251;++i)psg[i]=(int16_t)(i*1999);
    const int percentages[]={0,37,100};
    for(unsigned g=0;g<3;++g)for(unsigned extra=0;extra<2;++extra) {
        audio_discard_playback();
        rab_bridge expected; CHECK(!rab_init(&expected,&s_bridge.cfg));
        audio_set_master_volume(percentages[g]);
        audio_set_extra_source(extra?add_source:NULL);
        for(unsigned i=0;i<251;++i)for(unsigned c=0;c<2;++c) {
            int v=(int32_t)psg[i]/8+(int32_t)fm[(i*97/251)*2+c];
            if(v>32767)v=32767;else if(v< -32768)v=-32768;
            if(extra){v+=1234;if(v>32767)v=32767;}
            original[i*2+c]=(int16_t)v;
            gained[i*2+c]=(int16_t)(v*(percentages[g]*256/100)/256);
        }
        rab_push(&expected,gained,251);
        CHECK(!audio_wav_start(argv[1]));
        audio_flush(123,1,fm,97,psg,251);
        audio_wav_stop();
        CHECK(!memcmp(s_out,gained,sizeof gained));
        CHECK(s_bridge.in_count==expected.in_count);
        CHECK(s_bridge.out_pos==expected.out_pos);
        CHECK(!memcmp(&s_bridge.stats,&expected.stats,sizeof expected.stats));
        CHECK(!memcmp(s_bridge.ring,expected.ring,(size_t)expected.cap*2*sizeof(float)));
        FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(!fseek(f,44,SEEK_SET));
        CHECK(fread(wav,sizeof wav,1,f)==1);CHECK(fgetc(f)==EOF);fclose(f);
        CHECK(!memcmp(original,wav,sizeof wav));
        rab_free(&expected);
    }
    audio_set_playback_enabled(0);
    audio_flush(124,1,fm,97,psg,251); CHECK(s_bridge.in_count==0);
    audio_set_playback_enabled(1);SDL_PauseAudioDevice(s_dev,1);
    audio_flush(125,1,fm,97,psg,251);CHECK(s_bridge.in_count==251);
    audio_discard_playback();CHECK(s_bridge.in_count==0);
    audio_flush(126,0,fm,97,psg,251);CHECK(s_bridge.in_count==0);
    audio_close();SDL_Quit();remove(argv[1]);
    puts("PASS production audio ownership: gain, extra source, pre-volume WAV, ring, pause/reset/turbo");
    return 0;
}
