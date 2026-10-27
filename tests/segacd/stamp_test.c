#include "segacd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static SegaCD cd,split;
static uint8_t bios[SEGACD_BIOS_BYTES];
static void w(uint32_t a,uint16_t v){CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF8000u+a,v)==SEGACD_OK);}
static uint16_t r(uint32_t a){uint16_t v;CHECK(segacd_read16(&cd,SEGACD_SUB,0xFF8000u+a,&v)==SEGACD_OK);return v;}
static void ram_word(unsigned a,unsigned v){cd.word[a]=(uint8_t)(v>>8);cd.word[a+1]=(uint8_t)v;}
static void setup(void)
{
    CHECK(segacd_init(&cd,bios,sizeof bios));
    CHECK(segacd_write8(&cd,SEGACD_MAIN,0xA12003,2)==SEGACD_OK);
    w(0x32,2); /* Unmasked completion IRQ1. */
    w(0x5A,0x1000); /* Map at byte $4000. */
    w(0x5C,1);w(0x5E,0x4000); /* Two vertical cells, buffer at $10000. */
    w(0x62,1);w(0x64,4);
    /* Four corners of a 16x16 stamp at $80. The p36 cell diagram gives
     * offsets 0,$43,$3C,$7F. Each byte's other nibble stays zero. */
    cd.word[0x80]=0x10;cd.word[0xC3]=2;cd.word[0xBC]=0x30;cd.word[0xFF]=4;
    /* Trace vectors sample top-left/right, then bottom-left/right. */
    ram_word(0x6000,0);ram_word(0x6002,0);
    ram_word(0x6008,15*8);ram_word(0x600A,0);
    ram_word(0x6010,0);ram_word(0x6012,15*8);
    ram_word(0x6018,15*8);ram_word(0x601A,15*8);
}
int main(void)
{
    /* Corner permutations from the manual's flip/rotate diagram, checked
     * independently through the pinned reference (all 8 whole-RAM matches).
     * The initial clockwise/counterclockwise reading swapped rows 1 and 3;
     * build/qa-stamp-synthetic/corners-results.json retains the observations. */
    static const unsigned corners[8][4]={
        {1,2,3,4},{2,4,1,3},{4,3,2,1},{3,1,4,2},
        {2,1,4,3},{4,2,3,1},{3,4,1,2},{1,3,2,4}
    };
    for(unsigned transform=0;transform<8;++transform) {
        setup();ram_word(0x4000,1|(transform<<13));
        w(0x58,0);w(0x66,0x1800);
        CHECK(r(0x58)==0x8000 && r(0x64)==4 && !segacd_irq_level(&cd));
        segacd_advance(&cd,4);CHECK(cd.word[0x10000]==0 && r(0x64)==4);
        segacd_advance(&cd,1);CHECK(cd.word[0x10000]==corners[transform][0]*16 && r(0x64)==3);
        segacd_advance(&cd,15);CHECK(!r(0x58) && !r(0x64) && segacd_irq_level(&cd)==1);
        for(unsigned line=0;line<4;++line)CHECK(cd.word[0x10000+line*4]==corners[transform][line]*16);
        segacd_irq_ack(&cd,1);CHECK(!segacd_irq_level(&cd));
    }
    /* Off-map pixels are zero; repeat wraps, and stamp zero remains RAM. */
    for(unsigned repeat=0;repeat<2;++repeat) {
        setup();ram_word(0x4000,1);ram_word(0x6000,256*8);w(0x58,(uint16_t)repeat);w(0x64,1);w(0x66,0x1800);
        segacd_advance(&cd,5);CHECK(cd.word[0x10000]==(repeat?0x10:0));
    }
    setup();ram_word(0x4000,0);cd.word[0]=0xF0;w(0x64,1);w(0x66,0x1800);segacd_advance(&cd,5);CHECK(!cd.word[0x10000]);
    /* 32x32 corners use a four-by-four cell grid. Low stamp index bits
     * align to four 16x16 stamp slots. */
    setup();w(0x58,2);ram_word(0x4000,7);w(0x64,4);
    cd.word[0x200]=0x50;cd.word[0x383]=6;cd.word[0x27C]=0x70;cd.word[0x3FF]=8;
    ram_word(0x6008,31*8);ram_word(0x6012,31*8);ram_word(0x6018,31*8);ram_word(0x601A,31*8);
    w(0x66,0x1800);segacd_advance(&cd,20);
    for(unsigned line=0;line<4;++line)CHECK(cd.word[0x10000+line*4]==(5+line)*16);
    /* A half-dot delta accumulates without rounding each step; a negative
     * delta wraps only when repeat is enabled. */
    setup();ram_word(0x4000,1);cd.word[0x80]=0x12;cd.word[0x81]=0x34;
    ram_word(0x6004,0x0400);w(0x62,8);w(0x64,1);w(0x66,0x1800);segacd_advance(&cd,40);
    CHECK(cd.word[0x10000]==0x11 && cd.word[0x10001]==0x22 && cd.word[0x10002]==0x33 && cd.word[0x10003]==0x44);
    setup();ram_word(0x4000,1);ram_word(0x6004,0xF800);w(0x62,2);w(0x64,1);w(0x66,0x1800);segacd_advance(&cd,10);
    CHECK(cd.word[0x10000]==0x10);
    /* Dot/line offsets preserve neighboring nibbles. Crossing x=8 uses the
     * configured two-cell vertical stride rather than adjacent packed bytes. */
    setup();ram_word(0x4000,1);cd.word[0x80]=0x12;memset(cd.word+0x10000,0xBB,128);
    ram_word(0x6004,0x0800);w(0x60,15);w(0x62,2);w(0x64,1);w(0x66,0x1800);segacd_advance(&cd,10);
    CHECK(cd.word[0x10007]==0xB1 && cd.word[0x10044]==0x2B && cd.word[0x10008]==0xBB);
    /* Graphics priority is per nibble; the adjacent untouched half survives. */
    for(unsigned mode=0;mode<3;++mode) {
        setup();ram_word(0x4000,1);cd.word[0x80]=0x50;cd.word[0x10000]=0xBA;
        CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8003,(uint8_t)(2|(mode<<3)))==SEGACD_OK);
        w(0x62,2);ram_word(0x6004,0x0800);w(0x64,1);w(0x66,0x1800);segacd_advance(&cd,10);
        CHECK(cd.word[0x10000]==(mode==1?0xBA:mode==2?0x5A:0x50));
    }
    /* Exact split-step equivalence of output, busy countdown and IRQ. */
    setup();ram_word(0x4000,1);w(0x62,31);w(0x64,17);w(0x66,0x1800);split=cd;
    segacd_advance(&cd,2800);for(unsigned n=0;n<2800;++n)segacd_advance(&split,1);
    CHECK(!memcmp(&cd,&split,sizeof cd));
    /* Main-owned and 1M RAM cannot be silently rendered by the sub ASIC. */
    setup();CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8003,1)==SEGACD_OK);
    CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF8066,0x1800)==SEGACD_WAIT && !cd.stamp.busy);
    CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8003,4)==SEGACD_OK);
    CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF8066,0x1800)==SEGACD_UNMAPPED && !cd.stamp.busy);
    segacd_reset(&cd);CHECK(!cd.stamp.busy && !r(0x58));
    puts("Stamp cells, transforms, map bounds, fixed-point vectors, output stride/offsets, priority, completion IRQ and split-step state pass");
    return 0;
}
