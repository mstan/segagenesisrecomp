/* Diagnostic contact sheet from verified private owner data. No runtime hooks. */
#include "sonic2_donor_assets.h"
#include "genesis_dac.h"
#include "sha256.h"
#include "png_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv)
{
    if (argc!=4 || (strcmp(argv[1],"amy") && strcmp(argv[1],"s3k"))) {
        fprintf(stderr,"usage: sonic2_donor_inspect amy|s3k owner.bin output.png\n");return 2;
    }
    int amy=!strcmp(argv[1],"amy");
    const char *expected=amy?"9c028944730128f6b9999fc74babf69694b0edab50e3f42cc6b60a185d0b1457":
        "fba0677fde9f76df93f3e98d6310d8af68b9847bde16e253d73cd4dd8134ed23";
    size_t size=amy?0x200000:0x400000;
    uint8_t *rom=malloc(size); if (!rom)return 1;
    FILE *file=fopen(argv[2],"rb");
    if (!file) { free(rom); return 1; }
    size_t got=fread(rom,1,size,file);int extra=fgetc(file);fclose(file);
    if (got!=size || extra!=EOF) { fprintf(stderr,"Wrong donor size\n");free(rom);return 1; }
    uint8_t hash[32];char hex[65];recompui_sha256_compute(rom,size,hash);
    for (unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",hash[i]);
    if (strcmp(hex,expected)) { fprintf(stderr,"Wrong donor SHA-256: %s\n",hex);free(rom);return 1; }
    S2DonorBank bank={0};char error[160];
    if (!s2_donor_decode(rom,size,amy?&s2_amy_171_layout:&s2_sk_knuckles_layout,&bank,error,sizeof error)) {
        fprintf(stderr,"%s\n",error);free(rom);return 1;
    }
    free(rom);
    const int cell=96,width=cell*16,height=cell*16;
    uint32_t *argb=malloc((size_t)width*height*sizeof *argb);if(!argb){s2_donor_free(&bank);return 1;}
    for(int i=0;i<width*height;++i)argb[i]=0xFF282828;
    unsigned visible=0;
    for(unsigned i=0;i<bank.count;++i) {
        const S2DonorFrame *f=&bank.frames[i];
        if(f->pixels)++visible;
        for(unsigned y=0;y<f->height;++y)for(unsigned x=0;x<f->width;++x){
            int dx=cell/2+f->x+x,dy=cell/2+f->y+y;
            if(dx<0 || dy<0 || dx>=cell || dy>=cell)continue;
            unsigned pixel=f->pixels[y*f->width+x];if(!pixel)continue;
            unsigned color=genesis_dac_cram_to_argb(bank.palette[pixel],GENESIS_DAC_NORMAL);
            size_t p=(size_t)((i/16)*cell+dy)*width+(i%16)*cell+dx;
            argb[p]=color;
        }
    }
    int ok=png_write_argb(argv[3],argb,width,height,width)==0;
    printf("%s: SHA-256 verified; %u frames, %u visible; contact sheet %s\n",argv[1],bank.count,visible,ok?"written":"FAILED");
    free(argb);s2_donor_free(&bank);return ok?0:1;
}
