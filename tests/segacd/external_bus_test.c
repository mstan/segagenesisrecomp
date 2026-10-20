#include "segacd_coordinator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static SegaCD cd; static SegaCDCoordinator c; static uint8_t bios[SEGACD_BIOS_BYTES],hostmem[65536];
static unsigned host_writes,host_reads,delay_observations; static int reenter_once,host_wait; static M68kStepResult reenter_result;
static int stall_once;
static uint64_t device_ceiling=UINT64_MAX;
static void put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static M68kStepBusResult hread8(void*u,uint32_t a,uint8_t*v){(void)u;if(a<0xFF0000u)return M68K_BUS_FAULT;++host_reads;if(host_wait)return M68K_BUS_WAIT;*v=hostmem[a&65535u];return M68K_BUS_OK;}
static M68kStepBusResult hread16(void*u,uint32_t a,uint16_t*v){uint8_t h,l;if(hread8(u,a,&h)!=M68K_BUS_OK||hread8(u,a+1,&l)!=M68K_BUS_OK)return M68K_BUS_FAULT;*v=(uint16_t)((h<<8)|l);return M68K_BUS_OK;}
static M68kStepBusResult hwrite8(void*u,uint32_t a,uint8_t v){(void)u;if(a<0xFF0000u)return M68K_BUS_FAULT;++host_writes;hostmem[a&65535u]=v;return M68K_BUS_OK;}
static M68kStepBusResult hwrite16(void*u,uint32_t a,uint16_t v){return hwrite8(u,a,(uint8_t)(v>>8))==M68K_BUS_OK?hwrite8(u,a+1,(uint8_t)v):M68K_BUS_FAULT;}
static void delay_op(M68kStepContext*ctx){ctx->state.regs.D[0]++;}
static void main_time_op(M68kStepContext*ctx){ctx->state.regs.D[0]++;}
static void loop_op(M68kStepContext*ctx){ctx->state.regs.D[0]++;ctx->state.regs.PC=0x100;}
static void word_write_op(M68kStepContext*ctx){uint32_t n=ctx->state.regs.D[0]++;m68k_step_write8(ctx,0x080000u+n,(uint8_t)(0x31u+n));}
static void blocked_word_read_op(M68kStepContext*ctx){(void)m68k_step_read8(ctx,0x080000u);}
static void grant_op(M68kStepContext*ctx){m68k_step_write8(ctx,0xFF8003,1);}
static void timer_grant_op(M68kStepContext*ctx){ctx->state.regs.D[1]=0x27;m68k_step_write8(ctx,0xFF8003,1);}
static void fault_op(M68kStepContext*ctx){(void)m68k_step_read8(ctx,0x123456);}
static void stop_timer_op(M68kStepContext*ctx){m68k_step_write8(ctx,0xFF8033,8);m68k_step_write8(ctx,0xFF8031,1);m68k_step_stop(ctx,0x2000);}
static M68kStepResult nested_step_result,nested_run_result;static int nested_config_result;
static M68kStepEntry entries[8]; static M68kStepModule module; static const M68kStepModule *mods[]={&module};
static M68kStepEntry main_time_entries[2];static M68kStepModule main_time_module;static const M68kStepModule *main_time_mods[]={&main_time_module};
static uint64_t host_device_limit(void*u){(void)u;return device_ceiling;}
static void host_advance(void*u,uint64_t now){(void)u;(void)now;if(c.external_active && cd.memory_mode==2 && cd.word[0]==0x5A)++delay_observations;if(stall_once){stall_once=0;segacd_coordinator_main_stall(&c,7);}if(reenter_once){reenter_once=0;uint8_t v=0xAA;reenter_result=segacd_coordinator_external_main_read8(&c,0x200000,&v);CHECK(v==0xAA);nested_step_result=segacd_coordinator_step(&c);nested_run_result=segacd_coordinator_run_until(&c,c.time[0]);nested_config_result=segacd_coordinator_modules(&c,SEGACD_SUB,mods,1);}}
static void setup(const M68kStepEntry*es,size_t count,int sub_owner)
{
 memset(bios,0,sizeof bios);memset(hostmem,0,sizeof hostmem);memset(entries,0,sizeof entries);memset(&cd,0,sizeof cd);memset(&c,0,sizeof c);
 put32(bios,0xFFFF1800u);put32(bios+4,0x1000u);CHECK(segacd_init(&cd,bios,sizeof bios));
 put32(cd.prg,0x0007FF00u);put32(cd.prg+4,0x00000100u);
 CHECK(segacd_write8(&cd,SEGACD_MAIN,0xA12001,1)==SEGACD_OK);
 if(sub_owner)CHECK(segacd_write8(&cd,SEGACD_MAIN,0xA12003,2)==SEGACD_OK); /* 2M Word RAM, Sub owns it. */
 cd.word[0]=0x5A;
 M68kStepBus host={hread8,hread16,hwrite8,hwrite16,hread16,0,0,0};CHECK(segacd_coordinator_init(&c,&cd,&host,NULL));
 for(size_t i=0;i<count;++i){entries[i]=es[i];for(unsigned w=0;w<es[i].word_count;++w){cd.prg[es[i].pc+w*2]=(uint8_t)(es[i].words[w]>>8);cd.prg[es[i].pc+w*2+1]=(uint8_t)es[i].words[w];}}
 module=(M68kStepModule){"external bus WAIT test",0x100,0x100,0,entries,count};CHECK(segacd_coordinator_modules(&c,SEGACD_SUB,mods,1));
 c.sub_initialized=0;c.sub_was_running=0;c.reset_generation=cd.sub_reset_generation;
 host_writes=host_reads=delay_observations=0;reenter_once=stall_once=host_wait=0;device_ceiling=UINT64_MAX;reenter_result=M68K_STEP_OK;nested_step_result=nested_run_result=M68K_STEP_OK;nested_config_result=1;segacd_coordinator_host_clock(&c,host_advance,NULL);segacd_coordinator_device_limit(&c,host_device_limit);
}
int main(void)
{
 static const M68kStepEntry delayed[]={
  {0x100,{0x4E71},1,4,delay_op},{0x102,{0x4E71},1,4,delay_op},{0x104,{0x4E71},1,4,delay_op},{0x106,{0x4E71},1,4,grant_op}};
 setup(delayed,4,1);
 /* A prior host read is called once; the later WAIT retries only its target byte. */
 hostmem[0x1000]=0xC3;uint8_t source=0;
 CHECK(segacd_coordinator_external_main_read8(&c,0xFF1000,&source)==M68K_STEP_OK && source==0xC3);
 CHECK(host_reads==1);
 CHECK(segacd_coordinator_external_main_write8(&c,0xFF1000,0xC3)==M68K_STEP_OK);
 CHECK(host_writes==1 && hostmem[0x1000]==0xC3);
 reenter_once=1;CHECK(segacd_coordinator_external_main_write8(&c,0x200000,0xA5)==M68K_STEP_OK);
 CHECK(host_writes==1 && cd.word[0]==0xA5 && cd.memory_mode==1);
 CHECK(c.cpu[1].state.regs.D[0]==3 && delay_observations>=1);
 CHECK(reenter_result==M68K_STEP_REENTERED && nested_step_result==M68K_STEP_REENTERED && nested_run_result==M68K_STEP_REENTERED && !nested_config_result);
 CHECK(c.sub_initialized && c.time[0]==c.time[1] && c.device_time==c.time[0]);
 uint8_t value=0;CHECK(segacd_coordinator_external_main_read8(&c,0x200000,&value)==M68K_STEP_OK && value==0xA5);
 CHECK(segacd_coordinator_external_main_read8(&c,0xFF1000,&value)==M68K_STEP_OK && value==0xC3);
 CHECK(host_reads==2);
 /* Main may be ahead. The already-running Sub stays at its own time and
  * performs every real Word RAM write before releasing the grant. */
 static const M68kStepEntry behind[]={
  {0x100,{0x4E71},1,4,word_write_op},{0x102,{0x4E71},1,4,word_write_op},
  {0x104,{0x4E71},1,4,word_write_op},{0x106,{0x4E71},1,4,word_write_op},
  {0x108,{0x4E71},1,4,grant_op}};
 setup(behind,5,1);CHECK(m68k_step_reset(&c.cpu[1])==M68K_STEP_OK);c.sub_initialized=1;c.sub_was_running=1;
 c.time[0]=100;c.time[1]=0;c.device_time=0;
 CHECK(segacd_coordinator_external_main_write8(&c,0x200100,0xEE)==M68K_STEP_OK);
 CHECK(c.cpu[1].state.regs.D[0]==4 && cd.word[0]==0x31 && cd.word[1]==0x32 && cd.word[2]==0x33 && cd.word[3]==0x34);
 CHECK(cd.word[0x100]==0xEE && c.time[0]==100 && c.time[1]==20 && c.device_time==20);
 /* A real timer IRQ wakes STOPped Sub; its handler grants Word RAM. */
 static const M68kStepEntry timer[]={
  {0x100,{0x4E72,0x2000},2,4,stop_timer_op},{0x180,{0x4E71},1,4,timer_grant_op}};
 setup(timer,2,1);put32(cd.prg+27*4,0x180);
 CHECK(segacd_coordinator_external_main_write8(&c,0x200000,0x7B)==M68K_STEP_OK);
 CHECK(c.cpu[1].state.regs.D[1]==0x27 && cd.word[0]==0x7B && (cd.irq_pending&8)==0);
 /* If Sub itself waits on Main-owned Word RAM while the external Main master
  * is paused on PRG access, the nested wait must fail loudly, not reenter Main. */
 static const M68kStepEntry nested_wait[]={{0x100,{0x4E71},1,4,blocked_word_read_op}};
 setup(nested_wait,1,0);uint64_t main_before=c.time[0];
 CHECK(segacd_coordinator_external_main_read8(&c,0x020000,&value)==M68K_STEP_NO_WAIT_HANDLER);
 CHECK(c.wait_result==M68K_STEP_REENTERED && c.time[0]==main_before && cd.memory_mode==1);
 /* Sub fault propagates; it cannot fabricate a byte or mutate the denied target. */
 static const M68kStepEntry bad[]={{0x100,{0x4E71},1,4,fault_op}};
 setup(bad,1,1);value=0xE1;
 CHECK(segacd_coordinator_external_main_read8(&c,0x200000,&value)==M68K_STEP_BUS_ERROR);
 CHECK(value==0xE1 && cd.word[0]==0x5A && cd.memory_mode==2);
 /* A bounded wait cannot return a fabricated success or alter the denied byte. */
 static const M68kStepEntry loop[]={{0x100,{0x4E71},1,4,loop_op}};
 setup(loop,1,1);value=0xE1;c.busy[0]=1;
 CHECK(segacd_coordinator_external_main_read8(&c,0x200000,&value)==M68K_STEP_REENTERED && value==0xE1 && cd.word[0]==0x5A);
 c.busy[0]=0;c.wait_limit=2;
 CHECK(segacd_coordinator_external_main_read8(&c,0x200000,&value)==M68K_STEP_NO_WAIT_HANDLER);
 CHECK(value==0xE1 && cd.word[0]==0x5A && cd.memory_mode==2 && !c.external_active && !c.busy[0]);
 /* Device faults precede even immediate host reads, and 24-bit aliases cannot
  * evade the CD-port aperture check. */
 setup(loop,1,1);cd.media_error=1;host_reads=0;value=0xE1;
 CHECK(segacd_coordinator_external_main_read8(&c,0xFF1000,&value)==M68K_STEP_BUS_ERROR && host_reads==0 && value==0xE1);
 setup(loop,1,1);host_reads=0;value=0xE1;
 CHECK(segacd_coordinator_external_main_read8(&c,0xFFA12080,&value)==M68K_STEP_BUS_ERROR && host_reads==0 && value==0xE1);
 /* Zero wait budget still allows an immediate successful byte probe. */
 setup(loop,1,1);hostmem[0x1000]=0xD4;c.wait_limit=0;value=0;
 CHECK(segacd_coordinator_external_main_read8(&c,0xFF1000,&value)==M68K_STEP_OK && value==0xD4 && host_reads==1 && !c.sub_initialized);
 setup(loop,1,1);host_wait=1;value=0xE1;
 CHECK(segacd_coordinator_external_main_read8(&c,0xFF1000,&value)==M68K_STEP_NO_WAIT_HANDLER && value==0xE1 && host_reads==1);
 /* Unmapped CD register aperture is an error, never host fallback. */
 CHECK(segacd_coordinator_external_main_read8(&c,0xA12080,&value)==M68K_STEP_BUS_ERROR);
 /* A host VDP DMA freeze has no external-master cycle sink. Do not silently
  * charge it to the paused 68K or claim the waiting bus byte succeeded. */
 setup(loop,1,1);uint64_t main_cycles=c.cpu[0].state.cycles;stall_once=1;
 CHECK(segacd_coordinator_external_main_read8(&c,0x200000,&value)==M68K_STEP_NO_WAIT_HANDLER);
 CHECK(c.cpu[0].state.cycles==main_cycles && !c.external_active && !c.busy[0]);
 CHECK(c.external_stall_seen==0 && cd.word[0]==0x5A && cd.memory_mode==2);
 /* With no external owner operation, device time is capped by the independent
  * third master. It advances exactly as that master moves forward. */
 setup(loop,0,0);CHECK(segacd_write8(&cd,SEGACD_MAIN,0xA12001u,0)==SEGACD_OK);
 c.cpu[0].state.stopped=1;device_ceiling=17;
 CHECK(segacd_coordinator_run_until(&c,17)==M68K_STEP_OK && c.device_time==17);
 uint64_t before_main_time=c.time[0],before_sub_time=c.time[1],before_device_time=c.device_time;
 CHECK(segacd_coordinator_run_until(&c,18)==M68K_STEP_NO_WAIT_HANDLER);
 CHECK(c.time[0]==before_main_time && c.time[1]==before_sub_time && c.device_time==before_device_time);
 device_ceiling=29;
 CHECK(segacd_coordinator_run_until(&c,29)==M68K_STEP_OK && c.device_time==29);
 /* Instruction-boundary overshoot advances the 68K timeline past the ceiling,
  * but shared devices still stop at the third master's timestamp. */
 setup(loop,0,0);CHECK(segacd_write8(&cd,SEGACD_MAIN,0xA12001u,0)==SEGACD_OK);
 main_time_entries[0]=(M68kStepEntry){0x1000u,{0x4E71u},1,8,main_time_op};
 main_time_entries[1]=(M68kStepEntry){0x1002u,{0x4E71u},1,8,main_time_op};
 cd.bios[0x1000]=cd.bios[0x1002]=0x4E;cd.bios[0x1001]=cd.bios[0x1003]=0x71;
 main_time_module=(M68kStepModule){"clock ceiling main steps",0x1000u,4u,0,main_time_entries,2};
 CHECK(segacd_coordinator_modules(&c,SEGACD_MAIN,main_time_mods,1));
 c.cpu[SEGACD_MAIN].state.stopped=0;device_ceiling=17;
 CHECK(segacd_coordinator_run_until(&c,17)==M68K_STEP_OK);
 CHECK(c.time[SEGACD_MAIN]>17 && c.device_time==17 && c.cpu[SEGACD_MAIN].state.regs.D[0]==2);
 /* A synchronous Sub ownership WAIT overrides that ceiling until the byte is
  * granted; the external master is paused, but owner/device clocks must move. */
 setup(delayed,4,1);device_ceiling=0;
 CHECK(segacd_coordinator_external_main_write8(&c,0x200000,0x6C)==M68K_STEP_OK);
 CHECK(cd.word[0]==0x6C && cd.memory_mode==1 && c.device_time>device_ceiling);
 puts("external Main byte WAIT: side effects, Sub ownership handoff, STOP wake, faults, reentry, VDP-stall and third-clock ceiling pass");return 0;
}
