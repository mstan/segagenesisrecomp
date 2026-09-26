#include "sonic1_video.h"
#include "genesis_runtime.h"
#include "video/genesis_vdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t g_ram[65536];
uint8_t g_rom[0x400000];
M68KState g_cpu;
void m68k_write8(uint32_t a,uint8_t v) { g_ram[a&65535]=v; }
void m68k_write16(uint32_t a,uint16_t v) { m68k_write8(a,v>>8);m68k_write8(a+1,v); }
void m68k_write32(uint32_t a,uint32_t v) { m68k_write16(a,v>>16);m68k_write16(a+2,v); }
/* Host-side writes from the video adapter (glue_poke*, genesis_host_mem.h). */
void glue_poke8(uint32_t a,uint8_t v){m68k_write8(a,v);}
void glue_poke16(uint32_t a,uint16_t v){m68k_write16(a,v);}
void glue_poke32(uint32_t a,uint32_t v){m68k_write32(a,v);}
static unsigned ring_calls;
void recomp_call_addr(uint32_t a) { if(a==0x9C76)++ring_calls; }
static char reply[1024];
void cmd_send_response(const char *json) { snprintf(reply,sizeof reply,"%s",json); }
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static void word(uint8_t *p, int a, int v) { p[a] = (uint8_t)(v >> 8); p[a+1] = (uint8_t)v; }

static void read_capture(const char *path,uint8_t *out,size_t size)
{
    FILE *f=fopen(path,"rb");CHECK(f);
    CHECK(fread(out,1,size,f)==size);CHECK(fgetc(f)==EOF);fclose(f);
}

int main(int argc,char **argv)
{
    static GVDP v, before;
    static uint8_t ram_before[65536];
    uint32_t native[320], out[8192];
    for (int i = 0; i < 320; ++i) native[i] = 0xFF123400u + i;
    g_ram[0xF600] = 12; g_ram[0xD000] = 1;
    for (int i = 0; i < 5; ++i) g_ram[0xA400+i] = 1;
    for (int i = 0; i < 4; ++i) word(g_ram, 0xB000+i*2, 1);
    v.reg[1] = 64; v.reg[2] = 0x30; v.reg[4] = 7;
    v.reg[5] = 0x50; v.reg[12] = 1; v.reg[13] = 0x3F; v.reg[16] = 1;
    v.cram[1] = 0xE; memset(v.vram+32, 0x11, 32);
    /* Optional offline replay of GHZ RAM/VRAM captures. No game is launched:
     * use the captured VBlank scroll buffers and standard Sonic plane setup.
     * This tests layout reconstruction, not sprite timing or live raster IRQs. */
    if(argc==4 && !strcmp(argv[1],"--replay")) {
        read_capture(argv[2],g_ram,sizeof g_ram);
        read_capture(argv[3],v.vram,sizeof v.vram);
        v.reg[11]=3;
        v.vsram[0]=(uint16_t)((g_ram[0xF616]<<8)|g_ram[0xF617]);
        v.vsram[1]=(uint16_t)((g_ram[0xF618]<<8)|g_ram[0xF619]);
        CHECK(sonic1_video.configure("32:9"));
        for(int y=0;y<224;++y)sonic1_video.scanline(&v,y,native,320,out,796);
        s1_video_command(1,NULL);puts(reply);
        return strstr(reply,"\"terrain_errors\":0,") &&
               strstr(reply,"\"background_errors\":0,") ? 0:1;
    }
    CHECK(sonic1_video.configure("fit"));
    CHECK(sonic1_video.width(320,224,320,224) == 320);
    CHECK(sonic1_video.width(4000,500,320,224) == 1792);
    CHECK(sonic1_video.width(100,1000,320,224) == 320);
    CHECK(sonic1_video.configure("32:9"));
    CHECK(sonic1_video.width(640,448,320,224) == 796);
    CHECK(sonic1_video.configure("64:9"));
    CHECK(sonic1_video.width(640,448,320,224) == 1593);
    CHECK(!sonic1_video.configure("32:0")); CHECK(!sonic1_video.configure("nan:9"));
    CHECK(!sonic1_video.configure("32:9junk")); CHECK(!sonic1_video.configure("inf:1"));
    CHECK(sonic1_video.configure("stage"));
    CHECK(sonic1_video.width(640,448,320,224) == 1280);
    before = v; memcpy(ram_before,g_ram,sizeof g_ram);
    sonic1_video.scanline(&v,0,native,320,out,796);
    CHECK(!memcmp(out,native,sizeof native));
    CHECK(out[400] == 0xFFFF0000u); /* terrain beyond the native viewport */
    CHECK(!memcmp(&before,&v,sizeof v)); CHECK(!memcmp(ram_before,g_ram,sizeof g_ram));
    word(g_ram,0xFF10,500); word(v.vram,0xFC00,(uint16_t)-500);
    sonic1_video.scanline(&v,0,native,320,out,796);
    CHECK(!memcmp(out+238,native,sizeof native)); /* centered during scrolling */
    CHECK(out[10] == 0xFFFF0000u); CHECK(out[795] == 0xFFFF0000u);
    g_ram[0xF600] = 4;
    sonic1_video.scanline(&v,0,native,320,out,8192);
    CHECK(!memcmp(out+(8192-320)/2,native,sizeof native));
    CHECK(out[0] == 0xFF000000u); CHECK(out[8191] == 0xFF000000u);
    /* A scene change does not collapse stage mode back to the window aspect. */
    CHECK(sonic1_video.width(640,448,320,224)==1280);

    /* Title scenery spans the window, stays through an outgoing fade, and
     * does not leak into level select after its background plane is cleared. */
    g_ram[0xD040]=0x0E;g_ram[0xD000]=0;g_ram[0xA440]=1;
    word(v.vram,0xE000,1);
    sonic1_video.scanline(&v,0,native,320,out,796);
    CHECK(out[0]==0xFFFF0000u);CHECK(out[795]==0xFFFF0000u);
    CHECK(!memcmp(out+238,native,sizeof native));
    g_ram[0xF600]=12;
    sonic1_video.scanline(&v,0,native,320,out,796);
    CHECK(out[0]==0xFFFF0000u);
    g_ram[0xF600]=4;word(v.vram,0xE000,0);
    sonic1_video.scanline(&v,0,native,320,out,796);
    CHECK(out[0]==0xFF000000u);
    g_ram[0xD000]=1;g_ram[0xD040]=0;g_ram[0xA440]=0;

    /* Build a frame-aligned host list: one screen HUD and one world sprite
     * beyond the native screen and SAT's representable coordinate range. */
    g_ram[0xF600]=12;
    word(g_ram,0xF700,500);word(g_ram,0xFF10,500);
    word(v.vram,0xFC00,(uint16_t)-500);
    CHECK(sonic1_video.configure("64:9"));
    CHECK(sonic1_video.width(640,448,320,224)==1593);
    word(g_rom,0x1000,2);g_rom[0x1002]=1;
    g_rom[0x1003]=0;g_rom[0x1004]=0;word(g_rom,0x1005,2);g_rom[0x1007]=0;
    g_ram[0xD040]=0x21;word(g_ram,0xD048,144);word(g_ram,0xD04A,128);
    m68k_write32(0xD044,0x1000);
    g_ram[0xD800]=0x26;g_ram[0xD801]=4;g_ram[0xD819]=8;
    word(g_ram,0xD808,1200);word(g_ram,0xD80C,0);m68k_write32(0xD804,0x1000);
    word(g_ram,0xAC00,4);word(g_ram,0xAC02,0xD040);word(g_ram,0xAC04,0xD800);
    v.cram[2]=0xE00;memset(v.vram+64,0x22,32);
    CHECK(!s1_video_hook(0xD65E));CHECK(!s1_video_hook(0xD746));
    CHECK(g_ram[0xD801]&128); /* extended visibility reaches object behavior */
    memcpy(v.vram+((v.reg[5]&127u)<<9),g_ram+0xF800,640);
    sonic1_video.scanline(&v,0,native,320,out,1593);
    CHECK(out[16]==0xFF0000FFu); /* left anchored HUD, not native_x+16 */
    CHECK(out[1356]==0xFF0000FFu); /* world sprite at 1200 - (-156) */
    s1_video_command(1,NULL);CHECK(strstr(reply,"\"sprites\":2"));
    /* Mid-frame publications cannot overwrite the selected display list. */
    word(g_ram,0xD048,200);
    for(int n=0;n<3;++n) {s1_video_hook(0xD65E);s1_video_hook(0xD746);}
    sonic1_video.scanline(&v,1,native,320,out,1593);
    CHECK(out[16]==0xFF0000FFu);
    /* Widened culling allows both margins, rejects beyond the actual view. */
    g_cpu.A[0]=0xFFD800;
    CHECK(s1_video_hook(0xD87E));CHECK(g_cpu.D[0]==0);
    word(g_ram,0xD808,1600);
    CHECK(s1_video_hook(0xD87E));CHECK(g_cpu.D[0]==1);
    g_cpu.D[0]=(uint16_t)(1200-((500-128)&0xFF80));
    CHECK(!s1_video_hook(0x9C36));CHECK((g_cpu.D[0]&65535)==320);

    /* Full-layout loader: rings live on the host, distant real objects still
     * instantiate. Ring state uses the original group's collected-bit byte. */
    memset(g_ram+0xD800,0,0x1800);memset(g_ram+0xFC00,0,0xC0);
    word(g_rom,0x6B000,0x100);
    word(g_rom,0x6B100,100);word(g_rom,0x6B102,80);g_rom[0x6B104]=0xA5;g_rom[0x6B105]=2;
    word(g_rom,0x6B106,1100);word(g_rom,0x6B108,80);g_rom[0x6B10A]=0xA6;
    word(g_rom,0x6B10C,65535);
    word(g_ram,0xD008,100);word(g_ram,0xD00C,80);g_ram[0xD016]=19;
    g_ram[0xF76C]=0;CHECK(!s1_video_hook(0xD8DA));g_ram[0xF76C]=2;
    CHECK(s1_video_hook(0xD980));
    CHECK(g_ram[0xD800]==0x26);CHECK(g_ram[0xD840]==0);
    CHECK(g_ram[0xD823]==2);CHECK(g_ram[0xFC04]&128);
    CHECK(!s1_video_hook(0x1ADE4));CHECK(ring_calls==1);CHECK(g_ram[0xFC03]&1);
    CHECK(!s1_video_hook(0x1ADE4));CHECK(ring_calls==1); /* no duplicate collection */
    CHECK(s1_video_hook(0xD980));CHECK(g_ram[0xD840]==0); /* no duplicate spawn */

    /* MZ's first grassy platform is at X=640. A smooth camera at X=1
     * previously spawned it, then the 128px-rounded culler deleted it and
     * the host loader remembered it forever. Load and retain the SAME cells. */
    CHECK(sonic1_video.configure("10:7"));sonic1_video.width(640,448,320,224);
    memset(g_ram+0xD800,0,0x1800);memset(g_ram+0xFC00,0,0xC0);
    word(g_rom,0x6B100,640);word(g_rom,0x6B102,680);g_rom[0x6B104]=0x2F;g_rom[0x6B105]=1;
    word(g_rom,0x6B106,65535);
    word(g_ram,0xF700,1);g_ram[0xF76C]=0;s1_video_hook(0xD8DA);g_ram[0xF76C]=2;
    CHECK(s1_video_hook(0xD980));CHECK(g_ram[0xD800]==0);
    word(g_ram,0xF700,128);CHECK(s1_video_hook(0xD980));CHECK(g_ram[0xD800]==0x2F);
    g_cpu.D[0]=640;CHECK(!s1_video_hook(0xB0DC));CHECK((g_cpu.D[0]&65535)==320);
    /* The same invariant holds with a non-128-aligned wide canvas. */
    CHECK(sonic1_video.configure("32:9"));sonic1_video.width(796,224,320,224);
    memset(g_ram+0xD800,0,0x1800);word(g_rom,0x6B100,1024);word(g_ram,0xF700,238);
    g_ram[0xF76C]=0;s1_video_hook(0xD8DA);g_ram[0xF76C]=2;
    CHECK(s1_video_hook(0xD980));CHECK(g_ram[0xD800]==0);
    word(g_ram,0xF700,274);CHECK(s1_video_hook(0xD980));CHECK(g_ram[0xD800]==0x2F);
    g_cpu.D[0]=1024-128;CHECK(!s1_video_hook(0xB0DC));CHECK((g_cpu.D[0]&65535)==320);
    CHECK(sonic1_video.configure("64:9"));sonic1_video.width(1593,224,320,224);

    /* Special-stage layout: draw a block far beyond the old 320px window,
     * using the game's sine table and live block frame/palette descriptors. */
    memset(g_ram,0,0x4000);memset(g_ram+0xAC00,0,0x400);
    g_ram[0xF600]=16;g_ram[0xD000]=9;
    word(g_ram,0xF700,588);word(g_ram,0xF704,588);
    word(g_rom,0x29F2,0);word(g_rom,0x29F2+128,256);
    m68k_write32(0x4008,0x1000);word(g_ram,0x400C,0);word(g_ram,0x400E,0);
    g_ram[32*128+50]=1;
    CHECK(!s1_video_hook(0xD65E));CHECK(!s1_video_hook(0xD746));
    CHECK(!s1_video_hook(0x1B156));CHECK(!s1_video_hook(0x1B286));
    memcpy(v.vram+((v.reg[5]&127u)<<9),g_ram+0xF800,640);
    sonic1_video.scanline(&v,0,native,320,out,1593);
    sonic1_video.scanline(&v,112,native,320,out,1593);
    CHECK(out[1228]==0xFF0000FFu);
    s1_video_command(1,NULL);CHECK(strstr(reply,"\"scene\":2"));

    CHECK(sonic1_video.configure("off")); CHECK(!sonic1_video.enabled());
    g_cpu.D[0]=123;CHECK(!s1_video_hook(0x9C36));CHECK(g_cpu.D[0]==123);
    /* A clamped wide camera must not make GHZ's sky slide when the native
     * camera starts following Sonic. A striped BG makes sub-tile drift visible. */
    memset(g_ram,0,sizeof g_ram);
    g_ram[0xF600]=12;g_ram[0xD000]=1;
    for(int i=0;i<5;++i)g_ram[0xA400+i]=1;
    g_ram[0xA440]=2;
    for(int i=0;i<256;++i)word(g_ram,512+i*2,1);
    for(int i=0;i<4;++i)word(g_ram,0xB008+i*2,0x8001);
    for(int i=0;i<32;++i)v.vram[32+i]=(uint8_t)(i&1?0x22:0x11);
    v.vsram[0]=v.vsram[1]=0;
    CHECK(sonic1_video.configure("32:9"));sonic1_video.width(796,224,320,224);
    s1_video_hook(0xD65E);s1_video_hook(0xD746);
    memcpy(v.vram+((v.reg[5]&127u)<<9),g_ram+0xF800,640);
    word(v.vram,0xFC00,0);word(v.vram,0xFC02,0);
    sonic1_video.scanline(&v,0,native,320,out,796);
    uint32_t sky[32];memcpy(sky,out,sizeof sky);
    word(g_ram,0xF700,32);word(g_ram,0xFF10,32);
    word(v.vram,0xFC00,(uint16_t)-32);word(v.vram,0xFC02,(uint16_t)-12);
    sonic1_video.scanline(&v,0,native,320,out,796);
    CHECK(!memcmp(sky,out,sizeof sky));
    puts("Sonic 1 custom video: geometry, terrain, anchored HUD, wide sprites, culling, mode continuity and native opt-out passed");
    return 0;
}
