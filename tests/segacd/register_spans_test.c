#include "segacd_coordinator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static SegaCD device,reference_device;
static SegaCDCoordinator c;
static uint8_t bios[SEGACD_BIOS_BYTES];
static uint64_t next_event;
static unsigned device_case,staged,variant_case;
static uint64_t third_clock;
static uint8_t host_ram[65536],reference_ram[65536];
static M68kStepBusResult read8(void*u,uint32_t a,uint8_t*v){(void)u;if(a<0xff0000u)return M68K_BUS_FAULT;*v=host_ram[a&65535u];return M68K_BUS_OK;}
static M68kStepBusResult read16(void*u,uint32_t a,uint16_t*v){uint8_t hi,lo;if(read8(u,a,&hi)!=M68K_BUS_OK||read8(u,a+1,&lo)!=M68K_BUS_OK)return M68K_BUS_FAULT;*v=(uint16_t)((unsigned)hi<<8|lo);return M68K_BUS_OK;}
static M68kStepBusResult write8(void*u,uint32_t a,uint8_t v){(void)u;if(a<0xff0000u)return M68K_BUS_FAULT;host_ram[a&65535u]=v;return M68K_BUS_OK;}
static M68kStepBusResult write16(void*u,uint32_t a,uint16_t v){if(write8(u,a,(uint8_t)(v>>8))!=M68K_BUS_OK)return M68K_BUS_FAULT;return write8(u,a+1,(uint8_t)v);}
typedef struct{uint64_t time;M68kStepState cpu[2];} Observation;
static Observation seen[4096],reference_seen[4096];static unsigned observations;
static void record(void){CHECK(observations<4096);Observation*o=&seen[observations++];memset(o,0,sizeof*o);o->time=c.device_time;o->cpu[0]=c.cpu[0].state;o->cpu[1]=c.cpu[1].state;}
static void host(void*u,uint64_t now){(void)u;if(now>=next_event){record();next_event=now+31;if(device_case==1)segacd_coordinator_main_irq(&c,6);}}
static uint64_t event(void*u){(void)u;return next_event;}
static uint64_t third(void*u){(void)u;return third_clock;}
static void increment(M68kStepContext*cpu){++cpu->state.regs.D[0];cpu->state.regs.SR=(uint16_t)((cpu->state.regs.SR&0xffe0u)|(cpu->state.regs.D[0]&31u));}
static void span_operation(M68kStepContext*cpu){increment(cpu);increment(cpu);increment(cpu);increment(cpu);}
static void observed(M68kStepContext*cpu){record();cpu->state.regs.PC=0x100;}
static const M68kStepEntry entries[]={
 {0x100,{0x7001},1,4,increment},{0x102,{0x7001},1,4,increment},
 {0x104,{0x7001},1,4,increment},{0x106,{0x7001},1,4,increment},
 {0x108,{0x4e71},1,4,observed}
};
static const M68kStepEntry *const members[]={entries,entries+1,entries+2,entries+3};
static const M68kStepRegisterSpan spans[]={{members,span_operation,4,16}};
static const M68kStepModule module={"spans",0x100,10,0,entries,5};
static const M68kStepModule*const modules[]={&module};
static const uint8_t flags[]={1,1,1,1,0};
static const M68kStepEffectModule effects[]={{&module,flags}};
static void variant(M68kStepContext*cpu){cpu->state.regs.D[1]^=0x12345678;}
static const M68kStepEntry variant_entries[]={{0x104,{0x7002},1,6,variant}};
static const M68kStepModule variant_module={"later-member-variant",0x104,2,0,variant_entries,1};
static const M68kStepModule*const variant_modules[]={&module,&variant_module};
static const uint8_t variant_flags[]={1};
static const M68kStepEffectModule variant_effects[]={{&module,flags},{&variant_module,variant_flags}};
static const M68kStepEntry prepend_entries[]={{0x102,{0x7001},1,4,variant}};
static const M68kStepModule prepend_module={"higher-priority-interior",0x102,2,0,prepend_entries,1};
static const M68kStepModule*const prepend_modules[]={&prepend_module,&module};
static void put32(uint8_t*p,uint32_t n){p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
static void setup(int qualified,int sub)
{
 memset(bios,0,sizeof bios);memset(host_ram,0,sizeof host_ram);put32(bios,0xff1000);put32(bios+4,0x100);put32(bios+30*4,0x108);
 for(unsigned n=0;n<5;++n){bios[0x100+n*2]=(uint8_t)(entries[n].words[0]>>8);bios[0x101+n*2]=(uint8_t)entries[n].words[0];}
 CHECK(segacd_init(&device,bios,sizeof bios));put32(device.prg,0x7ff00);put32(device.prg+4,0x100);memcpy(device.prg+0x100,bios+0x100,10);
 CHECK(segacd_write8(&device,SEGACD_MAIN,0xa12001,(uint8_t)sub)==SEGACD_OK);
 if(variant_case){device.bios[0x105]=device.prg[0x105]=2;}
 if(device_case==2){
  device.cdc_destination=5;device.cdc_dma_address=0x60;device.cdc.ifctrl=2;device.cdc.dbc=15;device.cdc.dac=0;
  for(unsigned n=0;n<16;++n)device.cdc.ram[n]=(uint8_t)(0x40+n);
  CHECK(segacd_dma_begin(&device,&device.dma)==SEGACD_DMA_OK);segacd_cdc_address(&device.cdc,6);segacd_cdc_write(&device.cdc,1);
 }
 M68kStepBus bus={read8,read16,write8,write16,read16,NULL,NULL,NULL};
 CHECK(segacd_coordinator_init(&c,&device,&bus,NULL));
 for(unsigned cpu=0;cpu<2;++cpu){CHECK(segacd_coordinator_modules(&c,(SegaCDCpu)cpu,modules,1));CHECK(segacd_coordinator_region_effects(&c,(SegaCDCpu)cpu,effects,1));if(qualified)CHECK(segacd_coordinator_register_spans(&c,(SegaCDCpu)cpu,spans,1));}
 if(variant_case)for(unsigned cpu=0;cpu<2;++cpu){CHECK(segacd_coordinator_modules(&c,(SegaCDCpu)cpu,variant_modules,2));CHECK(segacd_coordinator_region_effects(&c,(SegaCDCpu)cpu,variant_effects,2));if(qualified)CHECK(segacd_coordinator_register_spans(&c,(SegaCDCpu)cpu,spans,1));}
 if(device_case==1)c.cpu[0].state.regs.SR=0x2000;
 memset(seen,0,sizeof seen);observations=0;next_event=31;segacd_coordinator_host_clock(&c,host,event);
 if(staged){third_clock=0;segacd_coordinator_device_limit(&c,third);}
}
static M68kStepResult run(unsigned stop){
 if(staged)for(unsigned time=13;time<stop;time+=13){third_clock=time;M68kStepResult result=segacd_coordinator_run_until(&c,time);if(result!=M68K_STEP_OK)return result;}
 third_clock=stop;return segacd_coordinator_run_until(&c,stop);
}
int main(void)
{
 uint64_t retired=0;
 unsigned cases=0;
 for(staged=0;staged<2;++staged)for(device_case=0;device_case<3;++device_case)for(variant_case=0;variant_case<2;++variant_case)
 for(int sub=0;sub<2;++sub)for(unsigned stop=1;stop<2000;stop+=37){
  setup(0,sub);M68kStepResult result=run(stop);CHECK(result==M68K_STEP_OK);
  reference_device=device;M68kStepState states[2]={c.cpu[0].state,c.cpu[1].state};uint64_t clocks[4]={c.time[0],c.time[1],c.device_time,c.main_remainder};unsigned count=observations;memcpy(reference_seen,seen,sizeof seen);
  memcpy(reference_ram,host_ram,sizeof host_ram);setup(1,sub);CHECK(run(stop)==result);
  CHECK(!memcmp(reference_ram,host_ram,sizeof host_ram));
  CHECK(!memcmp(&reference_device,&device,sizeof device));CHECK(!memcmp(&states[0],&c.cpu[0].state,sizeof states[0])&&!memcmp(&states[1],&c.cpu[1].state,sizeof states[1]));
  CHECK(clocks[0]==c.time[0]&&clocks[1]==c.time[1]&&clocks[2]==c.device_time&&clocks[3]==c.main_remainder);
  CHECK(count==observations&&!memcmp(reference_seen,seen,count*sizeof*seen));retired+=c.span_instructions[0]+c.span_instructions[1];++cases;
  if(device_case==1 && stop>100)CHECK(c.interrupts[0][6]);
 }
 CHECK(retired>0);
 device_case=staged=variant_case=0;
 /* A mismatched member/priority/effect cannot qualify; replacement revokes. */
 setup(1,0);CHECK(segacd_coordinator_modules(&c,SEGACD_MAIN,modules,1));CHECK(!c.register_span_count[0]);
 const uint8_t unknown[]={1,1,0,1,0};const M68kStepEffectModule changed[]={{&module,unknown}};
 CHECK(segacd_coordinator_region_effects(&c,SEGACD_MAIN,changed,1));CHECK(!segacd_coordinator_register_spans(&c,SEGACD_MAIN,spans,1));
 setup(1,0);CHECK(segacd_coordinator_modules(&c,SEGACD_MAIN,prepend_modules,2));
 CHECK(!segacd_coordinator_register_spans(&c,SEGACD_MAIN,spans,1));
 /* Guard/budget/trace/fault rejection changes no CPU state, including charge. */
 setup(1,0);M68kStepContext *cpu=&c.cpu[0];
 M68kStepContext before=*cpu;CHECK(m68k_step_execute_span(cpu,spans,15)==M68K_STEP_MISS);CHECK(!memcmp(&before,cpu,sizeof before));
 for(unsigned byte=0;byte<8;++byte){device.bios[0x100+byte]^=1;before=*cpu;CHECK(m68k_step_execute_span(cpu,spans,16)==M68K_STEP_MISS);CHECK(!memcmp(&before,cpu,sizeof before));device.bios[0x100+byte]^=1;}
 cpu->state.regs.SR|=0x8000;before=*cpu;CHECK(m68k_step_execute_span(cpu,spans,16)==M68K_STEP_MISS);CHECK(!memcmp(&before,cpu,sizeof before));
 cpu->state.regs.SR&=0x7fff;cpu->fault_escape=cpu;CHECK(m68k_step_execute_span(cpu,spans,16)==M68K_STEP_REENTERED);cpu->fault_escape=NULL;
 cpu->state.stopped=1;CHECK(m68k_step_execute_span(cpu,spans,16)==M68K_STEP_STOPPED);cpu->state.stopped=0;
 cpu->state.fault=M68K_STEP_BUS_ERROR;CHECK(m68k_step_execute_span(cpu,spans,16)==M68K_STEP_BUS_ERROR);
 printf("%u matched IRQ/DMA/third-master/variant scheduler cases; %llu fused instructions\n",cases,(unsigned long long)retired);return 0;
}
