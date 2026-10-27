/* Controller/board interface tests. Synthetic sector's edc_ok is supplied
 * explicitly; these checks do not claim EDC/ECC implementation or timing. */
#include "segacd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static SegaCDCdc c;static SegaCD cd;static uint8_t bios[SEGACD_BIOS_BYTES],raw[2352];
static void write_reg(unsigned a,uint8_t v){segacd_cdc_address(&c,(uint8_t)a);segacd_cdc_write(&c,v);}
static void sub_reg(unsigned a,uint8_t v){CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8005,(uint8_t)a)==SEGACD_OK);CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8007,v)==SEGACD_OK);}
static uint16_t read_word(SegaCDCpu which,uint32_t a){uint16_t v=0;CHECK(segacd_read16(&cd,which,a,&v)==SEGACD_OK);return v;}
static void start(unsigned destination)
{
    CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8004,(uint8_t)destination)==SEGACD_OK);
    sub_reg(1,0x42);sub_reg(2,3);sub_reg(3,0);sub_reg(4,0x08);sub_reg(5,0x40);sub_reg(6,0);
}
static void begin_host_transfer(unsigned bytes,uint16_t address,uint8_t ifctrl)
{
    CHECK(bytes>=1 && bytes<=4096);
    segacd_cdc_init(&c);
    for(unsigned n=0;n<16384;++n)c.ram[n]=(uint8_t)(n*37u+0x21u);
    write_reg(1,ifctrl);
    write_reg(2,(uint8_t)(bytes-1u));write_reg(3,(uint8_t)((bytes-1u)>>8));
    write_reg(4,(uint8_t)address);write_reg(5,(uint8_t)(address>>8));write_reg(6,0);
}
static void check_host_transfer_length(unsigned bytes,uint16_t address)
{
    begin_host_transfer(bytes,address,2); /* DOUTEN; mask DTEI until separately tested. */
    unsigned prefetched=bytes<2?bytes:2;
    CHECK(c.remaining==bytes && c.dbc==(bytes-1u) && c.dac==(uint16_t)(address+prefetched));
    CHECK(c.fifo_count==prefetched && !c.transfer_end && (c.ifstat&64) && !segacd_cdc_irq(&c));
    for(unsigned consumed=0;consumed<bytes;++consumed) {
        uint8_t value=0;
        CHECK(segacd_cdc_host_read(&c,&value));
        CHECK(value==(uint8_t)(((address+consumed)&16383u)*37u+0x21u));
        unsigned left=bytes-consumed-1u;
        unsigned loaded=consumed+1u+2u;
        if(loaded>bytes)loaded=bytes;
        CHECK(c.remaining==left && c.dbc==(uint16_t)((bytes-2u-consumed)&4095u));
        CHECK(c.dac==(uint16_t)(address+loaded));
        CHECK(c.fifo_count==(left<2?left:2));
        CHECK(c.transfer_end==(left==0));
        CHECK(((c.ifstat&64)!=0)==(left!=0));
        CHECK(!segacd_cdc_irq(&c));
    }
    CHECK(!c.transfer_active && c.remaining==0 && c.dbc==4095 && c.transfer_end);
}
static void test_transfer_edges(void)
{
    check_host_transfer_length(1,0x0200);
    check_host_transfer_length(2,0x0200);
    check_host_transfer_length(3,0x0200);
    check_host_transfer_length(4096,0x0200);

    /* DAC is 16-bit while source RAM wraps at 14 bits. */
    begin_host_transfer(4,0xFFFE,2);
    CHECK(c.dac==0 && c.fifo_count==2 && c.dbc==3 && (c.ifstat&64));
    for(unsigned n=0;n<4;++n) {
        uint8_t value=0;
        CHECK(segacd_cdc_host_read(&c,&value));
        CHECK(value==(uint8_t)((((0xFFFEu+n)&16383u)*37u)+0x21u));
    }
    CHECK(c.dac==2 && c.dbc==4095 && c.transfer_end && !(c.ifstat&64));

    /* A masked DTEI remains pending and appears when unmasked; DTACK clears it. */
    begin_host_transfer(1,0x3000,2);
    { uint8_t value; CHECK(segacd_cdc_host_read(&c,&value)); }
    CHECK(c.transfer_end && !(c.ifstat&64) && !segacd_cdc_irq(&c));
    write_reg(1,0x42);CHECK(segacd_cdc_irq(&c));
    write_reg(7,0);CHECK(!segacd_cdc_irq(&c) && (c.ifstat&64));

    /* Disabling DOUTEN aborts without producing the transfer-end interrupt. */
    begin_host_transfer(3,0x3100,0x42);
    CHECK(c.transfer_active && c.fifo_count==2 && (c.ifstat&64));
    write_reg(1,0x40);
    CHECK(!c.transfer_active && c.fifo_count==0 && c.remaining==0 && !c.transfer_end);
    CHECK((c.ifstat&64) && !segacd_cdc_irq(&c));

    /* Reset during transfer aborts, clears controller state, and preserves buffer RAM. */
    begin_host_transfer(3,0x3200,0x42);
    { uint8_t value; CHECK(segacd_cdc_host_read(&c,&value)); }
    uint8_t preserved=c.ram[0x3201];
    CHECK(c.transfer_active && c.remaining==2 && c.dbc==1);
    segacd_cdc_reset(&c);
    CHECK(!c.transfer_active && !c.transfer_end && !c.fifo_count && !c.remaining);
    CHECK(c.ifctrl==0 && (c.ifstat&64) && !segacd_cdc_irq(&c) && c.ram[0x3201]==preserved);
}
int main(void)
{
    segacd_cdc_init(&c);CHECK(c.ifstat==255 && !segacd_cdc_irq(&c));
    segacd_cdc_address(&c,0);segacd_cdc_write(&c,0xAB);CHECK(c.ar==0);CHECK(segacd_cdc_read(&c)==0 && c.ar==0);
    segacd_cdc_address(&c,1);segacd_cdc_write(&c,0x62);CHECK(c.ar==2 && c.ifctrl==0x62);
    for(unsigned n=0;n<2352;++n)raw[n]=(uint8_t)(n*17+1);
    raw[12]=0;raw[13]=2;raw[14]=0;raw[15]=1;
    write_reg(8,0xF8);write_reg(9,0x3F);write_reg(10,0x84);write_reg(11,0);
    segacd_cdc_sector(&c,raw,1);CHECK(c.sectors==1 && c.block_pointer==0x4004 && c.write_address==0x4928);
    CHECK(c.ram[0x3FF8]==raw[0] && c.ram[0]==raw[8] && c.ram[8]==raw[16] && segacd_cdc_irq(&c));
    segacd_cdc_address(&c,4);CHECK(segacd_cdc_read(&c)==0 && segacd_cdc_read(&c)==2 && segacd_cdc_read(&c)==0 && segacd_cdc_read(&c)==1);
    segacd_cdc_address(&c,15);CHECK(segacd_cdc_read(&c)==0 && c.ar==0 && !segacd_cdc_irq(&c));
    /* DAC advances on FIFO loads; DBC and DTEI count actual host reads.
     * Prefetching the final word cannot raise the completion interrupt. */
    write_reg(2,3);write_reg(3,0);write_reg(4,8);write_reg(5,0x40);write_reg(6,0);
    CHECK(c.fifo_count==2 && c.remaining==4 && c.dbc==3 && c.dac==0x400A && !c.transfer_end);
    uint8_t value;CHECK(segacd_cdc_host_read(&c,&value) && value==raw[16]);CHECK(segacd_cdc_host_read(&c,&value) && value==raw[17]);
    CHECK(!c.transfer_end && c.fifo_count==2 && c.dbc==1 && c.dac==0x400C && !segacd_cdc_irq(&c));
    CHECK(segacd_cdc_host_read(&c,&value) && value==raw[18]);CHECK(segacd_cdc_host_read(&c,&value) && value==raw[19]);
    CHECK(c.transfer_end && c.dbc==4095 && segacd_cdc_irq(&c));
    value=0xAA;CHECK(!segacd_cdc_host_read(&c,&value) && value==0xAA && (c.ifstat&2));
    write_reg(7,0);CHECK(!segacd_cdc_irq(&c));
    /* Corruption is reported without pretending that ECC repaired it. */
    segacd_cdc_sector(&c,raw,0);CHECK(!(c.status[0]&128) && (c.status[0]&1) && (c.status[2]&4));
    CHECK(c.decoder_irq_clocks==22500 && segacd_cdc_irq(&c));
    segacd_cdc_advance(&c,22499);CHECK(segacd_cdc_irq(&c));segacd_cdc_advance(&c,1);CHECK(!segacd_cdc_irq(&c));
    write_reg(10,128);segacd_cdc_sector(&c,raw,1);CHECK(c.decoder_irq_clocks==34375);
    segacd_cdc_advance(&c,34375);CHECK(!segacd_cdc_irq(&c));
    write_reg(10,132);raw[15]=2;raw[18]|=32;segacd_cdc_sector(&c,raw,1);CHECK(c.decoder_irq_clocks==18750);
    write_reg(10,0);CHECK(!c.decoder_irq_clocks && !segacd_cdc_irq(&c));
    uint8_t saved=c.ram[0];write_reg(15,0);CHECK(c.ar==0 && c.ifstat==255 && c.ifctrl==0 && c.ram[0]==saved);
    test_transfer_edges();
    CHECK(segacd_init(&cd,bios,sizeof bios));cd.cdc=c;cd.cdc.ram[8]=0x12;cd.cdc.ram[9]=0x34;cd.cdc.ram[10]=0x56;cd.cdc.ram[11]=0x78;
    start(3);CHECK((read_word(SEGACD_SUB,0xFF8004)&0x4700)==0x4300);
    CHECK(read_word(SEGACD_MAIN,0xA12008)==0xFFFF && cd.cdc.fifo_count==2);
    CHECK(read_word(SEGACD_SUB,0xFF8008)==0x1234 && !cd.cdc.transfer_end);
    CHECK(read_word(SEGACD_SUB,0xFF8008)==0x5678 && cd.cdc.fifo_count==0 && cd.cdc.transfer_end);
    CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8033,32)==SEGACD_OK && segacd_irq_level(&cd)==5);
    segacd_irq_ack(&cd,5);CHECK(segacd_irq_level(&cd)==5);sub_reg(7,0);CHECK(!segacd_irq_level(&cd)); /* level source, not IACK-cleared */
    start(2);CHECK(read_word(SEGACD_MAIN,0xA12004)&0x4000);CHECK(read_word(SEGACD_MAIN,0xA12008)==0x1234);CHECK(read_word(SEGACD_MAIN,0xA12008)==0x5678);
    CHECK(segacd_write8(&cd,SEGACD_MAIN,0xA12004,7)==SEGACD_OK && cd.cdc_destination==2);
    CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF800A,0x1234)==SEGACD_OK && cd.cdc_dma_address==0x1234);
    CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8004,0)==SEGACD_OK && cd.cdc_dma_address==0);
    /* A real register-driven PRG DMA start transfers after its actual clock
     * deadline. This replaces the earlier unimplemented-start expectation. */
    sub_reg(7,0);CHECK(!segacd_irq_level(&cd));start(5);
    CHECK(cd.dma.active && !cd.cdc.transfer_end);
    segacd_advance(&cd,5);CHECK(!cd.prg[0] && !cd.dma.words);
    segacd_advance(&cd,1);CHECK(cd.prg[0]==0x12 && cd.prg[1]==0x34 && cd.dma.words==1 && !cd.cdc.transfer_end);
    segacd_advance(&cd,6);CHECK(cd.prg[2]==0x56 && cd.prg[3]==0x78 && cd.dma.words==2 && cd.cdc.transfer_end && !cd.dma.active);
    CHECK(read_word(SEGACD_SUB,0xFF8004)&0x8000);
    CHECK(segacd_irq_level(&cd)==5);sub_reg(7,0);CHECK(!segacd_irq_level(&cd));
    puts("LC8951 AR/flags, sector-ring bounds/header, two-byte FIFO/count, CPU gating, timed PRG DMA and persistent IRQ/ack pass (ECC/bus timing pending)");return 0;
}
