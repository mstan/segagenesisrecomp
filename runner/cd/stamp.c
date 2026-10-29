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
/* A line is the device's existing atomic job. Decode only its immutable
 * register geometry here; map words, graphics and destination nibbles remain
 * live reads in sample-before-write order, including overlapping Word RAM. */
typedef struct {
    uint32_t map_base, map_mask;
    unsigned side_shift, side_mask, entries_shift, repeat;
} StampLine;
static unsigned sample(const StampLine *job,const uint8_t *ram,uint32_t x,uint32_t y)
{
    if(!job->repeat && ((x|y)>job->map_mask))return 0;
    x&=job->map_mask;y&=job->map_mask;
    uint32_t map=job->map_base+2u*(((y>>job->side_shift)<<job->entries_shift)+(x>>job->side_shift));
    unsigned entry=word_read(ram,map),id=entry&0x7FFu;
    if(job->side_shift==5)id&=~3u;
    if(!id)return 0; /* Stamp zero is ordinary RAM, never graphics data. */
    unsigned px=x&job->side_mask,py=y&job->side_mask;
    /* ares's source-coordinate transform order: flip, 180, then 90. */
    if(entry&0x8000u)px=job->side_mask-px;
    if(entry&0x4000u){px=job->side_mask-px;py=job->side_mask-py;}
    if(entry&0x2000u){unsigned old=px;px=job->side_mask-py;py=old;}
    /* 8x8, 32-byte cells, down each column before the next column. */
    uint32_t a=id*128u+32u*(((px>>3)<<(job->side_shift-3))+(py>>3))+4u*(py&7u)+((px&7u)>>1);
    unsigned packed=ram[a&0x3FFFFu];
    return (packed>>(px&1u?0:4))&15u;
}
static void render_line(SegaCDStamp *s,uint8_t *ram,unsigned priority)
{
    unsigned control=s->active[0],large=control&4u,wide=control&2u;
    StampLine job;
    job.side_shift=wide?5u:4u;job.side_mask=(1u<<job.side_shift)-1u;
    job.map_mask=large?4095u:255u;
    job.entries_shift=(large?12u:8u)-job.side_shift;job.repeat=control&1u;
    unsigned base_mask=large?(wide?0xE000u:0x8000u):(wide?0xFFE0u:0xFF80u);
    job.map_base=(uint32_t)(s->active[1]&base_mask)<<2;
    uint32_t vector=s->vector;
    /* X/Y are unsigned 13.3. Deltas are signed two's-complement 5.11.
     * Keep fractional bits until sampling, including 24-bit source wrapping. */
    uint32_t x=(uint32_t)word_read(ram,vector)<<8;
    uint32_t y=(uint32_t)word_read(ram,vector+2)<<8;
    uint32_t dx=(uint32_t)(int32_t)(int16_t)word_read(ram,vector+4);
    uint32_t dy=(uint32_t)(int32_t)(int16_t)word_read(ram,vector+6);
    unsigned yout=(s->active[4]>>3)+s->line;
    unsigned stride=(s->active[2]+1u)*32u;
    unsigned phase=s->active[4]&7u,width=s->active[5];
    uint32_t a=(((uint32_t)s->active[3]<<2)+(yout>>3)*32u+4u*(yout&7u)+(phase>>1))&0x3FFFFu;
    for(unsigned n=0;n<width;++n) {
        unsigned pixel=sample(&job,ram,(x&0xFFFFFFu)>>11,(y&0xFFFFFFu)>>11);
        unsigned shift=phase&1u?0:4,old=(ram[a]>>shift)&15u;
        if((priority!=1 || !old) && (priority!=2 || pixel))
            ram[a]=(uint8_t)((ram[a]&~(15u<<shift))|(pixel<<shift));
        /* Advance the column-major cell cursor without remapping each dot.
         * Do not combine nibble stores: a write can change the next sample. */
        if(++phase==8){phase=0;a=(a+stride-3u)&0x3FFFFu;}
        else if(!(phase&1u))a=(a+1u)&0x3FFFFu;
        x+=dx;y+=dy;
    }
    s->pixels+=width;s->vector=(s->vector+8)&0x3FFFFu;
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
