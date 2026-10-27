#include "sonic_cd_scene.h"

#include <limits.h>
#include <string.h>

/* Source-derived Main CPU layout from Level/_Definitions.inc and the R11A
 * listing: objDrawQueue=$FFAC00, objects=$FFD000..$FFF000, sprites=$FFF800,
 * spriteCount=$FFF62C. Object records are $40 bytes. */
enum {
    MAIN_RAM_REQUIRED = 0x10000,
    QUEUE_OFFSET = 0xAC00,
    OBJECTS_BEGIN = 0xD000,
    OBJECTS_END = 0xF000,
    SPRITE_COUNT_OFFSET = 0xF62C,
    CAMERA_FG_X = 0xF700,
    CAMERA_FG_Y = 0xF704,
    CAMERA_BG_X = 0xF708,
    CAMERA_BG_Y = 0xF70C,
    CAMERA_BG3_X = 0xF718,
    CAMERA_BG3_Y = 0xF71C,
    OBJECT_ID = 0x00,
    OBJECT_SPR_FLAGS = 0x01,
    OBJECT_TILE = 0x02,
    OBJECT_MAP = 0x04,
    OBJECT_X = 0x08,
    OBJECT_SCREEN_Y = 0x0A,
    OBJECT_Y = 0x0C,
    OBJECT_PRIORITY = 0x18,
    OBJECT_MAP_FRAME = 0x1A,
    RENDER_CAMERA_MASK = 0x0C,
    RENDER_STATIC_MAP = 0x20,
    RENDER_FLIP_X = 0x01,
    RENDER_FLIP_Y = 0x02,
    HUD_SLOT_FIRST = 0xD080,
    HUD_SLOT_LAST = 0xD7C0
};

typedef struct {
    const SegaCDSceneSnapshot *snapshot;
    SegaCDScene *scene;
    size_t count;
    size_t sat_count;
    int emit;
} WalkContext;

static uint16_t be16(const uint8_t *p)
{ return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int16_t signed16(uint16_t v)
{ return (int16_t)v; }

static int32_t signed8(uint8_t v)
{ return (v & 0x80u) ? (int32_t)v - 256 : (int32_t)v; }

static int main_span(const SegaCDSceneSnapshot *s,uint32_t address,
                     size_t length,size_t *offset)
{
    uint32_t physical=address & 0xFFFFFFu;
    uint32_t base=s->main_ram_base & 0xFFFFFFu;
    uint64_t end=(uint64_t)base+s->main_ram_size;
    if(physical<base||(uint64_t)physical+length>end)return 0;
    *offset=(size_t)(physical-base);
    return 1;
}

static int word_span(const SegaCDSceneSnapshot *s,uint32_t address,
                     size_t length,size_t *offset)
{
    uint32_t physical=address & 0xFFFFFFu;
    uint32_t base=s->word_ram_base & 0xFFFFFFu;
    uint64_t end=(uint64_t)base+s->word_ram_size;
    if(physical<base||(uint64_t)physical+length>end)return 0;
    *offset=(size_t)(physical-base);
    return 1;
}

static int read_main(const SegaCDSceneSnapshot *s,uint32_t address,
                     size_t length,const uint8_t **p)
{
    size_t off;
    if(!main_span(s,address,length,&off))return 0;
    *p=s->main_ram+off;
    return 1;
}

static int read_guest(const SegaCDSceneSnapshot *s,uint32_t address,
                      size_t length,const uint8_t **p)
{
    size_t off;
    if(word_span(s,address,length,&off)) {
        *p=s->word_ram+off;
        return 1;
    }
    if(main_span(s,address,length,&off)) {
        *p=s->main_ram+off;
        return 1;
    }
    return 0;
}

static uint16_t main_word(const SegaCDSceneSnapshot *s,uint16_t offset)
{
    const uint8_t *p;
    if(!read_main(s,s->main_ram_base+offset,2,&p))return 0;
    return be16(p);
}

static SegaCDSceneSlotKind classify_slot(uint16_t object)
{
    switch(object) {
    case 0xD000:return SEGACD_SCENE_SLOT_PLAYER;
    case 0xD040:return SEGACD_SCENE_SLOT_PLAYER2;
    case 0xD080:return SEGACD_SCENE_SLOT_HUD_SCORE;
    case 0xD0C0:return SEGACD_SCENE_SLOT_HUD_LIVES;
    case 0xD100:return SEGACD_SCENE_SLOT_TITLE_CARD;
    case 0xD140:return SEGACD_SCENE_SLOT_HUD_RINGS;
    case 0xD7C0:return SEGACD_SCENE_SLOT_HUD_ICON;
    default:
        if(object<0xD800)return SEGACD_SCENE_SLOT_RESERVED;
        return SEGACD_SCENE_SLOT_DYNAMIC;
    }
}

static SegaCDSceneStatus mapping_for_object(const SegaCDSceneSnapshot *s,
                                             const uint8_t *obj,
                                             uint32_t *mapping_address,
                                             uint8_t *piece_count,
                                             const uint8_t **piece_data)
{
    uint32_t map=be32(obj+OBJECT_MAP)&0xFFFFFFu;
    uint8_t flags=obj[OBJECT_SPR_FLAGS];
    const uint8_t *p;
    if(!map)return SEGACD_SCENE_UNRESOLVED_MAPPING;
    if(flags&RENDER_STATIC_MAP) {
        *piece_count=1;
        *mapping_address=map;
        if(!read_guest(s,map,5,&p))return SEGACD_SCENE_UNRESOLVED_MAPPING;
        *piece_data=p;
        return SEGACD_SCENE_OK;
    } else {
        uint32_t frame_table=map+(uint32_t)obj[OBJECT_MAP_FRAME]*2u;
        int16_t relative;
        if(!read_guest(s,frame_table,2,&p))
            return SEGACD_SCENE_UNRESOLVED_MAPPING;
        relative=signed16(be16(p));
        if((int64_t)map+relative<0||((int64_t)map+relative)>0xFFFFFF)
            return SEGACD_SCENE_MALFORMED_MAPPING;
        map=(uint32_t)((int64_t)map+relative);
    }
    if(!read_guest(s,map,1,&p))return SEGACD_SCENE_UNRESOLVED_MAPPING;
    *piece_count=(p[0]>=1u&&p[0]<=128u)?p[0]:0u;
    *mapping_address=map;
    if(!read_guest(s,map+1u,(size_t)*piece_count*5u,&p))
        return SEGACD_SCENE_UNRESOLVED_MAPPING;
    *piece_data=p;
    return SEGACD_SCENE_OK;
}

static void object_screen_origin(const SegaCDSceneSnapshot *s,
                                 const uint8_t *obj,uint8_t flags,
                                 uint16_t *origin_x,uint16_t *origin_y,
                                 uint8_t *camera_mode)
{
    uint16_t x=be16(obj+OBJECT_X),y;
    uint8_t mode=(uint8_t)((flags&RENDER_CAMERA_MASK)>>2);
    uint16_t camera_x=0,camera_y=0;
    if(mode==0) {
        y=be16(obj+OBJECT_SCREEN_Y);
        *origin_x=x;*origin_y=y;*camera_mode=0;
        return;
    }
    switch(mode) {
    case 1:camera_x=main_word(s,CAMERA_FG_X);camera_y=main_word(s,CAMERA_FG_Y);break;
    case 2:camera_x=main_word(s,CAMERA_BG_X);camera_y=main_word(s,CAMERA_BG_Y);break;
    default:camera_x=main_word(s,CAMERA_BG3_X);camera_y=main_word(s,CAMERA_BG3_Y);break;
    }
    y=be16(obj+OBJECT_Y);
    if(camera_y<0x0100u) {
        if(y>=0x0800u)y=(uint16_t)(y-0x0800u);
    } else if(camera_y>=0x0700u&&y<0x0100u) {
        y=(uint16_t)(y+0x0800u);
    }
    *origin_x=(uint16_t)(x-camera_x+128u);
    *origin_y=(uint16_t)(y-camera_y+128u);
    *camera_mode=mode;
}

static SegaCDSceneStatus emit_piece(WalkContext *w,uint8_t priority,
                                    uint16_t object_offset,const uint8_t *obj,
                                    uint32_t map_address,uint8_t piece_index,
                                    const uint8_t *raw_piece)
{
    uint8_t flags=obj[OBJECT_SPR_FLAGS],size=raw_piece[1];
    uint8_t camera_mode;
    uint16_t origin_x,origin_y,base_tile=be16(obj+OBJECT_TILE);
    int32_t xoff=signed8(raw_piece[4]),yoff=signed8(raw_piece[0]);
    int32_t x,y;
    uint16_t tile=(uint16_t)(base_tile+be16(raw_piece+2));
    int hflip=(flags&RENDER_FLIP_X)!=0,vflip=(flags&RENDER_FLIP_Y)!=0;
    SegaCDScenePiece out;
    object_screen_origin(w->snapshot,obj,flags,&origin_x,&origin_y,&camera_mode);
    if(hflip) {
        int32_t width=8+(int32_t)((size<<1)&0x18u);
        x=(int16_t)(uint16_t)(0u-(uint16_t)xoff-(uint16_t)width+origin_x);
        tile^=0x0800u;
    } else {
        x=(int16_t)(uint16_t)((uint16_t)xoff+origin_x);
    }
    if(vflip) {
        int32_t height=8+(int32_t)((size<<3)&0x18u);
        y=(int16_t)(uint16_t)(0u-(uint16_t)yoff-(uint16_t)height+origin_y);
        tile^=0x1000u;
    } else {
        y=(int16_t)(uint16_t)((uint16_t)yoff+origin_y);
    }
    if(w->count>=SEGACD_SCENE_MAX_PIECES)return SEGACD_SCENE_MALFORMED_MAPPING;
    if(w->emit) {
        memset(&out,0,sizeof out);
        out.native_sat_x_unwrapped=(int16_t)x;
        out.native_sat_y=(int16_t)y;
        out.world_relative=(uint8_t)(camera_mode!=0);
        out.screen_relative=(uint8_t)(camera_mode==0);
        out.x=(int16_t)(x-128);
        out.y=(int16_t)(y-128);
        out.sat_x=(uint16_t)((uint16_t)x&0x01FFu);
        if(out.sat_x==0)out.sat_x=1;
        out.sat_y=(uint16_t)y;
        out.tile_attribute=tile;
        out.object_ram_offset=object_offset;
        out.map_address=map_address;
        out.size=size;
        out.priority=priority;
        out.object_id=obj[OBJECT_ID];
        out.render_flags=flags;
        out.piece_index=piece_index;
        out.camera_mode=camera_mode;
        out.horizontal_flip=(uint8_t)hflip;
        out.vertical_flip=(uint8_t)vflip;
        out.slot_kind=(uint8_t)classify_slot(object_offset);
        out.hud_slot=(uint8_t)(out.slot_kind==SEGACD_SCENE_SLOT_HUD_SCORE||
                               out.slot_kind==SEGACD_SCENE_SLOT_HUD_LIVES||
                               out.slot_kind==SEGACD_SCENE_SLOT_HUD_RINGS||
                               out.slot_kind==SEGACD_SCENE_SLOT_HUD_ICON);
        if(w->sat_count<SEGACD_SCENE_SAT_ENTRIES) {
            size_t sat=(size_t)w->sat_count*8u;
            out.native_sat_emitted=1;
            out.sat_index=(uint8_t)w->sat_count;
            out.link=(uint8_t)(w->sat_count+1u);
            w->scene->native_sat[sat]=(uint8_t)(out.sat_y>>8);
            w->scene->native_sat[sat+1]=(uint8_t)out.sat_y;
            w->scene->native_sat[sat+2]=size;
            w->scene->native_sat[sat+3]=out.link;
            w->scene->native_sat[sat+4]=(uint8_t)(tile>>8);
            w->scene->native_sat[sat+5]=(uint8_t)tile;
            w->scene->native_sat[sat+6]=(uint8_t)(out.sat_x>>8);
            w->scene->native_sat[sat+7]=(uint8_t)out.sat_x;
            ++w->sat_count;
        } else {
            out.native_sat_emitted=0;
            out.sat_index=0xFF;
            out.link=0;
        }
        w->scene->pieces[w->count]=out;
    }
    ++w->count;
    return SEGACD_SCENE_OK;
}

static SegaCDSceneStatus walk_scene(WalkContext *w)
{
    const SegaCDSceneSnapshot *s=w->snapshot;
    const uint8_t *queue;
    if(!read_main(s,s->main_ram_base+QUEUE_OFFSET,
                  SEGACD_SCENE_QUEUE_BYTES,&queue))
        return SEGACD_SCENE_INVALID_SNAPSHOT;
    for(unsigned level=0;level<SEGACD_SCENE_QUEUE_LEVELS;++level) {
        const uint8_t *q=queue+level*SEGACD_SCENE_QUEUE_LEVEL_BYTES;
        uint16_t byte_count=be16(q);
        if(byte_count>0x7Eu||(byte_count&1u))
            return SEGACD_SCENE_MALFORMED_QUEUE;
        if(w->emit)w->scene->queue_counts[level]=byte_count;
        for(uint16_t at=2;at<2u+byte_count;at=(uint16_t)(at+2u)) {
            uint16_t object_offset=be16(q+at);
            const uint8_t *obj;
            uint8_t piece_count,camera_mode;
            uint32_t map_address;
            const uint8_t *pieces;
            SegaCDSceneStatus status;
            uint16_t origin_x,origin_y;
            if(object_offset<OBJECTS_BEGIN||
               (uint32_t)object_offset+SEGACD_SCENE_OBJECT_SIZE>OBJECTS_END||
               (object_offset&0x3Fu))return SEGACD_SCENE_INVALID_OBJECT_POINTER;
            obj=s->main_ram+(object_offset);
            if(!obj[OBJECT_ID])continue;
            if(w->count>=SEGACD_SCENE_MAX_PIECES)
                return SEGACD_SCENE_MALFORMED_MAPPING;
            if(w->emit)++w->scene->queued_object_count;
            object_screen_origin(s,obj,obj[OBJECT_SPR_FLAGS],&origin_x,&origin_y,
                                 &camera_mode);
            (void)origin_x;(void)origin_y;(void)camera_mode;
            status=mapping_for_object(s,obj,&map_address,&piece_count,&pieces);
            if(status!=SEGACD_SCENE_OK)return status;
            if((size_t)piece_count>SEGACD_SCENE_MAX_PIECES-w->count)
                return SEGACD_SCENE_MALFORMED_MAPPING;
            for(unsigned i=0;i<piece_count;++i) {
                status=emit_piece(w,(uint8_t)level,object_offset,obj,map_address,
                                  (uint8_t)i,pieces+i*5u);
                if(status!=SEGACD_SCENE_OK)return status;
            }
        }
    }
    return SEGACD_SCENE_OK;
}

static int valid_snapshot(const SegaCDSceneSnapshot *s)
{
    uint64_t word_end,main_end;
    if(!s||!s->word_ram||!s->main_ram||s->word_ram_size!=0x40000u||
       s->main_ram_size!=MAIN_RAM_REQUIRED)return 0;
    word_end=(uint64_t)s->word_ram_base+s->word_ram_size;
    main_end=(uint64_t)s->main_ram_base+s->main_ram_size;
    return !(s->word_ram_base&1u)&&word_end<=0x1000000u&&
           (s->main_ram_base&0xFFFFu)==0&&main_end<=0x1000000u&&
           (s->main_ram_base&0xFFFFFFu)==0xFF0000u;
}

SegaCDSceneStatus segacd_scene_capture(const SegaCDSceneSnapshot *snapshot,
                                      SegaCDScene *scene)
{
    WalkContext measure,emit;
    SegaCDSceneStatus status;
    if(!scene)return SEGACD_SCENE_INVALID_ARGUMENT;
    scene->piece_count=0;
    scene->required_piece_capacity=0;
    scene->queued_object_count=0;
    scene->native_count=0;
    scene->status=SEGACD_SCENE_INVALID_ARGUMENT;
    memset(scene->queue_counts,0,sizeof scene->queue_counts);
    memset(scene->native_sat,0,sizeof scene->native_sat);
    if(!valid_snapshot(snapshot)) {
        scene->status=SEGACD_SCENE_INVALID_SNAPSHOT;
        return scene->status;
    }
    memset(&measure,0,sizeof measure);
    measure.snapshot=snapshot;
    status=walk_scene(&measure);
    if(status!=SEGACD_SCENE_OK) {scene->status=status;return status;}
    scene->required_piece_capacity=measure.count;
    if(measure.count>scene->piece_capacity||
       (measure.count&&!scene->pieces)) {
        scene->status=SEGACD_SCENE_CAPACITY;
        return scene->status;
    }
    memset(&emit,0,sizeof emit);
    emit.snapshot=snapshot;
    emit.scene=scene;
    emit.emit=1;
    status=walk_scene(&emit);
    if(status!=SEGACD_SCENE_OK) {scene->status=status;return status;}
    scene->piece_count=emit.count;
    scene->native_count=(uint8_t)emit.sat_count;
    if(emit.sat_count<SEGACD_SCENE_SAT_ENTRIES) {
        size_t terminal=(size_t)emit.sat_count*8u;
        memset(scene->native_sat+terminal,0,4);
    } else {
        scene->native_sat[79u*8u+3u]=0;
    }
    scene->status=SEGACD_SCENE_OK;
    return SEGACD_SCENE_OK;
}

void segacd_scene_init(SegaCDScene *scene,SegaCDScenePiece *pieces,
                       size_t piece_capacity)
{
    if(!scene)return;
    memset(scene,0,sizeof *scene);
    scene->pieces=pieces;
    scene->piece_capacity=piece_capacity;
    scene->status=SEGACD_SCENE_INVALID_ARGUMENT;
}

void segacd_scene_dispose(SegaCDScene *scene)
{
    if(scene)memset(scene,0,sizeof *scene);
}

const char *segacd_scene_status_name(SegaCDSceneStatus status)
{
    switch(status) {
    case SEGACD_SCENE_OK:return "ok";
    case SEGACD_SCENE_INVALID_ARGUMENT:return "invalid-argument";
    case SEGACD_SCENE_INVALID_SNAPSHOT:return "invalid-snapshot";
    case SEGACD_SCENE_MALFORMED_QUEUE:return "malformed-queue";
    case SEGACD_SCENE_INVALID_OBJECT_POINTER:return "invalid-object-pointer";
    case SEGACD_SCENE_UNRESOLVED_MAPPING:return "unresolved-mapping";
    case SEGACD_SCENE_MALFORMED_MAPPING:return "malformed-mapping";
    case SEGACD_SCENE_CAPACITY:return "capacity";
    default:return "unknown-status";
    }
}
