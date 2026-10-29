/* Real all-STOP devices/observer events: compare bounded service with the
 * qualified event interval, never skip a guest instruction or fake a grant. */
#include "segacd_coordinator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u case=%u: %s\n",(unsigned)__LINE__,test_case,#x);exit(1);}}while(0)
static unsigned test_case,opaque_observer;
static SegaCD cd,expected_cd;
static SegaCDCoordinator c;
static uint8_t bios[SEGACD_BIOS_BYTES],ram[65536],expected_ram[65536];
static uint64_t calls[256],observations[16],event_index,call_count,observation_count;
static const uint64_t events[]={1000,2500,5000,UINT64_MAX};
static uint64_t ceiling;
static void put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static M68kStepBusResult read8(void*u,uint32_t a,uint8_t*v){(void)u;if(a<0xff0000)return M68K_BUS_FAULT;*v=ram[a&65535];return M68K_BUS_OK;}
static M68kStepBusResult read16(void*u,uint32_t a,uint16_t*v){uint8_t h,l;if(read8(u,a,&h)!=M68K_BUS_OK||read8(u,a+1,&l)!=M68K_BUS_OK)return M68K_BUS_FAULT;*v=(uint16_t)((h<<8)|l);return M68K_BUS_OK;}
static M68kStepBusResult write8(void*u,uint32_t a,uint8_t v){(void)u;if(a<0xff0000)return M68K_BUS_FAULT;ram[a&65535]=v;return M68K_BUS_OK;}
static M68kStepBusResult write16(void*u,uint32_t a,uint16_t v){if(write8(u,a,(uint8_t)(v>>8))!=M68K_BUS_OK)return M68K_BUS_FAULT;return write8(u,a+1,(uint8_t)v);}
static uint64_t next_host(void*u){(void)u;if(test_case==10)return event_index==0?0:event_index==1?100:UINT64_MAX;return test_case==1?events[event_index]:UINT64_MAX;}
static void host(void*u,uint64_t at){
 (void)u;CHECK(call_count<256);calls[call_count++]=at;
 while(test_case==10&&event_index<2&&at>=(event_index?100u:0u)){
  CHECK(observation_count<16);observations[observation_count++]=at;++event_index;
 }
 while(test_case==1&&events[event_index]!=UINT64_MAX&&at>=events[event_index]){
  CHECK((opaque_observer||at==events[event_index])&&observation_count<16);observations[observation_count++]=at;++event_index;
 }
 if((test_case==7&&!segacd_cdc_irq(&cd.cdc))||(test_case==8&&!cd.drive.command_pending)||(test_case==9&&at>=22500)){
  if(!observation_count){CHECK(observation_count<16);observations[observation_count++]=at;}
  if(test_case==9)CHECK(at>=22500&&!segacd_cdc_irq(&cd.cdc)&&!cd.drive.command_pending&&(cd.irq_pending&8));
 }
}
static uint64_t limit(void*u){(void)u;return ceiling;}
static void pure(M68kStepContext*cpu){++cpu->state.regs.D[0];cpu->state.regs.PC=0x100;}
static const M68kStepEntry entry={0x100,{0x4e71},1,4,pure};
static const M68kStepModule module={"event-wait",0x100,2,0,&entry,1};
static const M68kStepModule*const modules[]={&module};
static const uint8_t flags[]={M68K_STEP_EFFECT_REGISTER};
static const M68kStepEffectModule effects[]={{&module,flags}};
static void memory_operation(M68kStepContext*cpu)
{
 uint8_t value=m68k_step_read8(cpu,0xff0000);
 m68k_step_write8(cpu,0xff0000,(uint8_t)(value+1u));
 m68k_step_write8(cpu,0xff0001,(uint8_t)event_index);cpu->state.regs.PC=0x100;
}
static const M68kStepEntry memory_entry={0x100,{0x4e71},1,4,memory_operation};
static const M68kStepModule memory_module={"event-publication",0x100,2,0,&memory_entry,1};
static const M68kStepModule*const memory_modules[]={&memory_module};
static const uint8_t memory_flags[]={M68K_STEP_EFFECT_TRACKED_BUS};
static const M68kStepEffectModule memory_effects[]={{&memory_module,memory_flags}};
static void setup(int qualified,int opaque)
{
 opaque_observer=(unsigned)opaque;
 memset(bios,0,sizeof bios);memset(ram,0,sizeof ram);put32(bios,0xffff1800);put32(bios+4,0x100);put32(bios+30*4,0x120);
 bios[0x100]=0x4e;bios[0x101]=0x71;
 CHECK(segacd_init(&cd,bios,sizeof bios));put32(cd.prg,0x7ff00);put32(cd.prg+4,0x100);put32(cd.prg+27*4,0x120);
 cd.prg[0x100]=0x4e;cd.prg[0x101]=0x71;
 CHECK(segacd_write8(&cd,SEGACD_MAIN,0xa12001,1)==SEGACD_OK);
 M68kStepBus bus={read8,read16,write8,write16,read16,NULL,NULL,NULL};
 CHECK(segacd_coordinator_init(&c,&cd,&bus,NULL));
 CHECK(m68k_step_reset(&c.cpu[1])==M68K_STEP_OK);
 c.sub_initialized=c.sub_was_running=1;c.reset_generation=cd.sub_reset_generation;
 for(unsigned n=0;n<2;++n){CHECK(segacd_coordinator_modules(&c,(SegaCDCpu)n,modules,1));c.cpu[n].state.stopped=1;c.cpu[n].state.regs.SR=0x2700;if(qualified)CHECK(segacd_coordinator_region_effects(&c,(SegaCDCpu)n,effects,1));}
 call_count=observation_count=event_index=0;memset(calls,0,sizeof calls);memset(observations,0,sizeof observations);
 segacd_coordinator_host_clock(&c,host,opaque?NULL:next_host);
 if(test_case==2||test_case==3){CHECK(segacd_write8(&cd,SEGACD_SUB,0xff8033,8)==SEGACD_OK);CHECK(segacd_write8(&cd,SEGACD_SUB,0xff8031,1)==SEGACD_OK);}
 if(test_case==3)c.cpu[1].state.regs.SR=0x2000;
 if(test_case==4){
  cd.cdc_destination=5;cd.cdc_dma_address=0x60;cd.cdc.ifctrl=2;cd.cdc.dbc=15;cd.cdc.dac=0;
  for(unsigned n=0;n<16;++n)cd.cdc.ram[n]=(uint8_t)(0x40+n);
  CHECK(segacd_dma_begin(&cd,&cd.dma)==SEGACD_DMA_OK);segacd_cdc_address(&cd.cdc,6);segacd_cdc_write(&cd.cdc,1);
  CHECK(segacd_write8(&cd,SEGACD_MAIN,0xa12003,2)==SEGACD_OK);
  CHECK(segacd_write16(&cd,SEGACD_SUB,0xff805a,0x1000)==SEGACD_OK);
  CHECK(segacd_write16(&cd,SEGACD_SUB,0xff805c,1)==SEGACD_OK);
  CHECK(segacd_write16(&cd,SEGACD_SUB,0xff805e,0x4000)==SEGACD_OK);
  CHECK(segacd_write16(&cd,SEGACD_SUB,0xff8062,1)==SEGACD_OK);
  CHECK(segacd_write16(&cd,SEGACD_SUB,0xff8064,4)==SEGACD_OK);
  CHECK(segacd_write16(&cd,SEGACD_SUB,0xff8066,0x1800)==SEGACD_OK);
 }
 if(test_case==5){c.main_irq=6;c.cpu[0].state.regs.SR=0x2000;}
 if(test_case==6){c.main_irq=7;c.cpu[0].state.irq_pins=7;}
 if(test_case==7||test_case==9){
  static uint8_t sector[2352];memset(sector,0,sizeof sector);sector[15]=1;
  segacd_cdc_address(&cd.cdc,10);segacd_cdc_write(&cd.cdc,0x81);
  segacd_cdc_address(&cd.cdc,1);segacd_cdc_write(&cd.cdc,0x20);
  /* Deliberately invalid synthetic EDC, reported as such; a decoder failure
   * also produces the real finite DECI pulse. No unvalidated good-sector claim. */
  segacd_cdc_sector(&cd.cdc,sector,0);CHECK(cd.cdc.status[0]==1&&cd.cdc.decoder_irq_clocks==22500&&segacd_cdc_irq(&cd.cdc));
 }
 if(test_case==8||test_case==9){
  SegaCDTOC toc={0};toc.count=1;toc.leadout=750;toc.track[0]=(SegaCDTrack){-150,0,750,1};
  CHECK(segacd_cdd_mount(&cd.drive,&toc));segacd_cdd_write(&cd.drive,0x37,4);
  if(test_case==9){segacd_advance(&cd,21250);c.time[0]=c.time[1]=c.device_time=21250;}
  for(unsigned n=0;n<10;++n)segacd_cdd_write(&cd.drive,0x42+n,(uint8_t)(n==9?15:0));
  CHECK(cd.drive.command_pending&&segacd_cdd_next_event(&cd.drive)==(test_case==9?22500u:1250u));
  if(test_case==9){segacd_advance(&cd,482);c.time[0]=c.time[1]=c.device_time=21732;CHECK(segacd_write8(&cd,SEGACD_SUB,0xff8031,1)==SEGACD_OK);}
 }
}
int main(void)
{
 static M68kStepState expected_cpu[2];static uint64_t expected_times[2],expected_observations[16];
 for(test_case=0;test_case<10;++test_case)for(unsigned opaque=0;opaque<2;++opaque){
  uint64_t old_calls=0,expected_count=0,expected_time=0,expected_remainder=0;M68kStepResult expected_result=M68K_STEP_OK;
  for(unsigned qualified=0;qualified<2;++qualified){
   setup((int)qualified,(int)opaque);
   uint64_t deadline=test_case==3?769:test_case==5?1:(test_case==7||test_case==9)?23000:6000;
   M68kStepResult result=segacd_coordinator_run_until(&c,deadline);
   if(!qualified){
    expected_cd=cd;memcpy(expected_ram,ram,sizeof ram);for(unsigned n=0;n<2;++n){expected_cpu[n]=c.cpu[n].state;expected_times[n]=c.time[n];}
    expected_time=c.device_time;expected_remainder=c.main_remainder;expected_count=observation_count;memcpy(expected_observations,observations,sizeof observations);old_calls=call_count;expected_result=result;
   }else{
    CHECK(result==expected_result);CHECK(!memcmp(&expected_cd,&cd,sizeof cd)&&!memcmp(expected_ram,ram,sizeof ram));
    for(unsigned n=0;n<2;++n){CHECK(!memcmp(&expected_cpu[n],&c.cpu[n].state,sizeof expected_cpu[n]));CHECK(expected_times[n]==c.time[n]);}
    CHECK(expected_time==c.device_time&&expected_remainder==c.main_remainder);CHECK(expected_count==observation_count&&!memcmp(expected_observations,observations,sizeof observations));
    if(opaque)CHECK(call_count==old_calls);
    if(!opaque&&test_case<3)CHECK(call_count<old_calls);
    printf("case%u opaque%u: bounded=%llu qualified=%llu exact device/CPU/RAM/events\n",test_case,opaque,(unsigned long long)old_calls,(unsigned long long)call_count);
   }
  }
 }
 test_case=0;setup(1,0);CHECK(segacd_coordinator_step(&c)==M68K_STEP_OK&&c.device_time<=384); /* public single-step cadence */
 setup(1,0);ceiling=100;segacd_coordinator_device_limit(&c,limit);CHECK(segacd_coordinator_run_until(&c,101)==M68K_STEP_NO_WAIT_HANDLER&&!c.device_time&&!call_count);
 setup(1,0);cd.dma.last_result=SEGACD_DMA_UNMAPPED;CHECK(segacd_coordinator_run_until(&c,100)==M68K_STEP_BUS_ERROR&&!c.device_time);
 /* A newly opaque observer must not inherit the previous unbounded event
  * cache. Exercise the actual public between-operation configuration path. */
 setup(1,0);CHECK(segacd_write8(&cd,SEGACD_MAIN,0xa12001,0)==SEGACD_OK);c.cpu[0].state.stopped=0;
 CHECK(segacd_coordinator_run_isolated_until(&c,100)==M68K_STEP_OK&&c.region_event_valid&&c.region_event_deadline==UINT64_MAX);
 call_count=0;segacd_coordinator_host_clock(&c,host,NULL);
 CHECK(segacd_coordinator_run_isolated_until(&c,500)==M68K_STEP_OK&&call_count&&calls[0]<=400);
 CHECK(segacd_coordinator_region_effects(&c,SEGACD_MAIN,NULL,0)&&!c.region_event_valid);
 /* Coalesce only redundant pre-instruction publication in a qualified run.
  * Actual bus effects and an observer already due at clock0 stay identical.
  * The ordinary step interface and opaque callbacks keep every old call. */
 test_case=10;
 for(unsigned step=0;step<2;++step)for(unsigned opaque=0;opaque<2;++opaque){
  uint64_t old_calls=0,old_time=0,old_count=0,old_events[16]={0};M68kStepState old_cpu;
  for(unsigned qualified=0;qualified<2;++qualified){
   setup((int)qualified,(int)opaque);CHECK(segacd_write8(&cd,SEGACD_MAIN,0xa12001,0)==SEGACD_OK);
   c.cpu[0].state.stopped=0;CHECK(segacd_coordinator_modules(&c,SEGACD_MAIN,memory_modules,1));
   CHECK(segacd_coordinator_region_effects(&c,SEGACD_MAIN,qualified?memory_effects:NULL,qualified?1:0));
   CHECK(segacd_coordinator_region_effects(&c,SEGACD_SUB,NULL,0));
   if(step){while(c.time[0]<200)CHECK(segacd_coordinator_step(&c)==M68K_STEP_OK);}
   else CHECK(segacd_coordinator_run_until(&c,200)==M68K_STEP_OK);
   CHECK(observation_count==2&&observations[0]==0&&ram[0]>0&&ram[1]==2);
   if(!qualified){old_cpu=c.cpu[0].state;old_time=c.device_time;old_count=observation_count;memcpy(old_events,observations,sizeof old_events);expected_cd=cd;memcpy(expected_ram,ram,sizeof ram);old_calls=call_count;}
   else{
    CHECK(!memcmp(&old_cpu,&c.cpu[0].state,sizeof old_cpu)&&old_time==c.device_time);
    CHECK(old_count==observation_count&&!memcmp(old_events,observations,sizeof old_events));
    CHECK(!memcmp(&expected_cd,&cd,sizeof cd)&&!memcmp(expected_ram,ram,sizeof ram));
    if(step||opaque)CHECK(call_count==old_calls);else CHECK(call_count<old_calls);
    printf("publication step%u opaque%u bounded=%llu qualified=%llu exact CPU/device/RAM/events\n",step,opaque,(unsigned long long)old_calls,(unsigned long long)call_count);
   }
  }
 }
 puts("qualified event waits: exact timer/host/device/IRQ state, opaque fallback, step cadence, third master and faults pass");return 0;
}
