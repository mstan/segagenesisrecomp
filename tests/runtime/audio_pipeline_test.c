/* Production flush + deterministic dummy-device schedule, with an independent
 * frozen CD renderer. No test accessors are added to the shipping interface. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
static void hold_callback(SDL_AudioDeviceID device,int pause)
{(void)pause;SDL_PauseAudioDevice(device,1);}
#define SDL_PauseAudioDevice hold_callback
#include "../../runner/audio.c"
#undef SDL_PauseAudioDevice
#include "../../runner/cd/audio_mix.h"
#include <stdlib.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"pipeline line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
void ref_segacd_audio_mix_init(SegaCDAudioMix *);
SegaCDAudioMixResult ref_segacd_audio_mix_set_active(SegaCDAudioMix *,SegaCDAudioSource,int);
SegaCDAudioMixResult ref_segacd_audio_mix_push(SegaCDAudioMix *,SegaCDAudioSource,uint64_t,int16_t,int16_t);
SegaCDAudioMixResult ref_segacd_audio_mix_set_window(SegaCDAudioMix *,uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,size_t);
SegaCDAudioMixResult ref_segacd_audio_mix_bind_audio_extra(SegaCDAudioMix *);
void ref_segacd_audio_mix_unbind_audio_extra(SegaCDAudioMix *);
void ref_segacd_audio_mix_audio_extra_source(int16_t *,size_t,double);
static SegaCDAudioMix reference_mix,owned_mix;
static int16_t psg[16384],fm[32768],reference_output[32768],expected_output[32768],pull_reference[1024],pull_owned[1024],pull_expected[1024];
static unsigned producer_calls,nonzero_pulls;
static uint32_t rng=0x834721u;
static uint32_t random_value(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static int owned_frame(int16_t *out,size_t n,double rate,const int16_t *f,size_t fn,const int16_t *p,int divisor)
{++producer_calls;return segacd_audio_mix_owned_frame_source(out,n,rate,f,fn,p,divisor);}
static void reset_bridge(void)
{
    rab_reset(&s_bridge);
    memset(s_bridge.ring,0,(size_t)s_bridge.cap*2*sizeof(float));
    /* Force a nonzero pull in short fixtures; the existing DRC suite separately
     * preserves production preroll, concealment, recovery and reset behavior. */
    s_bridge.prime_ms=0;
}
static rab_bridge without_pointers(rab_bridge value)
{value.ring=NULL;value.coeffs=NULL;return value;}
static rab_bridge state_without_pointers(void)
{return without_pointers(s_bridge);}
static int files_equal(const char *a,const char *b)
{
    FILE *fa=fopen(a,"rb"),*fb=fopen(b,"rb");CHECK(fa&&fb);
    unsigned char x[4096],y[4096];int same=1;
    for(;;){size_t na=fread(x,1,sizeof x,fa),nb=fread(y,1,sizeof y,fb);if(na!=nb||memcmp(x,y,na)){same=0;break;}if(!na)break;}
    fclose(fa);fclose(fb);return same;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);CHECK(!SDL_setenv("SDL_AUDIODRIVER","dummy",1));CHECK(!SDL_Init(SDL_INIT_AUDIO));
    CHECK(!audio_init(223721));
    size_t ring_bytes=(size_t)s_bridge.cap*2*sizeof(float);float *ring_reference=malloc(ring_bytes);CHECK(ring_reference);
    char wave_reference[1024],wave_owned[1024];
    CHECK(snprintf(wave_reference,sizeof wave_reference,"%s.reference.wav",argv[1])>0);
    CHECK(snprintf(wave_owned,sizeof wave_owned,"%s.owned.wav",argv[1])>0);
    const size_t lengths[]={1,2,31,64,500,3700,16384};
    const int percents[]={0,1,25,50,75,100};
    for(unsigned k=0;k<42;++k) {
        size_t n=lengths[k%7u],fn=k%5u==0?0:(n+3u)/4u;
        int divisor=k%3u==0?1:k%3u==1?8:64;
        for(unsigned i=0;i<16384;++i){psg[i]=(int16_t)random_value();fm[2*i]=(int16_t)random_value();fm[2*i+1]=(int16_t)random_value();}
        ref_segacd_audio_mix_init(&reference_mix);
        for(unsigned source=0;source<2;++source) {
            int active=(k>>source)&1u;
            CHECK(ref_segacd_audio_mix_set_active(&reference_mix,(SegaCDAudioSource)source,active)==SEGACD_AUDIO_MIX_OK);
            if(active)for(unsigned i=0;i<(k%11u==0?2u:4096u);++i)
                CHECK(ref_segacd_audio_mix_push(&reference_mix,(SegaCDAudioSource)source,(uint64_t)i*(source?300u:384u),
                    (int16_t)random_value(),(int16_t)random_value())==SEGACD_AUDIO_MIX_OK);
        }
        CHECK(ref_segacd_audio_mix_set_window(&reference_mix,1000,k*3917u,40000000,715909,223721,n)==SEGACD_AUDIO_MIX_OK);
        owned_mix=reference_mix;
        audio_set_psg_vol_div(divisor);audio_set_master_volume(percents[k%6u]);
        reset_bridge();
        CHECK(ref_segacd_audio_mix_bind_audio_extra(&reference_mix)==SEGACD_AUDIO_MIX_OK);
        audio_set_extra_source(ref_segacd_audio_mix_audio_extra_source);
        CHECK(!audio_wav_start(wave_reference));audio_flush(k,1,fm,fn,psg,n);audio_wav_stop();
        /* Independently check the actual flush wiring: recover the generated
         * pre-volume stream, apply the OLD integer master formula, and use
         * unchanged rab_push rather than sharing the new scaled producer. */
        FILE *wave=fopen(wave_reference,"rb");CHECK(wave);CHECK(!fseek(wave,44,SEEK_SET));
        CHECK(fread(expected_output,sizeof(int16_t),n*2,wave)==n*2);CHECK(fgetc(wave)==EOF);fclose(wave);
        int expected_gain=percents[k%6u]*256/100;
        for(size_t i=0;i<n*2;++i)expected_output[i]=(int16_t)((int32_t)expected_output[i]*expected_gain/256);
        CHECK(!memcmp(expected_output,s_out,n*2*sizeof(int16_t)));
        rab_bridge expected;CHECK(!rab_init(&expected,&s_bridge.cfg));rab_reset(&expected);
        memset(expected.ring,0,ring_bytes);expected.prime_ms=0;
        rab_push(&expected,expected_output,(int)n);
        rab_bridge actual_input=state_without_pointers(),expected_input=without_pointers(expected);
        CHECK(!memcmp(&actual_input,&expected_input,sizeof actual_input));CHECK(!memcmp(expected.ring,s_bridge.ring,ring_bytes));
        memcpy(reference_output,s_out,n*2*sizeof(int16_t));memcpy(ring_reference,s_bridge.ring,ring_bytes);
        rab_bridge before=state_without_pointers();rab_pull(&s_bridge,pull_reference,512);rab_bridge after=state_without_pointers();
        rab_pull(&expected,pull_expected,512);expected_input=without_pointers(expected);
        CHECK(!memcmp(&after,&expected_input,sizeof after));CHECK(!memcmp(pull_reference,pull_expected,sizeof pull_reference));rab_free(&expected);
        ref_segacd_audio_mix_unbind_audio_extra(&reference_mix);

        reset_bridge();CHECK(segacd_audio_mix_bind_audio_extra(&owned_mix)==SEGACD_AUDIO_MIX_OK);
        audio_set_extra_source(segacd_audio_mix_audio_extra_source);
        audio_set_owned_frame_source(segacd_audio_mix_audio_extra_source,owned_frame);
        unsigned calls=producer_calls;
        CHECK(!audio_wav_start(wave_owned));audio_flush(k,1,fm,fn,psg,n);audio_wav_stop();
        CHECK(producer_calls==calls+1);CHECK(!memcmp(reference_output,s_out,n*2*sizeof(int16_t)));
        CHECK(!memcmp(&reference_mix,&owned_mix,sizeof owned_mix));
        CHECK(!memcmp(ring_reference,s_bridge.ring,ring_bytes));rab_bridge actual=state_without_pointers();CHECK(!memcmp(&before,&actual,sizeof before));
        rab_pull(&s_bridge,pull_owned,512);actual=state_without_pointers();CHECK(!memcmp(&after,&actual,sizeof after));
        CHECK(!memcmp(pull_reference,pull_owned,sizeof pull_owned));CHECK(files_equal(wave_reference,wave_owned));
        for(unsigned i=0;i<1024;++i)if(pull_owned[i]){++nonzero_pulls;break;}
        /* Any extra setter revokes the capability, including restoring the
         * exact same callback. Old owner/context must not be silently reused. */
        audio_set_extra_source(segacd_audio_mix_audio_extra_source);CHECK(!s_frame_source);
        segacd_audio_mix_unbind_audio_extra(&owned_mix);
    }
    CHECK(nonzero_pulls);remove(wave_reference);remove(wave_owned);free(ring_reference);audio_close();SDL_Quit();
    puts("Owned audio frame: frozen CD/base clipping, complete state, pre-volume WAV, nonzero gained ring/pull, underflow and revocation PASS");return 0;
}
