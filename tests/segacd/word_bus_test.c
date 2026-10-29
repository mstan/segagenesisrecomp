#include "segacd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static SegaCD old_bus,new_bus;
static uint8_t bios[SEGACD_BIOS_BYTES];

/* The original word transaction's byte-lane oracle. The three word-only
 * MMIO cases are tested by the CDC/stamp/memory contract suites rather than
 * treating two byte transactions as their oracle. */
static SegaCDResult byte_read(SegaCD *s,SegaCDCpu cpu,uint32_t a,uint16_t *v)
{
    if(a&1u)return SEGACD_ADDRESS_ERROR;
    a&=0xffffffu;
    uint8_t hi,lo;SegaCDResult result=segacd_read8(s,cpu,a,&hi);
    if(result!=SEGACD_OK)return result;
    result=segacd_read8(s,cpu,a+1,&lo);
    if(result==SEGACD_OK)*v=(uint16_t)(((unsigned)hi<<8)|lo);
    return result;
}
static SegaCDResult byte_write(SegaCD *s,SegaCDCpu cpu,uint32_t a,uint16_t v)
{
    if(a&1u)return SEGACD_ADDRESS_ERROR;
    a&=0xffffffu;
    SegaCDResult result=segacd_write8(s,cpu,a,(uint8_t)(v>>8));
    return result==SEGACD_OK?segacd_write8(s,cpu,a+1,(uint8_t)v):result;
}
static void equal(unsigned mode,unsigned control,unsigned bank,unsigned cpu,uint32_t address)
{
    if(memcmp(&old_bus,&new_bus,sizeof old_bus)) {
        fprintf(stderr,"state differs: mode=%u control=%u bank=%u cpu=%u address=%08x\n",
            mode,control,bank,cpu,address);exit(1);
    }
}
int main(void)
{
    static const uint32_t addresses[]={
        0,1,0x6e,0x70,0x72,0x73,0x74,0x1fc,0x1fe,0x200,0x1fdfe,0x1fe00,
        0x1fffe,0x20000,0x3fffe,0x40000,0x7fffe,0x80000,0x80002,0xbfffe,
        0xc0000,0xdfffe,0xe0000,0x200000,0x200002,0x21fffe,0x220000,0x22fffe,
        0x230000,0x237ffe,0x23bffe,0x23dffe,0x23fffe,0x240000,
        0xa12000,0xa12002,0xa12004,0xa12006,0xa12008,0xa1200a,0xa1200c,
        0xa1200e,0xa12010,0xa1202e,0xa12030,0xfe0000,0xfe3ffe,0xfe4000,
        0xff0000,0xff0008,0xff0010,0xff2000,0xff3ffe,0xff4000,
        0xff8000,0xff8002,0xff8004,0xff8006,0xff8008,0xff800a,0xff800c,
        0xff800e,0xff8010,0xff802e,0xff8030,0xff8032,0xff8034,0xff804a,
        0xff804c,0xff804e,0xff8050,0xff8056,0xff8058,0xff8064,0xff8066,
        0xff81fe,0xff8200,0xfffffe,0xffffff,0x10000200,0xff020000
    };
    for(unsigned i=0;i<sizeof bios;++i)bios[i]=(uint8_t)(i^(i>>8));
    CHECK(segacd_init(&old_bus,bios,sizeof bios));
    for(unsigned i=0;i<sizeof old_bus.prg;++i)old_bus.prg[i]=(uint8_t)(i*7u+(i>>9));
    for(unsigned i=0;i<sizeof old_bus.word;++i)old_bus.word[i]=(uint8_t)(i*13u+(i>>10));
    memcpy(&new_bus,&old_bus,sizeof old_bus);
    unsigned cases=0;
    for(unsigned mode=0;mode<32;++mode)for(unsigned control=0;control<4;++control)
    for(unsigned bank=0;bank<4;++bank)for(unsigned cpu=0;cpu<2;++cpu)
    for(unsigned n=0;n<sizeof addresses/sizeof *addresses;++n) {
        static const uint8_t protects[]={0,1,255};
        old_bus.memory_mode=new_bus.memory_mode=(uint8_t)mode;
        old_bus.main_control=new_bus.main_control=(uint8_t)control;
        old_bus.prg_bank=new_bus.prg_bank=(uint8_t)bank;
        old_bus.write_protect=new_bus.write_protect=protects[(mode+control+bank)%3];
        uint32_t a=addresses[n],masked=a&0xffffffu;
        int host_port=(cpu==SEGACD_MAIN&&masked==0xa12008u)||(cpu==SEGACD_SUB&&masked==0xff8008u);
        int word_write=(cpu==SEGACD_MAIN&&masked==0xa1200cu)||
            (cpu==SEGACD_SUB&&(masked==0xff800cu||masked==0xff8066u));
        if(!host_port) {
            uint16_t before=0xa55a,after=before;
            SegaCDResult expected=byte_read(&old_bus,(SegaCDCpu)cpu,a,&before);
            SegaCDResult actual=segacd_read16(&new_bus,(SegaCDCpu)cpu,a,&after);
            CHECK(expected==actual&&before==after);
            equal(mode,control,bank,cpu,a);
        }
        if(!word_write) {
            uint16_t value=(uint16_t)((a*19u)^(mode*1337u)^(control*0x1234u)^(bank*0x5555u));
            CHECK(byte_write(&old_bus,(SegaCDCpu)cpu,a,value)==
                  segacd_write16(&new_bus,(SegaCDCpu)cpu,a,value));
            equal(mode,control,bank,cpu,a);
        }
        ++cases;
    }
    printf("Sega CD word bus: %u byte-oracle/full-state cases pass\n",cases);
    return 0;
}
