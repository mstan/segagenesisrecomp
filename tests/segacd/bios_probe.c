/* Optional private-input CPU bring-up probe, NOT a complete game runner.
 * Uses the real main bus/VDP/CD memory devices; Z80/audio/dual-CPU scheduling
 * are deliberately absent. Stops on unimplemented CD ports or missing code.
 * Trace/audio sinks below are observations, never guest success substitutes. */
#include "segacd_bios.h"
#include "segacd.h"
#include "genesis_bus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint8_t g_rom[0x400000],g_ram[0x10000],*g_rom_ext;
uint32_t g_rom_ext_size,g_audio_cycle_counter,g_68k_stamp_rebase;
uint8_t g_sndwatch[0x2000];unsigned long g_snd_frame;unsigned g_snd_pcz;
static SegaCD cd;static GenesisBus genesis;static GVDP vdp;static M68kStepContext cpu;
static uint64_t audio_events,stalls;static uint32_t last_bus;static unsigned last_write;
uint32_t machine_z80_stamp(void){return 0;}
uint32_t machine_z80_pc(void){return 0;}
void snd_trace_68k_write(uint16_t o,uint8_t a,uint8_t b,int req,int reset){(void)o;(void)a;(void)b;(void)req;(void)reset;}
void snd_trace_z80(uint16_t o,uint8_t v,int w){(void)o;(void)v;(void)w;}
void snd_trace_busreq(const char*s,int v){(void)s;(void)v;}
void glue_charge_68k_stall(uint32_t c){stalls+=c;}
void audio_event_push(uint32_t stamp,uint8_t port,uint8_t value){(void)stamp;(void)port;(void)value;++audio_events;}
static M68kStepBusResult result(SegaCDResult r)
{return r==SEGACD_OK?M68K_BUS_OK:r==SEGACD_WAIT?M68K_BUS_WAIT:M68K_BUS_FAULT;}
static int missing_cd(uint32_t a){return a>=0xA12000 && a<0xA12200;}
static M68kStepBusResult read8(void*u,uint32_t a,uint8_t*v)
{
    (void)u;last_bus=a;last_write=0;SegaCDResult r=segacd_read8(&cd,SEGACD_MAIN,a,v);
    if(r!=SEGACD_UNMAPPED)return result(r);if(missing_cd(a))return M68K_BUS_FAULT;
    *v=gbus_read8(&genesis,a);return M68K_BUS_OK;
}
static M68kStepBusResult read16(void*u,uint32_t a,uint16_t*v)
{
    (void)u;last_bus=a;last_write=0;SegaCDResult r=segacd_read16(&cd,SEGACD_MAIN,a,v);
    if(r!=SEGACD_UNMAPPED)return result(r);if(missing_cd(a))return M68K_BUS_FAULT;
    *v=gbus_read16(&genesis,a);return M68K_BUS_OK;
}
static M68kStepBusResult write8(void*u,uint32_t a,uint8_t v)
{
    (void)u;last_bus=a;last_write=1;SegaCDResult r=segacd_write8(&cd,SEGACD_MAIN,a,v);
    if(r!=SEGACD_UNMAPPED)return result(r);if(missing_cd(a))return M68K_BUS_FAULT;
    gbus_write8(&genesis,a,v);return M68K_BUS_OK;
}
static M68kStepBusResult write16(void*u,uint32_t a,uint16_t v)
{
    (void)u;last_bus=a;last_write=1;SegaCDResult r=segacd_write16(&cd,SEGACD_MAIN,a,v);
    if(r!=SEGACD_UNMAPPED)return result(r);if(missing_cd(a))return M68K_BUS_FAULT;
    gbus_write16(&genesis,a,v);return M68K_BUS_OK;
}
static M68kStepBusResult fetch(void*u,uint32_t a,uint16_t*v)
{
    (void)u;SegaCDResult r=segacd_read16(&cd,SEGACD_MAIN,a,v);
    if(r!=SEGACD_UNMAPPED)return result(r);
    if(a>=0xFF0000){*v=gbus_peek16(&genesis,a);return M68K_BUS_OK;}
    return M68K_BUS_FAULT;
}
static uint16_t dma_read(void*u,uint32_t a){uint16_t v=0;(void)u;if(read16(NULL,a,&v)!=M68K_BUS_OK){fprintf(stderr,"unimplemented DMA source %06X\n",a);exit(4);}return v;}
static void dump(const char *dir,const char *name,const void *data,size_t bytes)
{
    char path[4096];int n=snprintf(path,sizeof path,"%s/%s",dir,name);
    if(n<0 || (size_t)n>=sizeof path){fprintf(stderr,"dump path too long\n");exit(4);}
    FILE*f=fopen(path,"wb");if(!f || fwrite(data,1,bytes,f)!=bytes || fclose(f)){fprintf(stderr,"dump failed: %s\n",name);exit(4);}
}
int main(int argc,char **argv)
{
    if(argc!=3 && argc!=4){fprintf(stderr,"usage: segacd_bios_probe BIOS INSTRUCTION_LIMIT [PRIVATE_DUMP_DIRECTORY]\n");return 2;}
    FILE*f=fopen(argv[1],"rb");if(!f || fread(g_rom,1,SEGACD_BIOS_BYTES,f)!=SEGACD_BIOS_BYTES || fgetc(f)!=EOF || fclose(f))return 2;
    if(!segacd_init(&cd,g_rom,SEGACD_BIOS_BYTES) || !m68k_step_module_valid(&segacd_bios_module))return 2;
    gvdp_init(&vdp);gbus_init(&genesis,&vdp);vdp.bus_read=dma_read;
    M68kStepBus ops={read8,read16,write8,write16,fetch,0,0,0};m68k_step_init(&cpu,&ops,NULL);
    M68kStepResult status=m68k_step_reset(&cpu);if(status!=M68K_STEP_OK)return 3;
    unsigned long long limit=strtoull(argv[2],NULL,0),n;
    struct {uint32_t pc;uint16_t sr,op;} ring[64]={{0}};
    for(n=0;n<limit;++n) {
        uint16_t word=0;(void)fetch(NULL,cpu.state.regs.PC,&word);
        ring[n%64].pc=cpu.state.regs.PC;ring[n%64].sr=cpu.state.regs.SR;ring[n%64].op=word;
        g_audio_cycle_counter=(uint32_t)cpu.state.cycles;
        status=m68k_step_module_execute(&cpu,&segacd_bios_module);
        if(status!=M68K_STEP_OK)break;
    }
    printf("main-only probe: status=%u instructions=%llu cycles=%llu pc=%06X bus=%06X write=%u sub_control=%u audio_events=%llu dma_stalls=%llu\n",
           (unsigned)status,(unsigned long long)cpu.state.instructions,(unsigned long long)cpu.state.cycles,cpu.state.regs.PC,last_bus,last_write,cd.main_control,(unsigned long long)audio_events,(unsigned long long)stalls);
    for(unsigned long long i=n<63?0:n-63;i<=n && i<limit;++i)printf("%llu pc=%06X sr=%04X op=%04X\n",i,ring[i%64].pc,ring[i%64].sr,ring[i%64].op);
    for(unsigned r=0;r<8;++r)printf("D%u=%08X A%u=%08X\n",r,cpu.state.regs.D[r],r,cpu.state.regs.A[r]);
    if(argc==4){dump(argv[3],"bios-main-ram.bin",g_ram,sizeof g_ram);dump(argv[3],"bios-sub-prg.bin",cd.prg,sizeof cd.prg);}
    return status==M68K_STEP_OK?0:3;
}
