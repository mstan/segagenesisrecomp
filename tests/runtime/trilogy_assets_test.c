#include "../../sonic3k/trilogy_assets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned word(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
int main(int argc,char **argv)
{
    uint8_t bad[16]={0},out[32];uint16_t words[16];
    for(unsigned n=0;n<sizeof bad;++n){assert(!tr_kosinski(bad,n,out,sizeof out));assert(!tr_enigma(bad,n,words,16,0));assert(!tr_nemesis(bad,n,out,sizeof out));}
    if(argc<3){puts("trilogy codecs: malformed input rejected (private ROM checks not requested)");return 0;}
    for(unsigned game=0;game<2;++game){
        FILE *f=fopen(argv[game+1],"rb");assert(f);size_t size=game?0x100000:0x80000;
        uint8_t *rom=malloc(size);assert(rom);assert(fread(rom,1,size,f)==size);assert(fgetc(f)==EOF);fclose(f);
        TrStageAssets *a=malloc(sizeof *a);assert(a);char error[256];
        for(unsigned act=0;act<(game?1u:3u);++act){
            unsigned id=game?0x2000:0x1000+act;
            if(!tr_stage_decode(id,rom,size,a,error,sizeof error)){fprintf(stderr,"stage %04X: %s\n",id,error);return 1;}
            assert(a->id==id&&a->chunk_count<=256&&a->block_count<=768&&a->object_count&&a->ring_count);
            assert(a->start_x<a->max_x&&a->start_y<word(a->layout+4)*128);
            for(unsigned y=0;y<32;++y)for(unsigned plane=0;plane<2;++plane){
                unsigned row=word(a->layout+8+y*4+plane*2),w=word(a->layout+plane*2);
                assert(row>=0x8088&&row+w<=0x9000);
                for(unsigned x=0;x<w;++x)assert(a->layout[row-0x8000+x]<a->chunk_count);
            }
            for(unsigned n=1;n<a->ring_count;++n)assert(a->rings[n-1].x<=a->rings[n].x);
            printf("%04X: %u chunks, %u blocks, %u tiles, %u objects, %u rings; start %04X,%04X; bounds %04X,%04X\n",
                id,a->chunk_count,a->block_count,a->tile_bytes/32,a->object_count,a->ring_count,a->start_x,a->start_y,a->max_x,a->max_y);
            if(argc>3){char path[1200];snprintf(path,sizeof path,"%s/stage-%04X.raw",argv[3],id);f=fopen(path,"wb");assert(f);assert(fwrite(a,1,sizeof *a,f)==sizeof *a);fclose(f);}
        }
        free(a);free(rom);
    }
    return 0;
}
