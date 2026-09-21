#include "trilogy_music.h"
#include "video/genesis_machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TrMusic *music[2];
static uint8_t native_banks[0x33],native_pointers[0x66],native_envelopes[78],native_drums[18];
static uint8_t native_fill[2];
static unsigned installed;
static int saved;
static void put(uint8_t *p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static int read8(uint32_t a,uint8_t *value)
{
    /* S1/S2 note fill is already in ticks; S3 multiplies it by the channel
     * divider. Keep native SFX semantics, but skip that multiply for music.
     * push af; ld a,(zUpdatingSFX); or a; jr nz,native; pop af;
     * jp cfNoteFill+3; native: pop af; jp cfNoteFill. */
    static const uint8_t fill[]={0xF5,0x3A,0x19,0x1C,0xB7,0x20,4,0xF1,0xC3,0xE4,0x0C,0xF1,0xC3,0xE1,0x0C};
    if(a>=0xA07F00&&a<0xA07F00+sizeof fill){*value=fill[a-0xA07F00];return 1;}
    if(a>=0x500000&&a<0x580000){unsigned bank=(a-0x500000)>>15,pack=bank/8,cue=bank%8;
        if(pack<2&&cue<TR_MUSIC_CUES&&music[pack]){*value=music[pack]->song[cue][a&0x7FFF];return 1;}}
    if(a>=0x580000&&a<0x590000){unsigned pack=(a-0x580000)>>15;
        if(music[pack]){*value=music[pack]->drums[a&0x7FFF];return 1;}}
    if(a>=0xA07000&&a<0xA07F00){unsigned offset=a-0xA07000,pack=offset/0x800,env=(offset%0x800)/128;
        if(pack<2&&music[pack]&&env<music[pack]->envelopes){*value=music[pack]->envelope[env][a&127];return 1;}}
    return 0;
}
static void restore(void)
{
    if(!installed||!saved)return;uint8_t *z=g_machine.bus.z80_ram;
    memcpy(z+0xB65,native_banks,sizeof native_banks);memcpy(z+0x1618,native_pointers,sizeof native_pointers);
    memcpy(z+0x1387,native_envelopes,sizeof native_envelopes);memcpy(z+0xD6,native_drums,sizeof native_drums);
    memcpy(z+0xC0D,native_fill,2);
    installed=0;
    fprintf(stderr,"[Trilogy audio] Native Sonic 3 score restored\n");
}
void tr_audio_reset(void)
{restore();for(unsigned i=0;i<2;++i){free(music[i]);music[i]=NULL;}saved=0;g_machine.bus.audio_read8=NULL;}
void tr_audio_state(TrStateIO *io)
{
    TR_STATE(io,native_banks);TR_STATE(io,native_pointers);TR_STATE(io,native_envelopes);
    TR_STATE(io,native_drums);TR_STATE(io,native_fill);TR_STATE(io,installed);TR_STATE(io,saved);
    if(io->mode==2)g_machine.bus.audio_read8=read8;
}
int tr_audio_load(unsigned pack,const uint8_t *rom,size_t size)
{
    if(pack<1||pack>2)return 0;
    TrMusic *candidate=malloc(sizeof *candidate);char error[192];
    if(!candidate)return 0;
    if(!tr_music_decode(pack,rom,size,candidate,error,sizeof error)){fprintf(stderr,"[Trilogy audio] %s\n",error);free(candidate);return 0;}
    free(music[pack-1]);music[pack-1]=candidate;
    fprintf(stderr,"[Trilogy audio] Sonic %u: %u original music cues, %u envelopes, %u drum pitches decoded\n",pack,TR_MUSIC_CUES,candidate->envelopes,candidate->drum_count);return 1;
}
void tr_audio_cue(unsigned pack,unsigned id)
{
    g_machine.bus.audio_read8=read8;
    /* Fade/stop/speed commands leave the current driver's tables in place. */
    if(!id||id>0x33)return;
    if(pack<1||pack>2||!music[pack-1]){restore();return;}
    int donor=id==1;
    for(unsigned i=0;i<TR_MUSIC_CUES;++i)if(id==tr_music_ids[i])donor=1;
    if(!donor){restore();return;}
    uint8_t *z=g_machine.bus.z80_ram;
    if(installed==pack&&z[0xB65]==0xA0+(pack-1)*8)return;
    restore();
    if(!saved){
        /* Exact verified S&K driver layout; never patch another driver. */
        if(z[0x1300]!=0x18||z[0x1301]!=0x16||z[0x130A]!=0x87||z[0x130B]!=0x13){
            fprintf(stderr,"[Trilogy audio] Sound driver is not ready\n");return;}
        memcpy(native_banks,z+0xB65,sizeof native_banks);memcpy(native_pointers,z+0x1618,sizeof native_pointers);
        memcpy(native_envelopes,z+0x1387,sizeof native_envelopes);memcpy(native_drums,z+0xD6,sizeof native_drums);saved=1;
        memcpy(native_fill,z+0xC0D,2);
    }
    for(unsigned i=0;i<TR_MUSIC_CUES;++i){unsigned index=tr_music_ids[i]-1;
        z[0xB65+index]=(uint8_t)(0xA0+(pack-1)*8+i);put(z+0x1618+index*2,0x8000);}
    z[0xB65]=z[0xB66];put(z+0x1618,0x8000);
    for(unsigned i=0;i<music[pack-1]->envelopes;++i)
        put(z+0x1387+(tr_music_envelope_ids[i]-1)*2,0x7000+(pack-1)*0x800+i*128);
    for(unsigned i=1;i<=music[pack-1]->drum_count;++i)z[0xD6+i]=(uint8_t)(0xB0+pack-1);
    put(z+0xC0D,0x7F00);
    installed=pack;fprintf(stderr,"[Trilogy audio] Sonic %u score active (cue %02X)\n",pack,id);
}
