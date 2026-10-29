/* Include-only implementation for one of the eight hardware geometry modes.
 * STAMP_MODE is constant: do not turn it into per-pixel mutable job metadata.
 * Only code is specialized. Every map/art/output byte remains live Word RAM. */
static unsigned STAMP_NAME(stamp_sample_)(const uint8_t *ram,uint32_t map_base,uint32_t x,uint32_t y)
{
    const unsigned side_shift=(STAMP_MODE&2)?5u:4u;
    const unsigned side_mask=(1u<<side_shift)-1u;
    const unsigned map_shift=(STAMP_MODE&4)?12u:8u;
    const unsigned map_mask=(1u<<map_shift)-1u;
#if !(STAMP_MODE&1)
    if((x|y)>map_mask)return 0;
#endif
    x&=map_mask;y&=map_mask;
    uint32_t map=map_base+2u*(((y>>side_shift)<<(map_shift-side_shift))+(x>>side_shift));
    unsigned entry=word_read(ram,map),id=entry&0x7FFu;
#if STAMP_MODE&2
    id&=~3u;
#endif
    if(!id)return 0;
    unsigned px=x&side_mask,py=y&side_mask;
    if(entry&0x8000u)px=side_mask-px;
    if(entry&0x4000u){px=side_mask-px;py=side_mask-py;}
    if(entry&0x2000u){unsigned old=px;px=side_mask-py;py=old;}
    uint32_t a=id*128u+32u*(((px>>3)<<(side_shift-3))+(py>>3))+4u*(py&7u)+((px&7u)>>1);
    return (ram[a&0x3FFFFu]>>(px&1u?0:4))&15u;
}
static void STAMP_NAME(stamp_line_)(SegaCDStamp *s,uint8_t *ram,unsigned priority)
{
    const unsigned base_mask=(STAMP_MODE&4)?((STAMP_MODE&2)?0xE000u:0x8000u):((STAMP_MODE&2)?0xFFE0u:0xFF80u);
    uint32_t map_base=(uint32_t)(s->active[1]&base_mask)<<2;
    uint32_t vector=s->vector;
    uint32_t x=(uint32_t)word_read(ram,vector)<<8;
    uint32_t y=(uint32_t)word_read(ram,vector+2)<<8;
    uint32_t dx=(uint32_t)(int32_t)(int16_t)word_read(ram,vector+4);
    uint32_t dy=(uint32_t)(int32_t)(int16_t)word_read(ram,vector+6);
    unsigned yout=(s->active[4]>>3)+s->line;
    unsigned stride=(s->active[2]+1u)*32u;
    unsigned phase=s->active[4]&7u,width=s->active[5],remaining=width;
    uint32_t column=(((uint32_t)s->active[3]<<2)+(yout>>3)*32u+4u*(yout&7u))&0x3FFFFu;
    while(remaining) {
        unsigned count=8u-phase;if(count>remaining)count=remaining;
        unsigned end=phase+count;
        for(unsigned dot=phase;dot<end;++dot) {
            unsigned pixel=STAMP_NAME(stamp_sample_)(ram,map_base,(x&0xFFFFFFu)>>11,(y&0xFFFFFFu)>>11);
            uint32_t a=(column+(dot>>1))&0x3FFFFu;
            unsigned shift=dot&1u?0:4,old=(ram[a]>>shift)&15u;
            if((priority!=1 || !old) && (priority!=2 || pixel))
                ram[a]=(uint8_t)((ram[a]&~(15u<<shift))|(pixel<<shift));
            x+=dx;y+=dy;
        }
        /* A span owns only destination geometry, never its bytes: even two
         * nibbles in one byte must preserve source read -> single write order. */
        remaining-=count;phase=0;column=(column+stride)&0x3FFFFu;
    }
    s->pixels+=width;s->vector=(s->vector+8)&0x3FFFFu;
    ++s->line;s->reg[6]=(uint16_t)(s->active[6]>s->line?s->active[6]-s->line:0);
}
