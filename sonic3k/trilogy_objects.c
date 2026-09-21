/* Donor objects use native S3 object slots, collision response and abilities.
 * The donor's five/eight-byte sprite maps are converted by the asset loader. */
#include "trilogy_objects.h"
#include "trilogy_runtime.h"
#include "genesis_runtime.h"
#include "video/genesis_machine.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

enum { HOST_CODE=0x420000, MAP_BASE=0x410000, BRIDGE_MAP_BASE=0x418000,
    COMPOSITE_BASE=0x430000, COMPOSITE_SIZE=1024,
    OBJECT_BASE=0xB0DE, OBJECT_SIZE=0x4A, OBJECT_COUNT=90, NO_ART=255,
    BOSS_OWNER=TR_MAX_OBJECTS+TR_MAX_RINGS-1, BALL_OWNER=BOSS_OWNER-1 };
typedef struct Object {
    unsigned guest,owner,kind,art,map,ticks,timer,phase;
    int x,y,vx,vy,origin_x,origin_y;
    unsigned subtype,flags,parent,flash;
    int angle,angular_speed;
} Object;
static TrStageAssets *assets;
static Object objects[OBJECT_COUNT];
static uint8_t finished[TR_MAX_OBJECTS+TR_MAX_RINGS];
static uint8_t special_finished[TR_MAX_OBJECTS+TR_MAX_RINGS];
static TrStageAssets *special_assets;
static unsigned art_tile[TR_ART_COUNT],bridge_bytes;
static uint8_t bridge_maps[0x4000];
static unsigned bridge_offsets[33];
static uint8_t composite_maps[OBJECT_COUNT][COMPOSITE_SIZE];
static unsigned frame,live,boss_started,boss_defeated,capsule_open,results_started;
static int previous_x[2],previous_y[2];
static unsigned word(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
static unsigned ram(unsigned a){return word(g_ram+(a&65535));}
static unsigned code(unsigned a){return (ram(a)<<16)|ram(a+2);}
static void put(unsigned a,unsigned n){g_ram[a]=(uint8_t)(n>>8);g_ram[a+1]=(uint8_t)n;}
static void putlong(unsigned a,unsigned n){put(a,n>>16);put(a+2,n);}
static void mapword(unsigned a,unsigned n){bridge_maps[a]=(uint8_t)(n>>8);bridge_maps[a+1]=(uint8_t)n;}
static void bytesword(uint8_t *p,unsigned n){p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n;}
static void call(unsigned address,unsigned guest)
{g_cpu.A[0]=0xFF0000|guest;recomp_call_addr(address);}
static void solid(unsigned a,int width,int height,int old_x,int full)
{
    g_cpu.D[1]=(uint16_t)width;g_cpu.D[2]=(uint16_t)height;g_cpu.D[3]=(uint16_t)height;g_cpu.D[4]=(uint16_t)old_x;
    call(full?0x1DC56:0x1E2A6,a);
}
static void sound(unsigned id)
{g_cpu.D[0]=id;recomp_call_addr(0x1380);}
static int sine(unsigned angle)
{g_cpu.D[0]=angle&255;recomp_call_addr(0x1D4C);return (int16_t)g_cpu.D[0];}
static unsigned new_native(unsigned address,int x,int y)
{
    for(unsigned i=0;i<OBJECT_COUNT;++i){unsigned a=OBJECT_BASE+i*OBJECT_SIZE;
        if(code(a))continue;
        memset(objects+i,0,sizeof objects[i]);
        memset(g_ram+a,0,OBJECT_SIZE);putlong(a,address);put(a+0x10,x);put(a+0x14,y);
        if(address==0x1E5E0)g_ram[a+5]=2; /* scenery/boss explosions release no extra animal */
        if(address==0x2C778)g_ram[a+0x38]=1; /* capsule animal: no enemy-score popup */
        return a;}
    return 0;
}
/* A composite is one native sprite mapping, so queued draws never depend on
 * temporarily moving an object slot or on a pointer into another ROM. */
static uint8_t *composite(Object *o)
{
    unsigned i=(o->guest-OBJECT_BASE)/OBJECT_SIZE;uint8_t *p=composite_maps[i];
    bytesword(p,2);bytesword(p+2,0);putlong(o->guest+0xC,COMPOSITE_BASE+i*COMPOSITE_SIZE);
    put(o->guest+0xA,0);g_ram[o->guest+0x22]=0;return p;
}
static void part(uint8_t *out,unsigned art,unsigned frame_id,int dx,int dy)
{
    const TrObjectArt *r=&assets->art[art];if(frame_id>=r->frames)return;
    const uint8_t *p=assets->object_maps+r->mapping;
    p+=word(p+frame_id*2);unsigned count=word(p),used=word(out+2);p+=2;
    for(unsigned n=0;n<count&&4+(used+1)*6<=COMPOSITE_SIZE;++n,p+=6){
        int y=(int8_t)p[0]+dy;if(y < -128 || y>127)continue;
        uint8_t *d=out+4+used++*6;d[0]=(uint8_t)y;d[1]=p[1];
        bytesword(d+2,word(p+2)+art_tile[art]+r->palette);
        bytesword(d+4,(int16_t)word(p+4)+dx);
    }
    bytesword(out+2,used);
}
static void begin_results(void)
{
    if(results_started)return;
    if(!new_native(0x2DAD0,0,0))return;
    results_started=1;g_ram[0xFE1E]=g_ram[0xF711]=0;
    put(0xEE14,ram(0xEE78));put(0xEE16,ram(0xEE78));
    fprintf(stderr,"[Trilogy] Stage %04X results\n",assets->id);
}
void tr_objects_start_results(void){begin_results();}
static int allocate_art(unsigned art)
{
    if(art==NO_ART)return 1;
    unsigned tiles=assets->art[art].tile_bytes/32;if(!tiles)return 1;
    if(art_tile[art])return 1;
    unsigned title=0;
    for(unsigned a=OBJECT_BASE;a<OBJECT_BASE+OBJECT_COUNT*OBJECT_SIZE;a+=OBJECT_SIZE)if(code(a)==0x2D690){title=1;break;}
    const unsigned starts[]={assets->tile_bytes/32,0x500,0x5F8},ends[]={0x43A,title?0x500:0x580,0x680};
    for(unsigned range=0;range<3;++range)for(unsigned at=starts[range];at+tiles<=ends[range];++at){
        int free=1;
        for(unsigned i=0;i<TR_ART_COUNT;++i)if(art_tile[i]&&at<art_tile[i]+assets->art[i].tile_bytes/32&&at+tiles>art_tile[i]){free=0;break;}
        if(free){art_tile[art]=at;return 1;}
    }
    return 0;
}
static unsigned donor_art(unsigned id)
{
    if(id==0x279)return TR_ART_SPECIAL_STARS;
    switch(id){
    case 0x11:return TR_ART_BRIDGE;case 0x18:return TR_ART_PLATFORM;
    case 0x1C:return assets->id==0x2000?TR_ART_STAKE:TR_ART_BRIDGE;
    case 0x22:return TR_ART_BUZZ;case 0x2B:return TR_ART_CHOP;
    case 0x49:return assets->id==0x2000?TR_ART_WATERFALL:NO_ART;
    case 0x4B:return assets->id==0x2000?TR_ART_BUZZ:NO_ART;
    case 0x5C:return TR_ART_CHOP;case 0x9D:return TR_ART_COCO;
    case 0x15:return TR_ART_SWING;case 0x1A:return TR_ART_LEDGE;case 0x3B:return TR_ART_ROCK;
    case 0x3C:return TR_ART_WALL;case 0x17:case 0x217:return TR_ART_POLE;case 0x44:return TR_ART_EDGE;
    case 0x1F:return TR_ART_CRAB;case 0x42:return TR_ART_NEWT;case 0x40:return TR_ART_MOTO;
    case 0x3D:return TR_ART_EGGMAN;case 0x3E:return TR_ART_CAPSULE;
    case 0x148:return TR_ART_BALL;
    case 0xD:return TR_ART_SIGN;
    default:return NO_ART;
    }
}
static unsigned native_code(unsigned id)
{
    switch(id){case 0x26:return 0x1D566;case 0x41:return 0x22D06;
    case 0x36:return 0x23F84;case 0x79:return 0x2CFA8;
    case 0x100:return 0x1A51A;case 6:return HOST_CODE;default:return 0;}
}
static unsigned bridge_map(unsigned length)
{
    if(length<1||length>32||bridge_bytes+4+length*6>sizeof bridge_maps)return MAP_BASE+assets->art[TR_ART_BRIDGE].mapping;
    if(bridge_offsets[length])return BRIDGE_MAP_BASE+bridge_offsets[length]-1;
    unsigned start=bridge_bytes;mapword(start,2);mapword(start+2,length);bridge_bytes+=4;
    bridge_offsets[length]=start+1;
    for(unsigned n=0;n<length;++n){unsigned at=bridge_bytes;
        bridge_maps[at]=0xF8;bridge_maps[at+1]=5;mapword(at+2,0);mapword(at+4,(int)n*16-(int)(length/2)*16-8);bridge_bytes+=6;}
    return BRIDGE_MAP_BASE+start;
}
static int spawn(unsigned owner,unsigned id,unsigned subtype,unsigned flags,unsigned x,unsigned y)
{
    unsigned art=donor_art(id),native=native_code(id);
    if(!native&&art==NO_ART)return 0;
    if(!allocate_art(art))return 0;
    for(unsigned i=0;i<OBJECT_COUNT;++i){unsigned a=OBJECT_BASE+i*OBJECT_SIZE;if(code(a))continue;
        Object *o=&objects[i];memset(o,0,sizeof *o);o->guest=a;o->owner=owner;o->kind=id;o->art=art;
        o->x=(int)x*256;o->y=(int)y*256;o->origin_x=(int)x;o->origin_y=(int)y;o->subtype=subtype;o->flags=flags;
        memset(g_ram+a,0,OBJECT_SIZE);putlong(a,native?native:HOST_CODE);
        put(a+0x10,x);put(a+0x14,y);g_ram[a+4]=(uint8_t)(4|flags);g_ram[a+0x2A]=(uint8_t)flags;g_ram[a+0x2C]=(uint8_t)subtype;
        if(owner<assets->object_count)put(a+0x48,0xEB00+owner);
        if(id==0x26){static const uint8_t s1types[]={0,0,1,4,7,8,3,0,0,0};
            static const uint8_t s2types[]={0,1,1,0,3,4,7,8,0,0};
            g_ram[a+0x2C]=(assets->id==0x2000?s2types:s1types)[subtype<10?subtype:0];}
        if(id==6)g_ram[a+7]=0xD0;
        if(!native){
            o->map=id==0x11?bridge_map(subtype):MAP_BASE+assets->art[art].mapping;
            putlong(a+0xC,o->map);put(a+0xA,art_tile[art]|assets->art[art].palette);
            put(a+8,0x200);g_ram[a+7]=32;g_ram[a+6]=32;
            if(id==0x11){g_ram[a+7]=(uint8_t)(subtype*8);g_ram[a+6]=16;}
            if(id==0x18&&subtype==0xA)g_ram[a+0x22]=1;
            if(assets->id==0x2000&&id==0x18)g_ram[a+0x22]=(uint8_t)(subtype>>4);
            if(id==0x1A)g_ram[a+0x22]=(uint8_t)(subtype&1);
            if(id==0x44)g_ram[a+0x22]=(uint8_t)(subtype&0xF);
            if(id==0x3C)g_ram[a+0x22]=(uint8_t)(subtype%3);
            if(id==0x1C)g_ram[a+0x22]=(uint8_t)(assets->id==0x2000?subtype%3:1);
            if(art==TR_ART_CHOP)o->vy=-0x700;
            if(art==TR_ART_CRAB||art==TR_ART_MOTO)o->vx=(flags&1)?0x100:-0x100;
            if(id==0x3D){g_ram[a+0x28]=0xF;g_ram[a+0x29]=8;g_ram[a+7]=48;g_ram[a+6]=32;}
            if(id==0x148){g_ram[a+0x28]=0x81;o->angle=0x4080;o->angular_speed=-0x200;}
            if(id==0x3E)g_ram[a+0x22]=(uint8_t)(subtype?1:0);
        }
        return 1;
    }
    return 0;
}
void tr_objects_reset(TrStageAssets *a)
{
    assets=a;memset(objects,0,sizeof objects);memset(finished,0,sizeof finished);
    memset(art_tile,0,sizeof art_tile);memset(bridge_offsets,0,sizeof bridge_offsets);
    bridge_bytes=frame=live=boss_started=boss_defeated=capsule_open=results_started=0;
    if(!a)return;
    for(unsigned i=0;i<2;++i){previous_x[i]=(int)ram(0xB010+i*OBJECT_SIZE);previous_y[i]=(int)ram(0xB014+i*OBJECT_SIZE);}
    /* Common objects keep native art addresses and native behavior. */
    static const unsigned art[][2]={{0x1927FE,0x494},{0x19200A,0x5A0},{0x1935A8,0x580},{0x193308,0x592}};
    for(unsigned i=0;i<sizeof art/sizeof *art;++i)
        tr_nemesis(g_rom+art[i][0],0x400000-art[i][0],g_machine.vdp.vram+art[i][1]*32,0x10000-art[i][1]*32);
}
unsigned tr_objects_live(void){return live;}
void tr_objects_checkpoint_stars(unsigned checkpoint)
{
    int x=(int)ram(checkpoint+0x10),y=(int)ram(checkpoint+0x14)-48;
    for(unsigned n=0;n<4;++n){
        unsigned owner=BOSS_OWNER-12-(g_ram[checkpoint+0x2C]&31)*4-n;
        if(spawn(owner,0x279,g_ram[checkpoint+0x2C]&127,0,x,y))
            for(unsigned j=0;j<OBJECT_COUNT;++j)if(objects[j].guest&&objects[j].owner==owner&&objects[j].kind==0x279){
                objects[j].angle=(int)n*64;objects[j].parent=checkpoint;break;}
    }
}
void tr_objects_special_save(void)
{special_assets=assets;memcpy(special_finished,finished,sizeof finished);}
void tr_objects_special_restore(void)
{if(special_assets==assets)memcpy(finished,special_finished,sizeof finished);special_assets=NULL;}
int tr_objects_results_started(void){return results_started!=0;}
static void paths(void)
{
    for(unsigned i=0;i<2;++i){unsigned p=0xB000+i*OBJECT_SIZE;
        int x=(int)ram(p+0x10),y=(int)ram(p+0x14);
        if(g_ram[p+5]!=2)continue;
        if(assets->id<0x2000){
            unsigned cx=(unsigned)x>>8,cy=(unsigned)y>>8;
            unsigned chunk=cx<assets->s1_width&&cy<assets->s1_height?assets->s1_chunks[cy*assets->s1_width+cx]:0;
            int alternate=g_ram[p+0x46]==14;
            if(chunk!=0xB5&&chunk!=0x7F)alternate=0;
            else if((chunk==0x7F&&(g_ram[p+0x2A]&2))||(x&255)<44)alternate=0;
            else if((x&255)>=224)alternate=1;
            else if(!alternate&&g_ram[p+0x26]&&g_ram[p+0x26]<=128)alternate=1;
            else if(alternate&&g_ram[p+0x26]>128)alternate=0;
            g_ram[p+0x46]=(uint8_t)(alternate?14:12);g_ram[p+0x47]=(uint8_t)(alternate?15:13);
            if(alternate)g_ram[p+0xA]&=127;else g_ram[p+0xA]|=128;
            if((chunk==0x1F||chunk==0x20)&&!(g_ram[p+0x2A]&6)){
                g_ram[p+0x2A]|=4;g_ram[p+0x1E]=14;g_ram[p+0x1F]=7;g_ram[p+0x20]=2;put(p+0x14,y+5);
                if(!ram(p+0x1C))put(p+0x1C,0x200); /* donor tunnel entry supplies forward speed */
            }
        }else{
            for(unsigned n=0;n<assets->object_count;++n){const TrPlacement *o=assets->objects+n;if(o->id!=3)continue;
                int vertical=o->subtype&4,now=vertical?y:x,before=vertical?previous_y[i]:previous_x[i];
                int center=vertical?o->y:o->x,side=now>=center;
                if(side==(before>=center))continue;
                int other=(vertical?x:y)-(vertical?o->x:o->y),extent=32<<(o->subtype&3);
                if(other < -extent||other>=extent||((o->subtype&128)&&(g_ram[p+0x2A]&2)))continue;
                if(!(o->flags&1)){
                    int alt=(o->subtype>>(side?3:4))&1;
                    g_ram[p+0x46]=(uint8_t)(alt?14:12);g_ram[p+0x47]=(uint8_t)(alt?15:13);
                }
                if(o->subtype&(1<<(side?5:6)))g_ram[p+0xA]|=128;else g_ram[p+0xA]&=127;
            }
        }
        previous_x[i]=x;previous_y[i]=y;
    }
}
void tr_objects_load(void)
{
    if(!assets)return;
    unsigned used[TR_ART_COUNT]={0};int camera=(int)ram(0xEE78);live=0;++frame;
    paths();
    for(unsigned i=0;i<OBJECT_COUNT;++i)if(code(OBJECT_BASE+i*OBJECT_SIZE)==0x2DAD0)results_started=1;
    for(unsigned i=0;i<OBJECT_COUNT;++i){Object *o=objects+i;if(!o->guest)continue;unsigned a=o->guest;
        if(!code(a)){memset(o,0,sizeof *o);continue;}
        int x=(int)ram(a+0x10);
        if(o->kind==0x100&&g_ram[a+5]>=4)finished[o->owner]=1;
        if(o->art!=NO_ART&&code(a)!=HOST_CODE){finished[o->owner]=1;memset(o,0,sizeof *o);continue;}
        if((x<camera-256||x>camera+640)&&o->kind!=0x3D&&o->kind!=0x148){memset(g_ram+a,0,OBJECT_SIZE);memset(o,0,sizeof *o);continue;}
        if(o->art!=NO_ART)used[o->art]=1;++live;
        if(o->kind==0x148){used[TR_ART_SWING]=used[TR_ART_BOSS_ITEMS]=1;}
    }
    for(unsigned i=0;i<TR_ART_COUNT;++i)if(!used[i])art_tile[i]=0;
    if(results_started)return;
    if(assets->id==0x1002&&!boss_started&&camera>=0x2960){
        if(spawn(BOSS_OWNER,0x3D,0,0,0x2A60,0x280)){
            boss_started=1;put(0xEE14,0x2960);put(0xEE16,0x2960);
            g_cpu.D[0]=0x19;recomp_call_addr(0x1358);
            fprintf(stderr,"[Trilogy] Green Hill boss entered\n");
        }
    }
    for(unsigned n=0;n<assets->object_count+assets->ring_count;++n){
        if(finished[n])continue;
        unsigned x,y,id,subtype=0,flags=0;
        if(n<assets->object_count){const TrPlacement *p=assets->objects+n;x=p->x;y=p->y;id=p->id;subtype=p->subtype;flags=p->flags;}
        else{const TrRing *r=assets->rings+n-assets->object_count;x=r->x;y=r->y;id=0x100;}
        if((int)x<camera-128||(int)x>camera+448)continue;
        int exists=0;for(unsigned i=0;i<OBJECT_COUNT;++i)if(objects[i].guest&&objects[i].owner==n){exists=1;break;}
        if(!exists)spawn(n,id,subtype,flags,x,y);
    }
    for(unsigned i=0;i<TR_ART_COUNT;++i)if(art_tile[i])
        memcpy(g_machine.vdp.vram+art_tile[i]*32,assets->art[i].tiles,assets->art[i].tile_bytes);
}
int tr_objects_read16(uint32_t a,uint16_t *value)
{
    if(!assets)return 0;
    if(a>=MAP_BASE&&a+1<MAP_BASE+assets->map_bytes){*value=(uint16_t)word(assets->object_maps+a-MAP_BASE);return 1;}
    if(a>=BRIDGE_MAP_BASE&&a+1<BRIDGE_MAP_BASE+bridge_bytes){*value=(uint16_t)word(bridge_maps+a-BRIDGE_MAP_BASE);return 1;}
    if(a>=COMPOSITE_BASE&&a+1<COMPOSITE_BASE+sizeof composite_maps){*value=(uint16_t)word((uint8_t *)composite_maps+a-COMPOSITE_BASE);return 1;}
    return 0;
}
static void boss(Object *o)
{
    unsigned a=o->guest;
    if(g_ram[a+0x2A]&128&&o->phase<4){
        o->phase=4;o->timer=180;o->vx=o->vy=0;g_ram[a+0x28]=0;
        g_cpu.D[0]=100;recomp_call_addr(0xDCCA);
        fprintf(stderr,"[Trilogy] Green Hill boss defeated\n");
    }
    if(o->phase<4&&!g_ram[a+0x28]){
        if(!o->flash)o->flash=32;
        if(!--o->flash)g_ram[a+0x28]=0xF;
    }
    switch(o->phase){
    case 0:
        o->y+=0x100;
        if(o->y>=0x33800){o->y=0x33800;o->phase=1;}
        break;
    case 1:
        o->x-=0x100;o->y-=0x40;
        if(o->x<=0x2A0000){
            o->x=0x2A0000;o->phase=2;o->timer=120;
            if(allocate_art(TR_ART_SWING)&&allocate_art(TR_ART_BOSS_ITEMS)&&spawn(BALL_OWNER,0x148,0,0,o->x/256,o->y/256))
                for(unsigned i=0;i<OBJECT_COUNT;++i)if(objects[i].guest&&objects[i].kind==0x148)objects[i].parent=a;
        }
        break;
    case 2:
        if(o->timer)--o->timer;
        else{o->phase=3;o->timer=o->x==0x2A0000?128:64;o->vx=o->timer==128?0x40:0x100;if(!(g_ram[a+0x2A]&1))o->vx=-o->vx;}
        break;
    case 3:
        if(o->timer){--o->timer;o->x+=o->vx;}
        else{o->phase=2;o->timer=64;o->vx=0;g_ram[a+0x2A]^=1;}
        break;
    case 4:
        if(!(o->ticks&7))new_native(0x1E5E0,o->x/256+(int)(o->ticks%41)-20,o->y/256+(int)(o->ticks%31)-12);
        if(o->timer)--o->timer;else{o->phase=5;o->timer=90;g_ram[a+0x2A]=1;boss_defeated=1;put(0xEE16,0x2AC0);}
        break;
    case 5:
        o->x+=0x400;o->y-=0x40;
        if(o->x/256>(int)ram(0xEE78)+400){memset(g_ram+a,0,OBJECT_SIZE);return;}
        break;
    }
    put(a+0x10,o->x/256);put(a+0x14,o->y/256+(o->phase<4?sine(o->ticks*2)/64:0));
    g_ram[a+4]=(uint8_t)(4|(g_ram[a+0x2A]&1));
    uint8_t *map=composite(o);
    if(!(o->flash&1)){
        part(map,TR_ART_EGGMAN,0,0,0);
        part(map,TR_ART_EGGMAN,o->phase>=4?7:o->flash?5:1+(o->ticks/6)%2,0,0);
        if(o->vx||o->phase==5)part(map,TR_ART_EGGMAN,8+(o->ticks/4)%2,0,0);
    }
}
static void boss_ball(Object *o)
{
    unsigned a=o->guest,parent=o->parent;
    if(!parent||code(parent)!=HOST_CODE||(g_ram[parent+0x2A]&128)||boss_defeated){
        putlong(a,0x1E5E0);g_ram[a+5]=0;return;
    }
    if(o->ticks>96){o->angular_speed+=o->phase?-8:8;
        if(o->angular_speed==0x200)o->phase=1;if(o->angular_speed==-0x200)o->phase=0;
        o->angle+=o->angular_speed;}
    int sn=sine((unsigned)o->angle>>8),cs=sine(((unsigned)o->angle>>8)+64);
    int length=o->ticks<96?(int)o->ticks:96,px=(int)ram(parent+0x10),py=(int)ram(parent+0x14)+32;
    int x=px+cs*length/256,y=py+sn*length/256;
    put(a+0x10,x);put(a+0x14,y);g_ram[a+0x28]=0x81;
    uint8_t *map=composite(o);part(map,TR_ART_BALL,(o->ticks/8)&1,0,0);
    for(int d=16;d<=64;d+=16)part(map,TR_ART_SWING,1,px+cs*(d<length?d:length)/256-x,py+sn*(d<length?d:length)/256-y);
    part(map,TR_ART_BOSS_ITEMS,(o->ticks/8)&1,px-x,py-y);
    g_ram[a+6]=112;g_ram[a+7]=112;
}
static void capsule(Object *o,int old_x)
{
    unsigned a=o->guest;
    if(!o->subtype){g_ram[a+0x22]=(uint8_t)(capsule_open?2:0);if(!capsule_open)solid(a,43,24,old_x,1);return;}
    if(!capsule_open){
        solid(a,23,8,old_x,1);
        if(boss_defeated&&(g_ram[a+0x2A]&0x18)){capsule_open=1;o->timer=210;put(a+0x14,o->origin_y+8);g_ram[0xFE1E]=0;}
    }else if(o->timer){
        --o->timer;
        if(o->timer>150&&!(o->timer&7))new_native(0x1E5E0,o->origin_x+(int)(o->timer%41)-20,o->origin_y+32);
        if(o->timer<150&&!(o->timer&7))new_native(0x2C778,o->origin_x+(int)(o->timer%31)-15,o->origin_y+24);
        if(!o->timer)begin_results();
    }
}
static void spiral(Object *o)
{
    if(assets->id!=0x2000)return;
    for(unsigned i=0;i<2;++i){unsigned p=0xB000+i*OBJECT_SIZE,bit=8u<<i;
        int x=(int)ram(p+0x10)-o->origin_x;
        if(g_ram[o->guest+0x2A]&bit){
            if(abs((int16_t)ram(p+0x1C))<0x600||(g_ram[p+0x2A]&2)||x < -208||x>=208){
                g_ram[p+0x2A]&=(uint8_t)~8;g_ram[o->guest+0x2A]&=(uint8_t)~bit;
                g_ram[p+0x30]=0;g_ram[p+0x31]=4;
            }else if(g_ram[p+0x2A]&8){
                put(p+0x14,o->origin_y+(int8_t)assets->spiral_y[x+208]-(g_ram[p+0x1E]-19));
                g_ram[p+0x27]=assets->spiral_flip[(x+208)/8];
            }
        }else if(g_ram[p+5]==2&&!(g_ram[p+0x2A]&2)&&!g_ram[p+0x2E]){
            int on_object=g_ram[p+0x2A]&8,left=(int16_t)ram(p+0x18)>=0;
            int low=on_object?176:192,high=low+16,dist=left?-x:x;
            int y=(int)ram(p+0x14)-o->origin_y-16;
            if(dist>=low&&dist<=high&&y>=0&&y<48){g_cpu.A[1]=0xFF0000|p;g_cpu.D[6]=3+i;call(0x1E490,o->guest);}
        }
    }
}
static void projectile(Object *parent,unsigned art,unsigned first,unsigned frames,
                       int dx,int dy,int vx,int vy,unsigned gravity,unsigned delay)
{
    if(!allocate_art(art))return;
    unsigned a=new_native(HOST_CODE,parent->x/256+dx,parent->y/256+dy);if(!a)return;
    Object *o=objects+(a-OBJECT_BASE)/OBJECT_SIZE;
    o->guest=a;o->owner=BOSS_OWNER-2;o->kind=0x200;o->art=art;
    o->x=parent->x+dx*256;o->y=parent->y+dy*256;o->vx=vx;o->vy=vy;
    o->phase=gravity;o->timer=delay;o->angle=(int)first;o->subtype=frames;
    putlong(a+0xC,MAP_BASE+assets->art[art].mapping);put(a+0xA,art_tile[art]|assets->art[art].palette);
    put(a+8,0x180);g_ram[a+4]=(uint8_t)(4|(vx>0));g_ram[a+6]=g_ram[a+7]=16;
}
static void buzz(Object *o)
{
    unsigned a=o->guest;int direction=(o->flags&1)?1:-1;
    if(o->kind==0x22){
        if(o->timer)--o->timer;
        if(!o->phase){
            if(!o->timer){o->phase=1;o->timer=128;o->vx=direction*0x400;}
        }else if(o->phase==1){
            o->x+=o->vx;
            if(!o->timer){o->flags^=1;o->phase=0;o->timer=60;o->flash=0;}
            else if(!o->flash&&abs((int)ram(0xB010)-o->x/256)<96&&(g_ram[a+4]&128)){o->phase=2;o->timer=30;}
        }else if(o->phase==2&&!o->timer){
            projectile(o,TR_ART_MISSILE,2,2,direction*24,28,direction*0x200,0x200,0,30);
            o->phase=0;o->timer=60;o->flash=1;
        }
        g_ram[a+0x22]=(uint8_t)((o->phase==1?2:o->flash?4:0)+((o->ticks/2)&1));
        g_ram[a+0x28]=8;
    }else{
        if(o->ticks==1){o->timer=256;o->vx=direction*0x100;}
        if(o->phase==2){
            if(o->timer)--o->timer;
            if(o->timer==20)projectile(o,TR_ART_BUZZ,5,2,-direction*13,24,direction*0x180,0x180,0,0);
            if(!o->timer){o->phase=0;o->timer=256;}
        }else if(o->phase==1){
            if(o->timer)--o->timer;
            if(o->timer==15){o->vx=-o->vx;o->flags^=1;o->flash=0;}
            if(!o->timer){o->phase=0;o->timer=256;}
        }else{
            o->x+=o->vx;if(o->timer)--o->timer;
            int dx=(int)ram(0xB010)-o->x/256;
            if(!o->flash&&abs(dx)>=40&&abs(dx)<=48&&((dx>0)==(o->vx>0))){o->phase=2;o->timer=50;o->flash=1;}
            else if(!o->timer){o->phase=1;o->timer=30;}
        }
        uint8_t *map=composite(o);part(map,TR_ART_BUZZ,o->phase==2?1:0,0,0);
        if(o->phase==0)part(map,TR_ART_BUZZ,3+(o->ticks/3)%2,0,0);
        g_ram[a+0x28]=0xA;
    }
    put(a+0x10,o->x/256);g_ram[a+4]=(uint8_t)(4|(o->flags&1));
}
static int newtron(Object *o)
{
    unsigned a=o->guest;
    if(!o->phase){
        if(abs((int)ram(0xB010)-o->x/256)>=128)return 0;
        o->flags=(ram(0xB010)>(unsigned)(o->x/256));o->phase=1;o->timer=o->subtype?140:80;
        if(o->subtype)put(a+0xA,art_tile[o->art]|0x2000);
    }
    int direction=(o->flags&1)?1:-1;
    if(o->phase==1){
        if(o->timer)--o->timer;
        if(o->subtype){
            static const uint8_t frames[]={0,1,1,2,1,1,0};
            g_ram[a+0x22]=frames[(139-o->timer)/20];
            if(o->timer==79)projectile(o,TR_ART_MISSILE,2,2,direction*20,-8,direction*0x200,0,0,0);
            if(!o->timer){finished[o->owner]=1;memset(g_ram+a,0,OBJECT_SIZE);return 0;}
        }else{
            static const uint8_t frames[]={0,1,3,4};g_ram[a+0x22]=frames[(79-o->timer)/20];
            if(!o->timer){o->phase=2;o->vy=0;}
        }
    }else if(o->phase==2){
        o->y+=o->vy;o->vy+=0x38;put(a+0x14,o->y/256);g_ram[a+0x1E]=16;
        call(0xF938,a);int dy=(int16_t)g_cpu.D[1];
        if(dy<0){o->y+=dy*256;o->phase=3;o->vx=direction*0x200;}
        g_ram[a+0x22]=5;
    }else{
        o->x+=o->vx;put(a+0x10,o->x/256);
        if(o->phase==3){call(0xF938,a);int dy=(int16_t)g_cpu.D[1];if(dy>=-8&&dy<=12)o->y+=dy*256;else o->phase=4;}
        g_ram[a+0x22]=(uint8_t)((o->phase==3?6:8)+(o->ticks/3)%2);
    }
    put(a+0x14,o->y/256);g_ram[a+4]=(uint8_t)(4|(o->flags&1));
    g_ram[a+0x28]=(uint8_t)(o->phase>=2?0xC:0);return 1;
}
static void coco(Object *o)
{
    static const int movement[]={-1,1,-1,1,-1,1};
    static const unsigned duration[]={0x20,0x18,0x10,0x28,0x20,0x10};
    unsigned a=o->guest;if(o->ticks==1)o->timer=16;
    if(o->flash)--o->flash;
    o->flags=ram(0xB010)>(unsigned)(o->x/256);
    if(o->timer)--o->timer;
    if(o->phase==0){
        if(!o->flash&&abs((int)ram(0xB010)-o->x/256)<96){o->phase=2;o->timer=8;o->flash=32;}
        else if(!o->timer){o->phase=1;o->timer=duration[o->angle%6];o->vy=movement[o->angle++%6]*0x100;}
    }else if(o->phase==1){o->y+=o->vy;if(!o->timer){o->phase=0;o->timer=16;}}
    else if(o->phase==2&&!o->timer){
        int direction=o->flags?1:-1;projectile(o,TR_ART_COCO,3,1,-direction*11,-13,direction*0x100,-0x100,0x18,0);
        o->phase=3;o->timer=8;
    }else if(o->phase==3&&!o->timer){o->phase=1;o->timer=duration[o->angle%6];o->vy=movement[o->angle++%6]*0x100;}
    put(a+0x14,o->y/256);g_ram[a+4]=(uint8_t)(4|(o->flags&1));
    g_ram[a+0x22]=(uint8_t)(o->phase>=2?o->phase-1:(o->ticks/6)&1);g_ram[a+0x28]=9;
}
static void helix(Object *o)
{
    unsigned a=o->guest,index=o->subtype;
    if(o->kind==0x17){
        unsigned length=o->subtype;if(length<1)length=1;if(length>16)length=16;
        index=length/2;
        for(unsigned n=0;n<length;++n){
            if(n==index||(o->phase&(1u<<n)))continue;
            int x=o->origin_x+((int)n-(int)index)*16;
            if(spawn(BOSS_OWNER-3,0x217,n,0,(unsigned)x,(unsigned)o->origin_y)){
                o->phase|=1u<<n;
                for(unsigned i=0;i<OBJECT_COUNT;++i)if(objects[i].guest&&objects[i].kind==0x217&&!objects[i].parent&&objects[i].origin_x==x){objects[i].parent=a;break;}
            }
        }
    }else if(!o->parent||code(o->parent)!=HOST_CODE||objects[(o->parent-OBJECT_BASE)/OBJECT_SIZE].kind!=0x17){
        memset(g_ram+a,0,OBJECT_SIZE);return;
    }
    g_ram[a+0x22]=(uint8_t)((frame/8+index)&7);g_ram[a+0x28]=(uint8_t)(g_ram[a+0x22]?0:0x84);
    g_ram[a+7]=8;g_ram[a+6]=16;
}
static void crab(Object *o)
{
    unsigned a=o->guest;g_ram[a+0x1E]=16;
    if(!o->phase){
        o->y+=o->vy;o->vy+=0x38;put(a+0x14,o->y/256);call(0xF938,a);int dy=(int16_t)g_cpu.D[1];
        if(dy<0){o->y+=dy*256;o->vy=0;o->phase=1;}
    }else if(o->phase==1){
        if(o->timer)--o->timer;
        else if((o->flash^=1)!=0){o->phase=2;o->timer=128;o->flags^=1;o->vx=(o->flags&1)?0x80:-0x80;}
        else{
            projectile(o,TR_ART_CRAB,5,2,-16,0,-0x100,-0x400,0x38,0);
            projectile(o,TR_ART_CRAB,5,2,16,0,0x100,-0x400,0x38,0);o->phase=3;o->timer=60;
        }
    }else if(o->phase==2){
        int old=o->x;o->x+=o->vx;put(a+0x10,o->x/256);call(0xF938,a);int dy=(int16_t)g_cpu.D[1];
        if(dy>=-8&&dy<12)o->y+=dy*256;else{o->x=old;o->timer=1;}
        if(o->timer&&!--o->timer){o->phase=1;o->timer=60;}
    }else if(o->timer&&!--o->timer)o->phase=1;
    put(a+0x10,o->x/256);put(a+0x14,o->y/256);
    g_ram[a+4]=(uint8_t)(4|(o->flags&1));g_ram[a+0x28]=6;
    g_ram[a+0x22]=(uint8_t)(o->phase==3?4:o->phase==2&&o->ticks%48<32?1:0);
}
static int moto(Object *o)
{
    unsigned a=o->guest;int ready=o->phase!=0;g_ram[a+0x1E]=14;g_ram[a+0x1F]=8;
    /* S1 Moto_Main drops to the floor before starting its walking cycle.
     * At an edge it pauses for 60 ticks, then turns; it never flips each frame. */
    if(!o->phase){
        o->y+=o->vy;o->vy+=0x38;put(a+0x14,o->y/256);
        call(0xF938,a);int dy=(int16_t)g_cpu.D[1];
        if(dy<0){o->y+=dy*256;o->vy=0;o->phase=1;o->timer=0;o->flags^=1;}
    }else if(o->phase==1){
        if(o->timer)--o->timer;
        else{o->phase=2;o->flags^=1;o->vx=(o->flags&1)?0x100:-0x100;}
    }else{
        o->x+=o->vx;put(a+0x10,o->x/256);call(0xF938,a);int dy=(int16_t)g_cpu.D[1];
        if(dy>=-8&&dy<12)o->y+=dy*256;
        else{o->phase=1;o->timer=59;o->vx=0;}
    }
    put(a+0x10,o->x/256);put(a+0x14,o->y/256);
    g_ram[a+4]=(uint8_t)(4|(o->flags&1));g_ram[a+0x2A]=(uint8_t)((g_ram[a+0x2A]&~1u)|(o->flags&1));
    g_ram[a+0x22]=(uint8_t)(o->phase==2?(o->ticks/8)%3:0);g_ram[a+0x28]=0xC;
    /* Moto_Main does not display or collide until it has found a floor.
     * The original C50/34C placement is inside the cliff and stays hidden. */
    return ready;
}
static void checkpoint_star(Object *o)
{
    unsigned a=o->guest;
    if(g_ram[a+0x29]&1){tr_runtime_checkpoint_special(a,o->subtype);return;}
    g_ram[a+0x29]=0;
    /* Obj79_Star from S2: 128-tick expansion, 256-tick active circle,
     * 128-tick contraction; four stars orbit the checkpoint's upper pole. */
    o->angle+=10;int sn=sine((unsigned)o->angle&255)>>5,cs=sine(((unsigned)o->angle+64)&255)>>3;
    int segment=(o->angle&0x3E0)>>5,delta=0,add=cs;
    if(segment>16)add=-add;segment&=15;if(segment>8)segment=(-segment)&7;
    for(unsigned i=0;i<3;++i){segment>>=1;if(segment)delta+=add;add*=2;}
    sn+=delta>>4;int scale=(int)o->ticks;
    if(scale==128)g_ram[a+0x28]=0xD8;
    if(scale>384)scale=512-scale;
    if(scale<0){memset(g_ram+a,0,OBJECT_SIZE);return;}
    if(scale<128){sn=sn*scale>>7;cs=cs*scale>>7;}
    put(a+0x10,o->origin_x+cs);put(a+0x14,o->origin_y+sn);
    unsigned f=(o->ticks&6)>>1;g_ram[a+0x22]=(uint8_t)(f==3?1:f);
    g_ram[a+7]=g_ram[a+6]=8;
}
int tr_objects_dispatch(uint32_t address)
{
    if(address!=HOST_CODE||!assets)return 0;
    unsigned a=g_cpu.A[0]&65535;if(a<OBJECT_BASE||a>=OBJECT_BASE+OBJECT_COUNT*OBJECT_SIZE||(a-OBJECT_BASE)%OBJECT_SIZE)return 0;
    Object *o=&objects[(a-OBJECT_BASE)/OBJECT_SIZE];if(o->guest!=a)return 0;
    M68KState saved=g_cpu;int old_x=(int)ram(a+0x10);++o->ticks;
    switch(o->kind){
    case 0x279:checkpoint_star(o);break;
    case 0x200:
        if(o->timer){--o->timer;g_ram[a+0x22]=(uint8_t)((o->ticks/8)&1);}
        else{o->x+=o->vx;o->y+=o->vy;o->vy+=(int)o->phase;put(a+0x10,o->x/256);put(a+0x14,o->y/256);
            g_ram[a+0x22]=(uint8_t)(o->angle+(o->ticks/2)%o->subtype);g_ram[a+0x28]=0x87;}
        if(o->y/256>(int)ram(0xEE1A)+256||o->ticks>600)memset(g_ram+a,0,OBJECT_SIZE);
        break;
    case 6:spiral(o);g_cpu=saved;return 1;
    case 0x3D:boss(o);break;
    case 0x148:boss_ball(o);break;
    case 0x3E:capsule(o,old_x);break;
    case 0xD:
        if(ram(0xB010)>=(unsigned)o->origin_x&&g_ram[0xB005]<6){
            putlong(a,0x837B2);g_ram[a+5]=0;g_ram[a+0x28]=0;
            put(0xEE14,ram(0xEE78));put(0xEE16,ram(0xEE78));g_ram[0xFE1E]=0;
            call(0x837B2,a);g_cpu=saved;return 1;
        }
        break;
    case 0x18:{
        int wave=(int)g_ram[0xFE88]-64,type=o->subtype&15;
        if(type==1||type==5)o->x=(o->origin_x+(type==5?-wave:wave))*256;
        if(type==2||type==6||type==0xA)o->y=(o->origin_y+(type==6?-wave:type==0xA?wave/2:wave))*256;
        if(type==3&&(g_ram[a+0x2A]&0x18)&&!o->timer)o->timer=30;
        if(o->timer&&!--o->timer)o->phase=1;
        if(o->phase){o->y+=o->vy;o->vy+=0x38;}
        put(a+0x10,o->x/256);put(a+0x14,o->y/256);solid(a,32,12,old_x,0);break;}
    case 0x11:solid(a,(int)o->subtype*8,8,old_x,0);break;
    case 0x3B:solid(a,27,16,old_x,1);break;
    case 0x15:{
        int angle=0x40+sine(frame/2)/8;if(o->flags&1)angle=0x80-angle;
        int sn=sine(angle),cs=sine(angle+64),length=(o->subtype&15)*16;
        o->x=(o->origin_x+cs*length/256)*256;o->y=(o->origin_y+sn*length/256)*256;
        put(a+0x10,o->x/256);put(a+0x14,o->y/256);solid(a,24,8,old_x,0);
        uint8_t *map=composite(o);part(map,TR_ART_SWING,0,0,0);
        for(int d=0;d<length;d+=16)part(map,TR_ART_SWING,d?1:2,cs*(d-length)/256,sn*(d-length)/256);
        g_ram[a+6]=112;g_ram[a+7]=96;break;}
    case 0x1A:
        if((g_ram[a+0x2A]&0x18)&&!o->timer&&!o->phase)o->timer=30;
        if(o->timer&&!--o->timer)o->phase=1;
        if(o->phase){o->y+=o->vy;o->vy+=0x38;put(a+0x14,o->y/256);}else solid(a,48,16,old_x,0);break;
    case 0x3C:{
        int broken=0;
        for(unsigned p=0xB000;p<=0xB04A;p+=0x4A){
            int speed=(int16_t)ram(p+0x18),dx=(int)ram(p+0x10)-old_x,dy=(int)ram(p+0x14)-(int)ram(a+0x14);
            int knuckles=g_ram[p+0x38]==2;
            if(g_ram[p+5]<6&&abs(dx)<36&&abs(dy)<48&&(knuckles||((g_ram[p+0x2A]&4)&&abs(speed)>=0x480))){broken=1;break;}
        }
        if(broken){finished[o->owner]=1;putlong(a,0x1E5E0);g_ram[a+5]=0;g_ram[a+0x28]=0;}
        else solid(a,27,32,old_x,1);break;}
    case 0x17:case 0x217:helix(o);break;
    case 0x1F:crab(o);break;
    case 0x40:if(!moto(o)){g_cpu=saved;return 1;}break;
    case 0x2B:case 0x5C:
        o->y+=o->vy;o->vy+=0x18;
        if(o->y>=o->origin_y*256){o->y=o->origin_y*256;o->vy=o->kind==0x5C?-0x400:-0x700;}
        put(a+0x14,o->y/256);g_ram[a+0x22]=(uint8_t)((frame/8)&1);g_ram[a+0x28]=9;break;
    case 0x22:case 0x4B:buzz(o);break;
    case 0x42:if(!newtron(o)){g_cpu=saved;return 1;}break;
    case 0x9D:coco(o);break;
    case 0x49:g_ram[a+0x22]=(uint8_t)((frame/8)%assets->art[o->art].frames);break;
    default:break;
    }
    if(!code(a)||code(a)!=HOST_CODE){g_cpu=saved;return 1;}
    if(o->art!=NO_ART&&g_ram[a+0x22]>=assets->art[o->art].frames)g_ram[a+0x22]=0;
    if(g_ram[a+0x28])call(0x1040C,a);
    call(0x1ABC6,a);g_cpu=saved;return 1;
}
void tr_objects_state(TrStateIO *io,TrStageAssets *const imports[4])
{
    unsigned ids[2]={assets?assets->id:0,special_assets?special_assets->id:0};
    if(io->mode==1&&io->data&&io->pos+sizeof ids<=io->size)memcpy(ids,io->data+io->pos,sizeof ids);
    TR_STATE(io,ids);
    for(unsigned i=0;i<2;++i){TrStageAssets *found=NULL;
        for(unsigned j=0;j<4;++j)if(imports[j]&&imports[j]->id==ids[i])found=imports[j];
        if(ids[i]&&!found)io->ok=0;
        if(io->mode==2){if(i)special_assets=found;else assets=found;}}
    TR_STATE(io,objects);TR_STATE(io,finished);TR_STATE(io,special_finished);
    TR_STATE(io,art_tile);TR_STATE(io,bridge_bytes);TR_STATE(io,bridge_maps);TR_STATE(io,bridge_offsets);
    TR_STATE(io,composite_maps);TR_STATE(io,frame);TR_STATE(io,live);TR_STATE(io,boss_started);
    TR_STATE(io,boss_defeated);TR_STATE(io,capsule_open);TR_STATE(io,results_started);
    TR_STATE(io,previous_x);TR_STATE(io,previous_y);
}
