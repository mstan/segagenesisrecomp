/* Private owner-ROM preview. No extracted art is shipped. */
#include "sonic2_save_assets.h"
#include "sonic2_save_draw.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv)
{
    if (argc<3 || argc>6) { fprintf(stderr,"usage: sonic2_save_preview donor.bin output.ppm [selection [frame [normal|empty|delete|confirm]]]\n"); return 2; }
    FILE *f=fopen(argv[1],"rb"); if (!f) return 2;
    uint8_t *rom=malloc(0x400000); if (!rom) { fclose(f); return 2; }
    size_t n=fread(rom,1,0x400000,f); int extra=fgetc(f); fclose(f);
    if (n!=0x400000 || extra!=EOF) { free(rom); return 2; }
    uint8_t hash[32]; char hex[65]; recompui_sha256_compute(rom,n,hash);
    for (unsigned i=0;i<32;++i) snprintf(hex+2*i,3,"%02x",hash[i]);
    if (strcmp(hex,"fba0677fde9f76df93f3e98d6310d8af68b9847bde16e253d73cd4dd8134ed23")) {
        free(rom); fprintf(stderr,"Wrong donor identity\n"); return 2;
    }
    S2SaveAssets *a=NULL; char error[192];
    int ok=s2_save_assets_decode(rom,n,&a,error,sizeof error); free(rom);
    if (!ok) { fprintf(stderr,"%s\n",error); return 1; }
    S2SaveView v={0}; v.selection=argc>3?(unsigned)atoi(argv[3]):1; v.frame=argc>4?(unsigned)atoi(argv[4]):4;
    if (v.selection>9) { s2_save_assets_free(&a); return 2; }
    v.cursor=40+(int)v.selection*104; v.scroll=v.cursor-160;
    if (v.scroll<0) v.scroll=0; if (v.scroll>704) v.scroll=704;
    v.erase=argc>5 && (!strcmp(argv[5],"delete") || !strcmp(argv[5],"confirm"));
    v.confirm=argc>5 && !strcmp(argv[5],"confirm"); v.delete_frame=v.frame;
    v.delete_x=v.erase?v.cursor-(v.selection==9?8:0):968;
    s2_campaign_new(&v.data,0); v.data.slots[0]=(S2CampaignSlot){S2_SAVE_COMPLETE,1,0x7F};
    s2_campaign_new(&v.data,1); v.data.slots[1]=(S2CampaignSlot){S2_SAVE_ACTIVE,3,0x15};
    if (argc>5 && !strcmp(argv[5],"empty")) memset(&v.data,0,sizeof v.data);
    f=fopen(argv[2],"wb"); if (!f) { s2_save_assets_free(&a); return 2; }
    fprintf(f,"P6\n320 224\n255\n");
    for (int y=0;y<224;++y) {
        uint32_t row[320]; s2_save_draw_line(a,&v,y,row,320);
        for (int x=0;x<320;++x) { fputc((row[x]>>16)&255,f); fputc((row[x]>>8)&255,f); fputc(row[x]&255,f); }
    }
    ok=!ferror(f); if (fclose(f)) ok=0;
    s2_save_assets_free(&a); return ok?0:1;
}
