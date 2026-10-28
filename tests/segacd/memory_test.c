#include "segacd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %u: %s\n", (unsigned)__LINE__, #x); exit(1); } } while (0)
static SegaCD *s;
static uint8_t bios[SEGACD_BIOS_BYTES];
static void w(SegaCDCpu cpu, uint32_t a, uint16_t value)
{ CHECK(segacd_write16(s,cpu,a,value)==SEGACD_OK); }
static uint16_t r(SegaCDCpu cpu, uint32_t a)
{ uint16_t v; CHECK(segacd_read16(s,cpu,a,&v)==SEGACD_OK); return v; }
static void b(SegaCDCpu cpu, uint32_t a, uint8_t value)
{ CHECK(segacd_write8(s,cpu,a,value)==SEGACD_OK); }
static void check_code_spans(void)
{
    const uint32_t addresses[]={0,0x60,0x70,0x72,0x74,0x1fffe,0x20000,0x3fff0,
        0x7fffe,0x80000,0xbfff0,0xc0000,0xdfff0,0x200000,0x23fff0,0xa12000,0xff0000};
    for(unsigned mode=0;mode<8;++mode)for(unsigned control=0;control<4;++control)
    for(unsigned bank=0;bank<4;++bank) {
        s->memory_mode=(uint8_t)mode;s->main_control=(uint8_t)control;s->prg_bank=(uint8_t)bank;
        for(unsigned cpu=0;cpu<2;++cpu)for(unsigned a=0;a<sizeof addresses/sizeof *addresses;++a)
        for(size_t count=2;count<=22;count+=2) {
            uint32_t address=addresses[a];const uint8_t *span=segacd_code_span(s,(SegaCDCpu)cpu,address,count);
            if(span)for(size_t i=0;i<count;++i) {
                uint8_t value=0;CHECK(segacd_read8(s,(SegaCDCpu)cpu,address+(uint32_t)i,&value)==SEGACD_OK);
                CHECK(value==span[i]);
            }
            if(cpu==SEGACD_MAIN&&address<0x74&&address+count>0x72)CHECK(!span);
            if((mode&4)&&((cpu==SEGACD_MAIN&&address>=0x200000&&address<0x240000)||
               (cpu==SEGACD_SUB&&address>=0x80000&&address<0xe0000)))CHECK(!span);
        }
    }
    CHECK(!segacd_code_span(s,SEGACD_MAIN,1,2));
    CHECK(!segacd_code_span(s,SEGACD_MAIN,0xfffffe,4));
    CHECK(!segacd_code_span(s,SEGACD_MAIN,0x1000000,2));
    CHECK(!segacd_code_span(s,SEGACD_MAIN,0,0));
}

int main(void)
{
    s=malloc(sizeof *s);CHECK(s);
    for (unsigned i=0;i<sizeof bios;++i) bios[i]=(uint8_t)(i^(i>>8));
    CHECK(!segacd_init(s,bios,sizeof bios-1));
    CHECK(segacd_init(s,bios,sizeof bios));
    CHECK(r(SEGACD_MAIN,0x102)==0x0302);
    uint16_t value=0xDEAD;
    CHECK(segacd_read16(s,SEGACD_MAIN,1,&value)==SEGACD_ADDRESS_ERROR && value==0xDEAD);
    CHECK(segacd_write16(s,SEGACD_MAIN,0x20001,0)==SEGACD_ADDRESS_ERROR);
    CHECK(segacd_read16(s,SEGACD_MAIN,0x40000,&value)==SEGACD_UNMAPPED);
    CHECK(segacd_read16(s,SEGACD_SUB,0xFF8004,&value)==SEGACD_OK && value==0); /* CDC mode/address at reset */
    CHECK(!segacd_sub_runnable(s));
    /* All four main-CPU PRG windows select distinct sub-CPU storage. */
    for (unsigned bank=0;bank<4;++bank) {
        b(SEGACD_MAIN,0xA12003,(uint8_t)(bank<<6));
        w(SEGACD_MAIN,0x20000,(uint16_t)(0xA000+bank));
        CHECK(r(SEGACD_SUB,bank*0x20000)==0xA000+bank);
    }
    b(SEGACD_MAIN,0xA12001,1);CHECK(segacd_sub_runnable(s));
    CHECK(segacd_read16(s,SEGACD_MAIN,0x20000,&value)==SEGACD_WAIT);
    CHECK(segacd_write16(s,SEGACD_MAIN,0x20000,0)==SEGACD_WAIT);
    b(SEGACD_MAIN,0xA12001,3);CHECK(!segacd_sub_runnable(s));
    CHECK(r(SEGACD_MAIN,0x20000)==0xA003);
    uint32_t generation=s->sub_reset_generation;
    b(SEGACD_MAIN,0xA12001,2);b(SEGACD_MAIN,0xA12001,1);
    CHECK(s->sub_reset_generation==generation+1 && segacd_sub_runnable(s));
    b(SEGACD_MAIN,0xA12001,3);
    /* Write protection belongs to main; protected sub writes are ignored. */
    b(SEGACD_MAIN,0xA12002,1);w(SEGACD_SUB,0x1FE,0xEEEE);CHECK(r(SEGACD_SUB,0x1FE)==0);
    w(SEGACD_SUB,0x200,0x1234);CHECK(r(SEGACD_SUB,0x200)==0x1234);
    b(SEGACD_SUB,0xFF8002,0);CHECK(s->write_protect==1);
    /* 2M ownership must be explicitly transferred in both directions. */
    w(SEGACD_MAIN,0x200000,0x1357);
    CHECK(segacd_read16(s,SEGACD_SUB,0x80000,&value)==SEGACD_WAIT);
    b(SEGACD_MAIN,0xA12003,2);CHECK((r(SEGACD_MAIN,0xA12002)&3)==2);
    CHECK(segacd_read16(s,SEGACD_MAIN,0x200000,&value)==SEGACD_WAIT);
    CHECK(segacd_write16(s,SEGACD_MAIN,0x200000,0)==SEGACD_WAIT);
    CHECK(r(SEGACD_SUB,0x80000)==0x1357);w(SEGACD_SUB,0x80000,0x2468);
    b(SEGACD_SUB,0xFF8003,1);CHECK(r(SEGACD_MAIN,0x200000)==0x2468);
    /* 1M banks exchange without copying/corrupting the physical 2M words. */
    w(SEGACD_MAIN,0x200000,0x1122);w(SEGACD_MAIN,0x200002,0x3344);
    b(SEGACD_SUB,0xFF8003,4);
    CHECK(r(SEGACD_MAIN,0x200000)==0x1122);CHECK(r(SEGACD_SUB,0xC0000)==0x3344);
    b(SEGACD_MAIN,0xA12003,0);CHECK(r(SEGACD_MAIN,0xA12002)&2);
    b(SEGACD_SUB,0xFF8003,5);CHECK(!(r(SEGACD_MAIN,0xA12002)&2));
    CHECK(r(SEGACD_MAIN,0x200000)==0x3344);CHECK(r(SEGACD_SUB,0xC0000)==0x1122);
    /* Published cell-map examples, independent of our address formula. */
    w(SEGACD_MAIN,0x200004,0x5566);CHECK(r(SEGACD_MAIN,0x220400)==0x5566);
    w(SEGACD_MAIN,0x200100,0x7788);CHECK(r(SEGACD_MAIN,0x220004)==0x7788);
    for (unsigned band=0;band<5;++band) {
        static const uint32_t bases[]={0,0x10000,0x18000,0x1C000,0x1E000};
        w(SEGACD_MAIN,0x200000+bases[band],(uint16_t)(0x9000+band));
        CHECK(r(SEGACD_MAIN,0x220000+bases[band])==0x9000+band);
    }
    /* Sub dot image is two unpacked low nibbles; priority applies per pixel. */
    w(SEGACD_SUB,0xC0000,0xA30F);CHECK(r(SEGACD_SUB,0x80000)==0x0A03);
    b(SEGACD_SUB,0x80001,5);CHECK(r(SEGACD_SUB,0xC0000)==0xA50F);
    b(SEGACD_SUB,0xFF8003,13);w(SEGACD_SUB,0x80000,0x0102);w(SEGACD_SUB,0x80002,0x0304);CHECK(r(SEGACD_SUB,0xC0000)==0xA53F);
    b(SEGACD_SUB,0xFF8003,21);w(SEGACD_SUB,0x80000,0);w(SEGACD_SUB,0x80002,2);CHECK(r(SEGACD_SUB,0xC0000)==0xA532);
    /* Direct RAM bypasses dot priority, including incoming zero pixels. */
    w(SEGACD_SUB,0xC0000,0x1020);CHECK(r(SEGACD_SUB,0xC0000)==0x1020);
    w(SEGACD_SUB,0xC0000,0xA532);
    b(SEGACD_SUB,0xFF8003,1);
    CHECK(r(SEGACD_MAIN,0x200000)==0xA532); /* same physical bank after mode change */
    /* Backup RAM's odd lane, first/last byte, and persistence across reset. */
    CHECK(r(SEGACD_SUB,0xFE0000)==0xFFFF);w(SEGACD_SUB,0xFE0000,0x1234);
    w(SEGACD_SUB,0xFE3FFE,0x5678);CHECK(r(SEGACD_SUB,0xFE0000)==0xFF34);
    CHECK(r(SEGACD_SUB,0xFE3FFE)==0xFF78);segacd_reset(s);CHECK(r(SEGACD_SUB,0xFE0000)==0xFF34);
    /* Both directions have independent ownership, including byte writes. */
    w(SEGACD_MAIN,0xA12010,0x1234);w(SEGACD_SUB,0xFF8010,0x9999);CHECK(r(SEGACD_SUB,0xFF8010)==0x1234);
    b(SEGACD_MAIN,0xA12011,0x56);CHECK(r(SEGACD_SUB,0xFF8010)==0x1256);
    w(SEGACD_SUB,0xFF802E,0xABCD);w(SEGACD_MAIN,0xA1202E,0);CHECK(r(SEGACD_MAIN,0xA1202E)==0xABCD);
    w(SEGACD_MAIN,0xA1200E,0x1234);w(SEGACD_SUB,0xFF800E,0x5678);CHECK(r(SEGACD_MAIN,0xA1200E)==0x1278);
    /* IRQ2 generation/mask/ack and periodic IRQ3 use emulated time only. */
    b(SEGACD_SUB,0xFF8033,12);b(SEGACD_MAIN,0xA12000,1);
    CHECK(segacd_irq_level(s)==2);CHECK(r(SEGACD_MAIN,0xA12000)&0x8100);
    segacd_irq_ack(s,2);CHECK(!segacd_irq_level(s));
    b(SEGACD_SUB,0xFF8031,1);segacd_advance(s,767);CHECK(!segacd_irq_level(s));
    segacd_advance(s,1);CHECK(segacd_irq_level(s)==3);CHECK(r(SEGACD_SUB,0xFF8030)==1);
    CHECK(r(SEGACD_MAIN,0xA1200C)==2);segacd_irq_ack(s,3);
    segacd_advance(s,768*2);CHECK(segacd_irq_level(s)==3);
    w(SEGACD_SUB,0xFF800C,0);CHECK(r(SEGACD_MAIN,0xA1200C)==0);
    b(SEGACD_SUB,0xFF8031,0);segacd_irq_ack(s,3);segacd_advance(s,384*4096);CHECK(!segacd_irq_level(s));
    CHECK(r(SEGACD_SUB,0xFF800C)==0);
    /* Font expansion and H-int vector operate without ROM mutation. */
    w(SEGACD_SUB,0xFF804C,0xA3);w(SEGACD_SUB,0xFF804E,0x8001);
    CHECK(r(SEGACD_SUB,0xFF8050)==0xA333);CHECK(r(SEGACD_SUB,0xFF8056)==0x333A);
    w(SEGACD_MAIN,0xA12006,0x9876);CHECK(r(SEGACD_MAIN,0x72)==0x9876);CHECK(s->bios[0x72]==bios[0x72]);
    /* Exercise every 1M linear byte and all cell-band limits under sanitizers. */
    b(SEGACD_SUB,0xFF8003,4);
    for (uint32_t a=0;a<0x20000;++a) {
        uint8_t v;
        CHECK(segacd_read8(s,SEGACD_MAIN,0x220000+a,&v)==SEGACD_OK);
        CHECK(segacd_write8(s,SEGACD_SUB,0xC0000+a,(uint8_t)a)==SEGACD_OK);
    }
    check_code_spans();
    free(s);puts("Sega CD memory, ownership, communication, timer/IRQ, code spans and font contracts pass");
    return 0;
}
