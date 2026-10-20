/* Derived in part from ares Mega-CD cdc-transfer.cpp and mcd.cpp at
 * commit 9408cb43d4948fc3ea6e152a307a34348df3fe04.
 * Copyright (c) 2004-2025 ares team, Near et al.
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 * Six-clock pacing, PCM +4-byte word stepping, and lane order below are
 * reference-profile facts,
 * not independently measured hardware behavior. */
#include "dma.h"
#include "segacd.h"
#include <string.h>
enum { DMA_MODE_1M=4, DMA_DOUTEN=2, DMA_PCM=4, DMA_PRG=5, DMA_WORD=7 };

void segacd_dma_init(SegaCDDma *dma)
{
    if(dma)memset(dma,0,sizeof *dma);
}

void segacd_dma_address_programmed(SegaCD *cd,SegaCDDma *dma)
{
    if(!cd||!dma)return;
    dma->address=((uint32_t)cd->cdc_dma_address<<3)&0x7ffffu;
    dma->address_valid=1;
}

SegaCDDmaResult segacd_dma_begin(SegaCD *cd,SegaCDDma *dma)
{
    if(!cd||!dma)return SEGACD_DMA_BAD_DESTINATION;
    if(cd->cdc_destination!=DMA_PCM&&cd->cdc_destination!=DMA_PRG&&cd->cdc_destination!=DMA_WORD)
        return dma->last_result=SEGACD_DMA_BAD_DESTINATION;
    if(!(cd->cdc.ifctrl&DMA_DOUTEN))return dma->last_result=SEGACD_DMA_DISABLED;
    /* LC8951 DBC represents byte_count-1. This first draft transfers words;
     * reject an odd byte count rather than dropping/duplicating a lane. */
    if(((cd->cdc.dbc+1u)&1u)!=0)return dma->last_result=SEGACD_DMA_ODD_BYTE_COUNT;
    if(!dma->address_valid) {
        dma->address=((uint32_t)cd->cdc_dma_address<<3)&0x7ffffu;
        dma->address_valid=1;
    }
    dma->active=1;
    dma->last_result=SEGACD_DMA_OK;
    return SEGACD_DMA_OK;
}

static SegaCDDmaResult transfer_word(SegaCD *cd,SegaCDDma *dma)
{
    SegaCDCdc *cdc=&cd->cdc;
    if(!dma->active)return SEGACD_DMA_OK;
    if(!cdc->transfer_active) {
        dma->active=0;
        return dma->last_result=SEGACD_DMA_OK;
    }
    if(!(cdc->ifctrl&DMA_DOUTEN))return dma->last_result=SEGACD_DMA_DISABLED;
    if(cdc->fifo_count<2)return dma->last_result=SEGACD_DMA_OK;

    uint8_t high=cdc->fifo[cdc->fifo_head];
    uint8_t low=cdc->fifo[cdc->fifo_head^1u];
    uint16_t value=(uint16_t)(((unsigned)high<<8)|low);
    SegaCDResult result=SEGACD_OK;
    if(cd->cdc_destination==DMA_PCM) {
        /* ares RF5C164 DMA writes odd lane/high byte, then even/low byte.
         * Preserve that reference ordering through the local chip mapping. */
        unsigned index=(dma->address>>1)&0x0fffu;
        rf5c164_write(&cd->pcm,0x1000u|index|1u,high);
        rf5c164_write(&cd->pcm,0x1000u|index,low);
    } else if(cd->cdc_destination==DMA_PRG) {
        result=segacd_write16(cd,SEGACD_SUB,dma->address&0x7fffeu,value);
    } else if(cd->cdc_destination==DMA_WORD) {
        uint32_t mask=(cd->memory_mode&DMA_MODE_1M)?0x1ffffu:0x3ffffu;
        uint32_t base=(cd->memory_mode&DMA_MODE_1M)?0x0c0000u:0x080000u;
        result=segacd_write16(cd,SEGACD_SUB,base|(dma->address&mask&~1u),value);
    } else return dma->last_result=SEGACD_DMA_BAD_DESTINATION;

    if(result==SEGACD_WAIT) {
        ++dma->waits;
        return dma->last_result=SEGACD_DMA_WAIT;
    }
    if(result!=SEGACD_OK)return dma->last_result=SEGACD_DMA_UNMAPPED;

    /* Destination write succeeded, so consume exactly its two source bytes.
     * With no reentrant callbacks between peek/write/consume, both reads must
     * succeed; the precondition above prevents an underflow. */
    if(!segacd_cdc_host_read(cdc,&high)||!segacd_cdc_host_read(cdc,&low))
        return dma->last_result=SEGACD_DMA_UNMAPPED;
    dma->address=(dma->address+(cd->cdc_destination==DMA_PCM?4u:2u))&0x7ffffu;
    cd->cdc_dma_address=(uint16_t)(dma->address>>3);
    ++dma->words;
    if(cdc->transfer_end)dma->active=0;
    return dma->last_result=SEGACD_DMA_OK;
}

SegaCDDmaResult segacd_dma_advance(SegaCD *cd,SegaCDDma *dma,uint32_t clocks)
{
    if(!cd||!dma)return SEGACD_DMA_BAD_DESTINATION;
    uint64_t total=(uint64_t)dma->phase+clocks;
    uint32_t events=(uint32_t)(total/6u);
    dma->phase=(uint8_t)(total%6u);
    SegaCDDmaResult result=dma->last_result;
    for(uint32_t n=0;n<events && dma->active;++n) {
        result=transfer_word(cd,dma);
        if(result!=SEGACD_DMA_OK && result!=SEGACD_DMA_WAIT)break;
    }
    return result;
}
