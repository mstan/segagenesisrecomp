#include "trilogy_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned be16(const uint8_t *p) { return (unsigned)p[0]*256+p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)be16(p)*65536+be16(p+2); }
typedef struct Bits { const uint8_t *p; size_t size,pos; int bad; } Bits;
static unsigned take(Bits *b,unsigned n)
{
    unsigned v=0;
    if (b->pos+n>b->size*8) { b->bad=1; return 0; }
    while (n--) { v=v*2+((b->p[b->pos/8]>>(7-b->pos%8))&1); ++b->pos; }
    return v;
}
/* Bounded ports of Kos_Decomp, Eni_Decomp and Nem_Decomp from pinned source.
 * The tiny preview also checks these against the actual verified donor. */
typedef struct Kos { const uint8_t *p; size_t size,pos; unsigned desc,bits; int bad; } Kos;
static unsigned byte(Kos *k)
{ if (k->pos>=k->size) { k->bad=1; return 0; } return k->p[k->pos++]; }
static unsigned kbit(Kos *k)
{
    unsigned v=k->desc&1; k->desc>>=1;
    if (!--k->bits) { unsigned lo=byte(k),hi=byte(k); k->desc=lo|(hi<<8); k->bits=16; }
    return v;
}
size_t tr_kosinski(const uint8_t *src,size_t size,uint8_t *out,size_t cap)
{
    if (size<2) return 0;
    Kos k={src,size,2,(unsigned)src[0]|((unsigned)src[1]<<8),16,0}; size_t n=0;
    while (!k.bad && k.pos<=size) {
        if (kbit(&k)) { if (n>=cap) return 0; out[n++]=(uint8_t)byte(&k); continue; }
        int distance; unsigned count;
        if (kbit(&k)) {
            unsigned lo=byte(&k),hi=byte(&k); distance=(int)((hi&248)*32+lo)-8192; count=hi&7;
            if (!count) {
                count=byte(&k);
                if (!count) return k.bad ? 0 : n;
                if (count==1) continue;
                ++count;
            } else count+=2;
        } else {
            unsigned high=kbit(&k),low=kbit(&k); count=high*2+low+2; distance=(int)byte(&k)-256;
        }
        if (k.bad || distance>=0 || (size_t)-distance>n || count>cap-n) return 0;
        while (count--) { out[n]=out[n+distance]; ++n; }
    }
    return 0;
}
static unsigned literal(Bits *b,unsigned flags,unsigned width)
{
    unsigned attr=0;
    for (int bit=4;bit>=0;--bit) if (flags&(1u<<bit)) attr|=take(b,1)<<(11+bit);
    return attr|take(b,width);
}
size_t tr_enigma(const uint8_t *p,size_t size,uint16_t *out,size_t cap,unsigned base)
{
    if (size<6 || p[0]>11 || p[1]>31) return 0;
    Bits b={p,size,48,0}; unsigned inc=be16(p+2),common=be16(p+4); size_t n=0;
    while (!b.bad) {
        unsigned kind,count,value;
        if (!take(&b,1)) {
            kind=take(&b,1); count=take(&b,4)+1;
            if (count>cap-n) return 0;
            while (count--) out[n++]=(uint16_t)(base+(kind?common:inc++));
        } else {
            kind=take(&b,2); count=take(&b,4)+1;
            if (kind==3 && count==16) return b.bad ? 0 : n;
            if (count>cap-n) return 0;
            value=literal(&b,p[1],p[0]);
            for (unsigned i=0;i<count;++i) {
                out[n++]=(uint16_t)(value+base);
                if (kind==3 && i+1<count) value=literal(&b,p[1],p[0]);
                else if (kind==1) ++value; else if (kind==2) --value;
            }
        }
    }
    return 0;
}
size_t tr_nemesis(const uint8_t *p,size_t size,uint8_t *out,size_t cap)
{
    if (size<4) return 0;
    unsigned header=be16(p), nibbles=(header&0x7FFF)*64, palette=0;
    if (nibbles/2>cap) return 0;
    uint16_t table[9][256]={{0}}; size_t pos=2;
    while (pos<size && p[pos]!=255) {
        unsigned descriptor=p[pos++];
        if (descriptor&128) { palette=descriptor&15; continue; }
        unsigned len=descriptor&15;
        if (!len || len>8 || pos>=size) return 0;
        unsigned code=p[pos++];
        if (code>=(1u<<len) || table[len][code]) return 0;
        table[len][code]=(uint16_t)(((descriptor&0x70)|palette)+1);
    }
    if (pos>=size) return 0;
    Bits b={p,size,(pos+1)*8,0}; unsigned written=0;
    memset(out,0,nibbles/2);
    while (!b.bad && written<nibbles) {
        unsigned code=0,value=0;
        for (unsigned len=1;len<=8;++len) {
            code=code*2+take(&b,1);
            if (len==6 && code==63) { value=take(&b,7)+1; break; }
            if (table[len][code]) { value=table[len][code]; break; }
        }
        if (!value || b.bad) return 0;
        --value; unsigned count=(value>>4)+1,color=value&15;
        if (count>nibbles-written) return 0;
        while (count--) { out[written/2]|=(uint8_t)(color<<((written&1)?0:4)); ++written; }
    }
    if (header&0x8000) for (unsigned i=4;i<nibbles/2;++i) out[i]^=out[i-4];
    return !b.bad && written==nibbles ? nibbles/2 : 0;
}
