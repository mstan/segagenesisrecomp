#include "segacd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"CHECK failed %s:%d: %s\n",__FILE__,__LINE__,#c);exit(1);} } while(0)
SegaCDResult reference_segacd_read8(SegaCD *,SegaCDCpu,uint32_t,uint8_t *);
SegaCDResult reference_segacd_write8(SegaCD *,SegaCDCpu,uint32_t,uint8_t);
SegaCDResult reference_segacd_read16(SegaCD *,SegaCDCpu,uint32_t,uint16_t *);
SegaCDResult reference_segacd_write16(SegaCD *,SegaCDCpu,uint32_t,uint16_t);
static SegaCD actual,reference;
static uint8_t bios[SEGACD_BIOS_BYTES],visited[0x20000];
static unsigned byte_cases,word_cases;
int main(void)
{
    static const unsigned bases[]={0,0x10000,0x18000,0x1c000,0x1e000};
    static const unsigned rows[]={32,16,8,4,4};
    for(unsigned bank=0;bank<2;++bank) {
        CHECK(segacd_init(&actual,bios,sizeof bios));actual.memory_mode=(uint8_t)(4|bank);
        for(unsigned i=0;i<sizeof actual.word;++i)actual.word[i]=(uint8_t)(i^(i>>7)^(i>>13));
        reference=actual;memset(visited,0,sizeof visited);
        /* Independent coordinate enumeration: each 8x8, four-byte-wide cell
         * resides in one of 64 columns and this band's 32/16/8/4/4 rows. */
        for(unsigned band=0;band<5;++band)for(unsigned column=0;column<64;++column)
        for(unsigned row=0;row<rows[band];++row)for(unsigned y=0;y<8;++y)
        for(unsigned x=0;x<4;++x) {
            unsigned cell=bases[band]+(column*rows[band]+row)*32+y*4+x;
            unsigned linear=bases[band]+(row*8+y)*256+column*4+x;
            unsigned physical=(linear/2)*4+bank*2+linear%2;
            CHECK(cell<sizeof visited&&!visited[cell]);visited[cell]=1;
            uint8_t a=0,b=0;
            CHECK(segacd_read8(&actual,SEGACD_MAIN,0x220000+cell,&a)==SEGACD_OK);
            CHECK(reference_segacd_read8(&reference,SEGACD_MAIN,0x220000+cell,&b)==SEGACD_OK);
            CHECK(a==b&&a==actual.word[physical]);
            uint8_t value=(uint8_t)(cell^(cell>>8)^0xa7);
            CHECK(segacd_write8(&actual,SEGACD_MAIN,0x220000+cell,value)==SEGACD_OK);
            CHECK(reference_segacd_write8(&reference,SEGACD_MAIN,0x220000+cell,value)==SEGACD_OK);
            CHECK(actual.word[physical]==value);
            ++byte_cases;
            if(!(cell&1u)) {
                uint16_t wa=0,wb=0;
                CHECK(segacd_read16(&actual,SEGACD_MAIN,0x220000+cell,&wa)==SEGACD_OK);
                CHECK(reference_segacd_read16(&reference,SEGACD_MAIN,0x220000+cell,&wb)==SEGACD_OK);
                CHECK(wa==wb&&wa==(((unsigned)actual.word[physical]<<8)|actual.word[physical+1]));
                wa=(uint16_t)(cell^(cell>>5)^0x9678);
                CHECK(segacd_write16(&actual,SEGACD_MAIN,0x220000+cell,wa)==SEGACD_OK);
                CHECK(reference_segacd_write16(&reference,SEGACD_MAIN,0x220000+cell,wa)==SEGACD_OK);
                CHECK(actual.word[physical]==(uint8_t)(wa>>8)&&actual.word[physical+1]==(uint8_t)wa);
                ++word_cases;
            }
        }
        for(unsigned i=0;i<sizeof visited;++i)CHECK(visited[i]);
        CHECK(memcmp(&actual,&reference,sizeof actual)==0);
    }
    printf("Cell layout: %u byte and %u word read/write cases, independent five-band coordinates, both RET banks and full device state pass\n",byte_cases,word_cases);
    return 0;
}
