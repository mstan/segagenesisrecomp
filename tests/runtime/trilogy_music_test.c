#include "../../sonic3k/trilogy_music.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv)
{
    TrMusic *music=malloc(sizeof *music);assert(music);char error[256];
    if(argc!=3){puts("Private verified donor ROM arguments required");free(music);return 0;}
    for(unsigned pack=1;pack<=2;++pack){size_t size=pack==1?0x80000:0x100000;uint8_t *rom=malloc(size);assert(rom);
        FILE *f=fopen(argv[pack],"rb");assert(f);assert(fread(rom,1,size,f)==size);fclose(f);
        if(!tr_music_decode(pack,rom,size,music,error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
        for(unsigned i=0;i<TR_MUSIC_CUES;++i){assert(music->song_bytes[i]>6&&music->song_bytes[i]<0x8000);
            printf("S%u cue %02X: %u bytes, tempo %02X\n",pack,tr_music_ids[i],music->song_bytes[i],music->song[i][5]);}
        printf("S%u: %u envelopes, %u drum pitches\n",pack,music->envelopes,music->drum_count);free(rom);
    }
    free(music);return 0;
}
