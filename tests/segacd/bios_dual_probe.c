/* Private-input native dual-program bring-up. Real main bus/VDP/CD memories
 * and coordinator; no Z80/audio rendering yet.
 * Missing devices/code halt loudly. This diagnostic is never a game release. */
#ifndef SEGACD_BIOS_PROBE_NO_CATALOGS
#include "segacd_bios.h"
#include "segacd_main_ram.h"
#include "segacd_sub.h"
#include "segacd_bios_extra_modules.h"
#ifdef SEGACD_BIOS_PROBE_SUB_PATCH
#include "segacd_sub_patch.h"
#endif
#ifdef SEGACD_BIOS_PROBE_BOOT_MAIN
#include "segacd_boot_main.h"
#endif
#ifdef SEGACD_BIOS_PROBE_BOOT_SUB
#include "segacd_boot_sub.h"
#endif
#endif
#include "segacd_coordinator.h"
#include "genesis_bus.h"
#include "genesis_dac.h"
#include "disc_image.h"
#include "m68k_floor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint8_t g_rom[0x400000],g_ram[0x10000],*g_rom_ext,g_sndwatch[0x2000];
uint32_t g_rom_ext_size,g_audio_cycle_counter,g_68k_stamp_rebase;
unsigned long g_snd_frame;unsigned g_snd_pcz;
static SegaCD cd;static SegaCDCoordinator coordinator;static GenesisBus genesis;static GVDP vdp;
static uint64_t line_number,audio_events,dma_stalls;
static unsigned irq4,irq6,frame_width=320;
static uint32_t pixels[240][GVDP_MAX_WIDTH];
static const char *dump_directory;
static uint64_t start_frame=UINT64_MAX;
static uint64_t pad_reads,pressed_pad_reads;
#define PROBE_MAX_SCRIPT_EVENTS 64u
typedef struct { uint64_t frame,duration;uint16_t mask; } ProbePadEvent;
static ProbePadEvent pad_events[PROBE_MAX_SCRIPT_EVENTS];
static unsigned pad_event_count;
static int pad_events_override_start;
static uint32_t snapshot_frames[PROBE_MAX_SCRIPT_EVENTS];
static unsigned snapshot_frame_count;
static int custom_snapshot_frames;
static void dump_frame_named(const char *,const char *);
static void dump(const char *,const char *,const void *,size_t);
static int parse_uint_span(const char *begin,const char *end,unsigned base,uint64_t *out)
{
    uint64_t value=0;
    if(begin==end)return 0;
    for(const char*p=begin;p<end;++p) {
        unsigned digit;
        if(*p>='0'&&*p<='9')digit=(unsigned)(*p-'0');
        else if(base==16&&*p>='a'&&*p<='f')digit=(unsigned)(*p-'a'+10);
        else if(base==16&&*p>='A'&&*p<='F')digit=(unsigned)(*p-'A'+10);
        else return 0;
        if(digit>=base || value>(UINT64_MAX-digit)/base)return 0;
        value=value*base+digit;
    }
    *out=value;return 1;
}
static int parse_decimal_span(const char *begin,const char *end,uint64_t *out)
{ return parse_uint_span(begin,end,10,out); }
static int parse_mask_span(const char *begin,const char *end,uint16_t *out)
{
    unsigned base=10;uint64_t value;
    if(end-begin>=2&&begin[0]=='0'&&(begin[1]=='x'||begin[1]=='X')){base=16;begin+=2;}
    if(!parse_uint_span(begin,end,base,&value)||value>0xFFFu)return 0;
    *out=(uint16_t)value;return 1;
}
static int parse_pad_events(const char *text)
{
    const char *cursor=text;
    if(!text||!*text)return 0;
    while(*cursor) {
        if(pad_event_count>=PROBE_MAX_SCRIPT_EVENTS)return 0;
        const char *end=strchr(cursor,',');if(!end)end=cursor+strlen(cursor);
        const char *c1=memchr(cursor,':',(size_t)(end-cursor));
        if(!c1)return 0;
        const char *c2=memchr(c1+1,':',(size_t)(end-c1-1));
        if(!c2||memchr(c2+1,':',(size_t)(end-c2-1)))return 0;
        uint64_t frame,duration;uint16_t mask;
        if(!parse_decimal_span(cursor,c1,&frame)||!parse_decimal_span(c1+1,c2,&duration)||
           !parse_mask_span(c2+1,end,&mask)||!duration||frame>UINT64_MAX-duration)return 0;
        pad_events[pad_event_count++]=(ProbePadEvent){frame,duration,mask};
        if(!*end)break;
        cursor=end+1;if(!*cursor)return 0;
    }
    return pad_event_count!=0;
}
static int parse_snapshot_frames(const char *text)
{
    const char *cursor=text;
    if(!text||!*text)return 0;
    custom_snapshot_frames=1;
    while(*cursor) {
        if(snapshot_frame_count>=PROBE_MAX_SCRIPT_EVENTS)return 0;
        const char *end=strchr(cursor,',');if(!end)end=cursor+strlen(cursor);
        uint64_t frame;
        if(!parse_decimal_span(cursor,end,&frame)||frame>UINT32_MAX)return 0;
        for(unsigned n=0;n<snapshot_frame_count;++n)if(snapshot_frames[n]==(uint32_t)frame)return 0;
        snapshot_frames[snapshot_frame_count++]=(uint32_t)frame;
        if(!*end)break;
        cursor=end+1;if(!*cursor)return 0;
    }
    return snapshot_frame_count!=0;
}
static int parse_probe_inputs(void)
{
    const char *events=getenv("SEGACD_PROBE_PAD_EVENTS");
    const char *override=getenv("SEGACD_PROBE_PAD_EVENTS_OVERRIDE_START");
    const char *snapshots=getenv("SEGACD_PROBE_SNAPSHOT_FRAMES");
    if(events&&!parse_pad_events(events)){fprintf(stderr,"invalid SEGACD_PROBE_PAD_EVENTS (expected up to 64 frame:duration:12-bit-mask events)\n");return 0;}
    if(override) {
        if(!events||strcmp(override,"1")){fprintf(stderr,"SEGACD_PROBE_PAD_EVENTS_OVERRIDE_START requires pad events and exact value 1\n");return 0;}
        pad_events_override_start=1;
    }
    if(snapshots&&!parse_snapshot_frames(snapshots)){fprintf(stderr,"invalid SEGACD_PROBE_SNAPSHOT_FRAMES (expected up to 64 unique decimal frames <= UINT32_MAX)\n");return 0;}
    return 1;
}
static uint16_t scripted_pad(uint64_t frame)
{
    uint16_t mask=0;
    for(unsigned n=0;n<pad_event_count;++n) {
        const ProbePadEvent *e=&pad_events[n];
        if(frame>=e->frame&&frame-e->frame<e->duration)mask|=e->mask;
    }
    if(!pad_events_override_start&&frame>=start_frame&&frame-start_frame<10)mask|=GPAD_START;
    return mask;
}
static int snapshot_requested(uint32_t frame)
{
    if(!custom_snapshot_frames)return frame==60||frame==120||frame==180||frame==240||frame==288||frame==360||frame==600||frame==900;
    for(unsigned n=0;n<snapshot_frame_count;++n)if(snapshot_frames[n]==frame)return 1;
    return 0;
}
static int input_selftest(void)
{
    const char *request=getenv("SEGACD_PROBE_INPUT_SELFTEST");
    if(!request)return 0;
    if(strcmp(request,"1")){fprintf(stderr,"SEGACD_PROBE_INPUT_SELFTEST accepts only 1\n");return -1;}
    for(unsigned n=0;n<pad_event_count;++n) {
        const ProbePadEvent *e=&pad_events[n];
        printf("pad-boundary event=%u frame=%llu mask=%03X\n",n,(unsigned long long)e->frame,scripted_pad(e->frame));
        printf("pad-boundary event=%u frame=%llu mask=%03X\n",n,(unsigned long long)(e->frame+e->duration-1),scripted_pad(e->frame+e->duration-1));
        printf("pad-boundary event=%u frame=%llu mask=%03X\n",n,(unsigned long long)(e->frame+e->duration),scripted_pad(e->frame+e->duration));
    }
    if(start_frame!=UINT64_MAX) {
        printf("start-pulse frame=%llu mask=%03X\n",(unsigned long long)start_frame,scripted_pad(start_frame));
        if(start_frame<=UINT64_MAX-9)printf("start-pulse frame=%llu mask=%03X\n",(unsigned long long)(start_frame+9),scripted_pad(start_frame+9));
        if(start_frame<=UINT64_MAX-10)printf("start-pulse frame=%llu mask=%03X\n",(unsigned long long)(start_frame+10),scripted_pad(start_frame+10));
    }
    if(custom_snapshot_frames) {
        printf("snapshot-frames count=%u",snapshot_frame_count);
        for(unsigned n=0;n<snapshot_frame_count;++n)printf(" %u",snapshot_frames[n]);
        printf("\n");
    }
    return 1;
}
static void observe_input(uint32_t a,unsigned value)
{
    if((a&0xFFFFFEu)!=0xA10002u)return;
    ++pad_reads;if(genesis.pad[0]&GPAD_START) {
        if(pressed_pad_reads++<32)printf("P1 read frame=%llu address=%06X raw=%02X TH=%u pad=%04X pc=%06X\n",(unsigned long long)(line_number/262),a,value,!!(genesis.io_data[0]&64u),genesis.pad[0],coordinator.cpu[0].state.instruction_pc);
    }
}
static int media_read(void *image,int32_t lba,uint8_t raw[2352])
{return segacd_image_read(image,lba,raw);}
uint32_t machine_z80_stamp(void){return (uint32_t)((line_number%262)*3420);}
uint32_t machine_z80_pc(void){return 0;}
void snd_trace_68k_write(uint16_t o,uint8_t a,uint8_t b,int req,int reset){(void)o;(void)a;(void)b;(void)req;(void)reset;}
void snd_trace_z80(uint16_t o,uint8_t v,int w){(void)o;(void)v;(void)w;}
void snd_trace_busreq(const char*s,int v){(void)s;(void)v;}
void audio_event_push(uint32_t stamp,uint8_t port,uint8_t value){(void)stamp;(void)port;(void)value;++audio_events;}
void glue_charge_68k_stall(uint32_t cycles){dma_stalls+=cycles;segacd_coordinator_main_stall(&coordinator,cycles);}
static M68kStepBusResult read8(void*u,uint32_t a,uint8_t*v){(void)u;*v=gbus_read8(&genesis,a);observe_input(a,*v);return M68K_BUS_OK;}
static M68kStepBusResult read16(void*u,uint32_t a,uint16_t*v){(void)u;*v=gbus_read16(&genesis,a);observe_input(a,*v);return M68K_BUS_OK;}
static M68kStepBusResult write8(void*u,uint32_t a,uint8_t v){(void)u;gbus_write8(&genesis,a,v);return M68K_BUS_OK;}
static M68kStepBusResult write16(void*u,uint32_t a,uint16_t v){(void)u;gbus_write16(&genesis,a,v);return M68K_BUS_OK;}
static M68kStepBusResult fetch(void*u,uint32_t a,uint16_t*v){(void)u;if(a<0xFF0000)return M68K_BUS_FAULT;*v=gbus_peek16(&genesis,a);return M68K_BUS_OK;}
static unsigned ack(void*u,unsigned level){(void)u;if(level==6)irq6=0;if(level==4)irq4=0;return M68K_STEP_AUTOVECTOR;}
static uint16_t dma_read(void*u,uint32_t a){(void)u;return segacd_coordinator_vdp_read16(&coordinator,a);}
static uint64_t line_tick(uint64_t line)
{
    uint64_t master=line*3420;
    return (master/53693175u)*12500000u+((master%53693175u)*12500000u+53693174u)/53693175u;
}
static uint64_t next_host_event(void*u){(void)u;return line_tick(line_number);}
static void advance_host(void*u,uint64_t now)
{
    (void)u;
    while(line_tick(line_number)<=now) {
        unsigned line=(unsigned)(line_number%262);
        if(!line) {
            uint64_t frame=line_number/262;
            genesis.pad[0]=scripted_pad(frame);
            if(frame<=UINT32_MAX&&snapshot_requested((uint32_t)frame)) {
                char name[64];snprintf(name,sizeof name,"frame-%04u.ppm",(unsigned)frame);
                dump_frame_named(dump_directory,name);
                if((!custom_snapshot_frames&&(frame==360||frame==900))||custom_snapshot_frames) {
                    snprintf(name,sizeof name,"frame-%04u-main-ram.bin",(unsigned)frame);dump(dump_directory,name,g_ram,sizeof g_ram);
                    snprintf(name,sizeof name,"frame-%04u-sub-prg.bin",(unsigned)frame);dump(dump_directory,name,cd.prg,sizeof cd.prg);
                    printf("snapshot frame=%llu time=%llu main=%06X sub=%06X drive=%u sectors=%llu flags=%02X/%02X\n",(unsigned long long)frame,(unsigned long long)now,coordinator.cpu[0].state.regs.PC,coordinator.cpu[1].state.regs.PC,cd.drive.status,(unsigned long long)cd.media_sectors,cd.flags[0],cd.flags[1]);
                }
            }
        }
        if(line_number && (line+261)%262<240) {
            unsigned row=(line+261)%262;uint8_t indices[GVDP_MAX_WIDTH];
            int width=gvdp_render_scanline(&vdp,(int)row,indices);
            if(width>0 && width<=GVDP_MAX_WIDTH) {
                frame_width=(unsigned)width;
                for(int x=0;x<width;++x){unsigned index=indices[x];pixels[row][x]=index<192?genesis_dac_cram_to_argb(vdp.cram[index%64],(int)(index/64)):0xFF000000u;}
            }
        }
        unsigned interrupts=gvdp_begin_scanline(&vdp,(int)line);
        if(interrupts&GVDP_IRQ_VBLANK)irq6=1;
        if(interrupts&GVDP_IRQ_HBLANK)irq4=1;
        g_snd_frame=(unsigned long)(line_number/262);++line_number;
    }
    segacd_coordinator_main_irq(&coordinator,irq6?6u:irq4?4u:0u);
}
static void dump(const char*dir,const char*name,const void*data,size_t bytes)
{
    char path[4096];int n=snprintf(path,sizeof path,"%s/%s",dir,name);
    if(n<0 || (size_t)n>=sizeof path){fprintf(stderr,"dump path too long\n");exit(4);}
    FILE*f=fopen(path,"wb");if(!f || fwrite(data,1,bytes,f)!=bytes || fclose(f)){fprintf(stderr,"dump failed: %s\n",name);exit(4);}
}
static void dump_frame_named(const char*dir,const char*name)
{
    char path[4096];int n=snprintf(path,sizeof path,"%s/%s",dir,name);if(n<0 || (size_t)n>=sizeof path)exit(4);
    FILE*f=fopen(path,"wb");if(!f)exit(4);unsigned height=(unsigned)gvdp_screen_height(&vdp);if(height>240)height=240;
    fprintf(f,"P6\n%u %u\n255\n",frame_width,height);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<frame_width;++x){uint32_t p=pixels[y][x];uint8_t rgb[3]={(uint8_t)(p>>16),(uint8_t)(p>>8),(uint8_t)p};if(fwrite(rgb,1,3,f)!=3)exit(4);}
    if(fclose(f))exit(4);
}
static int read_exact_input(const char*path,void*data,size_t bytes)
{
    FILE*f=fopen(path,"rb");if(!f)return 0;
    int ok=fread(data,1,bytes,f)==bytes && fgetc(f)==EOF && !ferror(f);
    if(fclose(f))ok=0;
    return ok;
}
int main(int argc,char**argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc<4 || argc>6){fprintf(stderr,"usage: segacd_bios_dual_probe BIOS SUB_CLOCK_DEADLINE PRIVATE_DUMP_DIRECTORY [IMAGE.sgcd [START_FRAME]]\n");return 2;}
    dump_directory=argv[3];if(argc==6)start_frame=strtoull(argv[5],NULL,0);
    if(!parse_probe_inputs())return 2;
    int input_test=input_selftest();if(input_test<0)return 2;if(input_test>0)return 0;
    for(unsigned n=0;n<pad_event_count;++n)printf("P1 scripted event frame=%llu duration=%llu buttons=%03X\n",(unsigned long long)pad_events[n].frame,(unsigned long long)pad_events[n].duration,pad_events[n].mask);
    uint32_t watch_word=UINT32_MAX;uint64_t watch_after=0;
    const char *watch=getenv("SEGACD_PROBE_WATCH_WORD_BYTE");
    if(watch) {
        char *end=NULL;unsigned long value=strtoul(watch,&end,0);
        if(!*watch || *end || value>=SEGACD_WORD_BYTES)return 2;
        watch_word=(uint32_t)value;
        const char *after=getenv("SEGACD_PROBE_WATCH_AFTER");
        if(after)watch_after=strtoull(after,NULL,0);
    }
    if(!read_exact_input(argv[1],g_rom,SEGACD_BIOS_BYTES))return 2;
    if(!segacd_init(&cd,g_rom,SEGACD_BIOS_BYTES))return 2;
    const char*bram=getenv("SEGACD_PROBE_BRAM_IMAGE");
    if(bram) {
        if(!read_exact_input(bram,cd.backup,sizeof cd.backup)) {
            fprintf(stderr,"backup RAM input must be exactly %u readable bytes\n",(unsigned)sizeof cd.backup);return 2;
        }
        printf("Loaded persisted backup RAM (%u bytes); no guest formatting/progression inferred.\n",(unsigned)sizeof cd.backup);
    }
    gvdp_init(&vdp);gbus_init(&genesis,&vdp);vdp.bus_read=dma_read;
    M68kStepBus host={read8,read16,write8,write16,fetch,0,ack,0};if(!segacd_coordinator_init(&coordinator,&cd,&host,NULL))return 2;
    SegaCDImage *image=NULL;
    if(argc>=5) {
        printf("Validating complete disc payload before mounting...\n");
        char error[256];image=segacd_image_open(argv[4],error,sizeof error);
        if(!image){fprintf(stderr,"media: %s\n",error);return 2;}
        printf("Disc payload verified; starting original programs.\n");
        if(!segacd_cdd_mount(&cd.drive,segacd_image_toc(image))){segacd_image_close(image);return 2;}
        segacd_coordinator_media(&coordinator,media_read,image,NULL,NULL);
    }
#ifndef SEGACD_BIOS_PROBE_NO_CATALOGS
    const M68kStepModule*main_modules[]={&segacd_bios_module,&segacd_main_ram_module
#ifdef SEGACD_BIOS_PROBE_BOOT_MAIN
        ,&segacd_boot_main_module
#endif
        SEGACD_BIOS_PROBE_EXTRA_MAIN_MODULES
    };
    const M68kStepModule*sub_modules[]={&segacd_sub_module
#ifdef SEGACD_BIOS_PROBE_SUB_PATCH
        ,&segacd_sub_patch_module
#endif
#ifdef SEGACD_BIOS_PROBE_BOOT_SUB
        ,&segacd_boot_sub_module
#endif
        SEGACD_BIOS_PROBE_EXTRA_SUB_MODULES
    };
    int floor_only=getenv("SEGACD_PROBE_FLOOR_ONLY") && !strcmp(getenv("SEGACD_PROBE_FLOOR_ONLY"),"1");
    if(!floor_only && (!segacd_coordinator_modules(&coordinator,SEGACD_MAIN,main_modules,sizeof main_modules/sizeof *main_modules) || !segacd_coordinator_modules(&coordinator,SEGACD_SUB,sub_modules,sizeof sub_modules/sizeof *sub_modules)))return 2;
#else
    int floor_only=1;
#endif
    int require_native=getenv("SEGACD_PROBE_REQUIRE_NATIVE") && !strcmp(getenv("SEGACD_PROBE_REQUIRE_NATIVE"),"1");
    if(!require_native && !segacd_coordinator_floor(&coordinator,segacd_m68k_floor_step))return 2;
    segacd_coordinator_host_clock(&coordinator,advance_host,next_host_event);
    uint64_t deadline=strtoull(argv[2],NULL,0),steps=0;
    struct {uint32_t main,sub;uint64_t time;} ring[64]={{0}};M68kStepResult status=M68K_STEP_OK;
    unsigned mask_before=cd.irq_mask,wp_before=cd.write_protect,control_before=cd.drive.control,event_count=0,commands=0,queries=0;
    uint64_t cdc_before=0;unsigned transfer_before=0,transfers=0;
    uint64_t command_deadline_before=0;
    uint64_t observed_boot_native[2]={0},observed_boot_floor[2]={0};
    uint32_t first_boot_pc[2]={0},floor_pc[2][16]={{0}};unsigned floor_pc_count[2]={0};
    uint32_t selfloop_pc[2][8]={{0}};unsigned selfloop_count[2]={0};
    unsigned watch_events=0;uint8_t watch_before=0;
    while(coordinator.device_time<deadline && steps<100000000u) {
        ring[steps%64].main=coordinator.cpu[0].state.regs.PC;ring[steps%64].sub=coordinator.cpu[1].state.regs.PC;ring[steps%64].time=coordinator.device_time;
        g_audio_cycle_counter=(uint32_t)coordinator.cpu[0].state.cycles;
        uint64_t native_before[2]={coordinator.native_steps[0],coordinator.native_steps[1]};
        uint64_t floor_before[2]={coordinator.floor_steps[0],coordinator.floor_steps[1]};
        status=segacd_coordinator_step(&coordinator);++steps;
        if(watch_word!=UINT32_MAX && coordinator.device_time>=watch_after && watch_events<1024 &&
           (!watch_events || cd.word[watch_word]!=watch_before)) {
            printf("Word RAM watch time=%llu offset=%05X old=%02X new=%02X mode=%02X cpu=%u main=%06X sub=%06X D0=%08X/%08X D1=%08X/%08X D2=%08X/%08X\n",(unsigned long long)coordinator.device_time,watch_word,watch_before,cd.word[watch_word],cd.memory_mode,coordinator.last_cpu,coordinator.cpu[0].state.instruction_pc,coordinator.cpu[1].state.instruction_pc,coordinator.cpu[0].state.regs.D[0],coordinator.cpu[1].state.regs.D[0],coordinator.cpu[0].state.regs.D[1],coordinator.cpu[1].state.regs.D[1],coordinator.cpu[0].state.regs.D[2],coordinator.cpu[1].state.regs.D[2]);
            uint32_t base=watch_word&~7u;printf("Word RAM watch bytes base=%05X value=",base);
            for(unsigned n=0;n<8;++n)printf("%02X",cd.word[base+n]);printf("\n");
            watch_before=cd.word[watch_word];++watch_events;
        }
        for(unsigned which=0;which<2;++which) {
            uint64_t native_delta=coordinator.native_steps[which]-native_before[which];
            uint64_t floor_delta=coordinator.floor_steps[which]-floor_before[which];
            /* A WAIT can retire several nested operations. Attribute only a
             * single retirement, whose instruction PC is unambiguous here. */
            if(native_delta+floor_delta!=1)continue;
            uint32_t pc=coordinator.cpu[which].state.instruction_pc&0xFFFFFFu;
            M68kStepState *s=&coordinator.cpu[which].state;
            if((s->regs.PC&0xFFFFFFu)==pc && (s->fault_opcode==0x60FEu || s->fault_opcode==0x6000u) && selfloop_count[which]<8) {
                unsigned n=0;while(n<selfloop_count[which] && selfloop_pc[which][n]!=pc)++n;
                if(n==selfloop_count[which]) {
                    selfloop_pc[which][selfloop_count[which]++]=pc;
                    printf("Unconditional self-loop cpu=%u pc=%06X time=%llu sr=%04X\n",which,pc,(unsigned long long)coordinator.device_time,s->regs.SR);
                    for(unsigned r=0;r<8;++r)printf("self-loop D%u=%08X A%u=%08X\n",r,s->regs.D[r],r,s->regs.A[r]);
                    for(uint64_t k=steps>32?steps-32:0;k<steps;++k)printf("self-loop predecessor time=%llu main=%06X sub=%06X\n",(unsigned long long)ring[k%64].time,ring[k%64].main,ring[k%64].sub);
                    char name[64];snprintf(name,sizeof name,"selfloop-cpu%u-%06X-memory.bin",which,pc);
                    dump(dump_directory,name,which?cd.prg:g_ram,which?sizeof cd.prg:sizeof g_ram);
                }
            }
            int boot=which==0?(pc>=0xFF0000u && pc<0xFF7E00u):(pc>=0x6000u && pc<0xD000u);
            if(boot) {
                observed_boot_native[which]+=native_delta;observed_boot_floor[which]+=floor_delta;
                if(!first_boot_pc[which]){first_boot_pc[which]=pc;printf("Observed boot-address-range execution cpu=%u pc=%06X native=%u time=%llu\n",which,pc,(unsigned)native_delta,(unsigned long long)coordinator.device_time);}
            }
            if(floor_delta && floor_pc_count[which]<16) {
                unsigned n=0;while(n<floor_pc_count[which] && floor_pc[which][n]!=pc)++n;
                if(n==floor_pc_count[which]){floor_pc[which][floor_pc_count[which]++]=pc;printf("Floor PC cpu=%u pc=%06X time=%llu\n",which,pc,(unsigned long long)coordinator.device_time);}
            }
        }
        if(status!=M68K_STEP_OK)break;
        if(cd.cdc.sectors!=cdc_before) {
            if(cd.cdc.sectors<=24)printf("CDC sector time=%llu count=%llu header=%02X%02X%02X%02X ctrl=%02X/%02X if=%02X/%02X PT=%04X WA=%04X stat=%02X/%02X/%02X/%02X sub=%06X\n",(unsigned long long)coordinator.device_time,(unsigned long long)cd.cdc.sectors,cd.cdc.header[0],cd.cdc.header[1],cd.cdc.header[2],cd.cdc.header[3],cd.cdc.ctrl0,cd.cdc.ctrl1,cd.cdc.ifctrl,cd.cdc.ifstat,cd.cdc.block_pointer,cd.cdc.write_address,cd.cdc.status[0],cd.cdc.status[1],cd.cdc.status[2],cd.cdc.status[3],coordinator.cpu[1].state.regs.PC);
            cdc_before=cd.cdc.sectors;
        }
        if(cd.cdc.transfer_active && !transfer_before && transfers++<24)printf("CDC transfer time=%llu dest=%u DAC=%04X DBC=%04X remaining=%u sub=%06X\n",(unsigned long long)coordinator.device_time,cd.cdc_destination,cd.cdc.dac,cd.cdc.dbc,cd.cdc.remaining,coordinator.cpu[1].state.instruction_pc);
        transfer_before=cd.cdc.transfer_active;
        if(cd.drive.command_pending && cd.drive.command_deadline!=command_deadline_before && cd.drive.pending[0]) {
            int log=cd.drive.pending[0]==2?queries++<24:commands++<256;
            if(log) {
                printf("CDD command time=%llu main=%06X sub=%06X packet=",(unsigned long long)coordinator.device_time,coordinator.cpu[0].state.instruction_pc,coordinator.cpu[1].state.instruction_pc);
                for(unsigned n=0;n<10;++n)printf("%X",cd.drive.pending[n]);printf("\n");
            }
        }
        command_deadline_before=cd.drive.command_deadline;
        if(mask_before!=cd.irq_mask || wp_before!=cd.write_protect || control_before!=cd.drive.control) {
            if(event_count++<64)printf("device event time=%llu main=%06X sub=%06X mask=%02X wp=%02X drive_control=%02X\n",(unsigned long long)coordinator.device_time,coordinator.cpu[0].state.instruction_pc,coordinator.cpu[1].state.instruction_pc,cd.irq_mask,cd.write_protect,cd.drive.control);
            mask_before=cd.irq_mask;wp_before=cd.write_protect;control_before=cd.drive.control;
        }
    }
    printf("native dual-program probe: status=%u cpu=%u steps=%llu device_time=%llu lines=%llu waits=%llu wait_cpu=%u wait_result=%u\n",
        (unsigned)status,coordinator.last_cpu,(unsigned long long)steps,(unsigned long long)coordinator.device_time,(unsigned long long)line_number,(unsigned long long)coordinator.waits,coordinator.wait_cpu,(unsigned)coordinator.wait_result);
    for(unsigned which=0;which<2;++which) {
        M68kStepState*s=&coordinator.cpu[which].state;
        printf("cpu%u pc=%06X sr=%04X instructions=%llu cycles=%llu fault=%u address=%06X instruction=%06X opcode=%04X\n",which,s->regs.PC,s->regs.SR,(unsigned long long)s->instructions,(unsigned long long)s->cycles,s->fault,s->fault_address,s->instruction_pc,s->fault_opcode);
        for(unsigned r=0;r<8;++r)printf("cpu%u D%u=%08X A%u=%08X\n",which,r,s->regs.D[r],r,s->regs.A[r]);
    }
    for(uint64_t n=steps>64?steps-64:0;n<steps;++n)printf("%llu time=%llu main=%06X sub=%06X\n",(unsigned long long)n,(unsigned long long)ring[n%64].time,ring[n%64].main,ring[n%64].sub);
    printf("sub_control=%u audio_events=%llu dma_stalls=%llu media_sectors=%llu cdc_sectors=%llu cdda_sectors=%llu media_error=%u lba=%d (Z80/audio output absent)\n",cd.main_control,(unsigned long long)audio_events,(unsigned long long)dma_stalls,(unsigned long long)cd.media_sectors,(unsigned long long)cd.cdc.sectors,(unsigned long long)cd.cdda_sectors,cd.media_error,cd.media_error_lba);
    printf("stamp operations=%llu pixels=%llu busy=%u remaining_lines=%u\n",(unsigned long long)cd.stamp.operations,(unsigned long long)cd.stamp.pixels,cd.stamp.busy,cd.stamp.reg[6]);
    printf("gate mask=%02X pending=%02X flags=%02X/%02X wp=%02X events=%u; drive status=%u control=%02X command=%u unsupported=%u lba=%d frames=%llu rx=",cd.irq_mask,cd.irq_pending,cd.flags[0],cd.flags[1],cd.write_protect,event_count,cd.drive.status,cd.drive.control,cd.drive.command_pending,cd.drive.unsupported_command,cd.drive.lba,(unsigned long long)cd.drive.frames);
    for(unsigned n=0;n<10;++n)printf("%X",cd.drive.rx[n]);printf(" tx=");
    for(unsigned n=0;n<10;++n)printf("%X",cd.drive.tx[n]);printf("\n");
    printf("retired native=%llu/%llu floor=%llu/%llu floor_only=%u\n",(unsigned long long)coordinator.native_steps[0],(unsigned long long)coordinator.native_steps[1],(unsigned long long)coordinator.floor_steps[0],(unsigned long long)coordinator.floor_steps[1],(unsigned)floor_only);
    printf("observed boot-address-range native=%llu/%llu floor=%llu/%llu first_pc=%06X/%06X require_native=%u (single-retirement observations; address range alone does not identify loaded code)\n",(unsigned long long)observed_boot_native[0],(unsigned long long)observed_boot_native[1],(unsigned long long)observed_boot_floor[0],(unsigned long long)observed_boot_floor[1],first_boot_pc[0],first_boot_pc[1],(unsigned)require_native);
    printf("P1 reads=%llu while_Start_pressed=%llu start_frame=%llu\n",(unsigned long long)pad_reads,(unsigned long long)pressed_pad_reads,(unsigned long long)start_frame);
    printf("CDC final transfers=%u dest=%u DAC=%04X DBC=%04X PT=%04X WA=%04X ctrl=%02X/%02X if=%02X/%02X DMA_words=%llu\n",transfers,cd.cdc_destination,cd.cdc.dac,cd.cdc.dbc,cd.cdc.block_pointer,cd.cdc.write_address,cd.cdc.ctrl0,cd.cdc.ctrl1,cd.cdc.ifctrl,cd.cdc.ifstat,(unsigned long long)cd.dma.words);
    dump(argv[3],"cdc-ram.bin",cd.cdc.ram,sizeof cd.cdc.ram);
    dump(argv[3],"backup-ram.bin",cd.backup,sizeof cd.backup);
    dump(argv[3],"vdp-registers.bin",vdp.reg,sizeof vdp.reg);
    dump(argv[3],"cram-host-u16.bin",vdp.cram,sizeof vdp.cram);
    dump(argv[3],"vsram-host-u16.bin",vdp.vsram,sizeof vdp.vsram);
    dump(argv[3],"main-ram.bin",g_ram,sizeof g_ram);dump(argv[3],"sub-prg.bin",cd.prg,sizeof cd.prg);dump(argv[3],"word-ram.bin",cd.word,sizeof cd.word);dump(argv[3],"vram.bin",vdp.vram,sizeof vdp.vram);dump_frame_named(argv[3],"frame.ppm");
    segacd_image_close(image);
    return status==M68K_STEP_OK && coordinator.device_time>=deadline?0:3;
}
