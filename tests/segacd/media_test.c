/* Real CDD packet -> emulated sector deadline -> CDC/audio callbacks. No
 * direct success flags or wall-clock sleeps; deterministic synthetic media. */
#include "segacd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
typedef struct {unsigned reads,audio;int fail_lba,corrupt;uint64_t stamp[3];} Fixture;
static uint8_t bios[SEGACD_BIOS_BYTES];
static SegaCD a,b;
static void data_sector(uint8_t raw[2352])
{
    memset(raw,0,2352);memset(raw+1,255,10);raw[13]=2;raw[14]=0x10;raw[15]=1;
    for(unsigned n=16;n<2064;++n)raw[n]=(uint8_t)(n*37u+11u);
    raw[2064]=0x70;raw[2065]=1;raw[2066]=0x10;raw[2067]=0x7D;
}
static int read_sector(void *user,int32_t lba,uint8_t raw[2352])
{
    Fixture *f=user;++f->reads;CHECK(lba>=0 && lba<5);
    if(lba==f->fail_lba)return 0;
    if(lba<2){data_sector(raw);if(f->corrupt)raw[100]^=1;}
    else memset(raw,(int)(unsigned char)lba,2352);
    return 1;
}
static void audio_sector(void *user,uint64_t stamp,int32_t lba,const uint8_t raw[2352])
{
    Fixture *f=user;CHECK(f->audio<3 && lba==(int32_t)(f->audio+2));
    for(unsigned n=0;n<2352;++n)CHECK(raw[n]==lba);
    f->stamp[f->audio++]=stamp;
}
static void prepare(SegaCD *s)
{
    const SegaCDTOC toc={{{-150,0,2,1},{2,3,5,0}},2,5};
    CHECK(segacd_init(s,bios,sizeof bios));CHECK(segacd_cdd_mount(&s->drive,&toc));
    /* Decoder enabled with buffer writes and decoder IRQ enabled. */
    segacd_cdc_address(&s->cdc,1);segacd_cdc_write(&s->cdc,32);
    segacd_cdc_address(&s->cdc,10);segacd_cdc_write(&s->cdc,132);
    segacd_cdd_write(&s->drive,0x37,4);
    uint8_t packet[10]={3,0,0,0,0,2,0,0,0,0};unsigned sum=0;
    for(unsigned n=0;n<9;++n)sum+=packet[n];packet[9]=(uint8_t)(~sum&15u);
    for(unsigned n=0;n<10;++n)segacd_cdd_write(&s->drive,0x42+n,packet[n]);
}
int main(void)
{
    Fixture fa={0,0,-1,0,{0}},fb=fa;SegaCDIO ia={0},ib={0};
    ia.read_sector=ib.read_sector=read_sector;ia.cdda_sector=ib.cdda_sector=audio_sector;
    ia.media_user=ia.cdda_user=&fa;ib.media_user=ib.cdda_user=&fb;
    prepare(&a);prepare(&b);
    segacd_advance_io(&a,SEGACD_SUB_CLOCK,&ia);
    for(unsigned elapsed=0;elapsed<SEGACD_SUB_CLOCK;) {
        unsigned n=(elapsed%1543)+1;if(n>SEGACD_SUB_CLOCK-elapsed)n=SEGACD_SUB_CLOCK-elapsed;
        segacd_advance_io(&b,n,&ib);elapsed+=n;
    }
    CHECK(!memcmp(&a,&b,sizeof a));CHECK(!memcmp(&fa,&fb,sizeof fa));
    CHECK(fa.reads==5 && fa.audio==3 && a.media_sectors==5 && a.cdda_sectors==3 && a.cdc.sectors==2 && !a.media_error);
    CHECK(fa.stamp[0]<fa.stamp[1] && fa.stamp[1]<fa.stamp[2]);
    CHECK(fa.stamp[1]-fa.stamp[0]>=166666 && fa.stamp[1]-fa.stamp[0]<=166667);
    CHECK(fa.stamp[2]-fa.stamp[0]==333333 || fa.stamp[2]-fa.stamp[0]==333334);
    CHECK(a.cdc.block_pointer==2364 && a.cdc.write_address==4704 && !a.cdc.decoder_irq_clocks);
    CHECK(a.cdc.ram[12]==0 && a.cdc.ram[13]==2 && a.cdc.ram[15]==1 && (a.cdc.status[0]&128));
    /* Stop while the final decoder pulse is live, then check its exact edge. */
    prepare(&a);fa=(Fixture){0,0,-1,0,{0}};
    segacd_advance_io(&a,1500000,&ia);CHECK(a.cdc.sectors==2 && a.cdc.decoder_irq_clocks==22500);
    segacd_advance_io(&a,22499,&ia);CHECK(segacd_cdc_irq(&a.cdc));
    segacd_advance_io(&a,1,&ia);CHECK(!segacd_cdc_irq(&a.cdc));
    /* Corrupt EDC is reported by the chip; no fake repair or media success. */
    prepare(&a);fa=(Fixture){0,0,-1,1,{0}};
    segacd_advance_io(&a,1500000,&ia);CHECK(!a.media_error && a.cdc.sectors==2 && (a.cdc.status[0]&1) && !(a.cdc.status[0]&128));
    prepare(&a);fa=(Fixture){0,0,1,0,{0}};
    segacd_advance_io(&a,SEGACD_SUB_CLOCK,&ia);
    CHECK(a.media_error==1 && a.media_error_lba==1 && a.media_sectors==1 && a.cdc.sectors==1 && fa.reads==2 && !fa.audio);
    prepare(&a);segacd_advance(&a,SEGACD_SUB_CLOCK);
    CHECK(a.media_error==1 && a.media_error_lba==0 && !a.media_sectors && !a.cdc.sectors);
    puts("CDD media deadlines, raw data/EDC to CDC, timed audio-sector events, split-step equivalence and read-failure diagnostics pass");
    return 0;
}
