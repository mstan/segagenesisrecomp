/* LC8951 controller interface. Sanyo Design Manual sections 10-12, appendix E.
 * The caller supplies a decoded raw sector/error result; ECC and serial input
 * phasing remain separate qualification work. No host/media pointers in state. */
#ifndef SEGACD_CDC_H
#define SEGACD_CDC_H
#include <stdint.h>
typedef struct {
    uint8_t ram[16384];
    uint8_t ar,ifstat,ifctrl,ctrl0,ctrl1,comin,sbout;
    uint8_t header[4],subheader[4],status[4];
    /* DAC counts FIFO loads; DBC/remaining count actual host reads. */
    uint16_t dbc,dac,write_address,block_pointer,remaining;
    uint8_t fifo[2],fifo_head,fifo_count,transfer_active,transfer_end;
    uint32_t decoder_irq_clocks;
    uint64_t sectors;
} SegaCDCdc;
void segacd_cdc_init(SegaCDCdc *);
/* Soft reset preserves external buffer RAM and undefined address registers. */
void segacd_cdc_reset(SegaCDCdc *);
void segacd_cdc_address(SegaCDCdc *,uint8_t);
uint8_t segacd_cdc_peek(const SegaCDCdc *);
uint8_t segacd_cdc_read(SegaCDCdc *);
void segacd_cdc_write(SegaCDCdc *,uint8_t);
unsigned segacd_cdc_irq(const SegaCDCdc *);
/* Original LC8951 appendix E pulse profile, at 12.5 MHz. Manufacturer
 * durations are approximate; exact input/decoder pipeline phase is pending. */
void segacd_cdc_advance(SegaCDCdc *,uint32_t clocks);
/* Returns zero when no data is available; never fabricates a transferred byte. */
int segacd_cdc_host_read(SegaCDCdc *,uint8_t *);
/* MODE1/2352 input. edc_ok must come from actual validation; this function
 * does not silently label an unvalidated/damaged sector as corrected. */
void segacd_cdc_sector(SegaCDCdc *,const uint8_t raw[2352],int edc_ok);
#endif
