#include "segacd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"CHECK failed %s:%d: %s\n",__FILE__,__LINE__,#c);exit(1);} } while(0)
static uint8_t bios[SEGACD_BIOS_BYTES];
static SegaCD cd;
#define dma (cd.dma)
static void init_case(unsigned dest,uint16_t start_reg,unsigned bytes)
{
    CHECK(segacd_init(&cd,bios,sizeof bios));
    segacd_dma_init(&dma);
    cd.cdc_destination=(uint8_t)dest;
    cd.cdc_dma_address=start_reg;
    cd.cdc.ifctrl=2;
    cd.cdc.dbc=(uint16_t)(bytes-1);
    cd.cdc.dac=0;
    for(unsigned n=0;n<bytes;++n)cd.cdc.ram[n&16383u]=(uint8_t)(0x40u+n);
    CHECK(segacd_dma_begin(&cd,&dma)==SEGACD_DMA_OK);
    segacd_cdc_address(&cd.cdc,6);
    segacd_cdc_write(&cd.cdc,1);
    CHECK(cd.cdc.transfer_active&&cd.cdc.fifo_count==2);
}
static void test_pcm_boundary_and_rate(void)
{
    init_case(4,0xffffu,8);
    cd.pcm.bank=3;
    CHECK(segacd_dma_advance(&cd,&dma,5)==SEGACD_DMA_OK);
    CHECK(cd.cdc.fifo_count==2&&dma.words==0);
    CHECK(segacd_dma_advance(&cd,&dma,1)==SEGACD_DMA_OK);
    CHECK(cd.pcm.wave[3*4096+0x0ffd]==0x40&&cd.pcm.wave[3*4096+0x0ffc]==0x41);
    CHECK(segacd_dma_advance(&cd,&dma,18)==SEGACD_DMA_OK);
    CHECK(dma.words==4&&!dma.active&&cd.cdc.transfer_end);
    CHECK(cd.pcm.wave[3*4096+0x0fff]==0x42&&cd.pcm.wave[3*4096+0x0ffe]==0x43);
    CHECK(cd.pcm.wave[3*4096+0x0001]==0x44&&cd.pcm.wave[3*4096+0x0000]==0x45);
    CHECK(cd.pcm.wave[3*4096+0x0003]==0x46&&cd.pcm.wave[3*4096+0x0002]==0x47);
    CHECK(dma.address==8); /* 19-bit byte-address wrap; high-lane/low-lane pairs remain distinct. */
    CHECK(cd.cdc.remaining==0&&cd.cdc.dbc==0x0fffu);
}
static void test_pcm_consecutive_short_transfers(void)
{
    init_case(4,0x0123u,2);
    cd.pcm.bank=2;
    CHECK(segacd_dma_advance(&cd,&dma,6)==SEGACD_DMA_OK);
    CHECK(dma.address==((uint32_t)0x0123u*8u+4u));
    cd.cdc.transfer_active=0;cd.cdc.transfer_end=0;cd.cdc.fifo_count=0;
    cd.cdc.ifctrl=2;cd.cdc.dbc=1;cd.cdc.dac=0;
    cd.cdc.ram[0]=0x70;cd.cdc.ram[1]=0x71;
    segacd_cdc_address(&cd.cdc,6);segacd_cdc_write(&cd.cdc,1);
    CHECK(segacd_dma_begin(&cd,&dma)==SEGACD_DMA_OK);
    CHECK(dma.address==((uint32_t)0x0123u*8u+4u)); /* No register rewrite: retain full internal byte address. */
    CHECK(segacd_dma_advance(&cd,&dma,6)==SEGACD_DMA_OK);
    CHECK(cd.pcm.wave[2*4096+0x48f]==0x70&&cd.pcm.wave[2*4096+0x48e]==0x71);
    CHECK(dma.address==((uint32_t)0x0123u*8u+8u));
    cd.cdc.transfer_active=0;cd.cdc.transfer_end=0;
    cd.cdc_dma_address=0x0002u;
    segacd_dma_address_programmed(&cd,&dma);
    CHECK(dma.address==16);
}
static void test_prg_boundary_and_write_protect(void)
{
    init_case(5,0xffffu,12);
    CHECK(segacd_dma_advance(&cd,&dma,36)==SEGACD_DMA_OK);
    CHECK(dma.words==6&&cd.prg[0x7fff8]==0x40&&cd.prg[0x7fff9]==0x41);
    CHECK(cd.prg[0x7fffe]==0x46&&cd.prg[0x7ffff]==0x47);
    CHECK(cd.prg[0]==0x48 && cd.prg[1]==0x49 && cd.prg[2]==0x4A && cd.prg[3]==0x4B);
    CHECK(cd.cdc.transfer_end);
    init_case(5,0,2);
    cd.write_protect=1;
    CHECK(segacd_dma_advance(&cd,&dma,6)==SEGACD_DMA_OK);
    CHECK(cd.prg[0]==0&&cd.cdc.transfer_end); /* protected writes are ignored but consumed */
}
static void test_word_wait_resume_and_1m_mapping(void)
{
    init_case(7,0,4);
    /* Reset ownership defaults to main. The blocked event must preserve FIFO, source and target. */
    CHECK(segacd_dma_advance(&cd,&dma,30)==SEGACD_DMA_WAIT);
    CHECK(cd.cdc.fifo_count==2&&cd.cdc.dac==2&&dma.address==0&&dma.words==0&&dma.waits==5);
    cd.memory_mode=0; /* sub owns 2M Word RAM */
    CHECK(segacd_dma_advance(&cd,&dma,6)==SEGACD_DMA_OK);
    CHECK(cd.word[0]==0x40&&cd.word[1]==0x41&&dma.address==2);
    CHECK(segacd_dma_advance(&cd,&dma,6)==SEGACD_DMA_OK);
    CHECK(cd.word[2]==0x42&&cd.word[3]==0x43&&cd.cdc.transfer_end);
    init_case(7,0,2);
    cd.memory_mode=4; /* 1M mode; RET=0 selects sub-visible physical bank 1 */
    CHECK(segacd_dma_advance(&cd,&dma,6)==SEGACD_DMA_OK);
    CHECK(cd.word[2]==0x40&&cd.word[3]==0x41);
}
static void test_irq_mask_unmask_ack_and_odd_refusal(void)
{
    init_case(5,0,2);
    cd.cdc.ifctrl|=0x40; /* DTEI enable; gate-array level 5 remains masked */
    CHECK(segacd_dma_advance(&cd,&dma,6)==SEGACD_DMA_OK);
    CHECK(cd.cdc.transfer_end&&segacd_irq_level(&cd)==0);
    cd.irq_mask|=0x20;
    CHECK(segacd_irq_level(&cd)==5);
    segacd_cdc_address(&cd.cdc,7);segacd_cdc_write(&cd.cdc,0);
    CHECK(segacd_irq_level(&cd)==0); /* acknowledge transfer completion at CDC source */
    init_case(5,0,2);
    segacd_cdc_reset(&cd.cdc);segacd_dma_init(&dma);cd.cdc.ifctrl=2;cd.cdc.dbc=2; /* three-byte transfer */
    CHECK(segacd_dma_begin(&cd,&dma)==SEGACD_DMA_ODD_BYTE_COUNT);
    CHECK(!dma.active);
}
static void run_split(int split)
{
    init_case(5,0,12);cd.memory_mode=0;
    if(split){CHECK(segacd_dma_advance(&cd,&dma,2)==SEGACD_DMA_OK);CHECK(segacd_dma_advance(&cd,&dma,4)==SEGACD_DMA_OK);CHECK(segacd_dma_advance(&cd,&dma,7)==SEGACD_DMA_OK);CHECK(segacd_dma_advance(&cd,&dma,23)==SEGACD_DMA_OK);}
    else CHECK(segacd_dma_advance(&cd,&dma,36)==SEGACD_DMA_OK);
}
static void test_long_vs_split(void)
{
    uint8_t memory[SEGACD_PRG_BYTES];uint32_t addr;uint64_t words;
    run_split(0);memcpy(memory,cd.prg,sizeof memory);addr=dma.address;words=dma.words;
    run_split(1);CHECK(memcmp(memory,cd.prg,sizeof memory)==0&&addr==dma.address&&words==dma.words&&dma.phase==0);
    for(unsigned n=0;n<12;n++)CHECK(cd.prg[n]==(uint8_t)(0x40u+n));
}
int main(void)
{
    test_pcm_boundary_and_rate();test_pcm_consecutive_short_transfers();test_prg_boundary_and_write_protect();
    test_word_wait_resume_and_1m_mapping();test_irq_mask_unmask_ack_and_odd_refusal();
    test_long_vs_split();puts("CDC DMA destination, FIFO/ownership, completion IRQ, internal-address and split-clock tests pass");return 0;
}
