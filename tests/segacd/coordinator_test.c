#include "segacd_coordinator.h"
#include "dual_main.h"
#include "dual_sub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static uint8_t bios[SEGACD_BIOS_BYTES],ram[65536];static SegaCD cd;static SegaCDCoordinator c;
static uint32_t stall_on_write;
static void put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static M68kStepBusResult read8(void*u,uint32_t a,uint8_t*v){(void)u;if(a<0xFF0000)return M68K_BUS_FAULT;*v=ram[a&65535];return M68K_BUS_OK;}
static M68kStepBusResult read16(void*u,uint32_t a,uint16_t*v){uint8_t hi,lo;if(read8(u,a,&hi)!=M68K_BUS_OK || read8(u,a+1,&lo)!=M68K_BUS_OK)return M68K_BUS_FAULT;*v=(uint16_t)(((unsigned)hi<<8)|lo);return M68K_BUS_OK;}
static M68kStepBusResult write8(void*u,uint32_t a,uint8_t v){(void)u;if(a<0xFF0000)return M68K_BUS_FAULT;ram[a&65535]=v;return M68K_BUS_OK;}
static M68kStepBusResult write16(void*u,uint32_t a,uint16_t v){if(write8(u,a,(uint8_t)(v>>8))!=M68K_BUS_OK)return M68K_BUS_FAULT;M68kStepBusResult r=write8(u,a+1,(uint8_t)v);if(r==M68K_BUS_OK && stall_on_write)segacd_coordinator_main_stall(&c,stall_on_write);return r;}
static void load(uint8_t*dst,const char*path,size_t bytes){FILE*f=fopen(path,"rb");CHECK(f && fread(dst,1,bytes,f)==bytes && fgetc(f)==EOF && !fclose(f));}
static void initialize(const char*main_path,const char*sub_path)
{
    memset(bios,0,sizeof bios);memset(ram,0,sizeof ram);put32(bios,0xFFFF1800u);put32(bios+4,0x1000);
    put32(bios+30*4,0x1030);
    load(bios+0x1000,main_path,dual_main_module.image_size);CHECK(segacd_init(&cd,bios,sizeof bios));
    put32(cd.prg,0x7FF00);put32(cd.prg+4,0x100);put32(cd.prg+27*4,0x120);
    load(cd.prg+dual_sub_module.image_base,sub_path,dual_sub_module.image_size);
    M68kStepBus host={read8,read16,write8,write16,read16,0,0,0};CHECK(segacd_coordinator_init(&c,&cd,&host,NULL));
    static const M68kStepModule *const main_modules[]={&dual_main_module},*const sub_modules[]={&dual_sub_module};
    CHECK(segacd_coordinator_modules(&c,SEGACD_MAIN,main_modules,1));CHECK(segacd_coordinator_modules(&c,SEGACD_SUB,sub_modules,1));
}
static uint64_t host_time,host_event;
static void host_advance(void*u,uint64_t time){(void)u;CHECK(time>=host_time);host_time=time;if(host_event && time>=host_event){host_event=0;segacd_coordinator_main_irq(&c,6);}}
static uint64_t host_next(void*u){(void)u;return host_event?host_event:UINT64_MAX;}
static void dma_trigger_operation(M68kStepContext*ctx)
{m68k_step_write16(ctx,0xFF0500,0x1234);ctx->state.regs.SR&=0xFFF0;}
static void cdd_stage_packet(const uint8_t packet[10])
{for(unsigned n=0;n<9;++n)m68k_step_write8(&c.cpu[SEGACD_SUB],0xFF8042u+n,packet[n]);}
int main(int argc,char**argv)
{
    CHECK(argc==3);initialize(argv[1],argv[2]);
    for(unsigned n=0;n<30 && !(c.cpu[0].state.stopped && c.cpu[1].state.stopped);++n)CHECK(segacd_coordinator_step(&c)==M68K_STEP_OK);
    CHECK(c.cpu[0].state.stopped && c.cpu[1].state.stopped);
    CHECK(c.cpu[0].state.regs.D[0]==0x12345678 && c.cpu[0].state.regs.PC==0x101A);
    CHECK(c.cpu[1].state.regs.PC==0x116 && c.cpu[1].state.regs.A[7]==0x7FF00);
    CHECK(c.waits==1 && c.wait_address==0x200000 && (cd.memory_mode&1));
    CHECK(c.cpu[0].state.instructions==4 && c.cpu[1].state.instructions==3);
    CHECK(c.main_remainder<53693175u);
    /* STOP advances peripherals to a precise deadline with no guest work. */
    uint64_t before_main=c.cpu[0].state.instructions,before_sub=c.cpu[1].state.instructions;
    CHECK(segacd_coordinator_run_until(&c,1000)==M68K_STEP_OK);
    CHECK(c.time[0]==1000 && c.time[1]==1000 && cd.sub_cycles==1000);
    CHECK(c.cpu[0].state.instructions==before_main && c.cpu[1].state.instructions==before_sub);
    /* Real timer -> autovector frame -> generated RTE, without a success flag. */
    CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8033,8)==SEGACD_OK);
    CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8031,1)==SEGACD_OK);
    uint64_t event=cd.timer_origin+768;
    CHECK(segacd_coordinator_run_until(&c,event)==M68K_STEP_OK);CHECK(cd.irq_pending&8);
    CHECK(segacd_coordinator_step(&c)==M68K_STEP_OK);
    CHECK(c.cpu[1].state.exception_vector==27 && c.cpu[1].state.regs.PC==0x120 && !c.cpu[1].state.stopped && !(cd.irq_pending&8));
    CHECK(c.interrupts[SEGACD_SUB][3]==1 && c.last_interrupt_level[SEGACD_SUB]==3);
    CHECK(c.last_interrupt_vector[SEGACD_SUB]==27 && c.last_interrupt_pc[SEGACD_SUB]==0x116 && c.last_interrupt_target[SEGACD_SUB]==0x120);
    CHECK(m68k_step_read32(&c.cpu[1],0x7FEFC)==0x116 && m68k_step_read16(&c.cpu[1],0x7FEFA)==0x2000);
    CHECK(segacd_coordinator_step(&c)==M68K_STEP_OK);
    CHECK(c.cpu[1].state.regs.PC==0x116 && c.cpu[1].state.regs.A[7]==0x7FF00 && c.cpu[1].state.regs.SR==0x2000);
    /* Reset pulse preserves PRG, but clears CPU registers and reloads vectors. */
    c.cpu[1].state.regs.D[7]=0xBAD;CHECK(segacd_write8(&cd,SEGACD_MAIN,0xA12001,0)==SEGACD_OK);
    CHECK(segacd_write8(&cd,SEGACD_MAIN,0xA12003,2)==SEGACD_OK);
    CHECK(segacd_write8(&cd,SEGACD_MAIN,0xA12001,1)==SEGACD_OK);
    CHECK(segacd_coordinator_step(&c)==M68K_STEP_OK);CHECK(c.cpu[1].state.regs.D[7]==0 && c.cpu[1].state.regs.PC==0x10A);
    CHECK(c.reset_generation==cd.sub_reset_generation);
    /* The owner can STOP and rely on its real timer before returning RAM. */
    initialize(argv[1],argv[2]);put32(cd.prg+4,0xF0);
    CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8033,8)==SEGACD_OK);
    CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8031,1)==SEGACD_OK);
    CHECK(segacd_coordinator_run_until(&c,2000)==M68K_STEP_OK);
    CHECK(c.cpu[0].state.regs.D[0]==0x12345678 && c.cpu[0].state.stopped && c.cpu[1].state.instructions>=6 && c.waits==1);
    /* Host events also run while stopped; a queued IRQ uses a real frame/RTE. */
    initialize(argv[1],argv[2]);CHECK(segacd_coordinator_run_until(&c,1000)==M68K_STEP_OK);
    host_time=1000;host_event=2000;segacd_coordinator_host_clock(&c,host_advance,host_next);
    CHECK(segacd_coordinator_run_until(&c,2000)==M68K_STEP_OK && host_time==2000 && c.main_irq==6);
    CHECK(segacd_coordinator_step(&c)==M68K_STEP_OK);
    CHECK(c.cpu[0].state.regs.PC==0x1030 && c.cpu[0].state.exception_vector==30 && c.cpu[0].state.regs.A[7]==0xFFFF17FAu);
    CHECK(segacd_coordinator_step(&c)==M68K_STEP_OK);
    CHECK(c.cpu[0].state.regs.PC==0x101A && c.cpu[0].state.regs.A[7]==0xFFFF1800u && c.cpu[0].state.regs.SR==0x2000);
    /* An absent module stops with the actual SUB PC, never skips its program. */
    initialize(argv[1],argv[2]);CHECK(segacd_coordinator_modules(&c,SEGACD_SUB,NULL,0));
    M68kStepResult r=M68K_STEP_OK;for(unsigned n=0;n<5 && r==M68K_STEP_OK;++n)r=segacd_coordinator_step(&c);
    CHECK(r==M68K_STEP_NO_WAIT_HANDLER && c.cpu[1].state.regs.PC==0x100 && c.last_cpu==0);
    CHECK(c.wait_cpu==1 && c.wait_result==M68K_STEP_MISS);
    /* The VDP path returns the prior Word RAM word. CPU reads neither delay
     * nor disturb that latch; non-CD DMA sources remain ordinary bus reads. */
    initialize(argv[1],argv[2]);cd.word[0]=0x12;cd.word[1]=0x34;cd.word[2]=0x56;cd.word[3]=0x78;
    CHECK(segacd_coordinator_vdp_read16(&c,0x200000)==0);
    CHECK(m68k_step_read16(&c.cpu[0],0x200002)==0x5678);
    CHECK(segacd_coordinator_vdp_read16(&c,0x200002)==0x1234 && cd.vdp_word_latch==0x5678);
    ram[0x500]=0xAB;ram[0x501]=0xCD;
    CHECK(segacd_coordinator_vdp_read16(&c,0xFF0500)==0xABCD && cd.vdp_word_latch==0x5678);
    /* A synthetic MOVE operation invokes the actual host write callback;
     * its DMA freeze must be included in retirement and rational elapsed time. */
    static const M68kStepEntry dma_entry={0x1040,{0x33FC,0x1234,0x00FF,0x0500},4,20,dma_trigger_operation};
    static const M68kStepModule dma_module={"dma trigger",0x1040,8,0,&dma_entry,1};
    const M68kStepModule* dma_modules[]={&dma_module};
    for(unsigned n=0;n<4;++n){bios[0x1040+n*2]=(uint8_t)(dma_entry.words[n]>>8);bios[0x1041+n*2]=(uint8_t)dma_entry.words[n];}
    memcpy(cd.bios+0x1040,bios+0x1040,8);c.cpu[0].state.regs.PC=0x1040;
    CHECK(segacd_coordinator_modules(&c,SEGACD_MAIN,dma_modules,1));stall_on_write=100;
    CHECK(segacd_coordinator_step(&c)==M68K_STEP_OK);stall_on_write=0;
    CHECK(c.cpu[0].state.cycles==120 && c.cpu[0].state.instructions==1 && ram[0x500]==0x12 && ram[0x501]==0x34);
    CHECK(c.time[0]==(UINT64_C(120)*87500000u)/53693175u);
    /* A later idle charge updates elapsed time without inventing instructions. */
    initialize(argv[1],argv[2]);
    segacd_coordinator_main_stall(&c,53693175u);
    CHECK(c.time[0]==87500000u && c.cpu[0].state.cycles==53693175u && !c.cpu[0].state.instructions);
    CHECK(cd.sub_cycles==87500000u);
    segacd_reset(&cd);CHECK(!cd.vdp_word_latch);
    /* A stop cannot hide a failed device transfer or run past it as success. */
    initialize(argv[1],argv[2]);c.cpu[0].state.stopped=1;cd.dma.last_result=SEGACD_DMA_UNMAPPED;
    CHECK(segacd_coordinator_run_until(&c,1000)==M68K_STEP_BUS_ERROR && !c.device_time);
    /* Coordinator observation records actual accepted packet-submit bus
     * edges, not staged payload bytes, ignored HOCK-low writes or command-0
     * status polls. Word writes at 4A submit exactly once on their low byte. */
    initialize(argv[1],argv[2]);
    uint8_t packet1[10]={1,2,3,4,5,6,7,8,9,10};
    uint8_t packet2[10]={2,3,4,5,6,7,8,9,10,11};
    uint8_t poll[10]={0,1,2,3,4,5,6,7,8,9};
    SegaCDCddSnapshot cdd_snapshot;SegaCDCddSubmission cdd_events[4];
    c.cpu[SEGACD_SUB].state.instruction_pc=0x1234;
    m68k_step_write8(&c.cpu[SEGACD_SUB],0xFF8037,4);
    cdd_stage_packet(packet1);m68k_step_write8(&c.cpu[SEGACD_SUB],0xFF804B,packet1[9]);
    CHECK(segacd_coordinator_cdd_trace(&c,&cdd_snapshot,cdd_events,4)==1);
    CHECK(cdd_snapshot.accepted_total==1 && cdd_snapshot.retained_count==1);
    CHECK(!memcmp(cdd_events[0].packet,packet1,sizeof packet1) && cdd_events[0].instruction_pc==0x1234);
    CHECK(cdd_snapshot.command_pending && cdd_snapshot.pending[0]==packet1[0]);
    c.cpu[SEGACD_SUB].state.instruction_pc=0x2345;
    cdd_stage_packet(packet2);
    CHECK(segacd_coordinator_cdd_trace(&c,&cdd_snapshot,cdd_events,4)==1);
    CHECK(cdd_events[0].packet[0]==packet1[0]); /* Staging while busy is not a submit. */
    m68k_step_write16(&c.cpu[SEGACD_SUB],0xFF804A,(uint16_t)((packet2[8]<<8)|packet2[9]));
    CHECK(segacd_coordinator_cdd_trace(&c,&cdd_snapshot,cdd_events,4)==2);
    CHECK(cdd_snapshot.accepted_total==2 && cdd_events[1].instruction_pc==0x2345);
    CHECK(!memcmp(cdd_events[1].packet,packet2,sizeof packet2));
    CHECK(cdd_events[0].device_clock<=cdd_events[1].device_clock);
    m68k_step_write8(&c.cpu[SEGACD_SUB],0xFF8037,0); /* HOCK low cancels pending. */
    cdd_stage_packet(packet2);m68k_step_write8(&c.cpu[SEGACD_SUB],0xFF804B,packet2[9]);
    CHECK(segacd_coordinator_cdd_trace(&c,&cdd_snapshot,cdd_events,4)==2 && !cdd_snapshot.command_pending);
    m68k_step_write8(&c.cpu[SEGACD_SUB],0xFF8037,4);
    cdd_stage_packet(poll);m68k_step_write8(&c.cpu[SEGACD_SUB],0xFF804B,poll[9]);
    CHECK(segacd_coordinator_cdd_trace(&c,&cdd_snapshot,cdd_events,4)==2);
    CHECK(cdd_snapshot.command_pending && cdd_snapshot.pending[0]==0 && cdd_snapshot.status==cd.drive.status);
    for(unsigned n=0;n<70;++n) {
        uint8_t packet[10];for(unsigned i=0;i<10;++i)packet[i]=(uint8_t)((n+i+1u)&15u);
        packet[0]=(uint8_t)(n%15u+1u);c.cpu[SEGACD_SUB].state.instruction_pc=0x5000u+n;
        cdd_stage_packet(packet);m68k_step_write8(&c.cpu[SEGACD_SUB],0xFF804B,packet[9]);
    }
    SegaCDCddSubmission cdd_recent[SEGACD_CDD_TRACE_CAPACITY];
    CHECK(segacd_coordinator_cdd_trace(&c,&cdd_snapshot,cdd_recent,SEGACD_CDD_TRACE_CAPACITY)==SEGACD_CDD_TRACE_CAPACITY);
    CHECK(cdd_snapshot.accepted_total==72 && cdd_snapshot.retained_count==SEGACD_CDD_TRACE_CAPACITY);
    CHECK(cdd_recent[0].packet[0]==7 && cdd_recent[0].instruction_pc==0x5006);
    CHECK(cdd_recent[SEGACD_CDD_TRACE_CAPACITY-1].packet[0]==10 &&
          cdd_recent[SEGACD_CDD_TRACE_CAPACITY-1].instruction_pc==0x5045);
    puts("Coordinator CDD packet submission trace and two native original-ISA programs pass");return 0;
}
