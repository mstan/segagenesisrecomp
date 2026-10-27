#include "cdc.h"
#include <string.h>
enum { DTEI=64,DECI=32,DTBSY=8,DTEN=2,DOUTEN=2,DECEN=128,WRRQ=4 };
static void address_next(SegaCDCdc*c){if(c->ar)c->ar=(c->ar+1u)&15u;}
void segacd_cdc_reset(SegaCDCdc*c)
{
    c->ar=c->comin=c->ifctrl=c->ctrl0=c->ctrl1=c->sbout=0;c->ifstat=255;
    c->fifo_head=c->fifo_count=c->transfer_active=c->transfer_end=0;c->remaining=0;
    c->decoder_irq_clocks=0;
}
void segacd_cdc_init(SegaCDCdc*c){memset(c,0,sizeof *c);segacd_cdc_reset(c);}
void segacd_cdc_address(SegaCDCdc*c,uint8_t v){c->ar=v&15u;}
unsigned segacd_cdc_irq(const SegaCDCdc*c){return ((unsigned)~c->ifstat&c->ifctrl&0xE0u)!=0;}
void segacd_cdc_advance(SegaCDCdc*c,uint32_t clocks)
{
    if(!c->decoder_irq_clocks)return;
    if(clocks>=c->decoder_irq_clocks){c->decoder_irq_clocks=0;c->ifstat|=DECI;}
    else c->decoder_irq_clocks-=clocks;
}
static void refill(SegaCDCdc*c)
{
    if(!c->transfer_active)return;
    while(c->fifo_count<2 && c->fifo_count<c->remaining) {
        c->fifo[(c->fifo_head+c->fifo_count)&1u]=c->ram[c->dac&16383u];
        ++c->dac;++c->fifo_count;
    }
    if(c->fifo_count)c->ifstat&=(uint8_t)~DTEN;else c->ifstat|=DTEN;
}
int segacd_cdc_host_read(SegaCDCdc*c,uint8_t*v)
{
    if(!v || !c->fifo_count || !(c->ifctrl&DOUTEN))return 0;
    *v=c->fifo[c->fifo_head];c->fifo_head^=1u;--c->fifo_count;
    --c->remaining;c->dbc=(c->dbc-1u)&4095u;
    if(!c->remaining) {
        c->transfer_active=0;c->transfer_end=1;c->ifstat|=DTBSY;c->ifstat&=(uint8_t)~DTEI;
    } else refill(c);
    if(!c->fifo_count)c->ifstat|=DTEN;
    return 1;
}
uint8_t segacd_cdc_peek(const SegaCDCdc*c)
{
    switch(c->ar) {
    case 0:return c->comin;
    case 1:return c->ifstat;
    case 2:return (uint8_t)c->dbc;
    case 3:return (uint8_t)((c->dbc>>8)|((c->ifstat&DTEI)?0xF0:0));
    case 4:case 5:case 6:case 7:return (c->ctrl1&1u)?c->subheader[c->ar-4]:c->header[c->ar-4];
    case 8:return (uint8_t)c->block_pointer;
    case 9:return (uint8_t)(c->block_pointer>>8);
    case 10:return (uint8_t)c->write_address;
    case 11:return (uint8_t)(c->write_address>>8);
    case 12:case 13:case 14:case 15:return c->status[c->ar-12];
    default:return 0;
    }
}
uint8_t segacd_cdc_read(SegaCDCdc*c)
{
    uint8_t value=segacd_cdc_peek(c);
    if(c->ar==0){c->comin=0;c->ifstat|=128;}
    if(c->ar==15){c->ifstat|=DECI;c->decoder_irq_clocks=0;}
    address_next(c);return value;
}
void segacd_cdc_write(SegaCDCdc*c,uint8_t v)
{
    switch(c->ar) {
    case 0:c->sbout=v;break;
    case 1:
        c->ifctrl=v;
        if(!(v&DOUTEN)){c->transfer_active=c->fifo_count=0;c->remaining=0;c->ifstat|=DTBSY|DTEN;}
        break;
    case 2:if(!c->transfer_active)c->dbc=(uint16_t)((c->dbc&0xF00u)|v);break;
    case 3:if(!c->transfer_active)c->dbc=(uint16_t)((c->dbc&255u)|((v&15u)<<8));break;
    case 4:if(!c->transfer_active)c->dac=(uint16_t)((c->dac&0xFF00u)|v);break;
    case 5:if(!c->transfer_active)c->dac=(uint16_t)((c->dac&255u)|((unsigned)v<<8));break;
    case 6:
        if((c->ifctrl&DOUTEN) && !c->transfer_active) {
            c->remaining=(uint16_t)(c->dbc+1u);c->fifo_head=c->fifo_count=0;c->transfer_active=1;c->transfer_end=0;
            c->ifstat&=(uint8_t)~DTBSY;refill(c);
        }
        break;
    case 7:c->ifstat|=DTEI;break;
    case 8:c->write_address=(uint16_t)((c->write_address&0xFF00u)|v);break;
    case 9:c->write_address=(uint16_t)((c->write_address&255u)|((unsigned)v<<8));break;
    case 10:
        c->ctrl0=v&0xBFu; /* LC8951 has no LC8950 EDCRQ bit. */
        if(!(c->ctrl0&DECEN)){c->ifstat|=DECI;c->decoder_irq_clocks=0;}
        break;
    case 11:c->ctrl1=v;break;
    case 12:c->block_pointer=(uint16_t)((c->block_pointer&0xFF00u)|v);break;
    case 13:c->block_pointer=(uint16_t)((c->block_pointer&255u)|((unsigned)v<<8));break;
    case 14:break; /* Unused on the original LC8951. */
    case 15:segacd_cdc_reset(c);return;
    default:break;
    }
    address_next(c);
}
void segacd_cdc_sector(SegaCDCdc*c,const uint8_t raw[2352],int edc_ok)
{
    if(!raw || !(c->ctrl0&DECEN))return;
    ++c->sectors;
    if(c->ctrl0&WRRQ) {
        unsigned start=c->write_address;
        for(unsigned n=0;n<2352;++n)c->ram[(start+n)&16383u]=raw[n];
        c->block_pointer=(uint16_t)(start+12u);c->write_address=(uint16_t)(start+2352u);
    }
    memcpy(c->header,raw+12,4);memcpy(c->subheader,raw+16,4);
    c->status[0]=(uint8_t)(edc_ok?128:1); /* CRCOK or uncorrectable block. */
    c->status[1]=0;c->status[2]=(uint8_t)((raw[15]&15u)<<4);c->status[3]=0;
    /* A bad EDC is reported, never magically repaired. Appendix E describes
     * the original LC8951's finite DECI pulse (distinct from LC8950 timing). */
    if(!edc_ok)c->status[2]|=4; /* NOCOR */
    c->ifstat&=(uint8_t)~DECI;
    c->decoder_irq_clocks=!(c->ctrl0&7u)?34375u:
        (raw[15]==2 && (raw[18]&32u))?18750u:22500u;
}
