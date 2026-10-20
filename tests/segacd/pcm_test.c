#include "segacd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static Rf5c164 p,q;static SegaCD cd;static uint8_t bios[SEGACD_BIOS_BYTES];static uint64_t samples;
static void output(void*u,int16_t l,int16_t r){(void)u;CHECK(!l && !r);++samples;}
static void sample(int l,int r){int16_t left,right;rf5c164_sample(&p,&left,&right);CHECK(left==l && right==r);}
int main(void)
{
    rf5c164_init(&p);CHECK(p.disabled==255 && !p.enabled);
    for(unsigned bank=0;bank<16;++bank){rf5c164_write(&p,7,(uint8_t)bank);rf5c164_write(&p,0x1000,(uint8_t)(bank+1));rf5c164_write(&p,0x1FFF,(uint8_t)(bank+65));}
    for(unsigned bank=0;bank<16;++bank){rf5c164_write(&p,7,(uint8_t)bank);CHECK(rf5c164_read(&p,0x1000)==bank+1 && rf5c164_read(&p,0x1FFF)==bank+65);}
    /* One-unit output exercises the full 16-bit DAC precision. */
    rf5c164_init(&p);p.wave[0]=0x81;p.wave[1]=0x7F;p.wave[2]=0xFF;p.wave[0x100]=0x82;
    rf5c164_write(&p,7,0xC0);rf5c164_write(&p,0,32);rf5c164_write(&p,1,0x21);
    rf5c164_write(&p,2,0);rf5c164_write(&p,3,8);rf5c164_write(&p,5,1);rf5c164_write(&p,8,0xFE);
    sample(1,2);sample(-127,-254);sample(2,4);
    CHECK(rf5c164_read(&p,0x10)==0 && rf5c164_read(&p,0x11)==1);
    sample(2,4);CHECK(rf5c164_read(&p,0x10)==1);
    rf5c164_write(&p,8,255);sample(0,0);CHECK(p.channel[0].address==0);
    /* Fractional step, address wrap, disabled suspension and held start. */
    rf5c164_write(&p,2,0);rf5c164_write(&p,3,4);rf5c164_write(&p,8,0xFE);
    sample(1,2);sample(1,2);sample(-127,-254);
    rf5c164_write(&p,7,0x40);uint32_t address=p.channel[0].address;sample(0,0);CHECK(p.channel[0].address==address);
    rf5c164_write(&p,8,255);rf5c164_write(&p,6,0xFF);CHECK(p.channel[0].address==(0xFFu<<19));
    CHECK(rf5c164_read(&p,0)==255 && rf5c164_read(&p,0x20)==255);
    /* Eight channels, panning and saturation are summed before limiting. */
    rf5c164_init(&p);p.wave[0]=0xFE;
    for(unsigned n=0;n<8;++n){rf5c164_write(&p,7,(uint8_t)(0xC0+n));rf5c164_write(&p,0,255);rf5c164_write(&p,1,255);}
    rf5c164_write(&p,8,0);sample(32767,32767);p.wave[0]=0x7F;sample(-32768,-32768);
    q=p;rf5c164_reset(&p);CHECK(p.wave[0]==0x7F && !p.enabled && p.disabled==255);CHECK(q.enabled && q.disabled==0);
    rf5c164_advance(&p,12500000,output,NULL);CHECK(samples==32552 && p.phase==32 && p.samples==32552);
    /* Odd CPU lanes and word access use the same real chip interface. */
    CHECK(segacd_init(&cd,bios,sizeof bios));CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF000E,0xFF03)==SEGACD_OK);
    CHECK(cd.pcm.bank==3);CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF2000,0xAB42)==SEGACD_OK);
    uint16_t v=0;CHECK(segacd_read16(&cd,SEGACD_SUB,0xFF2000,&v)==SEGACD_OK && v==0xFF42);
    CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF2000,0x99)==SEGACD_OK && cd.pcm.wave[0x3000]==0x42);
    segacd_reset(&cd);CHECK(cd.pcm.wave[0x3000]==0x42 && cd.pcm.bank==0);
    puts("RF5C164 banks/lanes, independent channels, signed wave/loop/fraction, full DAC precision, saturation and 384-clock output pass");return 0;
}
