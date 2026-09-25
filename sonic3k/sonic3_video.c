/* Opt-in Sonic 3 family world renderer (combined S3&K first).
 * Stock S&K ROM: Render_Sprites $1AD20, Load_Sprites $1B690.
 * Native collision, decompression and tile streaming remain untouched.
 * Host scene publication follows the actual native SAT upload.
 */
#include "sonic3_video.h"
#include "genesis_runtime.h"
#include "video/genesis_vdp.h"
#include "video/genesis_dac.h"
#include "cmd_server.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The standalone S3 cartridge has the same object/layout formats, with a
 * different engine address map. S&K and the combined cartridge share code. */
#ifdef SONIC3_STANDALONE
enum { S3_COMPETITION=0xFFD8, S3_PLACEMENTS=0x5E0D8, S3_RINGS=0x5E198,
       S3_HUD_MAP=0xE932, S3_RING_MAP=0xF87E,
       S3_LOAD_INIT=0x19CF2, S3_LOAD_UPDATE=0x19DD0,
       S3_CAPTURE=0x1938E, S3_PUBLISH=0x194B8,
       S3_RING_RANGE=0xF6D8, S3_RING_STATUS=0xF6E2,
       S3_ART_BANK=0, S3_PAL_INTRO=0x8C314, S3_PAL_FOREST=0x8C374 };
#else
enum { S3_COMPETITION=0xFFE8, S3_PLACEMENTS=0x1E3D98, S3_RINGS=0x1E3E58,
       S3_HUD_MAP=0xDBB6, S3_RING_MAP=0xEBEE,
       S3_LOAD_INIT=0x1B690, S3_LOAD_UPDATE=0x1B7F2,
       S3_CAPTURE=0x1AD20, S3_PUBLISH=0x1AE56,
       S3_RING_RANGE=0xEA32, S3_RING_STATUS=0xEA3C,
       S3_ART_BANK=0x200000, S3_PAL_INTRO=0xA8B1C, S3_PAL_FOREST=0xA8B7C };
#endif

enum { VIDEO_OFF, VIDEO_FIT, VIDEO_RATIO, VIDEO_STAGE };
static int s_mode;
static double s_ratio = 16.0 / 9.0;
static int s_width, s_left, s_camera, s_stage_width, s_native_x, s_requested_width = 320;
static int s_camera_y;
static unsigned s_frames, s_fallbacks, s_terrain_checks, s_terrain_errors;
static unsigned s_bg_checks, s_bg_errors, s_bg_unstreamed;
static unsigned s_scene_misses, s_scene_holds;
static unsigned s_uninitialized_scroll_lines;
static unsigned s_terrain_unstreamed;
static int s_bad_x,s_bad_y;
static unsigned s_bad_attr,s_bad_expected;
static int s_bad_world_y,s_bad_hscroll;
static int s_bg_bad_x,s_bg_bad_y,s_bg_bad_a,s_bg_bad_expected;
static GVDP s_video_frame;
static uint8_t s_world_frame[0xA800];
static uint8_t s_background_frame[0xA800];
static int s_frame_level,s_frame_special,s_frame_fg_x,s_frame_fg_y,s_frame_bg_y;
static unsigned s_frame_zone,s_frame_act,s_frame_bg_event;
static void scene_mode_changed(int was_enabled);
static void ss_capture(void);

/* AIZ reuses its intro's VRAM and block definitions for the forest, and
 * temporarily installs Knuckles' palette during the emerald cutscene.
 * An expanded view sees both banks simultaneously. Decode their original
 * ROM assets into host-only storage; never preload them into guest memory. */
typedef struct { uint8_t blocks[0x1800]; GVDP video; uint16_t colors[48]; } AizArt;
static AizArt s_aiz_intro,s_aiz_main;
static int s_aiz_art_ready,s_aiz_art_attempted;
typedef struct { const uint8_t *p,*end; unsigned desc,bits; int error; } KosReader;
static unsigned kos_byte(KosReader *r)
{
    if(r->p==r->end){r->error=1;return 0;}
    return *r->p++;
}
static unsigned kos_bit(KosReader *r)
{
    unsigned bit=r->desc&1u;r->desc>>=1;
    if(!--r->bits) {
        unsigned low=kos_byte(r);r->desc=low|(kos_byte(r)<<8);r->bits=16;
    }
    return bit;
}
static size_t art_kos(const uint8_t *src,size_t size,uint8_t *out,size_t capacity,size_t *used)
{
    KosReader r={src,src+size,0,16,0};size_t n=0;
    unsigned low=kos_byte(&r);r.desc=low|(kos_byte(&r)<<8);
    while(!r.error) {
        if(kos_bit(&r)) {
            unsigned value=kos_byte(&r);
            if(r.error || n==capacity)return 0;
            out[n++]=(uint8_t)value;continue;
        }
        unsigned count;int offset;
        if(kos_bit(&r)) {
            unsigned lo=kos_byte(&r),hi=kos_byte(&r);
            offset=(int)(((hi&248u)<<5)|lo)-8192;count=hi&7u;
            if(!count) {
                count=kos_byte(&r);
                if(r.error)return 0;
                if(!count){*used=(size_t)(r.p-src);return n;}
                if(count==1)continue;
                ++count;
            } else count+=2;
        } else {
            count=kos_bit(&r)<<1;count=(count|kos_bit(&r))+2;
            offset=(int)kos_byte(&r)-256;
        }
        if(r.error || (size_t)-offset>n || count>capacity-n)return 0;
        while(count--){out[n]=out[n-(size_t)-offset];++n;}
    }
    return 0;
}
static size_t art_kosm(unsigned address,size_t compressed,uint8_t *out,size_t capacity)
{
    if(compressed<2 || address>0x400000u-compressed)return 0;
    const uint8_t *src=g_rom+address;
    size_t total=(size_t)((src[0]<<8)|src[1]),pos=2,n=0;
    if(total==0xA000)total=0x8000;
    if(total>capacity)return 0;
    while(n<total && pos<compressed) {
        size_t used=0,expected=total-n>4096?4096:total-n;
        size_t got=art_kos(src+pos,compressed-pos,out+n,expected,&used);
        if(got!=expected)return 0;
        n+=got;pos+=used;pos=2+((pos-2+15)&~(size_t)15);
    }
    return n==total?n:0;
}
static void load_aiz_art(void)
{
    if(s_aiz_art_attempted)return;
    s_aiz_art_attempted=1;
    size_t used=0;
    if(art_kos(g_rom+S3_ART_BANK+0x1A374A,0x200,s_aiz_intro.blocks,0x268,&used)!=0x268 ||
       art_kos(g_rom+S3_ART_BANK+0x1A394A,0xC40,s_aiz_intro.blocks+0x268,0x1058,&used)!=0x1058 ||
       art_kosm(S3_ART_BANK+0x1A566A,0xE12,s_aiz_intro.video.vram,0x17C0)!=0x17C0 ||
       art_kosm(S3_ART_BANK+0x1A647C,0x2FD2,s_aiz_intro.video.vram+0x17C0,0x6260)!=0x6260)return;
    memcpy(s_aiz_main.blocks,s_aiz_intro.blocks,0x268);
    memcpy(s_aiz_main.video.vram,s_aiz_intro.video.vram,0x17C0);
    if(art_kos(g_rom+S3_ART_BANK+0x1A458A,0x10E0,s_aiz_main.blocks+0x268,0x14F0,&used)!=0x14F0 ||
       art_kosm(S3_ART_BANK+0x1A944E,0x27F2,s_aiz_main.video.vram+0x17C0,0x4920)!=0x4920)return;
    for(unsigned i=0;i<48;++i) {
        s_aiz_intro.colors[i]=(uint16_t)((g_rom[S3_PAL_INTRO+i*2]<<8)|g_rom[S3_PAL_INTRO+i*2+1]);
        s_aiz_main.colors[i]=(uint16_t)((g_rom[S3_PAL_FOREST+i*2]<<8)|g_rom[S3_PAL_FOREST+i*2+1]);
    }
    s_aiz_art_ready=1;
}

static uint16_t ram16(unsigned a)
{
    a &= 0xFFFFu;
    return (uint16_t)((g_ram[a] << 8) | g_ram[(a + 1) & 0xFFFFu]);
}
static uint16_t vram16(const GVDP *v, unsigned a)
{
    return (uint16_t)((v->vram[a & 0xFFFFu] << 8) | v->vram[(a + 1) & 0xFFFFu]);
}
static int gameplay(void)
{
    int mode=g_ram[0xF600]&127;
    return (mode==8 || mode==12) && (ram16(0xB000)||ram16(0xB002)) && !ram16(S3_COMPETITION);
}
static int stage_width(void)
{
    int w=ram16(0x8000)*128;
    return w>=320 && w<=32768?w:320;
}
static int level_ready(void)
{
    unsigned w=ram16(0x8000),row=ram16(0x8008);
    return gameplay() && !(g_ram[0xF600]&128) && w>=3 && w<=256 && row>=0x8088 && row<0x9000;
}
static int configure(const char *mode)
{
    if (!mode) return 0;
    int was_enabled = s_mode != VIDEO_OFF;
    if (!strcmp(mode, "off")) s_mode = VIDEO_OFF;
    else if (!strcmp(mode, "fit") || !strcmp(mode, "adaptive")) s_mode = VIDEO_FIT;
    else if (!strcmp(mode, "stage")) s_mode = VIDEO_STAGE;
    else {
        char *end;
        double w = strtod(mode, &end);
        if (end == mode || *end != ':') return 0;
        const char *den = end + 1;
        double h = strtod(den, &end);
        if (end == den || *end || !(w > 0) || !(h > 0) ||
            !(w / h > 0) || w / h > (double)INT_MAX / 480.0) return 0;
        s_ratio = w / h; s_mode = VIDEO_RATIO;
    }
    scene_mode_changed(was_enabled);
    return 1;
}
static int enabled(void) { return s_mode != VIDEO_OFF; }
unsigned s3_video_main_cpu_divisor(void)
{
    /* LevelLoop ($650C) still waits for the real V-int every tick.
     * Expanded activation must not inherit the 7.67 MHz object's lag budget.
     * Loading, special stages, native 2P and opt-out keep original timing. */
    int mode=g_ram[0xF600];
    return enabled() && (mode==8 || mode==12) && gameplay() && g_ram[0xF711] ? 4 : 1;
}
static int width(int dw, int dh, int nw, int nh)
{
    if (!enabled()) return 0;
    double result = nw;
    if (s_mode == VIDEO_STAGE) {
        if (gameplay()) s_stage_width = stage_width();
        result = s_stage_width > nw ? s_stage_width : nw;
    }
    else if (s_mode == VIDEO_RATIO) result = nh * s_ratio;
    else if (dw > 0 && dh > 0) result = (double)nh * dw / dh;
    /* Bound by representable dimensions and the SDL texture limit (runner),
     * never by an aspect preset. Even a stage-length viewport is supported. */
    if (result > INT_MAX - 1.0) result = INT_MAX - 1.0;
    int pixels = (int)(result + 0.5);
    s_requested_width = pixels < nw ? nw : pixels;
    return s_requested_width;
}

/* Full 16-bit scroll is unwrapped against the live camera's 10-bit VDP
 * position. This avoids the one-frame camera/streaming seam seen in SMB. */
static int unwrap(int value, int reference)
{
    return reference + ((value - reference + 512) & 1023) - 512;
}
static int native_terrain_streamed(int wy, int copied_camera_y)
{
    /* Native foreground streaming uses 16px rows at copied-camera offsets -16 and
     * +224. Live VScroll can run ahead of that copy during a fast fall.
     * Outside this precise coverage, the native name table is not a terrain
     * oracle: the enhanced renderer must still draw the real stage block. */
    int top = (copied_camera_y & ~15) - 16;
    return wy >= top && wy < top + 256;
}
static uint16_t world_attr_blocks(const uint8_t *ram,const uint8_t *blocks,int wx,int wy,int background)
{
    unsigned header=background?0x8002u:0x8000u;
    int width=((ram[header]<<8)|ram[header+1])*128;
    if(width<=0 || wx<0 || wx>=width)return 0;
    unsigned row=((unsigned)wy>>5)&0x7Cu;
    unsigned p=0x8008u+row+(background?2u:0u);
    unsigned rowaddr=(ram[p]<<8)|ram[p+1];
    unsigned cell=rowaddr+((unsigned)wx>>7);
    if(cell<0x8088 || cell>=0x9000)return 0;
    unsigned address=ram[cell]*128u+((unsigned)wy&112u)+((unsigned)wx&112u)/8u;
    uint16_t block=(uint16_t)((ram[address]<<8)|ram[address+1]);
    int tx=(wx>>3)&1,ty=(wy>>3)&1;
    if(block&0x400)tx^=1;
    if(block&0x800)ty^=1;
    address=0x9000u+(block&1023u)*8u+(unsigned)(ty*2+tx)*2u;
    if(address+1>=0xA800)return 0;
    address-=0x9000;
    return (uint16_t)(((blocks[address]<<8)|blocks[address+1])^((block&0xC00u)<<1));
}
static uint16_t world_attr(const uint8_t *ram,int wx,int wy,int background)
{ return world_attr_blocks(ram,ram+0x9000,wx,wy,background); }
static uint8_t pattern_pixel(const GVDP *v, uint16_t attr, int x, int y)
{
    int fx = x & 7, fy = y & 7;
    if (attr & 0x0800) fx ^= 7;
    if (attr & 0x1000) fy ^= 7;
    unsigned a = (attr & 2047u) * 32u + (unsigned)fy * 4u + (unsigned)fx / 2u;
    int byte = v->vram[a & 0xFFFFu];
    int nibble = fx & 1 ? byte & 15 : byte >> 4;
    return nibble ? (uint8_t)(((attr >> 9) & 48) | nibble) : 0;
}
static int plane_tiles(int bits) { return bits == 1 ? 64 : bits == 3 ? 128 : 32; }
static uint16_t plane_attr(const GVDP *v, unsigned base, int x, int y)
{
    int wt = plane_tiles(v->reg[16] & 3), ht = plane_tiles((v->reg[16] >> 4) & 3);
    int col = (x >> 3) & (wt - 1), row = (y >> 3) & (ht - 1);
    return vram16(v, base + (unsigned)(row * wt + col) * 2u);
}

/* Enhanced scene data lives on the host: no SAT coordinate wrap, 80-piece
 * ceiling, or 20-sprites-per-line ceiling. Native BuildSprites still executes
 * for the hardware pass and publishes a signature used to align our list
 * with the exact VBlank upload displayed by the VDP. */
typedef struct { int x, y; uint16_t attr; uint8_t size, hud; } SceneSprite;
enum { SCENE_SPRITES = 32768, SCENE_PLACEMENTS = 1024 };
typedef struct {
    SceneSprite sprites[SCENE_SPRITES];
    unsigned count, serial;
    uint8_t sat[640];
    int scene; /* 1 level, 2 special, 0 menu */
    int camera_x, camera_y; /* coordinates used when capturing world sprites */
} SceneFrame;
static SceneFrame s_build, s_history[3], s_display_frame;
static const SceneFrame *s_display;
static unsigned s_serial, s_scene_tick, s_spawned, s_pool_pressure;
static unsigned s_tick_samples,s_tick_updates,s_tick_lag,s_tick_multi;
static unsigned s_publication_lag;
void s3_video_vblank(void)
{
    if(enabled())ss_capture();
    static int was_active;
    static unsigned last_tick,last_zone,last_serial;
    unsigned tick=ram16(0xFE04),zone=ram16(0xFE10);
    int active=enabled() && g_ram[0xF600]==12 && gameplay() &&
        g_ram[0xF711] && g_ram[0xB005]<6 && !ram16(0xF63A);
    /* Sample at the same IRQ entry, not end-of-wall-frame: native variable
     * V-int DMA debt can shift the next tick across that later sample point. */
    if(active && was_active && zone==last_zone) {
        unsigned delta=(uint16_t)(tick-last_tick);
        ++s_tick_samples;s_tick_updates+=delta;
        s_tick_lag+=delta==0;s_tick_multi+=delta>1;
        s_publication_lag+=s_serial==last_serial;
    }
    was_active=active;last_tick=tick;last_zone=zone;last_serial=s_serial;
}
static unsigned s_visible_objects[144], s_visible_count;
typedef struct { unsigned address; uint16_t x, y; uint8_t id, subtype, loaded; uint16_t state; } Placement;
static Placement s_placements[SCENE_PLACEMENTS];
static unsigned s_placement_count, s_placement_base;
static int s_loader_active;

static uint8_t scene_read8(unsigned a)
{
    a &= 0xFFFFFFu;
    return a < 0x400000u ? g_rom[a] : a >= 0xFF0000u ? g_ram[a & 65535u] : 0;
}
static uint16_t scene_read16(unsigned a) { return (uint16_t)((scene_read8(a)<<8)|scene_read8(a+1)); }
static uint32_t scene_read32(unsigned a) { return ((uint32_t)scene_read16(a)<<16)|scene_read16(a+2); }
static void write8(unsigned a, unsigned v) { glue_poke8(0xFF0000u|(a&65535u),(uint8_t)v); }
static void write16(unsigned a, unsigned v) { glue_poke16(0xFF0000u|(a&65535u),(uint16_t)v); }
static void write32(unsigned a, unsigned v) { glue_poke32(0xFF0000u|(a&65535u),v); }
static int view_left(int camera, int w)
{
    int end = stage_width()-w, left = camera-(w-320)/2;
    if (end<0) return end/2;
    if (left<0) left=0;
    return left>end ? end : left;
}
static void activation_bounds(int camera,int w,int *lo,int *hi)
{
    int left=view_left(camera,w);
    /* out_of_range tests 128px-rounded positions. OPL must only instantiate
     * cells that this same culler retains. Loading at left+w+320 with an
     * unrounded camera let objects spawn one cell early, get deleted, and
     * remain marked loaded until they left the window (missing platforms). */
    *lo=(left-128)&~127;
    *hi=((left+w+192)&~127)+128;
}

static void scene_mode_changed(int was_enabled)
{
    if(was_enabled!=enabled()) {
        s_display=NULL;s_display_frame.serial=0;
        for(unsigned n=0;n<3;++n)s_history[n].serial=0;
    }
    /* Once adopted, the host loader also maintains a native-width window
     * on opt-out until the next level init, preserving existing object state. */
}
static void add_mapping(unsigned map,unsigned frame,unsigned gfx,unsigned flags,
                        int x,int y,int anchor,int static_mapping)
{
    unsigned p=map;int count=1;
    if(!static_mapping) {
        p+=(int16_t)scene_read16(p+(frame&255u)*2u);
        count=scene_read16(p);p+=2;
    }
    if(count>1024)return;
    for(int n=0;n<count && s_build.count<SCENE_SPRITES;++n,p+=6) {
        int dy=(int8_t)scene_read8(p),size=scene_read8(p+1)&15;
        int dx=(int16_t)scene_read16(p+4);
        uint16_t attr=(uint16_t)(scene_read16(p+2)+gfx);
        if(flags&1){dx=-dx-(((size>>2)&3)+1)*8;attr^=0x800;}
        if(flags&2){dy=-dy-((size&3)+1)*8;attr^=0x1000;}
        if(y+dy>=224 || y+dy+((size&3)+1)*8<=0)continue;
        SceneSprite *q=&s_build.sprites[s_build.count++];
        q->x=x+dx;q->y=y+dy;q->attr=attr;q->size=(uint8_t)size;q->hud=(uint8_t)anchor;
    }
}

static void load_placements(void)
{
    unsigned zone=ram16(0xFE10),index=(zone>>8)*8u+(zone&1u)*4u;
    unsigned base=scene_read32(S3_PLACEMENTS+index);
    if(base==s_placement_base && s_placement_count)return;
    s_placement_base=base;s_placement_count=0;
    if(base>=0x400000u)return;
    for(unsigned p=base;p+6<=0x400000u && s_placement_count<768;p+=6) {
        unsigned x=scene_read16(p);if(x==65535u)break;
        Placement *q=&s_placements[s_placement_count];
        q->address=p;q->x=(uint16_t)x;q->y=scene_read16(p+2);
        q->id=scene_read8(p+4);q->subtype=scene_read8(p+5);
        q->state=(uint16_t)(0xEB00+s_placement_count++);q->loaded=0;
    }
}
static void spawn_scene(void)
{
    load_placements();s_loader_active=1;
    int camera=ram16(0xEE78),cy=ram16(0xEE7C),w=enabled()?s_requested_width:320;
    int lo,hi;activation_bounds(camera,w,&lo,&hi);
    int player=ram16(0xB010);s_pool_pressure=0;
    int ylo=(cy&~127)-128,yhi=(cy&~127)+512;
    for(unsigned i=0;i<s_placement_count;++i) {
        Placement *p=&s_placements[i];
        if((int)p->x<lo || (int)p->x>=hi)p->loaded=0;
    }
    for(unsigned pass=0;pass<s_placement_count;++pass) {
        int best=-1,distance=INT_MAX;
        for(unsigned i=0;i<s_placement_count;++i) {
            Placement *p=&s_placements[i];
            if(p->loaded || (g_ram[p->state]&128) || (int)p->x<lo || (int)p->x>=hi)continue;
            int y=p->y&4095;
            if(!(p->y&0x8000)) {
                if((int16_t)ram16(0xEE18)<0) {
                    int mask=ram16(0xEEAA),dy=(y-ylo)&mask;
                    if(dy>640)continue;
                } else if(y<ylo || y>yhi)continue;
            }
            int d=abs((int)p->x-player);
            if(d<distance){best=(int)i;distance=d;}
        }
        if(best<0)break;
        Placement *p=&s_placements[best];unsigned o;
        for(o=0xB0DE;o<0xCAE2 && scene_read32(0xFF0000u+o);o+=0x4A){}
        if(o>=0xCAE2){++s_pool_pressure;break;}
        write16(o+0x10,p->x);write16(o+0x14,p->y&4095);
        unsigned flip=(p->y>>13)&3;
        write8(o+4,flip);write8(o+0x2A,flip);write16(o+0x48,p->state);
        write8(o+0x2C,p->subtype);write8(p->state,g_ram[p->state]|128);
#ifdef SONIC3_STANDALONE
        write32(o,scene_read32(0x5CC96u+p->id*4u));
#else
        write32(o,scene_read32(scene_read32(0xFFEF5Au)+p->id*4u));
#endif
        p->loaded=1;++s_spawned;
    }
    unsigned r=0,l=0;
    while(r<s_placement_count && s_placements[r].x<(unsigned)((camera&~127)+640))++r;
    while(l<s_placement_count && (int)s_placements[l].x<((camera-128)&~127))++l;
    write32(0xF772,s_placement_base+r*6);write32(0xF776,s_placement_base+l*6);
    write16(0xF77A,0xEB00+r);write16(0xF77C,0xEB00+l);
    write16(0xF76E,camera&0xFF80);write16(0xF770,cy&0xFF80);
    write16(0xF7DA,(camera-128)&0xFF80);write16(0xEF64,(cy-128)&0xFF80);
}
static unsigned rings_base(void)
{
    unsigned index=(g_ram[0xFE10]*8u)+((g_ram[0xFE11]&1u)*4u);
    /* Ring layouts begin with an x=0 sentinel, native status index zero. */
    return scene_read32(S3_RINGS+index);
}
static unsigned rings_end(void)
{
    unsigned p=rings_base();
    if(p>=0x400000u)return p;
    for(unsigned n=0;n<512 && scene_read16(p)!=65535u;++n)p+=4;
    return p;
}
static void capture_objects(void)
{
    s_build.count=0;s_visible_count=0;++s_scene_tick;
    s_build.scene=level_ready()?1:0;
    int cam=ram16(0xEE80),cy=ram16(0xEE84);
    s_build.camera_x=cam;s_build.camera_y=cy;
    int left=gameplay()?view_left(cam,s_requested_width):cam-(s_requested_width-320)/2;
    int mask=ram16(0xEEAA);
    if(gameplay() && g_ram[0xF711]) {
        unsigned blink=(g_ram[0xFE05]&8)==0;
        unsigned frame=blink*((ram16(0xFE20)==0?1:0)+(g_ram[0xFE23]==9?2:0));
        int hudx=(int8_t)g_ram[0xF711];if(hudx<0)hudx+=8;
        add_mapping(S3_HUD_MAP,frame,0x86CA,0,15+hudx,136,1,0);
        unsigned base=rings_base(),end=rings_end();
        for(unsigned p=base+4;p<end;p+=4) {
            unsigned status=0xE700+(p-base)/2;
            if(ram16(status)&0x8000)continue;
            int x=scene_read16(p),y=((scene_read16(p+2)-cy+8)&mask)-8;
            if(x<left-16 || x>=left+s_requested_width+16 || y<-16 || y>=240)continue;
            unsigned fr=g_ram[status+1]?g_ram[status+1]:g_ram[0xFEB3];
            unsigned m=S3_RING_MAP+fr*8;
            if(s_build.count<SCENE_SPRITES) {
                SceneSprite *q=&s_build.sprites[s_build.count++];
                q->x=x-cam+(int16_t)scene_read16(m+6);q->y=y+(int16_t)scene_read16(m);
                q->attr=scene_read16(m+4);q->size=(uint8_t)scene_read16(m+2);q->hud=0;
            }
        }
    }
    for(unsigned pri=0;pri<8;++pri) {
        unsigned queue=0xAC00+pri*128,bytes=ram16(queue);if(bytes>126)bytes=126;
        for(unsigned k=2;k<=bytes;k+=2) {
            unsigned o=ram16(queue+k);if(o<0xB000 || o>=0xCFE0 || !scene_read32(0xFF0000u+o))continue;
            unsigned flags=g_ram[o+4],multi=flags&64;
            int x,y,anchor=0;
            if(flags&4) {
                x=(int16_t)(ram16(o+0x10)-cam);
                int radius=g_ram[o+7],height=g_ram[o+6];
                y=((ram16(o+0x14)-cy+height)&mask)-height;
                if(x+radius<left-cam || x-radius>=left-cam+s_requested_width || y+height<0 || y-height>=224)continue;
                if(s_visible_count<144)s_visible_objects[s_visible_count++]=o;
            } else {x=(int16_t)ram16(o+0x10)-128;y=(int16_t)ram16(o+0x14)-128;anchor=2;}
            unsigned map=scene_read32(0xFF0000u+o+0xC),gfx=ram16(o+0xA);
            if(multi) {
                if(g_ram[o+0x22])add_mapping(map,g_ram[o+0x22],gfx,flags,x,y,anchor,0);
                unsigned count=ram16(o+0x16);if(count>8)count=8;
                for(unsigned n=0;n<count;++n) {
                    unsigned child=o+0x18+n*6;
                    int sx=(int16_t)ram16(child)-128,sy=(int16_t)ram16(child+2)-128;
                    if(flags&4){sx=(int16_t)(ram16(child)-cam);sy=((ram16(child+2)-cy+128)&mask)-128;}
                    add_mapping(map,g_ram[child+5],gfx,flags,sx,sy,anchor,0);
                }
            } else add_mapping(map,g_ram[o+0x22],gfx,flags,x,y,anchor,(flags&32)!=0);
        }
    }
}

static void publish_sprites(void)
{
    for(unsigned n=0;n<s_visible_count;++n) {
        unsigned o=s_visible_objects[n]; if(scene_read32(0xFF0000u+o))write8(o+4,g_ram[o+4]|128);
    }
    s_build.serial=++s_serial;memcpy(s_build.sat,g_ram+0xF800,640);
    SceneFrame *dst=&s_history[s_serial%3];
    dst->count=s_build.count;dst->serial=s_serial;dst->scene=s_build.scene;
    dst->camera_x=s_build.camera_x;dst->camera_y=s_build.camera_y;
    memcpy(dst->sat,s_build.sat,640);
    memcpy(dst->sprites,s_build.sprites,s_build.count*sizeof(SceneSprite));
}
int s3_video_hook(uint32_t pc)
{
    if(pc==S3_LOAD_INIT && !g_ram[0xF76C]) {
        s_placement_base=s_placement_count=0;s_loader_active=0;
    }
    if(pc==S3_LOAD_UPDATE && gameplay() && (enabled() || s_loader_active) && g_ram[0xFE10]<19) {
        spawn_scene();return 1;
    }
    if(!enabled() || ram16(S3_COMPETITION))return 0;
    switch(pc) {
    case S3_LOAD_INIT:return 0;
    case S3_CAPTURE:capture_objects();return 0;
    case S3_PUBLISH:publish_sprites();return 0;
    case S3_RING_RANGE:
        if(gameplay()) {g_cpu.A[1]=rings_base()+4;g_cpu.A[2]=rings_end();}
        return 0;
    case S3_RING_STATUS:
        if(gameplay())g_cpu.A[4]=0xFFFFE702u;
        return 0;
    default:
        if(gameplay()) {
            int x=(uint16_t)((uint16_t)g_cpu.D[0]+ram16(0xF7DA));
            int lo,hi;activation_bounds(ram16(0xEE78),s_requested_width,&lo,&hi);
            g_cpu.D[0]=(g_cpu.D[0]&0xFFFF0000u)|((x<lo || x>=hi)?641u:320u);
        }
        return 0;
    }
}

#include "sonic3_blue_spheres.inc"

static uint8_t *s_priority;
static int s_priority_capacity;
static void select_scene(const GVDP *v)
{
    s_display=NULL;
    unsigned sat=(v->reg[5]&127u)<<9;
    /* Keep the front buffer's publication alive even if the producer cycles
     * through every history slot before the next native sprite DMA. */
    for(unsigned n=0;n<4;++n) {
        const SceneFrame *p=n==3?&s_display_frame:&s_history[n];
        if(!p->serial || (s_display && p->serial<s_display->serial))continue;
        int matches=1;
        for(unsigned k=0;k<640;++k)
            if(p->sat[k]!=v->vram[(sat+k)&65535u]) {matches=0;break;}
        if(matches)s_display=p;
    }
    if(!s_display) {
        ++s_scene_misses;
        int scene=level_ready()?1:(g_ram[0xF600]&127)==0x34?2:0;
        /* An in-progress native SAT is not a custom-scene publication. Keep
         * the completed scene until its replacement is ready, without
         * flashing the native centered HUD or dropping host-only rings. */
        if(scene && s_display_frame.serial && s_display_frame.scene==scene) {
            s_display=&s_display_frame;++s_scene_holds;
        }
    }
    /* Keep the selected front buffer immutable for all displayed scanlines. */
    if(s_display && s_display!=&s_display_frame) {
        s_display_frame.count=s_display->count;s_display_frame.scene=s_display->scene;
        s_display_frame.serial=s_display->serial;
        s_display_frame.camera_x=s_display->camera_x;s_display_frame.camera_y=s_display->camera_y;
        memcpy(s_display_frame.sat,s_display->sat,sizeof s_display_frame.sat);
        memcpy(s_display_frame.sprites,s_display->sprites,s_display->count*sizeof(SceneSprite));
        s_display=&s_display_frame;
    }
}
static void draw_scene_sprites(const GVDP *v,int line,uint32_t *out,int width,
                               int origin,const uint32_t *palette)
{
    if(!s_display)return;
    for(unsigned n=0;n<s_display->count;++n) {
        const SceneSprite *s=&s_display->sprites[n];
        int cw=((s->size>>2)&3)+1,ch=(s->size&3)+1;
        int world=!s->hud && s_display->scene==1;
        int y=s->y+(world?s_display->camera_y-s_camera_y:0);
        if(line<y || line>=y+ch*8)continue;
        int base=s->x+(s->hud==1?0:s->hud==2?(width-320)/2:origin);
        /* Native camera scroll may advance while a completed host frame is
         * retained. Reproject its world coordinates, not its old screen
         * coordinates; otherwise stationary rings jitter with the camera. */
        if(world)base+=s_display->camera_x-s_camera;
        int iy=line-y;
        if(s->attr&0x1000)iy=ch*8-1-iy;
        for(int i=0;i<cw*8;++i) {
            int x=base+i;
            if(x<0 || x>=width || (s_priority[x]&2))continue;
            int ix=(s->attr&0x800)?cw*8-1-i:i;
            uint16_t cell=(uint16_t)((s->attr&0xE000u)|
                (((s->attr&2047u)+(unsigned)(ix/8*ch+iy/8))&2047u));
            uint8_t p=pattern_pixel(v,cell,ix,iy);
            if(!p)continue;
            s_priority[x]|=2;
            if(s->hud || (s->attr&0x8000) || !(s_priority[x]&1))out[x]=palette[p];
        }
    }
}
static int attributes_differ(const GVDP *v,uint16_t a,uint16_t b,int x,int y)
{
    if(a==b)return 0;
    /* Compare visible pixels/priority, not irrelevant transparent-tile flags. */
    for(int i=0;i<8;++i) {
        uint8_t ap=pattern_pixel(v,a,x+i,y),bp=pattern_pixel(v,b,x+i,y);
        if(ap!=bp || (ap && ((a^b)&0x8000)))return 1;
    }
    return 0;
}
static void aiz_palette(const AizArt *art,const GVDP *v,uint32_t *out)
{
    /* Follow fades without borrowing the cutscene's unrelated colour banks.
     * AIZ's common black/white entries also cover fades to/from white. */
    for(unsigned i=0;i<64;++i) {
        unsigned color=i<16?v->cram[i]:art->colors[i-16],faded=0;
        for(unsigned shift=0;shift<12;shift+=4) {
            int lo=(v->cram[16]>>shift)&14,hi=(v->cram[17]>>shift)&14;
            int c=lo+(int)((color>>shift)&14)*(hi-lo)/14;
            faded|=(unsigned)c<<shift;
        }
        out[i]=genesis_dac_cram_to_argb((uint16_t)faded,GENESIS_DAC_NORMAL);
    }
}

static void scanline(const GVDP *v, int line, const uint32_t *native, int nw,
                     uint32_t *out, int width)
{
    /* Publish one immutable art/scroll snapshot per frame. Palettes stay
     * live so water-line HBlank colour changes are retained. */
    uint32_t palette[64],shadow[64],intro_palette[64],forest_palette[64];
    int uniform_palette=1;
    for(int i=0;i<64;++i) {
        palette[i]=genesis_dac_cram_to_argb(v->cram[i],GENESIS_DAC_NORMAL);
        shadow[i]=genesis_dac_cram_to_argb(v->cram[i],GENESIS_DAC_SHADOW);
        if(palette[i]!=palette[0])uniform_palette=0;
    }
    if(line==0) {
        memcpy(&s_video_frame,v,sizeof s_video_frame);
        memcpy(s_world_frame,g_ram,sizeof s_world_frame);
        /* HUD visibility is not level readiness: AIZ hides it for the whole
         * landing/emerald cutscene while displaying the real stage planes. */
        s_frame_level=level_ready();
        /* The native special-stage exit changes mode before its 60-frame
         * fade. Keep the globe until its shadow/highlight video mode ends. */
        s_frame_special=(g_ram[0xF600]&127)==0x34 ||
            (s_frame_special && (v->reg[12]&8) && (v->reg[1]&64));
        if(s_frame_special)ss_begin(v,width);
        s_frame_zone=g_ram[0xFE10];
        s_frame_act=g_ram[0xFE11];s_frame_bg_event=ram16(0xEEC2);
        if(s_frame_level && !s_frame_zone && !s_frame_act)load_aiz_art();
        /* The intro queues the main-level block definitions before its
         * two-row-at-a-time name-table refresh. Keep the outgoing background
         * definitions intact while Kosinski is overwriting their RAM. */
        int replacing_intro=s_frame_level && s_frame_zone==0 && s_frame_act==0 &&
            s_frame_bg_event==0 && ram16(0xEEC6);
        if(!replacing_intro)memcpy(s_background_frame,s_world_frame,sizeof s_background_frame);
        s_frame_fg_x=ram16(0xEE80);s_frame_fg_y=ram16(0xEE84);s_frame_bg_y=ram16(0xEE90);
        s_width=width;s_requested_width=width;
        s_terrain_checks=s_terrain_errors=s_bg_checks=s_bg_errors=s_bg_unstreamed=0;
        s_uninitialized_scroll_lines=s_terrain_unstreamed=0;
        s_bad_x=s_bad_y=-1;s_bad_attr=s_bad_expected=0;
        s_bg_bad_x=s_bg_bad_y=-1;s_bg_bad_a=s_bg_bad_expected=0;
        if(width>s_priority_capacity) {
            uint8_t *p=(uint8_t *)realloc(s_priority,(size_t)width);
            if(p){s_priority=p;s_priority_capacity=width;}
        }
        select_scene(v);
    }
    v=&s_video_frame;
    uint32_t backdrop=palette[v->reg[7]&63];
    /* During a completed fade the game can clear/reload name tables before
     * retiring its old player/display list. A flat palette displays no stage
     * geometry and is not a valid tile-streaming comparison. Keep the full
     * canvas flat; never expose in-progress stage decompression. */
    if(uniform_palette && !(v->reg[12]&8)) {
        for(int x=0;x<width;++x)out[x]=palette[0];
        if(line==0){++s_frames;s_native_x=(width-nw)/2;}
        return;
    }
    /* Display-off/fades fill the same full-width canvas, not black sidebars.
     * A rare unsupported mode also retains the selected output dimensions. */
    if((nw!=320 && nw!=256) || !(v->reg[1]&64) || (v->reg[12]&6) ||
       ((v->reg[12]&8) && !s_frame_special) || s_priority_capacity<width) {
        uint32_t fill=!(v->reg[1]&64)?native[0]:backdrop;
        for(int x=0;x<width;++x)out[x]=fill;
        int x0=(width-nw)/2;
        if(x0>=0)memcpy(out+x0,native,(size_t)nw*sizeof(uint32_t));
        if(line==0){++s_fallbacks;s_native_x=x0;}
        return;
    }
    unsigned hsbase=(v->reg[13]&63u)<<10;
    int hmode=v->reg[11]&3,hi=hmode==0?0:hmode==2?line&~7:line;
    int hs_a=(int16_t)vram16(v,hsbase+(unsigned)hi*4);
    int hs_b=(int16_t)vram16(v,hsbase+(unsigned)hi*4+2);
    if(s_frame_special && ss_ready) {
        if(line==0){++s_frames;s_native_x=(width-nw)/2;}
        ss_scanline(v,line,out,width,palette,shadow,hs_b);
        return;
    }
    int level=s_frame_level,special=s_frame_special;
    /* The next player object can exist while the title-card SAT is still
     * displayed. Do not expose the incoming level before its own display
     * list/planes reach VBlank. */
    if(s_display && s_display->scene!=1)level=0;
    int missing_scroll=0;
    int camera=level?unwrap(-hs_a,s_frame_fg_x):ram16(0xEE78);
    int camera_y=unwrap(v->vsram[0],s_frame_fg_y);
    if(line==0) {
        if(level)s_stage_width=stage_width();
        s_camera=camera;s_camera_y=camera_y;++s_frames;
        s_left=level?view_left(camera,width):camera-(width-nw)/2;
    }
    int origin=level?s_camera-s_left:(width-nw)/2;
    s_native_x=origin;
    unsigned base_a=(v->reg[2]&56u)<<10,base_b=(v->reg[4]&7u)<<13;
    int wy=camera_y+line;
    int bg_world=level && !special;
    int aiz_banks=bg_world && !s_frame_zone && !s_frame_act &&
        s_frame_bg_event<=8 && s_aiz_art_ready;
    if(aiz_banks){aiz_palette(&s_aiz_intro,v,intro_palette);aiz_palette(&s_aiz_main,v,forest_palette);}
    int by=bg_world?unwrap(v->vsram[1],s_frame_bg_y)+line:line+(v->vsram[1]&1023);
    int bg_width=bg_world?((s_world_frame[0x8002]<<8)|s_world_frame[0x8003])*128:0;
    /* AIZ1's upper parallax bands are a fixed 512px canvas. Its layout
     * also stores the intro coastline and fire art farther right: those
     * are separate compositions, not the repeat period of the forest. */
    if(bg_world && s_frame_zone==0 && s_frame_act==0) {
        if(by<0x220)bg_width=512;
        else if(by<0x380)bg_width=30*128; /* lake section, before the fire banks */
        /* The flying intro starts with a negative virtual BG camera.
         * Its streamer clamps that camera to zero, leaving the first 512px
         * canvas in VRAM while hardware HScroll wraps through it. */
        if(s_frame_bg_event==0 && hs_b>0)bg_width=512;
    }
    for(int x=0;x<width;++x) {
        int nx=x-origin,wx=level?nx+camera:nx-hs_a;
        int reference_bx=nx-hs_b,bx=reference_bx;
        /* Title/menu backgrounds only initialize the visible 40 tile
         * columns; repeat that composition, not the stale 512px name table. */
        if(!level && !special){bx%=nw;if(bx<0)bx+=nw;}

        int background_x=bx;
        if(bg_width>0) { background_x%=bg_width;if(background_x<0)background_x+=bg_width; }
        uint16_t a=level?world_attr(s_world_frame,wx,wy,0):plane_attr(v,base_a,nx-hs_a,line+(v->vsram[0]&1023));
        uint16_t b=bg_world?world_attr(s_background_frame,background_x,by,1):plane_attr(v,base_b,bx,by);
        uint8_t ap=pattern_pixel(v,a,level?wx:nx-hs_a,level?wy:line+(v->vsram[0]&1023));
        uint8_t bp=pattern_pixel(v,b,bg_world?background_x:bx,by);
        const uint32_t *apalette=palette,*bpalette=palette;
        if(aiz_banks && (nx<0 || nx>=nw)) {
            /* The hollow tree at $1300 shares primary blocks across both
             * banks. The coast behind it and forest ahead need independent
             * art. Only extend beyond the native viewport; its live animated
             * art, palette changes and scripted reveals remain authoritative. */
            int intro=wx<0x1300;
            const AizArt *art=intro?&s_aiz_intro:&s_aiz_main;
            if(intro || s_frame_bg_event<8) {
                a=world_attr_blocks(s_world_frame,art->blocks,wx,wy,0);
                ap=pattern_pixel(&art->video,a,wx,wy);
                apalette=intro?intro_palette:forest_palette;
            }
            if(s_frame_bg_event==0) {
                art=by>=0x380?&s_aiz_intro:&s_aiz_main;
                b=world_attr_blocks(s_background_frame,art->blocks,background_x,by,1);
                bp=pattern_pixel(&art->video,b,background_x,by);
                bpalette=by>=0x380?intro_palette:forest_palette;
            }
        }
        if(level && (wx<0 || wx>=s_stage_width))ap=0;
        /* Menus have a centered foreground composition, with full-width
         * scenery underneath. Special-stage planes are repeating art. */
        if(!level && (nx<0 || nx>=nw))ap=0;
        s_priority[x]=(uint8_t)(((a&0x8000)&&ap)||((b&0x8000)&&bp));
        uint8_t p=ap&&(a&0x8000)?ap:bp&&(b&0x8000)?bp:ap?ap:bp;
        int foreground=ap && ((a&0x8000) || !bp || !(b&0x8000));
        out[x]=p?(foreground?apalette[p]:bpalette[p]):backdrop;
        /* Outside a native special-stage projection only planes are drawn,
         * so plane priority alone
         * determines shadow; native operator sprites remain in the center. */
        if(special && (v->reg[12]&8) && !s_priority[x])
            out[x]=shadow[p?p:v->reg[7]&63];
        if(nx>=0 && nx<nw && !(x&7) && !missing_scroll) {
            if(level && !native_terrain_streamed(wy,s_frame_fg_y)) {
                ++s_terrain_unstreamed;
            } else if(level) {
                uint16_t expected=plane_attr(v,base_a,nx-hs_a,line+(v->vsram[0]&1023));
                ++s_terrain_checks;
                int bad=attributes_differ(v,a,expected,wx,wy);
                if(bad && s_bad_y<0){s_bad_x=wx;s_bad_y=line;s_bad_attr=a;s_bad_expected=expected;s_bad_world_y=wy;s_bad_hscroll=hs_a;}
                s_terrain_errors+=bad;
            }
            if(bg_world && s_frame_zone==0 && s_frame_act==0 && s_frame_bg_event==4) {
                /* Native intro->forest refresh uploads two rows per frame.
                 * Until finished, the old name table is not an oracle for
                 * the new world layout, which the opt-in renderer shows. */
                ++s_bg_unstreamed;
            } else if(bg_world) {
                int rx=reference_bx;
                if(bg_width>0){rx%=bg_width;if(rx<0)rx+=bg_width;}
                uint16_t expected=plane_attr(v,base_b,reference_bx,by);
                ++s_bg_checks;
                uint16_t actual=world_attr(s_background_frame,rx,by,1);
                int bad=attributes_differ(v,actual,expected,reference_bx,by);
                if(bad && s_bg_bad_x<0){s_bg_bad_x=rx;s_bg_bad_y=by;s_bg_bad_a=actual;s_bg_bad_expected=expected;}
                s_bg_errors+=bad;
            }

        }
    }
    if(!level) {
        /* Logos/copyright/menus remain centered at their original pixel
         * scale. The full-width background follows the live scene palette. */
        if(origin>=0)memcpy(out+origin,native,(size_t)nw*sizeof(uint32_t));
    } else if(s_display) {
        draw_scene_sprites(v,line,out,width,origin,palette);
    } else {
        /* No matching DMA yet (e.g. load/fade): preserve the live native
         * scene while the host display list catches its next publication. */
        int first=origin<0?-origin:0,last=origin+nw>width?width-origin:nw;
        if(last>first)memcpy(out+origin+first,native+first,(size_t)(last-first)*sizeof(uint32_t));
    }
}

void s3_video_command(int id, const char *json)
{
    (void)json;
    char reply[2048];
    snprintf(reply, sizeof reply,
        "{\"id\":%d,\"enabled\":%d,\"width\":%d,\"view_left\":%d,\"camera\":%d,"
        "\"native_x\":%d,\"stage_width\":%d,\"frames\":%u,\"fallback_frames\":%u,"
        "\"terrain_checks\":%u,\"terrain_errors\":%u,\"background_checks\":%u,\"background_errors\":%u,\"background_unstreamed\":%u,"
        "\"sprites\":%u,\"scene\":%d,\"spawned\":%u,\"pool_pressure\":%u,\"native_uninitialized_scroll_lines\":%u,"
        "\"native_unstreamed_terrain_samples\":%u,\"first_bad_x\":%d,\"first_bad_y\":%d,\"first_bad_attr\":%u,\"first_bad_expected\":%u,\"first_bad_world_y\":%d,\"first_bad_hscroll\":%d,"
        "\"scene_match_misses\":%u,\"scene_held_frames\":%u,\"main_cpu_divisor\":%u,"
        "\"aiz_art_ready\":%d,\"aiz_main_palette\":%u,\"aiz_main_attr\":%u,\"aiz_main_pixel\":%u,"
        "\"bg_bad_x\":%d,\"bg_bad_y\":%d,\"bg_bad_attr\":%d,\"bg_bad_expected\":%d,\"bg_event\":%u,"
        "\"tick_samples\":%u,\"tick_updates\":%u,\"tick_lag\":%u,\"tick_multi\":%u,\"publication_lag\":%u,"
        "\"blue_spheres\":%d,\"special_spheres\":%u,\"special_margin_spheres\":%u}",
        id, enabled(), s_width, s_left, s_camera, s_native_x, s_stage_width,
        s_frames, s_fallbacks, s_terrain_checks, s_terrain_errors,s_bg_checks,s_bg_errors,s_bg_unstreamed,
        s_display?s_display->count:0,s_display?s_display->scene:-1,s_spawned,s_pool_pressure,s_uninitialized_scroll_lines,
        s_terrain_unstreamed,s_bad_x,s_bad_y,s_bad_attr,s_bad_expected,s_bad_world_y,s_bad_hscroll,
        s_scene_misses,s_scene_holds,s3_video_main_cpu_divisor(),
        s_aiz_art_ready,s_aiz_main.colors[2],world_attr_blocks(s_world_frame,s_aiz_main.blocks,0x1300,0x390,0),
        pattern_pixel(&s_aiz_main.video,0x582C,0x1300,0x390),
        s_bg_bad_x,s_bg_bad_y,s_bg_bad_a,s_bg_bad_expected,s_frame_bg_event,
        s_tick_samples,s_tick_updates,s_tick_lag,s_tick_multi,s_publication_lag,
        s_frame_special && ss_ready,ss_count,ss_margin_count);
    cmd_send_response(reply);
}
const GameVideo sonic3_video = { configure, enabled, width, scanline };
