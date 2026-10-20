/* Private-input native dual-program bring-up. Real main bus/VDP/CD memories
 * and coordinator; no disc/CDC/CDD/PCM/ASIC or Z80/audio rendering yet.
 * Missing devices/code halt loudly. This diagnostic is never a game release. */
#include "segacd_bios.h"
#include "segacd_main_ram.h"
#include "segacd_sub.h"
#include "segacd_coordinator.h"
#include "genesis_bus.h"
#include "genesis_dac.h"
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
uint32_t machine_z80_stamp(void){return (uint32_t)((line_number%262)*3420);}
uint32_t machine_z80_pc(void){return 0;}
void snd_trace_68k_write(uint16_t o,uint8_t a,uint8_t b,int req,int reset){(void)o;(void)a;(void)b;(void)req;(void)reset;}
void snd_trace_z80(uint16_t o,uint8_t v,int w){(void)o;(void)v;(void)w;}
void snd_trace_busreq(const char*s,int v){(void)s;(void)v;}
void audio_event_push(uint32_t stamp,uint8_t port,uint8_t value){(void)stamp;(void)port;(void)value;++audio_events;}
void glue_charge_68k_stall(uint32_t cycles){dma_stalls+=cycles;}
static M68kStepBusResult read8(void*u,uint32_t a,uint8_t*v){(void)u;*v=gbus_read8(&genesis,a);return M68K_BUS_OK;}
static M68kStepBusResult read16(void*u,uint32_t a,uint16_t*v){(void)u;*v=gbus_read16(&genesis,a);return M68K_BUS_OK;}
static M68kStepBusResult write8(void*u,uint32_t a,uint8_t v){(void)u;gbus_write8(&genesis,a,v);return M68K_BUS_OK;}
static M68kStepBusResult write16(void*u,uint32_t a,uint16_t v){(void)u;gbus_write16(&genesis,a,v);return M68K_BUS_OK;}
static M68kStepBusResult fetch(void*u,uint32_t a,uint16_t*v){(void)u;if(a<0xFF0000)return M68K_BUS_FAULT;*v=gbus_peek16(&genesis,a);return M68K_BUS_OK;}
static unsigned ack(void*u,unsigned level){(void)u;if(level==6)irq6=0;if(level==4)irq4=0;return M68K_STEP_AUTOVECTOR;}
static uint16_t dma_read(void*u,uint32_t a){(void)u;return m68k_step_read16(&coordinator.cpu[0],a);}
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
static void dump_frame(const char*dir)
{
    char path[4096];int n=snprintf(path,sizeof path,"%s/frame.ppm",dir);if(n<0 || (size_t)n>=sizeof path)exit(4);
    FILE*f=fopen(path,"wb");if(!f)exit(4);unsigned height=(unsigned)gvdp_screen_height(&vdp);if(height>240)height=240;
    fprintf(f,"P6\n%u %u\n255\n",frame_width,height);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<frame_width;++x){uint32_t p=pixels[y][x];uint8_t rgb[3]={(uint8_t)(p>>16),(uint8_t)(p>>8),(uint8_t)p};if(fwrite(rgb,1,3,f)!=3)exit(4);}
    if(fclose(f))exit(4);
}
int main(int argc,char**argv)
{
    if(argc!=4){fprintf(stderr,"usage: segacd_bios_dual_probe BIOS SUB_CLOCK_DEADLINE PRIVATE_DUMP_DIRECTORY\n");return 2;}
    FILE*f=fopen(argv[1],"rb");if(!f || fread(g_rom,1,SEGACD_BIOS_BYTES,f)!=SEGACD_BIOS_BYTES || fgetc(f)!=EOF || fclose(f))return 2;
    if(!segacd_init(&cd,g_rom,SEGACD_BIOS_BYTES))return 2;
    gvdp_init(&vdp);gbus_init(&genesis,&vdp);vdp.bus_read=dma_read;
    M68kStepBus host={read8,read16,write8,write16,fetch,0,ack,0};if(!segacd_coordinator_init(&coordinator,&cd,&host,NULL))return 2;
    const M68kStepModule*main_modules[]={&segacd_bios_module,&segacd_main_ram_module};
    const M68kStepModule*sub_modules[]={&segacd_sub_module};
    if(!segacd_coordinator_modules(&coordinator,SEGACD_MAIN,main_modules,2) || !segacd_coordinator_modules(&coordinator,SEGACD_SUB,sub_modules,1))return 2;
    segacd_coordinator_host_clock(&coordinator,advance_host,next_host_event);
    uint64_t deadline=strtoull(argv[2],NULL,0),steps=0;
    struct {uint32_t main,sub;uint64_t time;} ring[64]={{0}};M68kStepResult status=M68K_STEP_OK;
    while(coordinator.device_time<deadline && steps<100000000u) {
        ring[steps%64].main=coordinator.cpu[0].state.regs.PC;ring[steps%64].sub=coordinator.cpu[1].state.regs.PC;ring[steps%64].time=coordinator.device_time;
        g_audio_cycle_counter=(uint32_t)coordinator.cpu[0].state.cycles;
        status=segacd_coordinator_step(&coordinator);++steps;if(status!=M68K_STEP_OK)break;
    }
    printf("native dual-program probe: status=%u cpu=%u steps=%llu device_time=%llu lines=%llu waits=%llu wait_cpu=%u wait_result=%u\n",
        (unsigned)status,coordinator.last_cpu,(unsigned long long)steps,(unsigned long long)coordinator.device_time,(unsigned long long)line_number,(unsigned long long)coordinator.waits,coordinator.wait_cpu,(unsigned)coordinator.wait_result);
    for(unsigned which=0;which<2;++which) {
        M68kStepState*s=&coordinator.cpu[which].state;
        printf("cpu%u pc=%06X sr=%04X instructions=%llu cycles=%llu fault=%u address=%06X instruction=%06X opcode=%04X\n",which,s->regs.PC,s->regs.SR,(unsigned long long)s->instructions,(unsigned long long)s->cycles,s->fault,s->fault_address,s->instruction_pc,s->fault_opcode);
        for(unsigned r=0;r<8;++r)printf("cpu%u D%u=%08X A%u=%08X\n",which,r,s->regs.D[r],r,s->regs.A[r]);
    }
    for(uint64_t n=steps>64?steps-64:0;n<steps;++n)printf("%llu time=%llu main=%06X sub=%06X\n",(unsigned long long)n,(unsigned long long)ring[n%64].time,ring[n%64].main,ring[n%64].sub);
    printf("sub_control=%u audio_events=%llu dma_stalls=%llu (Z80/audio/disc devices absent)\n",cd.main_control,(unsigned long long)audio_events,(unsigned long long)dma_stalls);
    dump(argv[3],"main-ram.bin",g_ram,sizeof g_ram);dump(argv[3],"sub-prg.bin",cd.prg,sizeof cd.prg);dump(argv[3],"word-ram.bin",cd.word,sizeof cd.word);dump(argv[3],"vram.bin",vdp.vram,sizeof vdp.vram);dump_frame(argv[3]);
    return status==M68K_STEP_OK && coordinator.device_time>=deadline?0:3;
}
