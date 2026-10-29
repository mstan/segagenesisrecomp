/* SPDX-License-Identifier: ISC
 * Graphics sampling and line scheduling adapted from ares GPU, commit
 * 9408cb43d4948fc3ea6e152a307a34348df3fe04, ares/md/mcd/gpu.cpp.
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
 * Modifications: portable C/explicit state and big-endian byte RAM, gate-array
 * integration, bounded zero-size work, tests and diagnostics. See
 * docs/SEGA_CD_REUSE.md. ares's five-clock/dot timing is a functional profile,
 * not independent hardware qualification of contention/refresh. */
#include "stamp.h"
#include <string.h>

void segacd_stamp_reset(SegaCDStamp *s){memset(s,0,sizeof *s);}
uint16_t segacd_stamp_read(const SegaCDStamp *s,unsigned reg)
{
    if(reg>=7)return 0; /* Trace address is write only. */
    return (uint16_t)(s->reg[reg]|(reg==0 && s->busy?0x8000u:0));
}
void segacd_stamp_latch(SegaCDStamp *s,unsigned reg,uint16_t value)
{
    static const uint16_t masks[8]={7,0xFFE0,31,0xFFF8,63,511,255,0xFFFE};
    if(reg<8)s->reg[reg]=value&masks[reg];
}
void segacd_stamp_start(SegaCDStamp *s,uint16_t vector)
{
    segacd_stamp_latch(s,7,vector);
    memcpy(s->active,s->reg,sizeof s->active);
    s->vector=(uint32_t)s->reg[7]<<2;s->line=0;s->busy=1;
    /* Functional timing profile, pending contention/refresh qualification.
     * Finite asynchronous work preserves GRON and the completion interrupt;
     * no host-time shortcut or immediate fake completion. */
    s->line_clocks=s->active[5]?5u*s->active[5]:1u;
    s->remaining_clocks=s->line_clocks;++s->operations;
}
static uint16_t word_read(const uint8_t *ram,uint32_t a)
{return (uint16_t)(((unsigned)ram[a&0x3FFFFu]<<8)|ram[(a+1)&0x3FFFFu]);}
static unsigned sample(const SegaCDStamp *s,const uint8_t *ram,uint32_t x,uint32_t y)
{
    unsigned control=s->active[0],large=control&4u,side=control&2u?32u:16u;
    uint32_t map_side=large?4096u:256u;
    if(!(control&1u) && (x>=map_side || y>=map_side))return 0;
    x&=map_side-1;y&=map_side-1;
    unsigned entries=map_side/side;
    /* A 4096-dot map needs 128 KiB (16-dot stamps) or 32 KiB (32-dot).
     * The p36 labels are transposed; masks follow these table capacities. */
    unsigned base_mask=large?(side==16?0x8000u:0xE000u):(side==16?0xFF80u:0xFFE0u);
    uint32_t map=((uint32_t)(s->active[1]&base_mask)<<2)+2*((y/side)*entries+x/side);
    unsigned entry=word_read(ram,map),id=entry&0x7FFu;
    if(side==32)id&=~3u;
    if(!id)return 0; /* Stamp zero is ordinary RAM, never graphics data. */
    unsigned px=x&(side-1),py=y&(side-1);
    /* ares's source-coordinate transform order: flip, 180, then 90. */
    if(entry&0x8000u)px=side-1-px;
    if(entry&0x4000u){px=side-1-px;py=side-1-py;}
    if(entry&0x2000u){unsigned old=px;px=side-1-py;py=old;}
    /* 8x8, 32-byte cells, down each column before the next column. */
    uint32_t a=id*128u+32*((px/8)*(side/8)+py/8)+4*(py&7u)+(px&7u)/2;
    unsigned packed=ram[a&0x3FFFFu];
    return (packed>>(px&1u?0:4))&15u;
}
static void render_line(SegaCDStamp *s,uint8_t *ram,unsigned priority)
{
    uint32_t vector=s->vector;
    /* X/Y are unsigned 13.3. Deltas are signed two's-complement 5.11.
     * Accumulate in a wrapping 24-bit coordinate with eleven fraction bits;
     * discarded low bits truncate as shown in the p40 accumulator diagram. */
    uint32_t x=(uint32_t)word_read(ram,vector)<<8;
    uint32_t y=(uint32_t)word_read(ram,vector+2)<<8;
    uint32_t dx=(uint32_t)(int32_t)(int16_t)word_read(ram,vector+4);
    uint32_t dy=(uint32_t)(int32_t)(int16_t)word_read(ram,vector+6);
    unsigned yout=(s->active[4]>>3)+s->line;
    unsigned stride=(s->active[2]+1u)*32u;
    uint32_t base=(uint32_t)s->active[3]<<2;
    for(unsigned n=0;n<s->active[5];++n) {
        unsigned pixel=sample(s,ram,(x&0xFFFFFFu)>>11,(y&0xFFFFFFu)>>11);
        unsigned xout=(s->active[4]&7u)+n;
        uint32_t a=(base+(xout/8)*stride+(yout/8)*32+4*(yout&7u)+(xout&7u)/2)&0x3FFFFu;
        unsigned shift=xout&1u?0:4,old=(ram[a]>>shift)&15u;
        if((priority!=1 || !old) && (priority!=2 || pixel))
            ram[a]=(uint8_t)((ram[a]&~(15u<<shift))|(pixel<<shift));
        x+=dx;y+=dy;
    }
    s->pixels+=s->active[5];s->vector=(s->vector+8)&0x3FFFFu;
    ++s->line;s->reg[6]=(uint16_t)(s->active[6]>s->line?s->active[6]-s->line:0);
}
int segacd_stamp_advance(SegaCDStamp *s,uint8_t word[0x40000],unsigned priority,uint32_t clocks)
{
    if(!s->busy)return 0;
    while(clocks>=s->remaining_clocks) {
        clocks-=s->remaining_clocks;
        if(s->active[6])render_line(s,word,priority);
        if(s->line>=s->active[6]){s->busy=0;s->remaining_clocks=0;return 1;}
        s->remaining_clocks=s->line_clocks;
    }
    s->remaining_clocks-=clocks;return 0;
}
