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
static void track_skip_fields(unsigned direction_high,unsigned direction_low,unsigned count)
{
    uint8_t p[10]={0};p[0]=10;p[2]=(uint8_t)(direction_high&15u);p[3]=(uint8_t)(direction_low&15u);p[4]=(uint8_t)((count>>12)&15u);p[5]=(uint8_t)((count>>8)&15u);p[6]=(uint8_t)((count>>4)&15u);p[7]=(uint8_t)(count&15u);
    unsigned n=0;for(unsigned i=0;i<9;++i)n+=p[i];p[9]=(uint8_t)(~n&15u);
    for(unsigned i=0;i<10;++i)segacd_cdd_write(&d,0x42+i,p[i]);
    CHECK(segacd_cdd_read(&d,0x37)&1);segacd_cdd_advance(&d,1250,NULL,NULL);CHECK(!(segacd_cdd_read(&d,0x37)&1) && sum()==15);
}
static void track_skip(unsigned inward,unsigned count){track_skip_fields(0,inward?1:0,count);}
static void sector(void*u,int32_t lba){(void)u;CHECK(sector_count<256);sectors[sector_count++]=lba;}
int main(void)
{
    segacd_cdd_init(&d);CHECK(d.rx[0]==0 && d.rx[9]==15 && sum()==15 && segacd_cdd_read(&d,0x37)==0 && segacd_cdd_read(&d,0x36)==1);
    segacd_cdd_write(&d,0x37,4);CHECK(segacd_cdd_next_event(&d)==166667);CHECK(!segacd_cdd_advance(&d,166666,NULL,NULL));CHECK(segacd_cdd_advance(&d,1,NULL,NULL));
    SegaCDTOC toc={0};toc.count=2;toc.leadout=750;toc.track[0]=(SegaCDTrack){-150,0,300,1};toc.track[1]=(SegaCDTrack){300,450,750,0};
    CHECK(segacd_cdd_mount(&d,&toc));CHECK(d.status==0 && !d.motor_started && d.rx[0]==0 && d.rx[9]==15);
    command(0,0,0,0);for(unsigned n=0;n<9;++n)CHECK(d.rx[n]==0);CHECK(d.rx[9]==15);
    segacd_cdd_advance(&d,500000,sector,NULL);command(0,0,0,0);
    CHECK(d.status==0 && !d.motor_started && !sector_count);
    for(unsigned n=0;n<9;++n)CHECK(d.rx[n]==0);CHECK(d.rx[9]==15);
    command(2,4,0,0);CHECK(d.status==9 && d.rx[0]==9 && d.motor_started && d.report_valid);
    CHECK(d.rx[1]==4 && d.rx[3]==1 && d.rx[5]==2);
    segacd_cdd_advance(&d,(uint32_t)(segacd_cdd_next_event(&d)-d.cycles),sector,NULL);CHECK(d.status==4 && !sector_count);
    /* Motor stop is status zero; nine is a transient TOC-read state. Neither
     * phase may deliver sectors. Progress follows device time without a CPU
     * command manufacturing the paused/ready state. */
    command(1,0,0,0);CHECK(d.status==0 && !d.report_valid);
    for(unsigned n=0;n<9;++n)CHECK(d.rx[n]==0);CHECK(d.rx[9]==15);
    uint32_t motor_delay=(uint32_t)(segacd_cdd_next_event(&d)-d.cycles);
    CHECK(motor_delay>0);CHECK(!segacd_cdd_advance(&d,motor_delay-1,sector,NULL));CHECK(d.status==0);
    CHECK(segacd_cdd_advance(&d,1,sector,NULL));CHECK(d.status==9 && d.rx[0]==0 && d.rx[9]==15 && !sector_count);
    motor_delay=(uint32_t)(segacd_cdd_next_event(&d)-d.cycles);
    CHECK(segacd_cdd_advance(&d,motor_delay,sector,NULL));CHECK(d.status==4 && d.rx[0]==0 && d.rx[9]==15 && !sector_count);
    /* The host must observe the completed STOP packet, even when its next
     * interrupt arrives after mechanical state has already advanced. Only
     * an explicit new status request publishes the current paused state. */
    command(0,0,0,0);CHECK(d.rx[0]==4 && d.rx[9]==11);
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
    /* Seek to program zero, including the drive's three-sector pre-roll;
     * all delivered sectors retain their actual positions and cadence. */
    command(3,0,0,2);CHECK(d.status==2 && d.seek_target==-3 && d.seek_deadline>d.cycles);
    uint32_t delay=(uint32_t)(d.seek_deadline-d.cycles);segacd_cdd_advance(&d,delay,sector,NULL);CHECK(d.status==1);
    sector_count=0;segacd_cdd_advance(&d,12500000,sector,NULL);CHECK(sector_count==75 && sectors[0]==-3 && sectors[74]==71);
    command(6,0,0,0);int32_t at=d.lba;segacd_cdd_advance(&d,1000000,sector,NULL);CHECK(d.status==4 && d.lba==at);
    /* A uses a hexadecimal radial-turn count. This deterministic functional
     * profile uses ECMA-130's nominal pitch/geometry plus an explicit 10,000
     * turns/s usability timescale; it does not claim Sega motor calibration. */
    track_skip(0,2);CHECK(d.status==10 && d.track_skip_active && d.rx[0]==10 && d.track_skip_count==2);
    int32_t skip_start=d.lba;uint64_t skip_at=d.track_skip_deadline;
    CHECK(skip_at>d.cycles && segacd_cdd_next_event(&d)<=skip_at);
    segacd_cdd_advance(&d,(uint32_t)(skip_at-d.cycles-1),sector,NULL);CHECK(d.status==10 && d.lba==skip_start);
    segacd_cdd_advance(&d,1,sector,NULL);CHECK(d.status==10 && d.lba>skip_start && d.rx[0]==10);
    CHECK(sector_count==75); /* Tracking never streams sectors. */
    segacd_cdd_advance(&d,(uint32_t)(d.track_skip_deadline-d.cycles),sector,NULL);
    CHECK(d.status==4 && !d.track_skip_active && d.lba==d.track_skip_target_lba && d.rx[0]==10);
    command(0,0,0,0);CHECK(d.rx[0]==4);
    int32_t outward=d.lba;track_skip_fields(1,0,1);CHECK(d.status==10 && d.track_skip_target_lba<outward);
    segacd_cdd_advance(&d,(uint32_t)(d.track_skip_deadline-d.cycles),sector,NULL);
    CHECK(d.status==4 && d.lba<outward);
    /* The same elapsed clock interval has the same partial physical position
     * whether the scheduler delivers it whole or in subdivisions. */
    track_skip(0,8);SegaCDDrive whole=d,split=d;
    segacd_cdd_advance(&whole,5000,NULL,NULL); /* Four 1,250-clock turn intervals. */
    segacd_cdd_advance(&split,2222,NULL,NULL);segacd_cdd_advance(&split,2778,NULL,NULL);
    CHECK(whole.cycles==split.cycles && whole.lba==split.lba && whole.status==split.status &&
          whole.track_skip_step==split.track_skip_step && whole.track_skip_deadline==split.track_skip_deadline &&
          memcmp(whole.rx,split.rx,sizeof whole.rx)==0);
    d=whole;CHECK(d.track_skip_active);
    command(6,0,0,0); /* Explicit pause cancels the remaining turns. */
    CHECK(!d.track_skip_active && !d.track_skip_deadline);
    /* Zero is an immediate completed skip and preserves exact logical LBA,
     * including values which are not exactly invertible through the physical
     * radius model and the represented negative first-track pregap. */
    at=d.lba;d.lba=26;track_skip(0,0);CHECK(d.status==4 && !d.track_skip_active && d.lba==26);
    d.lba=-3;track_skip(0,0);CHECK(d.status==4 && !d.track_skip_active && d.lba==-3);
    track_skip_fields(1,0,1);CHECK(d.status==10 && d.track_skip_target_lba<-3 && d.track_skip_target_lba>=d.toc.track[0].index00);
    command(6,0,0,0);d.lba=at;
    /* A huge inward skip clamps to the represented first index-00 sector. */
    d.lba=0;track_skip_fields(1,0,0xFFFF);CHECK(d.track_skip_target_lba==d.toc.track[0].index00);
    for(unsigned n=0;n<32 && d.track_skip_active;++n)segacd_cdd_advance(&d,(uint32_t)(d.track_skip_deadline-d.cycles),sector,NULL);
    CHECK(d.status==4 && d.lba==d.toc.track[0].index00);d.lba=at;
    /* The largest packet count clamps to the physical profile's lead-out. */
    track_skip(0,0xFFFF);CHECK(d.status==10 && d.track_skip_target_lba==d.toc.leadout-1);
    for(unsigned n=0;n<30000 && d.track_skip_active;++n)segacd_cdd_advance(&d,(uint32_t)(d.track_skip_deadline-d.cycles),sector,NULL);
    CHECK(d.status==4 && !d.track_skip_active && d.lba==d.toc.leadout-1 && d.lba>at);
    command(1,0,0,0);CHECK(d.status==0 && !d.track_skip_active && !d.track_skip_deadline);
    /* An explicit seek replaces the in-flight skip destination and starts
     * from the already moved head. The expected head remains at that partial
     * skip position until normal seek completion. */
    d.lba=100;command(6,0,0,0);track_skip(0,20);CHECK(d.status==10);segacd_cdd_advance(&d,(uint32_t)(d.track_skip_deadline-d.cycles),sector,NULL);at=d.lba;
    command(4,0,0,12);CHECK(d.status==10 && d.track_skip_active && d.lba>at); /* malformed packet leaves skip active; physical motion continues */
    segacd_cdd_advance(&d,500,NULL,NULL);
    SegaCDDrive reached_head=d;segacd_cdd_advance(&reached_head,1250,NULL,NULL);
    at=reached_head.lba; /* A turn lands 500 clocks before valid packet execution. */
    uint64_t seek_apply=d.cycles+1250u;
    uint64_t expected_seek=seek_apply+1250000u+(uint64_t)abs(-3-at)*12500000u/(75u*1000u);
    command(4,0,0,2);CHECK(d.status==2 && !d.track_skip_active && d.lba==at && d.seek_target==-3 && d.seek_deadline==expected_seek);
    segacd_cdd_advance(&d,(uint32_t)(d.seek_deadline-d.cycles),sector,NULL);CHECK(d.status==4 && d.lba==-3);
    /* PLAY, STOP and tray open also cancel a pending physical skip. */
    track_skip(0,20);CHECK(d.track_skip_active);command(7,0,0,0);CHECK(d.status==1 && !d.track_skip_active);
    command(6,0,0,0);track_skip(0,20);command(1,0,0,0);CHECK(d.status==0 && !d.track_skip_active);
    command(6,0,0,0);track_skip(0,20);command(13,0,0,0);CHECK(d.status==5 && !d.track_skip_active && !d.track_skip_deadline);
    command(12,0,0,0);CHECK(!d.tray_open);
    /* Exercise the observed BIOS packet A000020B08 with a long enough disc
     * that 523 physical turns do not collapse into a tiny-fixture boundary. */
    SegaCDTOC long_toc={0};long_toc.count=1;long_toc.leadout=255826;
    long_toc.track[0]=(SegaCDTrack){-150,0,255826,1};CHECK(segacd_cdd_mount(&d,&long_toc));
    d.status=4;d.lba=100000;track_skip_fields(0,0,0x020B);
    CHECK(d.status==10 && d.track_skip_active && d.track_skip_count==523 && d.track_skip_target_lba>d.lba);
    segacd_cdd_advance(&d,(uint32_t)(d.track_skip_deadline-d.cycles),sector,NULL);
    CHECK(d.lba>100000 && d.status==10);
    at=d.lba;expected_seek=d.cycles+1250u+1250000u+(uint64_t)abs(-3-at)*12500000u/(75u*1000u);
    command(4,0,0,2);CHECK(!d.track_skip_active && d.status==2 && d.seek_target==-3 && d.lba==at && d.seek_deadline==expected_seek);
    segacd_cdd_advance(&d,(uint32_t)(d.seek_deadline-d.cycles),sector,NULL);CHECK(d.status==4 && d.lba==-3);
    /* BIOS packet A000328107 requests 12,929 turns. It must finish under
     * the game's 10-second wait while status/IRQ cadence remains 75 Hz. */
    d.lba=0;
    track_skip_fields(0,0,0x3281);
    CHECK(d.status==10 && d.track_skip_active && d.track_skip_target_lba>0 &&
          d.track_skip_target_lba<d.toc.leadout-1);
    uint64_t frame_before=d.frames;
    uint64_t profile_start=d.track_skip_origin_cycle;
    uint64_t profile_end=profile_start+(uint64_t)0x3281*1250u;
    CHECK(profile_end-d.cycles<12500000u*10u);
    unsigned skip_events=0,frame_irqs=0;
    while(d.track_skip_active) {
        frame_irqs+=segacd_cdd_advance(&d,(uint32_t)(d.track_skip_deadline-d.cycles),sector,NULL);
        ++skip_events;
    }
    CHECK(d.status==4 && !d.track_skip_active && d.lba==d.track_skip_target_lba &&
          d.cycles==profile_end && d.cycles-profile_start==(uint64_t)0x3281*1250u &&
          skip_events==0x3281);
    CHECK(d.frames-frame_before>=75 && d.frames-frame_before<750);
    CHECK(frame_irqs==d.frames-frame_before); /* Only regular 75-Hz frames raise IRQs. */
    CHECK(segacd_cdd_mount(&d,&toc));
    command(7,0,0,0);CHECK(d.status==1);command(1,0,0,0);CHECK(d.status==0 && d.rx[0]==0);
    for(unsigned n=0;n<9;++n)CHECK(d.rx[n]==0);CHECK(d.rx[9]==15);
    command(13,0,0,0);CHECK(d.tray_open && d.rx[0]==5);command(12,0,0,0);CHECK(!d.tray_open && d.rx[0]==0);
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
