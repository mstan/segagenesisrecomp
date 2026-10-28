#include "segacd_coordinator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static SegaCD cd,reference_cd;
static SegaCDCoordinator c;
static uint8_t bios[SEGACD_BIOS_BYTES];
typedef struct {uint64_t time;uint32_t pc[2],value[2];unsigned kind;} Observation;
static Observation observations[8192],reference_observations[8192];
static unsigned observation_count;static uint64_t host_event;
static unsigned device_case;
static unsigned staged_clock;static uint64_t clock_ceiling;
static uint64_t third_master_limit(void *user){(void)user;return clock_ceiling;}
static void observe(unsigned kind)
{
    CHECK(observation_count<8192);
    Observation *o=&observations[observation_count++];memset(o,0,sizeof *o);
    o->time=c.device_time;o->kind=kind;
    for(unsigned n=0;n<2;++n){o->pc[n]=c.cpu[n].state.regs.PC;o->value[n]=c.cpu[n].state.regs.D[0];}
}
static void host_advance(void *user,uint64_t now)
{(void)user;if(now>=host_event){observe(2);host_event=now+31;}}
static uint64_t host_next(void *user){(void)user;return host_event;}
static void pure(M68kStepContext *cpu)
{
    ++cpu->state.regs.D[0];cpu->state.regs.SR=(uint16_t)((cpu->state.regs.SR&0xffe0u)|(cpu->state.regs.D[0]&31));
    cpu->charge=4+(cpu->state.regs.D[0]&3);cpu->state.regs.PC=(cpu->state.regs.D[0]%7)?0x100:0x102;
}
static void observable(M68kStepContext *cpu)
{
    observe(cpu==&c.cpu[0]?0:1);
    uint32_t address=cpu==&c.cpu[0]?0xA12010u:0xFF8020u;
    m68k_step_write16(cpu,address,(uint16_t)cpu->state.regs.D[0]);
    cpu->state.regs.PC=0x100;
}
static const M68kStepEntry entries[]={
    {0x100,{0x4e71},1,4,pure},{0x102,{0x4e71},1,8,observable}
};
static const M68kStepModule module={"region-fixture",0x100,4,0,entries,2};
static const M68kStepModule *const modules[]={&module};
static const uint8_t flags[]={1,0};
static const M68kStepEffectModule effects[]={{&module,flags}};
static void arbitrary(M68kStepContext *cpu)
{++cpu->state.regs.D[7];cpu->state.regs.PC=0x100;}
static const M68kStepEntry unknown_entries[]={{0x100,{0x4e71},1,4,arbitrary}};
static const M68kStepModule unknown_module={"unknown-priority",0x100,2,0,unknown_entries,1};
static const M68kStepModule *const priority_modules[]={&unknown_module,&module};
static void put32(uint8_t *p,uint32_t v)
{p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void setup(int qualified,int enable_sub)
{
    memset(bios,0,sizeof bios);put32(bios,0xff1000);put32(bios+4,0x100);
    bios[0x100]=bios[0x102]=0x4e;bios[0x101]=bios[0x103]=0x71;
    CHECK(segacd_init(&cd,bios,sizeof bios));
    put32(cd.prg,0x7ff00);put32(cd.prg+4,0x100);
    memcpy(cd.prg+0x100,bios+0x100,4);
    CHECK(segacd_write8(&cd,SEGACD_MAIN,0xA12001u,(uint8_t)enable_sub)==SEGACD_OK);
    if(device_case) {
        /* Real six-clock CDC->PRG writes, including executable self-modification. */
        cd.cdc_destination=5;cd.cdc_dma_address=(uint16_t)(device_case==2?0x20:0x60);
        cd.cdc.ifctrl=2;cd.cdc.dbc=15;cd.cdc.dac=0;
        for(unsigned i=0;i<16;++i)cd.cdc.ram[i]=(uint8_t)(0x40+i);
        CHECK(segacd_dma_begin(&cd,&cd.dma)==SEGACD_DMA_OK);
        segacd_cdc_address(&cd.cdc,6);segacd_cdc_write(&cd.cdc,1);
        /* Four actual affine line completions and a periodic IRQ deadline. */
        CHECK(segacd_write8(&cd,SEGACD_MAIN,0xA12003,2)==SEGACD_OK);
        CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF805A,0x1000)==SEGACD_OK);
        CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF805C,1)==SEGACD_OK);
        CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF805E,0x4000)==SEGACD_OK);
        CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF8062,1)==SEGACD_OK);
        CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF8064,4)==SEGACD_OK);
        CHECK(segacd_write16(&cd,SEGACD_SUB,0xFF8066,0x1800)==SEGACD_OK);
        CHECK(segacd_write8(&cd,SEGACD_SUB,0xFF8031,1)==SEGACD_OK);
    }
    CHECK(segacd_coordinator_init(&c,&cd,NULL,NULL));
    CHECK(segacd_coordinator_modules(&c,SEGACD_MAIN,modules,1));
    CHECK(segacd_coordinator_modules(&c,SEGACD_SUB,modules,1));
    if(qualified) {
        CHECK(segacd_coordinator_region_effects(&c,SEGACD_MAIN,effects,1));
        CHECK(segacd_coordinator_region_effects(&c,SEGACD_SUB,effects,1));
    }
    observation_count=0;memset(observations,0,sizeof observations);host_event=31;
    segacd_coordinator_host_clock(&c,host_advance,host_next);
    if(staged_clock){clock_ceiling=0;segacd_coordinator_device_limit(&c,third_master_limit);}
}
static M68kStepResult run_to(unsigned stop,unsigned isolated)
{
    if(staged_clock)for(unsigned next=13;next<stop;next+=13) {
        clock_ceiling=next;
        M68kStepResult result=isolated?segacd_coordinator_run_isolated_until(&c,next):segacd_coordinator_run_until(&c,next);
        if(result!=M68K_STEP_OK)return result;
    }
    clock_ceiling=stop;
    M68kStepResult result=isolated?segacd_coordinator_run_isolated_until(&c,stop):segacd_coordinator_run_until(&c,stop);
    if(isolated&&result==M68K_STEP_OK)result=segacd_coordinator_run_until(&c,stop);
    return result;
}
int main(void)
{
    for(staged_clock=0;staged_clock<2;++staged_clock)
    for(device_case=0;device_case<3;++device_case)
    for(int sub=0;sub<2;++sub)for(unsigned stop=1;stop<4000;stop+=37) {
        setup(0,sub);M68kStepResult result=run_to(stop,0);
        CHECK(result==M68K_STEP_OK || (device_case==2&&result==M68K_STEP_MISS));
        reference_cd=cd;M68kStepState state[2]={c.cpu[0].state,c.cpu[1].state};
        uint64_t time[3]={c.time[0],c.time[1],c.main_remainder};
        unsigned count=observation_count;memcpy(reference_observations,observations,sizeof observations);
      for(unsigned isolated=0;isolated<2;++isolated) {
        setup(1,sub);
        CHECK(run_to(stop,isolated)==result);
        CHECK(!memcmp(&reference_cd,&cd,sizeof cd));
        CHECK(!memcmp(&state[0],&c.cpu[0].state,sizeof state[0]));
        CHECK(!memcmp(&state[1],&c.cpu[1].state,sizeof state[1]));
        CHECK(time[0]==c.time[0]&&time[1]==c.time[1]&&time[2]==c.main_remainder);
        CHECK(count==observation_count&&!memcmp(reference_observations,observations,count*sizeof *observations));
        if(stop>100&&device_case!=2)CHECK(c.region_runs&&c.region_instructions[0]);
      }
    }
    device_case=staged_clock=0;
    setup(1,1);CHECK(segacd_coordinator_run_until(&c,100)==M68K_STEP_OK);
    /* Complete live guard, unknown callback and qualification replacement exits. */
    c.cpu[0].state.regs.PC=c.cpu[1].state.regs.PC=0x100;cd.bios[0x101]^=1;cd.prg[0x101]^=1;
    CHECK(!segacd_coordinator_native_region(&c,c.device_time+100));
    cd.bios[0x101]^=1;cd.prg[0x101]^=1;
    setup(1,0);CHECK(segacd_coordinator_run_until(&c,100)==M68K_STEP_OK);
    CHECK(segacd_coordinator_modules(&c,SEGACD_MAIN,priority_modules,2));
    c.cpu[0].state.regs.PC=0x100;
    CHECK(segacd_coordinator_step(&c)==M68K_STEP_OK&&c.cpu[0].state.regs.D[7]==1);
    CHECK(!segacd_coordinator_native_region(&c,c.device_time+100));
    for(unsigned qualified=0;qualified<2;++qualified) {
        setup((int)qualified,0);CHECK(segacd_coordinator_run_until(&c,100)==M68K_STEP_OK);
        c.cpu[0].state.regs.SR|=0x8000u;
        M68kStepResult result=segacd_coordinator_run_until(&c,200);
        CHECK(result==M68K_STEP_BUS_ERROR);
        if(!qualified){reference_cd=cd;memcpy(reference_observations,&c.cpu[0].state,sizeof c.cpu[0].state);}
        else {CHECK(!memcmp(&reference_cd,&cd,sizeof cd));CHECK(!memcmp(reference_observations,&c.cpu[0].state,sizeof c.cpu[0].state));}
    }
    setup(1,0);
    CHECK(segacd_coordinator_region_effects(&c,SEGACD_MAIN,NULL,0));
    CHECK(segacd_coordinator_region_effects(&c,SEGACD_SUB,NULL,0));
    CHECK(!segacd_coordinator_native_region(&c,c.device_time+100));
    puts("regions preserve CPU/device/observer order across 1308 cases including staged third-master ceilings and DMA/stamp/timer");
    return 0;
}
