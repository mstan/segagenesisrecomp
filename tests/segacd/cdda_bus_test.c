/* Actual mapped fader/packet writes -> drive/media clock -> post-fader DAC.
 * No direct playback flags or success-state injection. */
#include "segacd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static SegaCD cd;
static uint8_t bios[SEGACD_BIOS_BYTES];
static uint64_t source_frames,zero_frames,last_stamp;
static int expect_first,expected_left,expected_right;

static void write_port(unsigned reg,uint8_t value)
{CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8000u+reg,value)==SEGACD_OK);}
static void command(unsigned opcode)
{
    uint8_t packet[10]={0};unsigned sum=0;
    packet[0]=(uint8_t)opcode;
    if(opcode==3){packet[5]=2;packet[7]=3;} /* seekplay LBA3, pre-roll starts0 */
    for(unsigned n=0;n<9;++n)sum+=packet[n];
    packet[9]=(uint8_t)(~sum&15u);
    for(unsigned n=0;n<10;++n)write_port(0x42+n,packet[n]);
}
static int read_sector(void *unused,int32_t lba,uint8_t raw[2352])
{
    (void)unused;CHECK(lba>=0 && lba<150);
    if(cd.drive.toc.count==2 && lba>=3) {
        memset(raw,0,2352);memset(raw+1,255,10);raw[13]=2;raw[14]=0x10;raw[15]=1;
        for(unsigned n=16;n<2064;++n)raw[n]=(uint8_t)(n*37u+11u);
        raw[2064]=0x70;raw[2065]=1;raw[2066]=0x10;raw[2067]=0x7D;
    } else {
        int16_t l=(int16_t)((lba+1)*256),r=(int16_t)(-2*(lba+1)*256);
        uint16_t left=(uint16_t)l,right=(uint16_t)r;
        for(unsigned n=0;n<588;++n) {
            raw[n*4]=(uint8_t)left;raw[n*4+1]=(uint8_t)(left>>8);
            raw[n*4+2]=(uint8_t)right;raw[n*4+3]=(uint8_t)(right>>8);
        }
    }
    return 1;
}
static void output(void *unused,uint64_t stamp,int16_t left,int16_t right)
{
    (void)unused;CHECK(stamp>last_stamp);last_stamp=stamp;
    if(left || right) {
        CHECK(left>0 && right==-2*left);
        if(expect_first){CHECK(left==expected_left && right==expected_right);expect_first=0;}
        ++source_frames;
    } else ++zero_frames;
}
static void advance(unsigned clocks)
{
    SegaCDIO io={0};io.read_sector=read_sector;io.cdda_sample=output;
    segacd_advance_io(&cd,clocks,&io);
    CHECK(!cd.cdda_error && !cd.media_error);
}
static void prepare(int data_after_audio)
{
    SegaCDTOC toc={{{-150,0,150,0}},1,150};
    if(data_after_audio){toc.track[0].end=3;toc.track[1]=(SegaCDTrack){3,3,16,1};toc.count=2;toc.leadout=16;}
    CHECK(segacd_init(&cd,bios,sizeof bios));CHECK(segacd_cdd_mount(&cd.drive,&toc));
    source_frames=zero_frames=last_stamp=0;expect_first=1;expected_left=196;expected_right=-392;
    /* High byte alone does not latch a new DAC target; low byte commits it. */
    write_port(0x34,0x31);CHECK(cd.cdda.fader_raw_target==0x4000);
    write_port(0x35,0);CHECK(cd.cdda.fader_raw_target==0x3100);
    write_port(0x37,4);command(3);
}
int main(void)
{
    prepare(0);advance(2500000);
    CHECK(source_frames>0 && zero_frames>0 && !expect_first);
    CHECK(segacd_fader_current_raw(&cd.cdda.fader)==0x3100);
    command(6);advance(1250);CHECK(cd.drive.status==4);
    uint64_t before_pause=source_frames;advance(1000000);
    CHECK(source_frames==before_pause && !cd.cdda.fifo_count);
    /* Resume must start the next delivered sector, never old queued bytes. */
    expect_first=1;expected_left=(cd.drive.lba+1)*196;expected_right=-2*expected_left;
    command(7);advance(1000000);CHECK(!expect_first && source_frames>before_pause);
    command(1);advance(1250);uint64_t before_stop=source_frames;advance(500000);
    CHECK(source_frames==before_stop && !cd.cdda.fifo_count);

    /* Last audio LBA2 remains audible for all588 frames. drive.lba already
     * points to data LBA3 while that final audio sector is being consumed. */
    prepare(1);advance(3000000);
    CHECK(cd.cdda_sectors==3 && source_frames==3u*588u && !cd.cdda.source_anchored);

    CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF8034,0x3104)==SEGACD_OK);
    CHECK(cd.cdda_error==SEGACD_CDDA_UNSUPPORTED);
    uint64_t before_error=source_frames;SegaCDIO io={0};io.cdda_sample=output;
    segacd_advance_io(&cd,1000,&io);CHECK(source_frames==before_error);
    uint64_t reset_clock=cd.drive.cycles;segacd_reset(&cd);
    CHECK(!cd.cdda_error && cd.cdda.clock_now==reset_clock && cd.cdda.epoch_origin==reset_clock);
    puts("Mapped CDD/fader to CDDA: gain, packet pause/resume/stop, final audio sector, explicit error and reset pass");
    return 0;
}
