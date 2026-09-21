#include "trilogy_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef TRILOGY_ASSETS_TRACE
#define TR_FAIL (fprintf(stderr,"asset conversion rejected at line %d\n",__LINE__),0)
#else
#define TR_FAIL 0
#endif

static unsigned word(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
static void putword(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void animation(TrStageAssets *a,const uint8_t *r,unsigned address,unsigned tile,unsigned tiles,
                      unsigned frames,const uint8_t *offset,const uint8_t *duration,unsigned bytes)
{
    TrTileAnimation *p=&a->animations[a->animation_count++];
    p->tile=tile;p->tiles=tiles;p->frames=frames;
    memcpy(p->offset,offset,frames);memcpy(p->duration,duration,frames);memcpy(p->data,r+address,bytes);
    unsigned end=(tile+tiles)*32;if(end>a->tile_bytes)a->tile_bytes=end;
}
void tr_stage_animate(const TrStageAssets *a,unsigned frame,uint8_t *vram)
{
    for(unsigned n=0;n<a->animation_count;++n){const TrTileAnimation *p=&a->animations[n];
        unsigned period=0,i=0;for(unsigned f=0;f<p->frames;++f)period+=p->duration[f];
        unsigned t=frame%period;while(t>=p->duration[i])t-=p->duration[i++];
        memcpy(vram+p->tile*32,p->data+p->offset[i]*32,p->tiles*32);
    }
}
static int ring(TrStageAssets *a,int x,int y)
{
    if(a->ring_count==TR_MAX_RINGS||x<0||x>32767||y<0||y>4095)return TR_FAIL;
    TrRing *r=&a->rings[a->ring_count++];r->x=(uint16_t)x;r->y=(uint16_t)y;return 1;
}
static int sort_rings(const void *a,const void *b)
{
    const TrRing *x=a,*y=b;return x->x!=y->x?(int)x->x-y->x:(int)x->y-y->y;
}
static int placements(TrStageAssets *a,const uint8_t *p,size_t size,int sonic1)
{
    static const int8_t dx[16]={16,24,32,0,0,0,16,24,32,-16,-24,-32,16,24,-16,-24};
    static const int8_t dy[16]={0,0,0,16,24,32,16,24,32,16,24,32,8,16,8,16};
    for(size_t pos=0;pos+2<=size;pos+=6){
        const uint8_t *r=p+pos;unsigned x=word(r);if(x==65535)return 1;
        if(pos+6>size)return TR_FAIL;
        unsigned rawy=word(r+2),y=rawy&0xFFF,id=sonic1?r[4]&127:r[4],subtype=r[5];
        if(sonic1&&id==0x25){
            unsigned count=(subtype&7)+1;if(count==8)count=7;
            for(unsigned j=0;j<count;++j)if(!ring(a,(int)x+(int)j*dx[subtype>>4],(int)y+(int)j*dy[subtype>>4]))return TR_FAIL;
            continue;
        }
        if(a->object_count==TR_MAX_OBJECTS)return TR_FAIL;
        TrPlacement *o=&a->objects[a->object_count++];o->x=(uint16_t)x;o->y=(uint16_t)y;
        o->id=(uint8_t)id;o->subtype=(uint8_t)subtype;
        o->flags=(uint8_t)(sonic1?(rawy>>14):((rawy>>13)&3));
    }
    return TR_FAIL;
}
/* S3 chunks are 128px, with a byte index in the layout. Deduplicate quadrants
 * from S1's 256px chunks, including alternate loop collision chunks. */
static int s1_quadrant(TrStageAssets *a,const uint8_t *chunks,size_t n,unsigned id,unsigned q)
{
    uint8_t converted[128]={0};
    if(id){
        if(id>n/512)return -1;
        const uint8_t *src=chunks+(id-1)*512+(q/2)*256+(q%2)*16;
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
            unsigned b=word(src+y*32+x*2),index=b&0x7FF;
            if(index>=a->block_count)return -1;
            if((b&0x6000)&&index>=410)return -1;
            unsigned attr=index|((b&0x1800)>>1)|((b&0x6000)>>1)|((b&0x6000)<<1);
            putword(converted+y*16+x*2,attr);
        }
    }
    for(unsigned i=0;i<a->chunk_count;++i)if(!memcmp(a->chunks+i*128,converted,128))return (int)i;
    if(a->chunk_count==256)return -1;
    memcpy(a->chunks+a->chunk_count*128,converted,128);return (int)a->chunk_count++;
}
static int s1_layout(TrStageAssets *a,const uint8_t *fg,size_t size,const uint8_t *bg,
                     const uint8_t *chunks,size_t chunk_size)
{
    unsigned width=(fg[0]+1)*2,height=(fg[1]+1)*2,bgwidth=(bg[0]+1)*2,bgheight=(bg[1]+1)*2;
    if(width>256||height>32||bgwidth>256||bgheight>32||size<2+(width/2)*(height/2))return TR_FAIL;
    unsigned pos=0x88;
    a->s1_width=width/2;a->s1_height=height/2;
    if(a->s1_width*a->s1_height>sizeof a->s1_chunks)return TR_FAIL;
    memcpy(a->s1_chunks,fg+2,a->s1_width*a->s1_height);
    putword(a->layout,width);putword(a->layout+2,bgwidth);putword(a->layout+4,height);putword(a->layout+6,bgheight);
    memcpy(a->alternate_layout,a->layout,8);
    for(unsigned row=0;row<32;++row){
        for(unsigned plane=0;plane<2;++plane){
            unsigned w=plane?bgwidth:width,h=plane?bgheight:height;
            /* Repeated background rows reproduce S1's one-chunk-high scenery. */
            unsigned sy=plane?row%h:row;
            if(!plane&&row>=h){putword(a->layout+8+row*4,0x8088);putword(a->alternate_layout+8+row*4,0x8088);continue;}
            if(pos+w>sizeof a->layout)return TR_FAIL;
            putword(a->layout+8+row*4+plane*2,0x8000+pos);
            putword(a->alternate_layout+8+row*4+plane*2,0x8000+pos);
            const uint8_t *source=plane?bg:fg;
            for(unsigned x=0;x<w;++x){
                unsigned raw=source[2+(sy/2)*(w/2)+x/2],id=raw&127,alt=id;
                if(!plane&&(raw&128)){alt=id+1;if(alt==0x29)alt=0x51;}
                unsigned q=(sy&1)*2+(x&1);
                int first=s1_quadrant(a,chunks,chunk_size,id,q),second=s1_quadrant(a,chunks,chunk_size,alt,q);
                if(first<0||second<0)return TR_FAIL;
                a->layout[pos+x]=(uint8_t)first;a->alternate_layout[pos+x]=(uint8_t)second;
            }
            pos+=w;
        }
    }
    return 1;
}
static int decode_s1(unsigned act,const uint8_t *r,TrStageAssets *a)
{
    static const unsigned layouts[]={0x68C7E,0x68D74,0x68E40},sizes[]={242,200,290};
    static const unsigned objs[]={0x6B096,0x6B5A0,0x6BB5E};
    uint16_t blocks[0xC00];uint8_t chunks[0x10000];
    size_t n=tr_enigma(r+0x3BCF2,2464,blocks,0xC00,0);
    if(!n||n%4)return TR_FAIL;a->block_count=(unsigned)n/4;
    for(size_t i=0;i<n;++i)putword(a->blocks+i*2,blocks[i]);
    size_t first=tr_nemesis(r+0x3C692,5727,a->tiles,sizeof a->tiles);
    if(first!=0x39A0)return TR_FAIL;
    size_t second=tr_nemesis(r+0x3DCF2,5031,a->tiles+first,sizeof a->tiles-first);
    if(second!=0x2E20)return TR_FAIL;a->tile_bytes=(unsigned)(first+second);
    /* The main PLC ends before the stalk and animated flowers/waterfall.
     * These tile slots are stage art, never free space for object graphics. */
    if(tr_nemesis(r+0x2F41E,0x80000-0x2F41E,a->tiles+0x358*32,4*32)!=4*32)return TR_FAIL;
    {const uint8_t offsets[]={0,8},duration[]={6,6};animation(a,r,0x66AD6,0x378,8,2,offsets,duration,0x200);}
    {const uint8_t offsets[]={0,16},duration[]={16,16};animation(a,r,0x66CD6,0x35C,16,2,offsets,duration,0x400);}
    {const uint8_t offsets[]={0,12,24,12},duration[]={128,8,128,8};animation(a,r,0x670D6,0x36C,12,4,offsets,duration,0x480);}
    tr_stage_animate(a,0,a->tiles);
    n=tr_kosinski(r+0x3F09A,8464,chunks,sizeof chunks);if(!n||n%512)return TR_FAIL;
    /* Explicit blank chunk; S1's chunk zero never indexes its chunk table. */
    a->chunk_count=1;
    if(!s1_layout(a,r+layouts[act],sizes[act],r+0x68F62,chunks,n))return TR_FAIL;
    /* Decorative blocks extend past the GHZ collision index. They have no
     * solidity bits and must not read into the next zone's index. */
    for(unsigned i=0;i<410;++i){a->collision[i*2]=r[0x64A00+i];a->collision[0x600+i*2]=r[0x64A00+i];}
    memcpy(a->angles,r+0x62900,256);memcpy(a->heights,r+0x62A00,4096);memcpy(a->widths,r+0x63A00,4096);
    for(unsigned i=0;i<48;++i)a->palette[i]=(uint16_t)word(r+0x23A8+i*2);
    for(unsigned i=0;i<16;++i){a->sprite_palette[i]=(uint16_t)word(r+0x2388+i*2);a->water_palette[i]=(uint16_t)word(r+0x1B86+i*2);}
    a->start_x=(uint16_t)word(r+0x6112+act*4);a->start_y=(uint16_t)word(r+0x6114+act*4);
    a->max_x=(uint16_t)word(r+0x5F20+act*12);a->max_y=(uint16_t)word(r+0x5F24+act*12);
    return placements(a,r+objs[act],act==0?1290:act==1?1470:0x800,1);
}
static int decode_s2(const uint8_t *r,TrStageAssets *a)
{
    memcpy(a->spiral_y,r+0x21668,sizeof a->spiral_y);
    memcpy(a->spiral_flip,r+0x21634,sizeof a->spiral_flip);
    uint8_t layout[0x1000],primary[0x600],secondary[0x600];
    size_t n=tr_kosinski(r+0x94E74,3504,a->blocks,sizeof a->blocks);if(!n||n%8)return TR_FAIL;a->block_count=(unsigned)n/8;
    n=tr_kosinski(r+0x95C24,10624,a->tiles,sizeof a->tiles);if(!n)return TR_FAIL;a->tile_bytes=(unsigned)n;
    {const uint8_t offsets[]={0,2,0,2,0,2},duration[]={128,20,8,8,8,8};animation(a,r,0x49714,0x394,2,6,offsets,duration,0x80);}
    {const uint8_t offsets[]={2,0,2,0,2,0,2,0},duration[]={128,12,12,12,6,6,6,6};animation(a,r,0x49794,0x396,2,8,offsets,duration,0x80);}
    {const uint8_t offsets[]={0,2},duration[]={8,8};animation(a,r,0x49814,0x398,2,2,offsets,duration,0x80);}
    {const uint8_t offsets[]={0,2,0,2,0,2,0,2},duration[]={128,8,8,8,8,12,12,12};animation(a,r,0x49894,0x39A,2,8,offsets,duration,0x80);}
    {const uint8_t offsets[]={0,2,4,6,4,2},duration[]={24,10,12,24,12,10};animation(a,r,0x49914,0x39C,2,6,offsets,duration,0x100);}
    tr_stage_animate(a,0,a->tiles);
    n=tr_kosinski(r+0x99D34,12960,a->chunks,sizeof a->chunks);if(!n||n%128)return TR_FAIL;a->chunk_count=(unsigned)n/128;
    n=tr_kosinski(r+0x45AC4,448,layout,sizeof layout);if(n!=0x1000)return TR_FAIL;
    /* S2's fixed 128-byte FG/BG rows become S3's interleaved row pointers. */
    putword(a->layout,128);putword(a->layout+2,128);putword(a->layout+4,16);putword(a->layout+6,16);
    /* Empty trailing source columns are omitted to fit the pointer header. */
    unsigned fgwidth=0,bgwidth=0;
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<128;++x){
        if(layout[y*256+x]&&x+1>fgwidth)fgwidth=x+1;
        if(layout[y*256+128+x]&&x+1>bgwidth)bgwidth=x+1;}
    if(fgwidth<3)fgwidth=3;if(bgwidth<3)bgwidth=3;
    putword(a->layout,fgwidth);putword(a->layout+2,bgwidth);
    unsigned pos=0x88;
    for(unsigned y=0;y<16;++y)for(unsigned plane=0;plane<2;++plane){unsigned w=plane?bgwidth:fgwidth;
        if(pos+w>sizeof a->layout)return TR_FAIL;
        putword(a->layout+8+y*4+plane*2,0x8000+pos);
        memcpy(a->layout+pos,layout+y*256+plane*128,w);pos+=w;}
    for(unsigned y=16;y<32;++y)memcpy(a->layout+8+y*4,a->layout+8+(y%16)*4,4);
    memcpy(a->alternate_layout,a->layout,sizeof a->layout);
    size_t p=tr_kosinski(r+0x44E50,240,primary,sizeof primary),s=tr_kosinski(r+0x44F40,256,secondary,sizeof secondary);
    if(p<a->block_count||s<a->block_count)return TR_FAIL;
    for(unsigned i=0;i<a->block_count;++i){a->collision[i*2]=primary[i];a->collision[0x600+i*2]=secondary[i];}
    memcpy(a->angles,r+0x42D50,256);memcpy(a->heights,r+0x42E50,4096);memcpy(a->widths,r+0x43E50,4096);
    for(unsigned i=0;i<48;++i)a->palette[i]=(uint16_t)word(r+0x2A22+i*2);
    for(unsigned i=0;i<16;++i){a->sprite_palette[i]=(uint16_t)word(r+0x2902+i*2);a->water_palette[i]=(uint16_t)word(r+0x1E7A+i*2);}
    a->start_x=(uint16_t)word(r+0xC1D0);a->start_y=(uint16_t)word(r+0xC1D2);a->max_x=0x29A0;a->max_y=0x320;
    for(unsigned pos=0;pos+2<=550;pos+=4){unsigned x=word(r+0xE4344+pos);if(x&0x8000)break;
        if(pos+4>550)return TR_FAIL;unsigned packed=word(r+0xE4346+pos),y=packed&0xFFF,count=((packed>>12)&7)+1;
        for(unsigned j=0;j<count;++j)if(!ring(a,(int)x+((packed&0x8000)?0:(int)j*24),(int)y+((packed&0x8000)?(int)j*24:0)))return TR_FAIL;}
    return placements(a,r+0xE684A,812,0); /* ObjectLayoutBoundary follows the binary */
}
int tr_stage_decode(unsigned id,const uint8_t *r,size_t size,TrStageAssets *out,char *error,size_t cap)
{
    const char *why="Unsupported imported stage or donor size";
    if(!r||!out)goto fail;
    if(!((id>=0x1000&&id<=0x1002&&size==0x80000)||(id==0x2000&&size==0x100000)))goto fail;
    TrStageAssets *a=calloc(1,sizeof *a);if(!a){why="Cannot allocate imported stage";goto fail;}
    a->id=id;int ok=id==0x2000?decode_s2(r,a):decode_s1(id-0x1000,r,a);
    if(ok)ok=tr_object_assets(r,size,a);
    if(!ok){free(a);why="Donor stage data failed bounded conversion";goto fail;}
    qsort(a->rings,a->ring_count,sizeof *a->rings,sort_rings);*out=*a;free(a);
    if(error&&cap)*error=0;return 1;
fail:if(error&&cap)snprintf(error,cap,"%s",why);return TR_FAIL;
}
