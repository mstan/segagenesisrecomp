/* Opt-in Sonic 2 REV01 world renderer.
 * Canonical s2disasm: GetBlock $E244, ObjectsManager $17AA4,
 * BuildSprites $16604, RingsManager $16F88. Native rendering is the default.
 * No collision, layout, decompression or tile-streaming data is rewritten.
 */
#include "sonic2_video.h"
#include "genesis_runtime.h"
#include "video/genesis_vdp.h"
#include "video/genesis_dac.h"
#include "cmd_server.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static GVDP s_video_frame;
static uint8_t s_world_frame[0xA800];
static int s_frame_level,s_frame_special,s_frame_fg_x,s_frame_fg_y,s_frame_bg_y;
static unsigned s_frame_zone;
static void scene_mode_changed(int was_enabled);

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
    int mode = g_ram[0xF600] & 0x7F;
    /* Game mode changes BEFORE a fade finishes. Scene identity follows the
     * still-live player/layout until the incoming scene replaces them. */
    return (mode == 8 || mode == 12 || mode == 16) && (g_ram[0xB000] == 1 || g_ram[0xB000] == 2) && !ram16(0xFFD8);
}
static int stage_width(void)
{
    int last = 1;
    for (int row = 0; row < 16; ++row)
        for (int col = 0; col < 128; ++col)
            if (g_ram[0x8000 + row * 256 + col] && col + 1 > last) last = col + 1;
    return last * 128;
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
unsigned s2_video_main_cpu_divisor(void)
{
    /* Level_MainLoop ($4360) still waits for the real V-int every tick.
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
    /* Draw_FG ($DAF6) streams 16px rows at copied-camera offsets -16 and
     * +224. Live VScroll can run ahead of that copy during a fast fall.
     * Outside this precise coverage, the native name table is not a terrain
     * oracle: the enhanced renderer must still draw the real stage block. */
    int top = (copied_camera_y & ~15) - 16;
    return wy >= top && wy < top + 256;
}
static uint16_t world_attr(const uint8_t *ram,int wx,int wy,int background)
{
    if(wx<0 || wx>=16384)return 0;
    unsigned layout=(background?0x8080u:0x8000u)+
        (((unsigned)wy>>7)&15u)*256u+((unsigned)wx>>7);
    unsigned chunk=ram[layout];
    unsigned address=chunk*128u+((unsigned)wy&112u)+((unsigned)wx&112u)/8u;
    uint16_t block=(uint16_t)((ram[address]<<8)|ram[address+1]);
    int tx=(wx>>3)&1,ty=(wy>>3)&1;
    if(block&0x0400)tx^=1;
    if(block&0x0800)ty^=1;
    address=0x9000u+(block&1023u)*8u+(unsigned)(ty*2+tx)*2u;
    /* Block_Table has 768 entries in Sonic 2; IDs above that are not valid
     * stage blocks. Do not read into the live object/display buffers. */
    if(address+1>=0xA800u)return 0;
    return (uint16_t)(((ram[address]<<8)|ram[address+1])^((block&0x0C00u)<<1));
}
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
void s2_video_vblank(void)
{
    static int was_active;
    static unsigned last_tick,last_zone,last_serial;
    unsigned tick=ram16(0xFE04),zone=ram16(0xFE10);
    int active=enabled() && g_ram[0xF600]==12 && gameplay() &&
        g_ram[0xF711] && g_ram[0xB024]<6 && !ram16(0xF63A);
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
typedef struct { unsigned address; uint16_t x, y; uint8_t id, subtype, state, loaded; } Placement;
static Placement s_placements[SCENE_PLACEMENTS];
static unsigned s_placement_count, s_placement_base;
static int s_loader_active;

static uint8_t scene_read8(unsigned a)
{
    a &= 0xFFFFFFu;
    return a < 0x100000u ? g_rom[a] : a >= 0xFF0000u ? g_ram[a & 65535u] : 0;
}
static uint16_t scene_read16(unsigned a) { return (uint16_t)((scene_read8(a)<<8)|scene_read8(a+1)); }
static uint32_t scene_read32(unsigned a) { return ((uint32_t)scene_read16(a)<<16)|scene_read16(a+2); }
static void write8(unsigned a, unsigned v) { m68k_write8(0xFF0000u|(a&65535u),(uint8_t)v); }
static void write16(unsigned a, unsigned v) { m68k_write16(0xFF0000u|(a&65535u),(uint16_t)v); }
static void write32(unsigned a, unsigned v) { m68k_write32(0xFF0000u|(a&65535u),v); }
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
    for(int n=0;n<count && s_build.count<SCENE_SPRITES;++n,p+=8) {
        int dy=(int8_t)scene_read8(p),size=scene_read8(p+1)&15;
        int dx=(int16_t)scene_read16(p+6);
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
    unsigned zone=ram16(0xFE10),index=((zone>>8)*4u)+((zone&1u)*2u);
    unsigned base=0xE6800u+(int16_t)scene_read16(0xE6800u+index);
    if(base==s_placement_base && s_placement_count)return;
    s_placement_base=base;s_placement_count=0;unsigned state=1;
    for(unsigned p=base;p+6<=0x100000u && s_placement_count<SCENE_PLACEMENTS;p+=6) {
        unsigned x=scene_read16(p);if(x==65535u)break;
        Placement *q=&s_placements[s_placement_count++];
        q->address=p;q->x=(uint16_t)x;q->y=scene_read16(p+2);
        q->id=scene_read8(p+4);q->subtype=scene_read8(p+5);
        q->state=(uint8_t)((q->y&0x8000)?state++:0);q->loaded=0;
    }
}
static int placement_alive(const Placement *p)
{
    for(unsigned o=0xB400;o<0xD000;o+=64) {
        if(!g_ram[o])continue;
        if(p->state && g_ram[o+0x23]==p->state)return 1;
        if(!p->state && g_ram[o]==p->id && ram16(o+8)==p->x)return 1;
    }
    return 0;
}
static void spawn_scene(void)
{
    load_placements();s_loader_active=1;
    int camera=ram16(0xEE00),w=enabled()?s_requested_width:320;
    int lo,hi;activation_bounds(camera,w,&lo,&hi);
    int player=ram16(0xB008);s_pool_pressure=0;
    for(unsigned i=0;i<s_placement_count;++i) {
        Placement *p=&s_placements[i];
        if((int)p->x<lo || (int)p->x>=hi)p->loaded=0;
        else if(!p->loaded && placement_alive(p))p->loaded=1;
    }
    for(unsigned pass=0;pass<s_placement_count;++pass) {
        int best=-1,distance=INT_MAX;
        for(unsigned i=0;i<s_placement_count;++i) {
            Placement *p=&s_placements[i];
            if(p->loaded || (int)p->x<lo || (int)p->x>=hi)continue;
            if(p->state && (g_ram[0xFC02+p->state]&128)){p->loaded=1;continue;}
            int d=abs((int)p->x-player);
            if(d<distance){best=(int)i;distance=d;}
        }
        if(best<0)break;
        Placement *p=&s_placements[best];unsigned o;
        for(o=0xB400;o<0xD000 && g_ram[o];o+=64){}
        if(o==0xD000){++s_pool_pressure;break;}
        write16(o+8,p->x);write16(o+12,p->y&4095);
        unsigned flip=(p->y>>13)&3;
        write8(o+1,flip);write8(o+0x22,flip);write8(o+0x23,p->state);
        write8(o+0x28,p->subtype);
        if(p->state)write8(0xFC02+p->state,g_ram[0xFC02+p->state]|128);
        write8(o,p->id);p->loaded=1;++s_spawned;
    }
    unsigned r=0,l=0,rs=1,ls=1;
    while(r<s_placement_count && s_placements[r].x<(unsigned)((camera&~127)+640)){rs+=s_placements[r].state!=0;++r;}
    while(l<s_placement_count && (int)s_placements[l].x<((camera-128)&~127)){ls+=s_placements[l].state!=0;++l;}
    write32(0xF770,s_placement_base+r*6);write32(0xF774,s_placement_base+l*6);
    write8(0xFC00,rs);write8(0xFC01,ls);write16(0xF76E,camera&0xFF80);
    write16(0xF7DA,(camera-128)&0xFF80);
}
static unsigned rings_end(void)
{
    unsigned o=0xE806;
    while(o+6<=0xEE00 && ram16(o+2)!=65535u)o+=6;
    return o;
}
static void capture_objects(void)
{
    s_build.count=0;s_visible_count=0;++s_scene_tick;
    s_build.scene=gameplay() && g_ram[0xF711]?1:(g_ram[0xF600]&127)==16?2:0;
    int cam=ram16(0xEEF0),cy=ram16(0xEEF4);
    s_build.camera_x=cam;s_build.camera_y=cy;
    int left=gameplay()?view_left(cam,s_requested_width):cam-(s_requested_width-320)/2;
    if(gameplay() && g_ram[0xF711]) {
        unsigned blink=(g_ram[0xFE05]&8)==0;
        unsigned frame=blink*((ram16(0xFE20)==0?1:0)+(g_ram[0xFE23]==9?2:0));
        /* BuildHUD uses spriteScreenPositionYCentered(24), not Y=24. */
        add_mapping(0x40A9A,frame,0x86CA,0,16,136,1,0);
        unsigned end=rings_end();
        for(unsigned o=0xE806;o<end;o+=6) {
            if(ram16(o)&0x8000)continue;
            int x=ram16(o+2),y=ram16(o+4);
            if(x<left-16 || x>=left+s_requested_width+16)continue;
            unsigned fr=g_ram[o+1]?g_ram[o+1]:g_ram[0xFEA3];
            unsigned map=0x1736Au+(int16_t)scene_read16(0x1736Au+fr*2u);
            add_mapping(map,0,0x26BC,0,x-cam,(int16_t)(y-cy),0,1);
        }
    }
    for(unsigned pri=0;pri<8;++pri) {
        unsigned queue=0xAC00+pri*128,bytes=ram16(queue);if(bytes>126)bytes=126;
        for(unsigned k=2;k<=bytes;k+=2) {
            unsigned o=ram16(queue+k);if(o<0xB000 || o>=0xD400 || !g_ram[o])continue;
            unsigned flags=g_ram[o+1],multi=flags&64,space=flags&12;
            int x,y,anchor=0;
            if(space || multi) {
                x=(int16_t)(ram16(o+8)-cam);
                y=(int16_t)(ram16(o+12)-cy);
                int radius=g_ram[o+(multi?0xE:0x19)];
                if(x+radius<left-cam || x-radius>=left-cam+s_requested_width)continue;
                if(flags&16){int h=g_ram[o+(multi?0x14:0x16)];if(y+h<0 || y-h>=224)continue;}
                else {y=((y+128)&2047)-128;if(y<-32 || y>=256)continue;}
                if(s_visible_count<144)s_visible_objects[s_visible_count++]=o;
            } else {x=(int16_t)ram16(o+8)-128;y=(int16_t)ram16(o+10)-128;anchor=2;}
            unsigned map=scene_read32(0xFF0000u+o+4),gfx=ram16(o+2);
            if(multi) {
                if(g_ram[o+0xB])add_mapping(map,g_ram[o+0xB],gfx,flags,x,y,anchor,0);
                unsigned count=g_ram[o+0xF];if(count>8)count=8;
                for(unsigned n=0;n<count;++n) {
                    unsigned child=o+0x10+n*6;
                    int sx=(int16_t)(ram16(child)-cam),sy=((ram16(child+2)-cy+128)&2047)-128;
                    add_mapping(map,g_ram[child+5],gfx,flags,sx,sy,0,0);
                }
            } else add_mapping(map,g_ram[o+0x1A],gfx,flags,x,y,anchor,(flags&32)!=0);
        }
    }
}

static void publish_sprites(void)
{
    for(unsigned n=0;n<s_visible_count;++n) {
        unsigned o=s_visible_objects[n]; if(g_ram[o])write8(o+1,g_ram[o+1]|128);
    }
    s_build.serial=++s_serial;memcpy(s_build.sat,g_ram+0xF800,640);
    SceneFrame *dst=&s_history[s_serial%3];
    dst->count=s_build.count;dst->serial=s_serial;dst->scene=s_build.scene;
    dst->camera_x=s_build.camera_x;dst->camera_y=s_build.camera_y;
    memcpy(dst->sat,s_build.sat,640);
    memcpy(dst->sprites,s_build.sprites,s_build.count*sizeof(SceneSprite));
}
int s2_video_hook(uint32_t pc)
{
    if(pc==0x17AA4 && !g_ram[0xF76C]) {
        s_placement_base=s_placement_count=0;s_loader_active=0;
    }
    if(pc==0x17B84 && gameplay() && (enabled() || s_loader_active)) {spawn_scene();return 1;}
    if(!enabled() || ram16(0xFFD8))return 0;
    switch(pc) {
    case 0x17AA4:return 0;
    case 0x16604:capture_objects();return 0;
    case 0x16712:case 0x1671A:publish_sprites();return 0;
    case 0x170D0:
        /* Use the game's original collision/reward/consumption routines for
         * both Sonic and Tails. Only lift the old camera's ring search gate. */
        if(gameplay()){g_cpu.A[1]=0xFFFFE806u;g_cpu.A[2]=0xFFFF0000u|rings_end();}
        return 0;
    case 0x16F16:case 0x16F3E: {
        if(!gameplay())return 0;
        unsigned o=g_cpu.A[0]&65535u;
        int x=ram16(o+8),y=(int16_t)(ram16(o+12)-ram16(0xEE04));
        int radius=pc==0x16F3E?g_ram[o+0x19]:0;
        int left=view_left(ram16(0xEE00),s_requested_width);
        g_cpu.D[0]=(x+radius<left || x-radius>=left+s_requested_width || y<0 || y>=224)?1:0;
        g_cpu.SR=(uint16_t)((g_cpu.SR&~15u)|(g_cpu.D[0]?0:4));
        return 1;
    }
    default:
        if(gameplay()) {
            int x=(uint16_t)((uint16_t)g_cpu.D[0]+ram16(0xF7DA));
            int lo,hi;activation_bounds(ram16(0xEE00),s_requested_width,&lo,&hi);
            g_cpu.D[0]=(g_cpu.D[0]&0xFFFF0000u)|((x<lo || x>=hi)?641u:320u);
        }
        return 0;
    }
}

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
        int scene=gameplay() && g_ram[0xF711]?1:(g_ram[0xF600]&127)==16?2:0;
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
/* Emerald Hill's camera-dependent component of SwScrl_EHZ ($C562).
 * Retain live water ripple phase; only reproject camera motion at stage edges. */
static int ehz_parallax(int camera,int line)
{
    int n=-camera;
    if(line<22 || (line>=101 && line<112))return 0;
    if(line<101)return -(n>>6);
    if(line<128)return -(n>>4);
    if(line<144){int a=n>>4;return -(a+(a>>1));}
    int row=line-144;
    if(row>=15 && row<33)row=15+(row-15)/2*2;
    else if(row>=33)row=33+(row-33)/3*3;
    int step=(((n>>1)-(n>>3))*256)/48;
    return -((n>>3)+((step*row)>>8));
}

static void scanline(const GVDP *v, int line, const uint32_t *native, int nw,
                     uint32_t *out, int width)
{
    /* Publish one immutable art/scroll snapshot per frame. Palettes stay
     * live so water-line HBlank colour changes are retained. */
    uint32_t palette[64],shadow[64];
    int uniform_palette=1;
    for(int i=0;i<64;++i) {
        palette[i]=genesis_dac_cram_to_argb(v->cram[i],GENESIS_DAC_NORMAL);
        shadow[i]=genesis_dac_cram_to_argb(v->cram[i],GENESIS_DAC_SHADOW);
        if(palette[i]!=palette[0])uniform_palette=0;
    }
    if(line==0) {
        memcpy(&s_video_frame,v,sizeof s_video_frame);
        memcpy(s_world_frame,g_ram,sizeof s_world_frame);
        s_frame_level=gameplay() && g_ram[0xF711];
        s_frame_special=(g_ram[0xF600]&127)==16 && !gameplay();
        s_frame_zone=g_ram[0xFE10];
        s_frame_fg_x=ram16(0xEE60);s_frame_fg_y=ram16(0xEE64);s_frame_bg_y=ram16(0xEE6C);
        s_width=width;s_requested_width=width;
        s_terrain_checks=s_terrain_errors=s_bg_checks=s_bg_errors=s_bg_unstreamed=0;
        s_uninitialized_scroll_lines=s_terrain_unstreamed=0;
        s_bad_x=s_bad_y=-1;s_bad_attr=s_bad_expected=0;
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
    int level=s_frame_level,special=s_frame_special;
    /* The next player object can exist while the title-card SAT is still
     * displayed. Do not expose the incoming level before its own display
     * list/planes reach VBlank. */
    if(s_display && s_display->scene!=1)level=0;
    /* Stock SwScrl_EHZ only initializes 222/224 rows. The last two contain
     * stale scroll words, not a valid terrain reference. Extend its last row
     * in the enhanced renderer and report the excluded native rows explicitly. */
    int missing_scroll=level && s_frame_zone==0 && line>=222;
    if(missing_scroll) {
        hs_a=(int16_t)vram16(v,hsbase+221u*4u);
        hs_b=(int16_t)vram16(v,hsbase+221u*4u+2u);
        ++s_uninitialized_scroll_lines;
    }
    int camera=level?unwrap(-hs_a,s_frame_fg_x):ram16(0xEE00);
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
    int by=bg_world?unwrap(v->vsram[1],s_frame_bg_y)+line:line+(v->vsram[1]&1023);
    int bg_width=0;
    if(bg_world) {
        int row=((by>>7)&15)*256;
        for(int col=0;col<128;++col)if(s_world_frame[0x8080+row+col])bg_width=(col+1)*128;
    }
    for(int x=0;x<width;++x) {
        int nx=x-origin,wx=level?nx+camera:nx-hs_a;
        int reference_bx=nx-hs_b,bx=reference_bx;
        if(level && s_frame_zone==0) {
            int margin=(width-nw)/2;
            bx=x-margin-hs_b+ehz_parallax(s_left+margin,line)-ehz_parallax(camera,line);
        }
        int background_x=bx;
        if(bg_width>0) { background_x%=bg_width;if(background_x<0)background_x+=bg_width; }
        uint16_t a=level?world_attr(s_world_frame,wx,wy,0):plane_attr(v,base_a,nx-hs_a,line+(v->vsram[0]&1023));
        uint16_t b=bg_world?world_attr(s_world_frame,background_x,by,1):plane_attr(v,base_b,bx,by);
        uint8_t ap=pattern_pixel(v,a,level?wx:nx-hs_a,level?wy:line+(v->vsram[0]&1023));
        uint8_t bp=pattern_pixel(v,b,bg_world?background_x:bx,by);
        if(level && (wx<0 || wx>=s_stage_width))ap=0;
        /* Menus have a centered foreground composition, with full-width
         * scenery underneath. Special-stage planes are repeating art. */
        if(!level && (nx<0 || nx>=nw))ap=0;
        s_priority[x]=(uint8_t)(((a&0x8000)&&ap)||((b&0x8000)&&bp));
        uint8_t p=ap&&(a&0x8000)?ap:bp&&(b&0x8000)?bp:ap?ap:bp;
        out[x]=p?palette[p]:backdrop;
        /* The half-pipe uses H32 plus shadow/highlight. Outside the original
         * track projection only planes are drawn, so plane priority alone
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
            if(bg_world) {
                int rx=reference_bx;
                if(bg_width>0){rx%=bg_width;if(rx<0)rx+=bg_width;}
                uint16_t expected=plane_attr(v,base_b,reference_bx,by);
                ++s_bg_checks;
                s_bg_errors+=attributes_differ(v,world_attr(s_world_frame,rx,by,1),expected,reference_bx,by);
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

void s2_video_command(int id, const char *json)
{
    (void)json;
    char reply[1536];
    snprintf(reply, sizeof reply,
        "{\"id\":%d,\"enabled\":%d,\"width\":%d,\"view_left\":%d,\"camera\":%d,"
        "\"native_x\":%d,\"stage_width\":%d,\"frames\":%u,\"fallback_frames\":%u,"
        "\"terrain_checks\":%u,\"terrain_errors\":%u,\"background_checks\":%u,\"background_errors\":%u,\"background_unstreamed\":%u,"
        "\"sprites\":%u,\"scene\":%d,\"spawned\":%u,\"pool_pressure\":%u,\"native_uninitialized_scroll_lines\":%u,"
        "\"native_unstreamed_terrain_samples\":%u,\"first_bad_x\":%d,\"first_bad_y\":%d,\"first_bad_attr\":%u,\"first_bad_expected\":%u,\"first_bad_world_y\":%d,\"first_bad_hscroll\":%d,"
        "\"scene_match_misses\":%u,\"scene_held_frames\":%u,\"main_cpu_divisor\":%u,"
        "\"tick_samples\":%u,\"tick_updates\":%u,\"tick_lag\":%u,\"tick_multi\":%u,\"publication_lag\":%u}",
        id, enabled(), s_width, s_left, s_camera, s_native_x, s_stage_width,
        s_frames, s_fallbacks, s_terrain_checks, s_terrain_errors,s_bg_checks,s_bg_errors,s_bg_unstreamed,
        s_display?s_display->count:0,s_display?s_display->scene:-1,s_spawned,s_pool_pressure,s_uninitialized_scroll_lines,
        s_terrain_unstreamed,s_bad_x,s_bad_y,s_bad_attr,s_bad_expected,s_bad_world_y,s_bad_hscroll,
        s_scene_misses,s_scene_holds,s2_video_main_cpu_divisor(),
        s_tick_samples,s_tick_updates,s_tick_lag,s_tick_multi,s_publication_lag);
    cmd_send_response(reply);
}
void s2_video_actor_origin(const GVDP *v, int line, int width, int *left, int *top)
{
    int mode=v->reg[11]&3, row=mode==0?0:mode==2?line&~7:line;
    if (!g_ram[0xFE10] && row>=222) row=221;
    unsigned base=(v->reg[13]&63u)<<10;
    int camera=unwrap(-(int16_t)vram16(v,base+row*4),ram16(0xEE60));
    *left=enabled()?view_left(camera,width):camera-(width-320)/2;
    *top=unwrap(v->vsram[0],ram16(0xEE64));
}
int s2_video_actor_pixel_visible(const GVDP *v, int wx, int wy, int high, const unsigned char *world)
{
    if (high) return 1;
    uint16_t attr=world_attr(world,wx,wy,0);
    return !(attr&0x8000) || !pattern_pixel(v,attr,wx,wy);
}
extern void s2_options_overlay(const GVDP *, int, uint32_t *, int);
const GameVideo sonic2_video = { configure, enabled, width, scanline, s2_options_overlay };
