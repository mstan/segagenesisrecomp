#include "segacd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static SegaCDDrive d;static SegaCD cd;static uint8_t bios[SEGACD_BIOS_BYTES];static int32_t sectors[256];static unsigned sector_count;
static unsigned sum(void){unsigned n=0;for(unsigned i=0;i<10;++i)n+=d.rx[i];return n&15;}
static void command(unsigned cmd,unsigned parameter,unsigned a,unsigned b)
{
    uint8_t p[10]={0};p[0]=(uint8_t)cmd;p[3]=(uint8_t)parameter;p[4]=(uint8_t)a;p[5]=(uint8_t)b;
    unsigned n=0;for(unsigned i=0;i<9;++i)n+=p[i];p[9]=(uint8_t)(~n&15u);
    for(unsigned i=0;i<10;++i)segacd_cdd_write(&d,0x42+i,p[i]);
    CHECK(segacd_cdd_read(&d,0x37)&1);segacd_cdd_advance(&d,1250,NULL,NULL);CHECK(!(segacd_cdd_read(&d,0x37)&1) && sum()==15);
}
static void sector(void*u,int32_t lba){(void)u;CHECK(sector_count<256);sectors[sector_count++]=lba;}
int main(void)
{
    segacd_cdd_init(&d);CHECK(d.rx[0]==0 && d.rx[9]==15 && sum()==15 && segacd_cdd_read(&d,0x37)==0 && segacd_cdd_read(&d,0x36)==1);
    segacd_cdd_write(&d,0x37,4);CHECK(segacd_cdd_next_event(&d)==166667);CHECK(!segacd_cdd_advance(&d,166666,NULL,NULL));CHECK(segacd_cdd_advance(&d,1,NULL,NULL));
    SegaCDTOC toc={0};toc.count=2;toc.leadout=750;toc.track[0]=(SegaCDTrack){-150,0,300,1};toc.track[1]=(SegaCDTrack){300,450,750,0};
    CHECK(segacd_cdd_mount(&d,&toc));CHECK(d.reset_handshake && d.rx[0]==0 && d.rx[9]==15);
    command(0,0,0,0);for(unsigned n=0;n<9;++n)CHECK(d.rx[n]==0);CHECK(d.rx[9]==15);
    SegaCDTOC bad=toc;bad.track[1].index00=299;CHECK(!segacd_cdd_mount(&d,&bad));
    command(2,4,0,0);CHECK(d.rx[1]==4 && d.rx[2]==0 && d.rx[3]==1 && d.rx[4]==0 && d.rx[5]==2);
    command(2,3,0,0);CHECK(d.rx[1]==3 && d.rx[4]==1 && d.rx[5]==2 && d.rx[6]==0 && d.rx[7]==0); /* leadout 00:12:00 */
    command(2,5,0,1);CHECK(d.rx[4]==0 && d.rx[5]==2 && d.rx[6]==8 && d.rx[8]==1); /* data 00:02:00, data flag */
    command(2,5,0,2);CHECK(d.rx[4]==0 && d.rx[5]==8 && d.rx[6]==0 && d.rx[8]==2); /* audio 00:08:00 */
    /* Track IDs in report 5 are decimal units, including tracks 10..99;
     * masking to a hex nibble falsely rejects a normal multi-track disc. */
    SegaCDTOC many={0};many.count=99;many.leadout=99*75;
    for(unsigned n=0;n<99;++n)many.track[n]=(SegaCDTrack){n?(int32_t)n*75:-150,(int32_t)n*75,(int32_t)(n+1)*75,(uint8_t)(n==0)};
    CHECK(segacd_cdd_mount(&d,&many));
    for(unsigned track=1;track<=99;++track){command(2,5,track/10,track%10);CHECK(d.rx[8]==track%10 && d.rx[1]==5);}
    command(2,0,0,0);CHECK(d.rx[8]==4);command(2,1,0,0);CHECK(d.rx[8]==4);
    CHECK(segacd_cdd_mount(&d,&toc));
    /* Read sector zero, wait for the emulated seek, then deliver raw sectors. */
    command(3,0,0,2);CHECK(d.status==2 && d.seek_deadline>d.cycles);
    uint32_t delay=(uint32_t)(d.seek_deadline-d.cycles);segacd_cdd_advance(&d,delay,sector,NULL);CHECK(d.status==1);
    sector_count=0;segacd_cdd_advance(&d,12500000,sector,NULL);CHECK(sector_count==75 && sectors[0]==0 && sectors[74]==74);
    command(6,0,0,0);int32_t at=d.lba;segacd_cdd_advance(&d,1000000,sector,NULL);CHECK(d.status==4 && d.lba==at);
    command(7,0,0,0);CHECK(d.status==1);command(1,0,0,0);CHECK(d.status==9 && d.rx[0]==0);
    command(13,0,0,0);CHECK(d.tray_open && d.rx[0]==5);command(12,0,0,0);CHECK(!d.tray_open && d.rx[0]==9);
    /* Wrong checksum is a protocol error, not an executed disc operation. */
    for(unsigned i=0;i<10;++i)segacd_cdd_write(&d,0x42+i,0);segacd_cdd_advance(&d,1250,NULL,NULL);CHECK(d.rx[0]==6 && sum()==15);
    command(5,0,0,0);CHECK(d.unsupported_command==5 && d.rx[0]==7);
    /* Power/IRQ gating and read-only status bytes on the actual SUB bus. */
    CHECK(segacd_init(&cd,bios,sizeof bios));CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8037,4)==SEGACD_OK);
    CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8033,16)==SEGACD_OK);segacd_advance(&cd,166667);CHECK(segacd_irq_level(&cd)==4);
    segacd_irq_ack(&cd,4);CHECK(!segacd_irq_level(&cd));
    uint16_t v=0;CHECK(segacd_read16(&cd,SEGACD_SUB,0xFF8038,&v)==SEGACD_OK && v==0);
    CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF8038,0xFFFF)==SEGACD_OK);CHECK(segacd_read16(&cd,SEGACD_SUB,0xFF8038,&v)==SEGACD_OK && v==0);
    puts("CDD nibbles/checksums, bounded TOC queries, emulated sector cadence, seek/pause/resume/tray state and SUB IRQ4 pass (timing profile provisional)");return 0;
}
