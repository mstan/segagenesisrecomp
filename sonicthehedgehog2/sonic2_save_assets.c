#include "sonic2_save_assets.h"
#include "sonic2_campaign.h"
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
static int kosinski_size(const uint8_t *src,size_t size,uint8_t *out,size_t cap)
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
                if (!count) return k.bad?0:(int)n;
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
static int kosinski(const uint8_t *src,size_t size,uint8_t *out,size_t cap)
{ return kosinski_size(src,size,out,cap)==(int)cap; }

typedef struct StageArt {
    /* ARZ's source deliberately includes $320 blocks, exceeding the native
     * $300-block table with empty padding (s2.asm BM16_ARZ comment). */
    uint8_t tiles[65536],blocks[0x2000],chunks[0x8000],layout[0x1000];
    uint16_t palette[64];
} StageArt;
static int nemesis(const uint8_t *p,size_t size,uint8_t *out,size_t cap);
static int stage_tornado(const uint8_t *r,StageArt *a,uint16_t *pixels)
{
    /* ObjB2 frame 0, ArtNem_Tornado, and its source-selected Tails pilot $10.
     * A 2:1 icon is intentional: a terrain-only Sky Chase card is empty sky. */
    if (!nemesis(r+0x8CC44,0x100000-0x8CC44,a->tiles+0xA000,0x6000)) return 0;
    unsigned dp=0x7446C+be16(r+0x7446C+0x10*2),dest=0x7A0*32;
    if (dp>0xFFFFE) return 0;
    unsigned cues=be16(r+dp); dp+=2;
    if (cues>64 || dp+cues*2>0x100000) return 0;
    while (cues--) {
        unsigned code=be16(r+dp),bytes=((code>>12)+1)*32,source=0x64320+(code&4095)*32; dp+=2;
        if (source>0x100000-bytes || dest>65536-bytes) return 0;
        memcpy(a->tiles+dest,r+source,bytes); dest+=bytes;
    }
    unsigned map=0x3AFF2+be16(r+0x3AFF2),count=be16(r+map); map+=2;
    if (count>20 || map+count*8>0x100000) return 0;
    for (int i=(int)count-1;i>=0;--i) {
        const uint8_t *p=r+map+i*8;
        int x=(int16_t)be16(p+6),y=(int8_t)p[0];
        unsigned w=(((p[1]>>2)&3)+1)*8,h=((p[1]&3)+1)*8,tile=be16(p+2)+0x500;
        for (unsigned py=0;py<h;py+=2) for (unsigned px=0;px<w;px+=2) {
            int dx=58+(x+(int)px)/2,dy=24+(y+(int)py)/2;
            if (dx<0 || dx>=80 || dy<0 || dy>=56) continue;
            unsigned sx=tile&0x800?w-1-px:px,sy=tile&0x1000?h-1-py:py;
            unsigned t=(tile&2047)+(sx/8)*(h/8)+sy/8;
            if (t>=2048) return 0;
            unsigned ink=(a->tiles[t*32+(sy&7)*4+(sx&7)/2]>>((sx&1)?0:4))&15;
            if (ink) pixels[dy*80+dx]=a->palette[((tile>>13)&3)*16+ink];
        }
    }
    return 1;
}
static int stage_kos(const uint8_t *rom,unsigned at,uint8_t *out,size_t cap)
{
    int ok=at<0x100000 && kosinski_size(rom+at,0x100000-at,out,cap)>0;
    if (!ok) fprintf(stderr,"[NOTE] Stage art decode at $%06X exceeds %zu-byte destination or has invalid data.\n",at,cap);
    return ok;
}
static unsigned stage_pixel(const StageArt *a,unsigned x,unsigned y,unsigned plane)
{
    if (x>=16384) return 0;
    unsigned chunk=a->layout[((y>>7)&15)*256+(plane?128:0)+(x>>7)];
    unsigned block=be16(a->chunks+chunk*128+(y&112)+(x&112)/8);
    unsigned tx=((x>>3)&1)^((block>>10)&1),ty=((y>>3)&1)^((block>>11)&1);
    unsigned at=(block&1023)*8+(ty*2+tx)*2;
    if (at+1>=sizeof a->blocks) return 0;
    unsigned t=be16(a->blocks+at)^((block&0xC00)<<1);
    tx=(x&7)^((t&0x800)?7:0); ty=(y&7)^((t&0x1000)?7:0);
    unsigned p=(a->tiles[(t&2047)*32+ty*4+tx/2]>>((tx&1)?0:4))&15;
    return p?((t>>13)&3)*16+p:0;
}
int s2_stage_images_decode(const uint8_t *r,size_t size,S2SaveAssets *assets)
{
    if (!r || size!=0x100000 || !assets) return 0;
    if (assets->stages_ready) return 1;
    StageArt *a=malloc(sizeof *a); if (!a) return 0;
    int ok=1;
    /* REV01 source ledger: LevelArtPointers $42594, Off_Level $45A80,
     * StartLocations $C1D0, PalPointers $2782, LoadZoneTiles $4E90.
     * No ROM instructions execute and no live machine state is touched. */
    static const unsigned animations[17]={0x3FF94,0,0,0,0x3FFF8,0x3FFF8,0,0x40038,0,0,
        0x400C8,0,0x4010E,0x401B2,0x401C4,0x401D6,0};
    for (unsigned stage=0;stage<S2_CAMPAIGN_STAGES && ok;++stage) {
        memset(a,0,sizeof *a);
        unsigned id=s2_campaign_stages[stage].native_id,zone=id>>8,act=id&1;
        unsigned table=0x42594+zone*12,tiles=be32(r+table)&0xFFFFFF;
        unsigned blocks=be32(r+table+4)&0xFFFFFF,chunks=be32(r+table+8)&0xFFFFFF;
        unsigned layout=0x45A80+be16(r+0x45A80+zone*4+act*2);
        ok=stage_kos(r,tiles,a->tiles,sizeof a->tiles) && stage_kos(r,blocks,a->blocks,sizeof a->blocks) &&
            stage_kos(r,chunks,a->chunks,sizeof a->chunks) && stage_kos(r,layout,a->layout,sizeof a->layout);
        if (zone==7) ok=ok && stage_kos(r,0x985A4,a->blocks+0x980,sizeof a->blocks-0x980) &&
            stage_kos(r,0x98AB4,a->tiles+0x1FC*32,sizeof a->tiles-0x1FC*32);
        if (zone==6) ok=ok && stage_kos(r,0xC7EC4,a->tiles+0x307*32,sizeof a->tiles-0x307*32);
        unsigned palette=be32(r+0x2782+r[table+8]*8);
        if (palette>size-96) { ok=0; break; }
        for (unsigned p=0;p<48;++p) a->palette[16+p]=(uint16_t)be16(r+palette+p*2);
        for (unsigned p=0;p<16;++p) a->palette[p]=(uint16_t)be16(r+0x29E2+p*2);
        /* Dynamic_Normal: use the first frame of each source animation script. */
        unsigned script=animations[zone];
        if (script) {
            unsigned count=be16(r+script)+1; script+=2;
            if (count>16) { ok=0; break; }
            while (count-- && ok) {
                if (script>size-10) { ok=0; break; }
                unsigned source=(be32(r+script)&0xFFFFFF)+r[script+8]*32;
                unsigned dest=be16(r+script+4),bytes=r[script+7]*32;
                if (source>size || bytes>size-source || dest>65536-bytes) { ok=0; break; }
                memcpy(a->tiles+dest,r+source,bytes);
                unsigned frames=r[script+6]*(r[script]&128?2:1);
                script+=8+((frames+1)&~1u);
            }
        }
        unsigned start=0xC1D0+zone*8+act*4;
        unsigned sx=be16(r+start),sy=be16(r+start+2);
        unsigned left=sx>128?sx-128:0,top=sy>96?sy-96:0;
        /* Thumbnail framing uses the native camera bias and InitCam_* BG
         * transforms. The preview is a static 320x224 view sampled at 4:1. */
        int bg_top=0;
        if (zone==4 || zone==5 || zone==13) bg_top=(int)top/4;
        if (zone==10) bg_top=(int)top/8+0x50;
        if (zone==11) bg_top=act?(int)top/6-0x10:(int)top/3-0x140;
        if (zone==15) bg_top=act?((int)top-0xE0)/2:(int)top-0x180;
        if (zone==14) bg_top=(int)top;
        if (zone==16) top=0;
        if (zone==6) {
            /* The start is an airborne Tornado approach. Frame the first
             * actual ship chunk, discovered from the stock layout itself. */
            unsigned best=~0u;
            for (unsigned cy=0;cy<16;++cy) for (unsigned cx=0;cx<128;++cx) {
                if (!a->layout[cy*256+cx]) continue;
                unsigned d=(unsigned)abs((int)(cx*128)-(int)sx)+(unsigned)abs((int)(cy*128)-(int)sy);
                if (d<best) { best=d; left=cx*128; top=cy*128; }
            }
            bg_top=(int)top;
        }
        for (unsigned y=0;y<56;++y) for (unsigned x=0;x<80;++x) {
            unsigned p=stage_pixel(a,left+x*4,top+y*4,0);
            if (!p) p=stage_pixel(a,x*4,(unsigned)(bg_top+(int)y*4),1);
            assets->stages[stage][y*80+x]=a->palette[p?p:32];
        }
        if (zone==16) ok=ok && stage_tornado(r,a,assets->stages[stage]);
    }
    free(a); assets->stages_ready=ok; return ok;
}
static unsigned literal(Bits *b,unsigned flags,unsigned width)
{
    unsigned attr=0;
    for (int bit=4;bit>=0;--bit) if (flags&(1u<<bit)) attr|=take(b,1)<<(11+bit);
    return attr|take(b,width);
}
static int enigma(const uint8_t *p,size_t size,uint16_t *out,size_t cap,unsigned base)
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
            if (kind==3 && count==16) return !b.bad && n==cap;
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
static int nemesis(const uint8_t *p,size_t size,uint8_t *out,size_t cap)
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
    return !b.bad && written==nibbles;
}
void s2_save_assets_free(S2SaveAssets **a) { if (a) { free(*a); *a=NULL; } }
int s2_save_assets_decode(const uint8_t *r,size_t size,S2SaveAssets **out,char *error,size_t cap)
{
    const char *why="Invalid save-screen donor data";
    S2SaveAssets *a=NULL;
    if (!r || !out || size!=0x400000) goto failure;
    a=calloc(1,sizeof *a);
    if (!a) { why="Cannot allocate save-screen assets"; goto failure; }
    /* Exact slices from the byte-matched combined listing, see source ledger. */
    if (!kosinski(r+0x39D4A4,0x1C60,a->tiles+0x20,0x53C0) ||
        !kosinski(r+0x3A23AA,0x13A0,a->tiles+0x53E0,0x36A0) ||
        !kosinski(r+0x15A774,0xC00,a->tiles+0x8A80,0x12C0) ||
        !enigma(r+0x39D2A2,0x202,a->background,40*28,1) ||
        !enigma(r+0x3A2020,0xBE,a->layout,405,0x829F) ||
        !nemesis(r+0xCA5E0,1396,a->tiles+0xAC40,sizeof a->tiles-0xAC40)) goto failure;
    for (unsigned i=0;i<16;++i) a->palette[i]=(uint16_t)be16(r+0x39D262+i*2);
    for (unsigned i=0;i<32;++i) a->palette[16+i]=(uint16_t)be16(r+0xCA78+i*2);
    for (unsigned i=0;i<70;++i) a->new_card[i]=(uint16_t)be16(r+0x3A20DE+i*2);
    for (unsigned frame=0;frame<4;++frame) {
        uint32_t p=be32(r+0x3A216A+frame*4)+0x200000;
        if (p>size-140) goto failure;
        for (unsigned i=0;i<70;++i) a->static_card[frame][i]=(uint16_t)be16(r+p+i*2);
    }
    for (unsigned f=0;f<S2_MENU_MAP_FRAMES;++f) {
        size_t at=0xCE0E+be16(r+0xCE0E+f*2);
        if (at>0xD13C) goto failure;
        unsigned count=be16(r+at); at+=2;
        if (count>S2_MENU_MAP_PIECES || at+6*count>0xD13E) goto failure;
        a->frames[f].count=count;
        for (unsigned i=0;i<count;++i,at+=6) {
            S2MenuPiece *p=&a->frames[f].pieces[i];
            p->y=(int8_t)r[at]; p->x=(int16_t)be16(r+at+4);
            p->width=(uint8_t)(((r[at+1]>>2)&3)+1); p->height=(uint8_t)((r[at+1]&3)+1);
            p->tile=(uint16_t)(be16(r+at+2)+0x829F);
            if ((p->tile&2047)+p->width*p->height>2048) goto failure;
        }
    }
    s2_save_assets_free(out); *out=a;
    if (error && cap) *error=0;
    return 1;
failure:
    free(a);
    if (error && cap) snprintf(error,cap,"%s",why);
    return 0;
}
