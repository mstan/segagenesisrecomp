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
/* Select constant hardware geometry once per existing atomic line job.
 * The included portable C kernel consumes exact <=8-dot destination spans;
 * live Word RAM reads/writes and scheduler observation boundaries are unchanged.
 * Keeping all eight modes as code avoids per-pixel metadata/variable shifts. */
#define STAMP_JOIN_INNER(a,b) a##b
#define STAMP_JOIN(a,b) STAMP_JOIN_INNER(a,b)
#define STAMP_NAME(prefix) STAMP_JOIN(prefix,STAMP_MODE)
#define STAMP_MODE 0
#include "stamp_line.inc"
#undef STAMP_MODE
#define STAMP_MODE 1
#include "stamp_line.inc"
#undef STAMP_MODE
#define STAMP_MODE 2
#include "stamp_line.inc"
#undef STAMP_MODE
#define STAMP_MODE 3
#include "stamp_line.inc"
#undef STAMP_MODE
#define STAMP_MODE 4
#include "stamp_line.inc"
#undef STAMP_MODE
#define STAMP_MODE 5
#include "stamp_line.inc"
#undef STAMP_MODE
#define STAMP_MODE 6
#include "stamp_line.inc"
#undef STAMP_MODE
#define STAMP_MODE 7
#include "stamp_line.inc"
#undef STAMP_MODE
#undef STAMP_NAME
#undef STAMP_JOIN
#undef STAMP_JOIN_INNER
static void render_line(SegaCDStamp *s,uint8_t *ram,unsigned priority)
{
    switch(s->active[0]&7u) {
    case 0:stamp_line_0(s,ram,priority);break;
    case 1:stamp_line_1(s,ram,priority);break;
    case 2:stamp_line_2(s,ram,priority);break;
    case 3:stamp_line_3(s,ram,priority);break;
    case 4:stamp_line_4(s,ram,priority);break;
    case 5:stamp_line_5(s,ram,priority);break;
    case 6:stamp_line_6(s,ram,priority);break;
    case 7:stamp_line_7(s,ram,priority);break;
    }
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
