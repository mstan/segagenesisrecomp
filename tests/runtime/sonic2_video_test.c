/* Exercise the actual per-game helpers, including shared spawn/cull cells. */
#include "../../sonicthehedgehog2/sonic2_video.c"

uint8_t g_ram[65536],g_rom[0x400000];
M68KState g_cpu;
void s2_options_overlay(const GVDP *v,int line,uint32_t *out,int width)
{ (void)v; (void)line; (void)out; (void)width; }
void m68k_write8(uint32_t a,uint8_t v){g_ram[a&65535]=v;}
void m68k_write16(uint32_t a,uint16_t v){m68k_write8(a,v>>8);m68k_write8(a+1,v);}
void m68k_write32(uint32_t a,uint32_t v){m68k_write16(a,v>>16);m68k_write16(a+2,v);}
void cmd_send_response(const char *json){(void)json;}
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static void word(uint8_t *p,unsigned a,unsigned v){p[a]=(uint8_t)(v>>8);p[a+1]=(uint8_t)v;}

int main(void)
{
    CHECK(!sonic2_video.enabled());CHECK(!sonic2_video.width(2560,720,320,224));
    g_ram[0xF600]=12;g_ram[0xB000]=1;
    for(int n=0;n<16;++n)g_ram[0x8000+n]=1;
    CHECK(stage_width()==2048);
    CHECK(sonic2_video.configure("fit"));
    CHECK(s2_video_main_cpu_divisor()==1);
    g_ram[0xF711]=1;CHECK(s2_video_main_cpu_divisor()==4);
    g_ram[0xFFD9]=1;CHECK(s2_video_main_cpu_divisor()==1);g_ram[0xFFD9]=0;
    g_ram[0xF600]=0x8C;CHECK(s2_video_main_cpu_divisor()==1);g_ram[0xF600]=12;
    g_ram[0xF600]=16;CHECK(s2_video_main_cpu_divisor()==1);g_ram[0xF600]=12;
    CHECK(sonic2_video.configure("off"));CHECK(s2_video_main_cpu_divisor()==1);
    CHECK(sonic2_video.configure("fit"));g_ram[0xF711]=0;
    CHECK(sonic2_video.width(4000,500,320,224)==1792);
    CHECK(sonic2_video.width(320,224,320,224)==320);
    CHECK(sonic2_video.configure("16:9"));CHECK(sonic2_video.width(1,1,320,224)==398);
    CHECK(sonic2_video.configure("21:9"));CHECK(sonic2_video.width(1,1,320,224)==523);
    CHECK(sonic2_video.configure("32:9"));CHECK(sonic2_video.width(1,1,320,224)==796);
    CHECK(sonic2_video.configure("64:9"));CHECK(sonic2_video.width(1,1,320,224)==1593);
    CHECK(sonic2_video.configure("stage"));CHECK(sonic2_video.width(1,1,320,224)==2048);
    CHECK(!sonic2_video.configure("32:0"));CHECK(!sonic2_video.configure("nan:9"));
    CHECK(!sonic2_video.configure("inf:1"));CHECK(!sonic2_video.configure("32:9junk"));
    CHECK(native_terrain_streamed(943,718));CHECK(!native_terrain_streamed(944,718));
    CHECK(native_terrain_streamed(991,763));CHECK(!native_terrain_streamed(992,763));
    CHECK(native_terrain_streamed(688,718));CHECK(!native_terrain_streamed(687,718));
    /* Interleaved 128px chunks, 16px blocks, independent block/tile flips. */
    word(g_ram,128,3);for(int n=0;n<4;++n)word(g_ram,0x9000+24+n*2,100+n);
    CHECK(world_attr(g_ram,0,0,0)==100);CHECK(world_attr(g_ram,8,8,0)==103);
    word(g_ram,128,3|0x400);CHECK(world_attr(g_ram,0,0,0)==(101^0x800));
    word(g_ram,128,3|0x800);CHECK(world_attr(g_ram,0,0,0)==(102^0x1000));
    word(g_ram,128,3|0xC00);CHECK(world_attr(g_ram,0,0,0)==(103^0x1800));
    g_ram[0x8080]=1;CHECK(world_attr(g_ram,0,0,1)==world_attr(g_ram,0,0,0));
    /* x=640 must not spawn a cell before the culler will retain it. */
    word(g_rom,0xE6800,0x100);word(g_rom,0xE6900,640);word(g_rom,0xE6902,0x8060);
    g_rom[0xE6904]=0x18;word(g_rom,0xE6906,1000);word(g_rom,0xE6908,0xE060);
    g_rom[0xE690A]=0x19;word(g_rom,0xE690C,65535);
    CHECK(sonic2_video.configure("10:7"));sonic2_video.width(1,1,320,224);
    s2_video_hook(0x17AA4);g_ram[0xF76C]=2;write16(0xEE00,1);
    CHECK(s2_video_hook(0x17B84)==1);CHECK(g_ram[0xB400]==0);
    write16(0xEE00,128);CHECK(s2_video_hook(0x17B84)==1);
    CHECK(g_ram[0xB400]==0x18 && ram16(0xB408)==640);
    g_cpu.D[0]=640-ram16(0xF7DA);s2_video_hook(0x105C6);CHECK((uint16_t)g_cpu.D[0]<=640);
    CHECK(sonic2_video.configure("32:9"));sonic2_video.width(1,1,320,224);
    s2_video_hook(0x17B84);CHECK(g_ram[0xB440]==0x19);
    CHECK(g_ram[0xB441]==3 && g_ram[0xB462]==3); /* placement flip bits 13/14 */
    /* Both Sonic and Tails retain the native ring collision code. */
    word(g_ram,0xE808,100);word(g_ram,0xE80E,65535);g_cpu.A[0]=0xFFFFB040;
    s2_video_hook(0x170D0);CHECK(g_cpu.A[1]==0xFFFFE806 && g_cpu.A[2]==0xFFFFE80C);
    CHECK(sonic2_video.configure("off"));
    g_cpu.D[0]=12345;s2_video_hook(0x105C6);CHECK(g_cpu.D[0]==12345);
    /* H32 special-stage scenery fills the wide canvas while retaining the
     * original track/sprite projection. Snapshot art stays immutable, CRAM
     * remains live for genuine HBlank palette effects. */
    static GVDP v;uint32_t native[256],out[796];
    for(unsigned n=0;n<256;++n)native[n]=0xFF123456;
    v.reg[1]=64;v.reg[2]=0x20;v.reg[4]=6;v.reg[13]=0x2C;v.reg[16]=3;
    memset(v.vram+32,0x11,32);v.cram[1]=0xE;
    for(unsigned n=0;n<4096;++n)word(v.vram,0xC000+n*2,1);
    g_ram[0xF600]=16;g_ram[0xB000]=9;
    CHECK(sonic2_video.configure("32:9"));
    sonic2_video.scanline(&v,0,native,256,out,796);
    CHECK(out[0]==0xFFFF0000 && out[795]==0xFFFF0000);
    CHECK(!memcmp(out+270,native,sizeof native));
    memset(v.vram+32,0,32);v.cram[1]=0xE0;
    sonic2_video.scanline(&v,1,native,256,out,796);
    CHECK(out[0]==0xFF00FF00 && out[795]==0xFF00FF00);
    /* Actual SpecialStage setup is register 12=$08, not plain H32. */
    v.reg[12]=8;memset(v.vram+32,0x11,32);
    sonic2_video.scanline(&v,0,native,256,out,796);
    CHECK(out[0]==genesis_dac_cram_to_argb(0xE0,GENESIS_DAC_SHADOW));
    CHECK(!memcmp(out+270,native,sizeof native));
    /* Completed host HUD/ring publication survives a partial native DMA and
     * producer-ring rollover, without a one-frame centered native fallback. */
    CHECK(sonic2_video.configure("off"));CHECK(sonic2_video.configure("32:9"));
    g_ram[0xF600]=12;g_ram[0xF711]=1;g_ram[0xB000]=1;
    memset(&v,0,sizeof v);v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;
    v.reg[5]=0x7C;v.reg[12]=1;v.reg[13]=0x3F;v.reg[16]=1;
    v.cram[1]=0xE;memset(v.vram+32,0x11,32);
    uint32_t native320[320];for(unsigned n=0;n<320;++n)native320[n]=0xFF123456;
    s_build.count=2;s_build.scene=1;
    s_build.sprites[0]=(SceneSprite){16,0,0x8001,0,1};
    s_build.sprites[1]=(SceneSprite){700,0,0x8001,0,0};
    publish_sprites();memcpy(v.vram+0xF800,g_ram+0xF800,640);
    sonic2_video.scanline(&v,0,native320,320,out,796);
    CHECK(out[16]==0xFFFF0000 && out[700]==0xFFFF0000);
    v.vram[0xF806]^=1;
    sonic2_video.scanline(&v,0,native320,320,out,796);
    CHECK(out[16]==0xFFFF0000 && out[700]==0xFFFF0000);
    CHECK(s_scene_holds==1);
    /* Retained world sprites use their captured camera, not the new camera:
     * ring world X=700 is now at X=438; HUD stays at X=16. */
    word(g_ram,0xEE60,500);word(v.vram,0xFC00,(uint16_t)-500);
    v.vsram[0]=4;
    sonic2_video.scanline(&v,0,native320,320,out,796);
    CHECK(out[16]==0xFFFF0000 && out[438]==0xFFFF0000);
    sonic2_video.scanline(&v,4,native320,320,out,796);
    CHECK(out[16]==0xFFFF0000 && out[438]!=0xFFFF0000);
    word(g_ram,0xEE60,0);word(v.vram,0xFC00,0);
    v.vsram[0]=0;
    v.vram[0xF806]^=1;
    sonic2_video.scanline(&v,0,native320,320,out,796);
    for(unsigned n=0;n<3;++n) {
        word(g_ram,0xF806,n+1);s_build.sprites[0].x=24;publish_sprites();
    }
    sonic2_video.scanline(&v,1,native320,320,out,796);
    CHECK(out[16]==0xFFFF0000 && out[700]==0xFFFF0000);
    sonic2_video.scanline(&v,0,native320,320,out,796);
    CHECK(out[16]==0xFFFF0000 && out[700]==0xFFFF0000);
    memcpy(v.vram+0xF800,g_ram+0xF800,640);
    sonic2_video.scanline(&v,0,native320,320,out,796);
    CHECK(out[24]==0xFFFF0000); /* new publication accepted */
    g_ram[0xF600]=4;g_ram[0xB000]=0;v.vram[0xF806]^=1;
    sonic2_video.scanline(&v,0,native320,320,out,796);
    CHECK(!s_display); /* never hold an old level over a different scene */
    CHECK(sonic2_video.configure("off"));CHECK(sonic2_video.configure("fit"));
    CHECK(!s_display_frame.serial);
    /* IRQ-entry cadence distinguishes real missed ticks from the variable
     * end-of-wall-frame phase of native V-int DMA work. */
    CHECK(sonic2_video.configure("off"));s2_video_vblank();
    CHECK(sonic2_video.configure("fit"));
    g_ram[0xF600]=12;g_ram[0xF711]=1;g_ram[0xB000]=1;g_ram[0xB024]=2;
    word(g_ram,0xFE04,10);s2_video_vblank();
    word(g_ram,0xFE04,11);++s_serial;s2_video_vblank();
    CHECK(s_tick_samples==1 && s_tick_updates==1 && !s_tick_lag && !s_tick_multi && !s_publication_lag);
    s2_video_vblank();CHECK(s_tick_lag==1 && s_publication_lag==1);
    word(g_ram,0xFE04,13);++s_serial;s2_video_vblank();CHECK(s_tick_multi==1);
    CHECK(sonic2_video.configure("off"));s2_video_vblank();CHECK(s_tick_samples==3);
    puts("Sonic 2 video defaults, terrain, spawn/cull cells and Sonic/Tails ring gates PASS");
    return 0;
}
