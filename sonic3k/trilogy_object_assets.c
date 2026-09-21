#include "trilogy_assets.h"
#include <string.h>

static unsigned word(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
static void put(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
/* Verified donor offsets; mappings become S3's count-word/six-byte pieces.
 * Art stays decoded in host memory and is assigned VRAM only while in use. */
typedef struct Resource { unsigned kind,map,art,palette; } Resource;
static const Resource s1[]={
    {TR_ART_PLATFORM,0x813C,0,0x4000},{TR_ART_BRIDGE,0x7934,0x2F582,0x4000},
    {TR_ART_ROCK,0xD10E,0x2FC10,0x6000},{TR_ART_SWING,0x7C48,0x2F468,0x4000},
    {TR_ART_LEDGE,0x85A0,0,0x4000},{TR_ART_WALL,0xD2B6,0x2FD3E,0x4000},
    {TR_ART_POLE,0x7DF8,0x2FAE4,0x4000},{TR_ART_EDGE,0xE250,0x2FDDC,0x4000},
    {TR_ART_CRAB,0x9750,0x35A06,0},{TR_ART_BUZZ,0x9A36,0x35EF4,0},
    {TR_ART_CHOP,0xABCA,0x36B6C,0},{TR_ART_NEWT,0xDF42,0x3780C,0},
    {TR_ART_MOTO,0xF7A0,0x37582,0},{TR_ART_EGGMAN,0x17D0C,0x5CC62,0},
    {TR_ART_BOSS_ITEMS,0x17DD4,0x5D4C6,0},{TR_ART_BALL,0x81AA,0x2F6B6,0x4000},
    {TR_ART_CAPSULE,0x1AD74,0x5D7B0,0},{TR_ART_SIGN,0xED38,0x3A53E,0},
    {TR_ART_MISSILE,0x9B06,0x35EF4,0x2000}
};
static const Resource s2[]={
    {TR_ART_PLATFORM,0x107F6,0,0x4000},{TR_ART_BRIDGE,0xFC70,0xF052A,0x4000},
    {TR_ART_STAKE,0x11552,0xF16EC,0x4000},{TR_ART_WATERFALL,0x20C50,0xF02D6,0x2000},
    {TR_ART_BUZZ,0x2D2EA,0x8316A,0},{TR_ART_CHOP,0x2D442,0x839EA,0},
    {TR_ART_COCO,0x37D96,0x8A87A,0},{TR_ART_SIGN,0x195BE,0x79BDE,0},
    {TR_ART_SPECIAL_STARS,0x1F4A0,0x79A86,0}
};
int tr_object_assets(const uint8_t *r,size_t size,TrStageAssets *a)
{
    int sonic1=a->id<0x2000;const Resource *list=sonic1?s1:s2;
    unsigned count=(unsigned)(sonic1?sizeof s1/sizeof *s1:sizeof s2/sizeof *s2);
    for(unsigned i=0;i<count;++i){const Resource *source=list+i;TrObjectArt *art=&a->art[source->kind];
        if(source->map+2>=size)return 0;
        unsigned frames=word(r+source->map)/2,start=a->map_bytes,pos=start+frames*2;
        if(!frames||frames>128||pos>=sizeof a->object_maps)return 0;
        art->mapping=start;art->frames=frames;art->palette=(uint16_t)source->palette;
        for(unsigned frame=0;frame<frames;++frame){
            unsigned input=source->map+word(r+source->map+frame*2);
            if(input+2>=size)return 0;
            unsigned pieces=sonic1?r[input++]:word(r+input);if(!sonic1)input+=2;
            unsigned stride=sonic1?5:8;
            if(pieces>128||input+pieces*stride>size||pos+2+pieces*6>sizeof a->object_maps)return 0;
            put(a->object_maps+start+frame*2,pos-start);put(a->object_maps+pos,pieces);pos+=2;
            for(unsigned piece=0;piece<pieces;++piece,input+=stride,pos+=6){
                memcpy(a->object_maps+pos,r+input,4);
                put(a->object_maps+pos+4,sonic1?(unsigned)(int16_t)(int8_t)r[input+4]:word(r+input+6));
            }
        }
        a->map_bytes=pos;
        if(source->art){if(source->art>=size)return 0;
            art->tile_bytes=(unsigned)tr_nemesis(r+source->art,size-source->art,art->tiles,sizeof art->tiles);
            if(!art->tile_bytes)return 0;
        }
    }
    return 1;
}
