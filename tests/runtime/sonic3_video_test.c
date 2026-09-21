/* Test the actual renderer's row-pointer layout and host publication helpers. */
#include "../../sonic3k/sonic3_video.c"
static int test_data_read16(uint32_t address,uint16_t *value)
{if(address!=0x480000)return 0;*value=0xABCD;return 1;}
static int test_required;
static uint32_t test_palette[64];
static int test_scene_required(void){return test_required;}
static const uint32_t *test_sprite_palette(uint32_t map){return map==0x410000?test_palette:NULL;}
const GameSpec g_game_spec={.data_read16=test_data_read16,.scene_required=test_scene_required,
    .scene_sprite_palette=test_sprite_palette};
uint8_t g_ram[65536],g_rom[0x400000];
M68KState g_cpu;
void m68k_write8(uint32_t a,uint8_t v){g_ram[a&65535]=v;}
void m68k_write16(uint32_t a,uint16_t v){m68k_write8(a,v>>8);m68k_write8(a+1,v);}
void m68k_write32(uint32_t a,uint32_t v){m68k_write16(a,v>>16);m68k_write16(a+2,v);}
void cmd_send_response(const char *json){(void)json;}
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static void word(uint8_t *p,unsigned a,unsigned v){p[a]=(uint8_t)(v>>8);p[a+1]=(uint8_t)v;}
static void longword(uint8_t *p,unsigned a,unsigned v){word(p,a,v>>16);word(p,a+2,v);}
int main(int argc,char **argv)
{
    CHECK(scene_read8(0x480000)==0xAB&&scene_read8(0x480001)==0xCD);
    if(argc==2) {
        FILE *rom=fopen(argv[1],"rb");CHECK(rom);
        CHECK(fread(g_rom,1,sizeof g_rom,rom)==S3_ART_BANK+0x200000u);fclose(rom);
        load_aiz_art();CHECK(s_aiz_art_ready);
        const uint8_t *sources[]={s_aiz_intro.blocks,s_aiz_main.blocks,s_aiz_intro.video.vram,s_aiz_main.video.vram};
        const unsigned sizes[]={0x1800,0x1800,0x10000,0x10000};
        const unsigned expected[]={0x6994E873u,0x81D9433Bu,0x0D697F52u,0xFF50471Du};
        for(unsigned j=0;j<4;++j) {
            unsigned hash=2166136261u;
            for(unsigned k=0;k<sizes[j];++k)hash=(hash^sources[j][k])*16777619u;
            printf("AIZ asset %u: %08x\n",j,hash);
            CHECK(hash==expected[j]);
        }
        printf("palette %04x %04x\n",s_aiz_intro.colors[2],s_aiz_main.colors[2]);
        return 0;
    }
    /* Bounded host decoding: literals, overlapping dictionary matches,
     * truncation, a reference before the output and insufficient capacity. */
    const uint8_t compressed[]={0xAF,0,'A','B','C','D',0xFC,0xFA,0,0xF8,0};
    uint8_t decoded[8];size_t used=0;
    CHECK(art_kos(compressed,sizeof compressed,decoded,sizeof decoded,&used)==8);
    CHECK(used==sizeof compressed && !memcmp(decoded,"ABCDABCD",8));
    CHECK(!art_kos(compressed,sizeof compressed-1,decoded,sizeof decoded,&used));
    CHECK(!art_kos(compressed,sizeof compressed,decoded,7,&used));
    const uint8_t invalid[]={2,0,0xFF,0xFA};
    CHECK(!art_kos(invalid,sizeof invalid,decoded,sizeof decoded,&used));
    CHECK(!enabled());CHECK(!width(2560,720,320,224));CHECK(s3_video_main_cpu_divisor()==1);
    test_required=1;CHECK(enabled());CHECK(width(2560,720,320,224)==320);
    test_required=0;CHECK(!enabled());
    write32(0xB000,0x123456);g_ram[0xF600]=12;g_ram[0xF711]=1;
    word(g_ram,0x8000,16);word(g_ram,0x8002,4);
    CHECK(stage_width()==2048);CHECK(configure("fit"));CHECK(width(4000,500,320,224)==1792);
    CHECK(s3_video_main_cpu_divisor()==4);
    g_ram[S3_COMPETITION+1]=1;CHECK(s3_video_main_cpu_divisor()==1);g_ram[S3_COMPETITION+1]=0;
    g_ram[0xF600]=0x8C;CHECK(s3_video_main_cpu_divisor()==1);g_ram[0xF600]=12;
    CHECK(configure("16:9"));CHECK(width(1,1,320,224)==398);
    CHECK(configure("21:9"));CHECK(width(1,1,320,224)==523);
    CHECK(configure("32:9"));CHECK(width(1,1,320,224)==796);
    CHECK(configure("64:9"));CHECK(width(1,1,320,224)==1593);
    CHECK(configure("stage"));CHECK(width(1,1,320,224)==2048);
    CHECK(!configure("32:0"));CHECK(!configure("nan:9"));CHECK(!configure("inf:1"));
    word(g_ram,0x8008,0x8100);word(g_ram,0x800A,0x8140);g_ram[0x8100]=g_ram[0x8140]=1;
    CHECK(level_ready());g_ram[0xF711]=0;CHECK(level_ready());
    g_ram[0xF600]=0x8C;CHECK(!level_ready());g_ram[0xF600]=12;g_ram[0xF711]=1;
    word(g_ram,0x8008,0);CHECK(!level_ready());word(g_ram,0x8008,0x8100);
    word(g_ram,128,3);for(unsigned n=0;n<4;++n)word(g_ram,0x9018+n*2,100+n);
    CHECK(world_attr(g_ram,0,0,0)==100);CHECK(world_attr(g_ram,8,8,0)==103);
    CHECK(world_attr(g_ram,0,0,1)==100);CHECK(world_attr(g_ram,2048,0,0)==0);
    word(g_ram,128,0xC03);CHECK(world_attr(g_ram,0,0,0)==(103^0x1800));
    /* Sorted placements, native respawn pointers and matching activation cells. */
    longword(g_rom,S3_PLACEMENTS,0x100000);word(g_rom,0x100000,640);word(g_rom,0x100002,0x8060);
    g_rom[0x100004]=1;word(g_rom,0x100006,1000);word(g_rom,0x100008,0xE060);
    g_rom[0x10000A]=2;word(g_rom,0x10000C,65535);
    write32(0xEF5A,0x110000);longword(g_rom,0x110004,0x123456);longword(g_rom,0x110008,0x234567);
    longword(g_rom,0x5CC9A,0x123456);longword(g_rom,0x5CC9E,0x234567);
    CHECK(configure("10:7"));width(1,1,320,224);
    s3_video_hook(S3_LOAD_INIT);g_ram[0xF76C]=4;write16(0xEE78,1);
    CHECK(s3_video_hook(S3_LOAD_UPDATE));CHECK(!ram16(0xB0DE));
    write16(0xEE78,128);CHECK(s3_video_hook(S3_LOAD_UPDATE));
    CHECK(scene_read32(0xFFB0DE)==0x123456 && ram16(0xB0EE)==640 && ram16(0xB126)==0xEB00);
    g_cpu.D[0]=640-ram16(0xF7DA);s3_video_hook(0x1B594);CHECK((uint16_t)g_cpu.D[0]<=640);
    CHECK(configure("32:9"));width(1,1,320,224);s3_video_hook(S3_LOAD_UPDATE);
    CHECK(scene_read32(0xFFB128)==0x234567 && g_ram[0xB12C]==3 && g_ram[0xB152]==3);
    CHECK(configure("off"));g_cpu.D[0]=12345;s3_video_hook(0x1B594);CHECK(g_cpu.D[0]==12345);
    /* Front-buffer retention through partial SAT DMA / producer rollover. */
    CHECK(configure("32:9"));write16(0xEE80,0);g_ram[0xF711]=1;
    static GVDP v;v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.cram[1]=0xE;
    memset(v.vram+32,0x11,32);uint32_t native[320]={0},out[796];
    s_build.count=2;s_build.scene=1;s_build.camera_x=s_build.camera_y=0;
    s_build.sprites[0]=(SceneSprite){16,0,0x8001,0,1};
    s_build.sprites[1]=(SceneSprite){700,0,0x8001,0,0};
    publish_sprites();memcpy(v.vram+0xF800,g_ram+0xF800,640);
    scanline(&v,0,native,320,out,796);CHECK(out[16]==0xFFFF0000 && out[700]==0xFFFF0000);
    /* A donor sprite has its own colors; native sprites and terrain CRAM
     * remain unchanged even when the mappings share a palette index. */
    test_palette[1]=0xFF00FF00;s_build.sprites[1].mapping=0x410000;
    publish_sprites();scanline(&v,0,native,320,out,796);
    CHECK(out[16]==0xFFFF0000&&out[700]==0xFF00FF00&&v.cram[1]==0xE);
    s_build.sprites[1].mapping=0;publish_sprites();scanline(&v,0,native,320,out,796);
    v.vram[0xF806]^=1;scanline(&v,0,native,320,out,796);
    CHECK(out[16]==0xFFFF0000 && out[700]==0xFFFF0000 && s_scene_holds==1);
    word(g_ram,0xEE80,500);word(v.vram,0xF000,(uint16_t)-500);
    scanline(&v,0,native,320,out,796);CHECK(out[16]==0xFFFF0000 && out[438]==0xFFFF0000);
    CHECK(configure("off"));CHECK(configure("fit"));CHECK(!s_display_frame.serial);
    /* S3's low-ROM title-card routines have a zero high word, not an empty
     * object slot. Six-byte static mappings must still be captured. */
    g_ram[0xF711]=0;word(g_ram,0xAC00,2);word(g_ram,0xAC02,0xB172);
    write32(0xB172,0x9000);write32(0xB17E,0x2000);write16(0xB182,144);write16(0xB186,136);
    g_ram[0xB176]=32;word(g_rom,0x2002,1);word(g_rom,0x2004,0);
    capture_objects();CHECK(s_build.count==1 && s_build.sprites[0].x==16 && s_build.sprites[0].hud==2);
    /* Never sample a half-decompressed outgoing intro background. */
    g_ram[0xF711]=1;word(g_ram,0xEEC2,0);word(g_ram,0xEEC6,0);
    scanline(&v,0,native,320,out,796);unsigned old=s_background_frame[128];
    word(g_ram,0xEEC6,0xFF00);g_ram[128]^=1;
    scanline(&v,0,native,320,out,796);CHECK(s_background_frame[128]==old);
    word(g_ram,0xEEC2,4);word(g_ram,0xEEC6,0);
    scanline(&v,0,native,320,out,796);CHECK(s_background_frame[128]==g_ram[128]);
    /* Blue Spheres' forward and inverse projections agree on the same
     * surface, including expanded margins. Behind-horizon points are not
     * mistaken for the near intersection. */
    for(int stretch=1;stretch<=6;++stretch)for(int z=0;z<5;++z)for(int x=-3;x<=3;++x) {
        double h=ss_radius-sqrt(ss_radius*ss_radius-x*x/(double)(stretch*stretch)-z*z);
        double d=ss_distance+z+ss_pitch*h;
        SSPoint p=ss_intersect(ss_focal*x/d,112+ss_focal*(ss_ground_lift-ss_pitch*z+h)/d,stretch);
        CHECK(fabs(p.x-x)<.0001 && fabs(p.z-z)<.0001 && fabs(p.depth-d)<.0001);
    }
    CHECK(!ss_intersect(0,0,1).depth);
    CHECK(ss_intersect(-790,200,5).depth>0);
    double px,py,depth;
    CHECK(ss_project(0,0,1,&px,&py,&depth));
    CHECK(fabs(px)<.001 && fabs(py-158)<3);
    CHECK(ss_project(2,1,1,&px,&py,&depth));
    CHECK(fabs(px-155)<3 && fabs(py-104)<3);
    CHECK(!ss_project(100,0,1,&px,&py,&depth));
    /* Full native board, more than 80 host spheres, and no guest writes.
     * Test without copyrighted assets using synthetic mappings/art. */
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    write32(SS_EXTRA+16,0x1000);word(g_ram,SS_EXTRA+20,0x4002);
    for(unsigned i=0;i<16;++i)word(g_rom,0x1000+i*2,32);
    word(g_rom,0x1020,1);g_rom[0x1022]=240;g_rom[0x1023]=15;
    word(g_rom,0x1024,0);word(g_rom,0x1026,65520);
    memset(v.vram+64,0x11,512);memset(g_ram+0xF100,2,1024);
    g_ram[0xF600]=0x34;ss_capture();
    static uint8_t before[65536];memcpy(before,g_ram,sizeof before);
    ss_begin(&v,1603);CHECK(ss_ready && ss_count>80 && ss_margin_count>80);
    CHECK(!memcmp(before,g_ram,sizeof before));
    CHECK(ss_texture[2][0][32*64+32]==33);
    write16(0xE422,512);ss_begin(&v,1603);CHECK(ss_x==0); /* no mixed publication */
    ss_capture();ss_begin(&v,1603);CHECK(ss_x==2);
    ss_begin(&v,398);CHECK(ss_ready && ss_width==398);
    ss_begin(&v,320);CHECK(ss_ready && ss_width==320);
    puts("Sonic 3 video terrain, activation, publication and Blue Spheres PASS");
    return 0;
}
