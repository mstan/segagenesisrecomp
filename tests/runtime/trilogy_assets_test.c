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
            uint8_t animation_vram[65536]={0};
            tr_stage_animate(a,0,animation_vram);
            if(!game){
                assert(a->tile_bytes>=0x380*32&&a->animation_count==3);
                assert(!memcmp(animation_vram+0x378*32,rom+0x66AD6,0x100));
                tr_stage_animate(a,6,animation_vram);
                assert(!memcmp(animation_vram+0x378*32,rom+0x66BD6,0x100));
                tr_stage_animate(a,16,animation_vram);
                assert(!memcmp(animation_vram+0x35C*32,rom+0x66ED6,0x200));
                tr_stage_animate(a,136,animation_vram);
                assert(!memcmp(animation_vram+0x36C*32,rom+0x673D6,0x180));
                assert(a->sprite_palette[12]==0xE);
            }else{
                assert(a->tile_bytes>=0x39E*32&&a->animation_count==5);
                assert(!memcmp(animation_vram+0x396*32,rom+0x497D4,64));
                tr_stage_animate(a,128,animation_vram);
                assert(!memcmp(animation_vram+0x394*32,rom+0x49754,64));
                assert(a->sprite_palette[12]==0xE);
            }
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
