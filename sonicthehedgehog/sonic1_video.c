/* Experimental Sonic 1 REV00 world renderer.
 *
 * Terrain addressing follows s1disasm GetBlockData ($6BD6):
 * eight interlaced FG/BG layout rows -> 256px chunks -> 16px blocks ->
 * live 8px VRAM patterns. Opt-in host scene hooks expand object visibility,
 * ring handling and sprite composition beyond the original hardware limits.
 * The native renderer and original instruction path remain the default.
 */
#include "sonic1_video.h"
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
static unsigned s_frames, s_fallbacks, s_terrain_checks, s_terrain_errors;
static unsigned s_bg_checks, s_bg_errors, s_bg_unstreamed;
static int s_title_world;
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
    return (mode == 8 || mode == 12 || mode == 16) && g_ram[0xD000] == 1;
}
static int stage_width(void)
{
    int last = 1;
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 64; ++col)
            if (g_ram[0xA400 + row * 128 + col] && col + 1 > last) last = col + 1;
    return last * 256;
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
static uint16_t world_attr(int wx, int wy, int background)
{
    if (wx < 0 || wx >= 16384) return 0;
    unsigned layout = (background ? 0xA440u : 0xA400u) + ((unsigned)wy / 256u & 7u) * 128u + (unsigned)wx / 256u;
    unsigned chunk = g_ram[layout];
    uint16_t block = 0;
    if (chunk) {
        unsigned address = ((chunk - 1u) & 127u) * 512u +
                           ((unsigned)wy & 240u) * 2u + ((unsigned)wx & 240u) / 8u;
        block = ram16(address);
    }
    int tx = (wx >> 3) & 1, ty = (wy >> 3) & 1;
    if (block & 0x0800) tx ^= 1;
    if (block & 0x1000) ty ^= 1;
    return ram16(0xB000u + (block & 1023u) * 8u + (unsigned)(ty * 2 + tx) * 2u) ^ (block & 0x1800u);
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
} SceneFrame;
static SceneFrame s_build, s_history[3], s_display_frame;
static const SceneFrame *s_display;
static unsigned s_serial, s_scene_tick, s_spawned, s_pool_pressure, s_ring_collected;
static unsigned s_visible_objects[128], s_visible_count;
typedef struct { unsigned address; uint16_t x, y; uint8_t id, subtype, state, loaded; } Placement;
static Placement s_placements[SCENE_PLACEMENTS];
static unsigned s_placement_count, s_placement_base;
static int s_loader_active, s_rings_adopted;
typedef struct { int x, y; unsigned tick; } RingSpark;
static RingSpark s_sparks[64];
static unsigned s_spark_next;

static uint8_t scene_read8(unsigned a)
{
    a &= 0xFFFFFFu;
    return a < 0x80000u ? g_rom[a] : a >= 0xFF0000u ? g_ram[a & 65535u] : 0;
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
    if (was_enabled == enabled()) return;
    s_rings_adopted=0;
    for (unsigned i=0;i<s_placement_count;++i)
        if (s_placements[i].id==0x25) s_placements[i].loaded=0;
}
static void add_mapping(unsigned map, unsigned frame, unsigned gfx, unsigned flags,
                        int x, int y, int hud, int static_mapping)
{
    unsigned p=map;
    int count=1;
    if (!static_mapping) {
        p+=(int16_t)scene_read16(p+(frame&127u)*2u);
        count=scene_read8(p++);
    }
    for (int n=0;n<count && s_build.count<SCENE_SPRITES;++n,p+=5) {
        int dy=(int8_t)scene_read8(p), size=scene_read8(p+1)&15;
        int dx=(int8_t)scene_read8(p+4);
        uint16_t attr=(uint16_t)(scene_read16(p+2)+gfx);
        if (flags&1) { dx=-dx-(((size>>2)&3)+1)*8; attr^=0x800; }
        if (flags&2) { dy=-dy-((size&3)+1)*8; attr^=0x1000; }
        if (y+dy>=224 || y+dy+((size&3)+1)*8<=0) continue;
        SceneSprite *s=&s_build.sprites[s_build.count++];
        s->x=x+dx; s->y=y+dy; s->attr=attr; s->size=(uint8_t)size; s->hud=(uint8_t)hud;
    }
}
static void load_placements(void)
{
    unsigned index=((ram16(0xFE10)&0xFF00u)>>4)|((ram16(0xFE10)&3u)<<2);
    unsigned base=0x6B000u+(int16_t)scene_read16(0x6B000u+index);
    if (base==s_placement_base && s_placement_count) return;
    s_placement_count=0; s_placement_base=base;
    unsigned state=1;
    for (unsigned p=base;p+6<=0x80000u && s_placement_count<SCENE_PLACEMENTS;p+=6) {
        unsigned x=scene_read16(p), id=scene_read8(p+4);
        if (x==65535u) break;
        Placement *q=&s_placements[s_placement_count++];
        q->address=p; q->x=(uint16_t)x; q->y=scene_read16(p+2);
        q->id=(uint8_t)(id&127); q->subtype=scene_read8(p+5);
        q->state=(uint8_t)((id&128) ? state++ : 0); q->loaded=0;
    }
}
static void adopt_rings(void)
{
    if (s_rings_adopted || !enabled()) return;
    /* Existing normal rings move to the host list when enabling mid-level.
     * Collected bits stay in the ROM's respawn table (and in save states).
     * Lost rings (object $37) continue using their original physics. */
    for (unsigned o=0xD800;o<0xF000;o+=64)
        if (g_ram[o]==0x25)
            for (int k=0;k<64;k+=4) write32(o+(unsigned)k,0);
    s_rings_adopted=1;
}
static int placement_alive(const Placement *p)
{
    for (unsigned o=0xD800;o<0xF000;o+=64) {
        if (!g_ram[o]) continue;
        if (p->state && g_ram[o+0x23]==p->state) return 1;
        if (!p->state && g_ram[o]==p->id && ram16(o+8)==p->x) return 1;
    }
    return 0;
}
static void spawn_scene(void)
{
    load_placements(); adopt_rings(); s_loader_active=1;
    int camera=ram16(0xF700), w=enabled()?s_requested_width:320;
    int lo,hi;activation_bounds(camera,w,&lo,&hi);
    int player=ram16(0xD008);
    s_pool_pressure=0;
    for (unsigned i=0;i<s_placement_count;++i) {
        Placement *p=&s_placements[i];
        if ((int)p->x<lo || (int)p->x>=hi) p->loaded=0;
        else if (!p->loaded && placement_alive(p)) p->loaded=1;
    }
    /* Fill closest to the player first, reserving space for collision-relevant
     * objects before remote scenery. Host-owned rings consume no object slots. */
    for (unsigned pass=0;pass<s_placement_count;++pass) {
        int best=-1, distance=INT_MAX;
        for (unsigned i=0;i<s_placement_count;++i) {
            Placement *p=&s_placements[i];
            if (p->loaded || (enabled() && p->id==0x25) || (int)p->x<lo || (int)p->x>=hi) continue;
            if (p->state && (g_ram[0xFC02+p->state]&128)) { p->loaded=1; continue; }
            int d=abs((int)p->x-player);
            if (d<distance) { best=(int)i; distance=d; }
        }
        if (best<0) break;
        Placement *p=&s_placements[best];
        unsigned o;
        for (o=0xD800;o<0xF000 && g_ram[o];o+=64) {}
        if (o==0xF000) { ++s_pool_pressure; break; }
        write16(o+8,p->x); write16(o+12,p->y&4095);
        unsigned flip=(p->y>>14)&3;
        write8(o+1,flip); write8(o+0x22,flip);
        write8(o+0x23,p->state); write8(o+0x28,p->subtype);
        if (p->state) write8(0xFC02+p->state,g_ram[0xFC02+p->state]|128);
        write8(o,p->id); p->loaded=1; ++s_spawned;
    }
    /* Keep original loader cursors coherent for tools/save-state resume. */
    unsigned r=0,l=0,rs=1,ls=1;
    while (r<s_placement_count && s_placements[r].x<(unsigned)(camera+640)) { rs+=s_placements[r].state!=0; ++r; }
    while (l<s_placement_count && (int)s_placements[l].x<camera-128) { ls+=s_placements[l].state!=0; ++l; }
    write32(0xF770,s_placement_base+r*6); write32(0xF774,s_placement_base+l*6);
    write8(0xFC00,rs); write8(0xFC01,ls); write16(0xF76E,camera&0xFF80);
}
static void ring_position(const Placement *p,int i,int *x,int *y)
{
    static const int8_t spacing[16][2]={{16,0},{24,0},{32,0},{0,16},{0,24},{0,32},
        {16,16},{24,24},{32,32},{-16,16},{-24,24},{-32,32},{16,8},{24,16},{-16,8},{-24,16}};
    *x=p->x+spacing[p->subtype>>4][0]*i;
    *y=(p->y&4095)+spacing[p->subtype>>4][1]*i;
}
static void collect_host_rings(void)
{
    if (!s_placement_count || ram16(0xD030)>=90) return;
    int px=ram16(0xD008), py=ram16(0xD00C), height=(int)g_ram[0xD016]-3;
    /* ReactToItem's ducking box (fr_Duck=$39 in REV00). */
    if (g_ram[0xD01A]==0x39) { py+=12-height+10; height=10; }
    for (unsigned n=0;n<s_placement_count;++n) {
        Placement *p=&s_placements[n];
        if (p->id!=0x25) continue;
        int count=(p->subtype&7)+1; if(count>7)count=7;
        for(int i=0;i<count;++i) {
            unsigned bit=1u<<i, state=0xFC02+p->state;
            if(g_ram[state]&bit)continue;
            int x,y; ring_position(p,i,&x,&y);
            if(abs(px-x)>14 || abs(py-y)>height+6)continue;
            write8(state,g_ram[state]|bit);
            /* Keep the game's score/life/SFX logic, including 100/200-ring
             * rewards. This call consumes no synthetic guest return slot. */
            M68KState saved=g_cpu;
            recomp_call_addr(0x9C76u);
            g_cpu=saved;
            s_sparks[s_spark_next++%64]=(RingSpark){x,y,s_scene_tick+1};
            ++s_ring_collected;
            return; /* original ReactToItem picks at most one ring per tick */
        }
    }
}
static void capture_objects(void)
{
    s_build.count=0; s_visible_count=0; ++s_scene_tick;
    s_build.scene=gameplay()?1:g_ram[0xD000]==9?2:0;
    int cam=ram16(0xF700), left=gameplay()?view_left(cam,s_requested_width):cam-(s_requested_width-320)/2;
    for(unsigned pri=0;pri<8;++pri) {
        unsigned queue=0xAC00+pri*128, bytes=ram16(queue);
        if(bytes>126)bytes=126;
        for(unsigned k=2;k<=bytes;k+=2) {
            unsigned o=ram16(queue+k);
            if(o<0xD000 || o>=0xF000 || !g_ram[o])continue;
            unsigned flags=g_ram[o+1], space=flags&12;
            int x,y,hud=g_ram[o]==0x21;
            if(space) {
                unsigned scroll=space==4?0xF700:space==8?0xF708:0xF718;
                x=(int16_t)(ram16(o+8)-ram16(scroll));
                y=(int16_t)(ram16(o+12)-ram16(scroll+4));
                int radius=g_ram[o+0x19];
                if(x+radius<left-cam || x-radius>=left-cam+s_requested_width)continue;
                if(flags&16) { int h=g_ram[o+0x16];if(y+h<0 || y-h>=224)continue; }
                else if(y<-32 || y>=256)continue;
                if(s_visible_count<128)s_visible_objects[s_visible_count++]=o;
            } else { x=(int16_t)ram16(o+8)-128; y=(int16_t)ram16(o+10)-128; }
            add_mapping(scene_read32(0xFF0000u+o+4),g_ram[o+0x1A],ram16(o+2),flags,x,y,hud,(flags&32)!=0);
        }
        if(pri==2 && gameplay()) {
            for(unsigned n=0;n<s_placement_count;++n) {
                Placement *p=&s_placements[n]; if(p->id!=0x25)continue;
                int count=(p->subtype&7)+1;if(count>7)count=7;
                for(int i=0;i<count;++i) {
                    if(g_ram[0xFC02+p->state]&(1u<<i))continue;
                    int x,y;ring_position(p,i,&x,&y);
                    if(x<left-16 || x>=left+s_requested_width+16)continue;
                    add_mapping(0x9F92,g_ram[0xFEC3]&3,0x27B2,0,x-cam,y-ram16(0xF704),0,0);
                }
            }
            for(unsigned n=0;n<64;++n) {
                RingSpark *p=&s_sparks[n];unsigned age=s_scene_tick-p->tick;
                if(p->tick && age<24)add_mapping(0x9F92,4+age/6,0x27B2,0,p->x-cam,p->y-ram16(0xF704),0,0);
            }
        }
    }
}
static void capture_special(void)
{
    s_build.scene=2;
    unsigned angle=g_ram[0xF780]&252u;
    int sn=(int16_t)scene_read16(0x29F2+angle*2), cs=(int16_t)scene_read16(0x29F2+((angle+64)&255u)*2);
    int cx=ram16(0xF700), cy=ram16(0xF704), margin=(s_requested_width-320)/2;
    for(int row=0;row<128;++row)for(int col=0;col<128;++col) {
        unsigned id=g_ram[row*128+col];if(!id || id>0x4E)continue;
        int dx=col*24-cx-180, dy=row*24-cy-180;
        int x=((cs*dx-sn*dy)>>8)+160, y=((sn*dx+cs*dy)>>8)+112;
        if(x<-margin-32 || x>=320+margin+32 || y<-32 || y>=256)continue;
        unsigned type=0x4000+id*8;
        add_mapping(scene_read32(0xFF0000u+type),ram16(type+4),ram16(type+6),0,x,y,0,0);
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
    memcpy(dst->sat,s_build.sat,640);
    memcpy(dst->sprites,s_build.sprites,s_build.count*sizeof(SceneSprite));
}
int s1_video_hook(uint32_t pc)
{
    if(pc==0xD8DA && !g_ram[0xF76C]) {
        s_placement_base=s_placement_count=0;s_loader_active=s_rings_adopted=0;
        memset(s_sparks,0,sizeof s_sparks);
    }
    if(pc==0xD980 && gameplay() && (enabled() || s_loader_active)) { spawn_scene();return 1; }
    if(!enabled())return 0;
    switch(pc) {
    case 0xD65E:capture_objects();return 0;
    case 0xD746:case 0xD74E:case 0x1B286:case 0x1B28E:publish_sprites();return 0;
    case 0x1B156:capture_special();return 0;
    case 0x1ADE4:if(gameplay())collect_host_rings();return 0;
    case 0xD8DA:return 0;
    case 0xD87E:case 0xD8A6: {
        if(!gameplay())return 0;
        unsigned o=g_cpu.A[0]&65535u;
        int x=ram16(o+8),y=(int16_t)(ram16(o+12)-ram16(0xF704));
        int r=pc==0xD8A6?g_ram[o+0x19]:0,left=view_left(ram16(0xF700),s_requested_width);
        g_cpu.D[0]=(x+r<left || x-r>=left+s_requested_width || y<0 || y>=224)?1:0;
        g_cpu.SR=(uint16_t)((g_cpu.SR&~15u)|(g_cpu.D[0]?0:4));
        return 1;
    }
    default:
        if(gameplay()) {
            /* All remaining configured sites are out_of_range's CMP. D0
             * holds a 128px-rounded offset from the original camera window.
             * Rebase it to our view; the following native CMP/BHI still owns
             * flags and the object's actual deletion/remember-state path. */
            int x=(uint16_t)((uint16_t)g_cpu.D[0]+((ram16(0xF700)-128)&0xFF80));
            int lo,hi;activation_bounds(ram16(0xF700),s_requested_width,&lo,&hi);
            unsigned outside=x<lo || x>=hi;
            g_cpu.D[0]=(g_cpu.D[0]&0xFFFF0000u)|(outside?641u:320u);
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
    /* DMA uploads all 640 bytes. Compare full bytes, not a lossy hash. */
    for(unsigned n=0;n<3;++n) {
        const SceneFrame *p=&s_history[n];
        if(!p->serial || (s_display && p->serial<s_display->serial))continue;
        int matches=1;
        for(unsigned k=0;k<640;++k)
            if(p->sat[k]!=v->vram[(sat+k)&65535u]) {matches=0;break;}
        if(matches)s_display=p;
    }
    /* The game fiber can publish another list between scanlines. Keep the
     * selected display immutable until the next frame's VBlank selection. */
    if(s_display) {
        s_display_frame.count=s_display->count;s_display_frame.scene=s_display->scene;
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
        if(line<s->y || line>=s->y+ch*8)continue;
        int base=s->x+(s->hud?0:origin),iy=line-s->y;
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
    /* Empty chunks can inherit stale flip bits from DrawBlock's A0.
     * Compare rendered pixels/priority, not irrelevant empty-tile flags. */
    for(int i=0;i<8;++i) {
        uint8_t ap=pattern_pixel(v,a,x+i,y),bp=pattern_pixel(v,b,x+i,y);
        if(ap!=bp || (ap && ((a^b)&0x8000)))return 1;
    }
    return 0;
}
/* Deform_GHZ's three scroll bands. Reproject the ORIGINAL and WIDE camera
 * through the same integer math, rather than moving a parallax layer by the
 * foreground's full edge-clamp correction. The latter made scenery slide
 * backwards while Sonic moved inside a stationary wide viewport at spawn. */
static int ghz_parallax(int camera,int y)
{
    if(y<112)return camera*3/8;
    int distant=camera/2;
    if(y<152)return distant;
    int step=(camera-512-distant)*256/104;
    return distant+((step*(y-152))>>8);
}
static void scanline(const GVDP *v, int line, const uint32_t *native, int nw,
                     uint32_t *out, int width)
{
    uint32_t palette[64];
    for(int i=0;i<64;++i)palette[i]=genesis_dac_cram_to_argb(v->cram[i],GENESIS_DAC_NORMAL);
    uint32_t backdrop=palette[v->reg[7]&63];
    if(line==0) {
        s_width=width;s_requested_width=width;
        s_terrain_checks=s_terrain_errors=s_bg_checks=s_bg_errors=s_bg_unstreamed=0;
        if(width>s_priority_capacity) {
            uint8_t *p=(uint8_t *)realloc(s_priority,(size_t)width);
            if(p){s_priority=p;s_priority_capacity=width;}
        }
        select_scene(v);
        /* Title objects/layout survive into the outgoing fade. Level select
         * retains those same RAM objects but clears Plane B: do not resurrect
         * the old title scenery behind its black menu. */
        s_title_world=0;
        if(g_ram[0xD040]==0x0E && g_ram[0xA440]) {
            unsigned base=(v->reg[4]&7u)<<13;
            for(unsigned k=0;k<4096;k+=2)
                if(vram16(v,base+k)&2047u) {s_title_world=1;break;}
        }
    }
    /* Display-off/fades fill the same full-width canvas, not black sidebars.
     * A rare unsupported mode also retains the selected output dimensions. */
    if(nw!=320 || !(v->reg[1]&64) || (v->reg[12]&14) || s_priority_capacity<width) {
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
    int level=gameplay(),special=s_display && s_display->scene==2;
    /* The next player object can exist while the title-card SAT is still
     * displayed. Do not expose the incoming level before its own display
     * list/planes reach VBlank. */
    if(s_display && s_display->scene!=1)level=0;
    int camera=level?unwrap(-hs_a,ram16(0xFF10)):ram16(0xF700);
    int camera_y=unwrap(v->vsram[0],ram16(0xFF14));
    if(line==0) {
        if(level)s_stage_width=stage_width();
        s_camera=camera;++s_frames;
        s_left=level?view_left(camera,width):camera-(width-nw)/2;
    }
    int origin=level?camera-s_left:(width-nw)/2;
    s_native_x=origin;
    unsigned base_a=(v->reg[2]&56u)<<10,base_b=(v->reg[4]&7u)<<13;
    int wy=camera_y+line;
    int bg_world=(level || s_title_world) && !special;
    int by=bg_world?unwrap(v->vsram[1],ram16(0xFF1C))+line:line+(v->vsram[1]&1023);
    int bg_width=0;
    if(bg_world) {
        int row=((by>>8)&7)*128;
        for(int col=0;col<64;++col)if(g_ram[0xA440+row+col])bg_width=(col+1)*256;
    }
    for(int x=0;x<width;++x) {
        int nx=x-origin,wx=level?s_left+x:nx-hs_a;
        int reference_bx=nx-hs_b,bx=reference_bx;
        if(level && g_ram[0xFE10]==0) {
            int margin=(width-nw)/2;
            bx=x-margin-hs_b+ghz_parallax(s_left+margin,by)-ghz_parallax(camera,by);
        }
        int background_x=bx;
        if(bg_width>0) { background_x%=bg_width;if(background_x<0)background_x+=bg_width; }
        uint16_t a=level?world_attr(wx,wy,0):plane_attr(v,base_a,nx-hs_a,line+(v->vsram[0]&1023));
        uint16_t b=bg_world?world_attr(background_x,by,1):plane_attr(v,base_b,bx,by);
        uint8_t ap=pattern_pixel(v,a,level?wx:nx-hs_a,level?wy:line+(v->vsram[0]&1023));
        uint8_t bp=pattern_pixel(v,b,bg_world?background_x:bx,by);
        if(level && (wx<0 || wx>=s_stage_width))ap=0;
        /* Menus have a centered foreground composition, with full-width
         * scenery underneath. Special-stage planes are repeating art. */
        if(!level && !special && (nx<0 || nx>=nw))ap=0;
        s_priority[x]=(uint8_t)(((a&0x8000)&&ap)||((b&0x8000)&&bp));
        uint8_t p=ap&&(a&0x8000)?ap:bp&&(b&0x8000)?bp:ap?ap:bp;
        out[x]=p?palette[p]:backdrop;
        if(nx>=0 && nx<nw && !(x&7)) {
            if(level) {
                uint16_t expected=plane_attr(v,base_a,nx-hs_a,line+(v->vsram[0]&1023));
                ++s_terrain_checks;
                s_terrain_errors+=attributes_differ(v,a,expected,wx,wy);
            }
            if(bg_world) {
                /* GHZ's perspective water can address outside the streamed
                 * 320px strip even in native mode, wrapping to stale cells.
                 * The enhanced world deliberately fills that missing data.
                 * Only compare against native cells the game actually keeps
                 * current, and report excluded samples separately. */
                int scroll=ram16(by<ram16(0xF7F0)?0xFF18:0xFF20)&~15;
                int lo=scroll<16?0:scroll-16;
                if(!g_ram[0xFE10] && (reference_bx<lo || reference_bx>=scroll+320))++s_bg_unstreamed;
                else {
                    /* Mapping diagnostic uses the original camera's sample;
                     * the displayed sample intentionally follows the new one. */
                    int rx=reference_bx;
                    if(bg_width>0){rx%=bg_width;if(rx<0)rx+=bg_width;}
                    uint16_t expected=plane_attr(v,base_b,reference_bx,by);
                    ++s_bg_checks;s_bg_errors+=attributes_differ(v,world_attr(rx,by,1),expected,reference_bx,by);
                }
            }
        }
    }
    if(!level && !special) {
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

void s1_video_command(int id, const char *json)
{
    (void)json;
    char reply[768];
    snprintf(reply, sizeof reply,
        "{\"id\":%d,\"enabled\":%d,\"width\":%d,\"view_left\":%d,\"camera\":%d,"
        "\"native_x\":%d,\"stage_width\":%d,\"frames\":%u,\"fallback_frames\":%u,"
        "\"terrain_checks\":%u,\"terrain_errors\":%u,\"background_checks\":%u,\"background_errors\":%u,\"background_unstreamed\":%u,"
        "\"sprites\":%u,\"scene\":%d,\"spawned\":%u,\"pool_pressure\":%u,\"host_rings_collected\":%u}",
        id, enabled(), s_width, s_left, s_camera, s_native_x, s_stage_width,
        s_frames, s_fallbacks, s_terrain_checks, s_terrain_errors,s_bg_checks,s_bg_errors,s_bg_unstreamed,
        s_display?s_display->count:0,s_display?s_display->scene:-1,s_spawned,s_pool_pressure,s_ring_collected);
    cmd_send_response(reply);
}
const GameVideo sonic1_video = { configure, enabled, width, scanline };
