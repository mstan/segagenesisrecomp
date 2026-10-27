#include "segacd.h"
#include "segacd_coordinator.h"
#include "z80_native.h"
#include "fixtures/z80_native/proof_ldi_step.h"
#include "fixtures/z80_native/proof_control_step.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);} } while(0)

static SegaCD cd;
static SegaCDCoordinator coordinator;
static SegaCDZ80 z80;
static uint8_t bios[SEGACD_BIOS_BYTES],program[8],sound_ram[8192];
static unsigned local_reads,source_reads,local_writes,local_data_reads;
static uint16_t bank_register;
static M68kStepEntry sub_entries[17];
static M68kStepModule sub_module;
static const M68kStepModule *sub_modules[]={&sub_module};
static uint64_t z80_device_limit(void*u)
{SegaCDZ80*s=(SegaCDZ80*)u;return s->in_step?segacd_z80_bus_time(s):s->time;}

static void put32(uint8_t*p,uint32_t v)
{p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static M68kStepBusResult host_read8(void*u,uint32_t a,uint8_t*v)
{(void)u;(void)a;(void)v;return M68K_BUS_FAULT;}
static M68kStepBusResult host_read16(void*u,uint32_t a,uint16_t*v)
{(void)u;(void)a;(void)v;return M68K_BUS_FAULT;}
static M68kStepBusResult host_write8(void*u,uint32_t a,uint8_t v)
{(void)u;(void)a;(void)v;return M68K_BUS_FAULT;}
static M68kStepBusResult host_write16(void*u,uint32_t a,uint16_t v)
{(void)u;(void)a;(void)v;return M68K_BUS_FAULT;}

static uint8_t local_read(void*u,uint16_t a,uint64_t at)
{
    (void)u;(void)at;
    if(a<sizeof program){++local_reads;return program[a];}
    if(a==0x0100u)++source_reads;
    ++local_data_reads;
    return sound_ram[a&0x1FFFu];
}
static void local_write(void*u,uint16_t a,uint8_t v,uint64_t at)
{(void)u;(void)at;++local_writes;sound_ram[a&0x1FFFu]=v;}
static uint16_t local_bank(void*u){(void)u;return bank_register;}

static void sub_delay(M68kStepContext*ctx){ctx->state.regs.D[0]++;}
static void sub_grant(M68kStepContext*ctx){m68k_step_write8(ctx,0xFF8003u,1);m68k_step_stop(ctx,0x2000u);}

static void setup(const uint8_t*bytes,size_t size,int sub_owns_wordram,int enable_sub)
{
    memset(bios,0,sizeof bios);memset(program,0,sizeof program);memset(sound_ram,0,sizeof sound_ram);
    memset(&cd,0,sizeof cd);memset(&coordinator,0,sizeof coordinator);memset(&z80,0,sizeof z80);
    memset(sub_entries,0,sizeof sub_entries);local_reads=source_reads=local_writes=local_data_reads=0;bank_register=0x40;
    CHECK(size<=sizeof program);memcpy(program,bytes,size);
    put32(bios,0xFFFF1800u);put32(bios+4,0x00000100u);
    CHECK(segacd_init(&cd,bios,sizeof bios));
    put32(cd.prg,0x0007FF00u);put32(cd.prg+4,0x00000100u);
    CHECK(segacd_write8(&cd,SEGACD_MAIN,0xA12001u,(uint8_t)(enable_sub?1:0))==SEGACD_OK);
    CHECK(segacd_write8(&cd,SEGACD_MAIN,0xA12003u,(uint8_t)(sub_owns_wordram?2:1))==SEGACD_OK);
    cd.word[0]=0x5A;sound_ram[0x100]=0xA6;
    M68kStepBus host={host_read8,host_read16,host_write8,host_write16,host_read16,NULL,NULL,NULL};
    CHECK(segacd_coordinator_init(&coordinator,&cd,&host,NULL));
    coordinator.cpu[SEGACD_MAIN].state.stopped=1;
    if(enable_sub) {
        for(unsigned i=0;i<16;++i)sub_entries[i]=(M68kStepEntry){0x100u+2u*i,{0x4E71u},1,4,sub_delay};
        sub_entries[16]=(M68kStepEntry){0x120u,{0x4E71u},1,4,sub_grant};
        for(unsigned i=0;i<17;++i){cd.prg[sub_entries[i].pc]=0x4E;cd.prg[sub_entries[i].pc+1]=0x71;}
        sub_module=(M68kStepModule){"synthetic Word RAM grant",0x100u,0x100u,0,sub_entries,17};
        CHECK(segacd_coordinator_modules(&coordinator,SEGACD_SUB,sub_modules,1));
    }
    SegaCDZ80Bus zbus={local_read,local_write,local_bank};
    CHECK(segacd_z80_init(&z80,&coordinator,&zbus,NULL,proof_ldi_step));
    coordinator.host_user=&z80;
    segacd_coordinator_device_limit(&coordinator,z80_device_limit);
}

static void test_rational_clock_and_hold_reset(void)
{
    static const uint8_t ldi[]={0xED,0xA0,0,0};
    setup(ldi,sizeof ldi,0,0);
    for(unsigned i=0;i<100;++i){z80.state.pc=2;CHECK(segacd_z80_step(&z80)==SEGACD_Z80_OK);}
    const uint64_t numerator=187500000u,denominator=53693175u,cycles=400u;
    CHECK(z80.time==(cycles*numerator)/denominator);
    CHECK(z80.remainder==(cycles*numerator)%denominator);
    CHECK(coordinator.device_time==z80.time);

    setup(ldi,sizeof ldi,0,0);
    CHECK(segacd_z80_hold_until(&z80,17));
    CHECK(!segacd_z80_hold_until(&z80,16));
    z80.state.pc=2;
    CHECK(segacd_z80_step(&z80)==SEGACD_Z80_OK);
    CHECK(z80.time==17+(4u*numerator)/denominator);
    CHECK(z80.remainder==(4u*numerator)%denominator);
    CHECK(coordinator.device_time==z80.time);
    z80.state.iff1=z80.state.iff2=1;z80.state.halted=1;segacd_z80_irq(&z80);
    segacd_z80_reset(&z80);
    CHECK(z80.state.pc==0 && !z80.state.iff1 && !z80.state.iff2 && !z80.state.halted);
    CHECK(z80.irq_pending==0 && z80.time==coordinator.device_time && z80.remainder==0);
}

static void init_ldi_state(void)
{
    z80.state.a=0x3C;z80.state.f=0xC1;z80.state.h=0x01;z80.state.l=0x00;
    z80.state.d=0x80;z80.state.e=0x00;z80.state.b=0;z80.state.c=1;
    z80.state.sp=0x7000;z80.state.r=0x80;z80.state.cyc=40;
}
static void test_generated_ldi_wait_and_no_replay(void)
{
    static const uint8_t ldi[]={0xED,0xA0,0,0};
    setup(ldi,sizeof ldi,1,1);init_ldi_state();
    {SegaCDZ80Result r=segacd_z80_step(&z80);if(r!=SEGACD_Z80_OK)fprintf(stderr,"LDI result=%u bus=%u pc=%04X time=%llu device=%llu word=%02X mode=%u substeps=%llu\n",(unsigned)r,(unsigned)z80.bus_result,z80.state.pc,(unsigned long long)z80.time,(unsigned long long)coordinator.device_time,cd.word[0],cd.memory_mode,(unsigned long long)coordinator.native_steps[1]);CHECK(r==SEGACD_Z80_OK);}
    if(!(cd.word[0]==0xA6 && source_reads==1 && z80.retired==1))fprintf(stderr,"word=%02X source=%u retired=%llu memory=%u d0=%u sub=%llu device=%llu result=%u bus=%u\n",cd.word[0],source_reads,(unsigned long long)z80.retired,cd.memory_mode,coordinator.cpu[1].state.regs.D[0],(unsigned long long)coordinator.native_steps[1],(unsigned long long)coordinator.device_time,(unsigned)z80.result,(unsigned)z80.bus_result);
    CHECK(cd.word[0]==0xA6 && source_reads==1 && z80.retired==1);
    CHECK(coordinator.native_steps[SEGACD_SUB]==17 && cd.memory_mode==1);
    CHECK(z80.state.pc==2 && z80.state.cyc==56 && z80.state.h==0x01 && z80.state.l==1);
    CHECK(z80.state.d==0x80 && z80.state.e==1 && z80.state.b==0 && z80.state.c==0);
    if(!(z80.state.f==0xE1 && z80.wait_ticks>0))fprintf(stderr,"f=%02X wait=%llu time=%llu device=%llu\n",z80.state.f,(unsigned long long)z80.wait_ticks,(unsigned long long)z80.time,(unsigned long long)coordinator.device_time);
    CHECK(z80.state.f==0xE1 && z80.wait_ticks>0);
}

static void test_native_miss_is_terminal(void)
{
    static const uint8_t wrong[]={0x00,0x00,0,0};
    setup(wrong,sizeof wrong,0,0);
    CHECK(segacd_z80_step(&z80)==SEGACD_Z80_MISS);
    CHECK(z80.miss_pc==0 && z80.bus_result==M68K_STEP_MISS && z80.retired==0);
    unsigned reads=local_reads;uint64_t time=z80.time;
    CHECK(segacd_z80_step(&z80)==SEGACD_Z80_MISS);
    CHECK(local_reads==reads && z80.time==time && z80.state.pc==0);
}

static void test_ei_delay_and_halt_irq(void)
{
    static const uint8_t ei_halt[]={0xFB,0x76};
    setup(ei_halt,sizeof ei_halt,0,0);
    z80.step=proof_control_step;z80.state.sp=0x7000;
    segacd_z80_irq(&z80);
    CHECK(segacd_z80_step(&z80)==SEGACD_Z80_OK);
    CHECK(z80.state.iff1 && z80.state.iff2 && z80.irq_pending);
    CHECK(segacd_z80_step(&z80)==SEGACD_Z80_OK);
    CHECK(!z80.state.halted && !z80.state.iff1 && !z80.state.iff2);
    CHECK(!z80.irq_pending && z80.state.pc==0x0038 && z80.state.sp==0x6FFEu);
    CHECK(sound_ram[0x0FFE]==0x02 && sound_ram[0x0FFF]==0x00);

    setup(ei_halt,sizeof ei_halt,0,0);z80.step=proof_control_step;
    z80.state.pc=1;z80.state.iff1=z80.state.iff2=1;z80.state.sp=0x7000;
    CHECK(segacd_z80_step(&z80)==SEGACD_Z80_OK && z80.state.halted && z80.state.pc==2);
    uint64_t halt_cycles=z80.state.cyc;
    CHECK(segacd_z80_step(&z80)==SEGACD_Z80_OK && z80.state.halted && z80.state.pc==2);
    CHECK(z80.state.cyc==halt_cycles+4);
    segacd_z80_irq(&z80);
    CHECK(segacd_z80_step(&z80)==SEGACD_Z80_OK && !z80.state.halted);
    CHECK(!z80.irq_pending && z80.state.pc==0x0038 && z80.state.sp==0x6FFEu);
}

static void test_io_in_out_are_unsupported_without_memory_access(void)
{
    static const uint8_t io_program[]={0xFB,0x76,0xDB,0x01,0xD3,0x01,0x00,0x00};
    setup(io_program,sizeof io_program,0,0);
    z80.step=proof_control_step;
    z80.state.pc=2;z80.state.a=0x7F;z80.state.b=0x7F;z80.state.c=0x01;
    CHECK(segacd_z80_step(&z80)==SEGACD_Z80_BUS_ERROR);
    CHECK(z80.bus_result==M68K_STEP_UNSUPPORTED && z80.state.pc==4 && z80.state.cyc==11);
    CHECK(z80.retired==0 && local_data_reads==0 && local_writes==0);
    CHECK(coordinator.device_time==z80.time);
    unsigned reads=local_reads;uint64_t time=z80.time;
    CHECK(segacd_z80_step(&z80)==SEGACD_Z80_BUS_ERROR);
    CHECK(z80.bus_result==M68K_STEP_UNSUPPORTED && z80.time==time && local_reads==reads);

    setup(io_program,sizeof io_program,0,0);
    z80.step=proof_control_step;
    z80.state.pc=4;z80.state.a=0x7F;z80.state.b=0x7F;z80.state.c=0x01;
    CHECK(segacd_z80_step(&z80)==SEGACD_Z80_BUS_ERROR);
    CHECK(z80.bus_result==M68K_STEP_UNSUPPORTED && z80.state.pc==6 && z80.state.cyc==11);
    CHECK(z80.retired==0 && local_data_reads==0 && local_writes==0);
    CHECK(coordinator.device_time==z80.time);
    reads=local_reads;time=z80.time;
    CHECK(segacd_z80_step(&z80)==SEGACD_Z80_BUS_ERROR);
    CHECK(z80.bus_result==M68K_STEP_UNSUPPORTED && z80.time==time && local_reads==reads);
}

int main(void)
{
    test_rational_clock_and_hold_reset();
    test_generated_ldi_wait_and_no_replay();
    test_native_miss_is_terminal();
    test_ei_delay_and_halt_irq();
    test_io_in_out_are_unsupported_without_memory_access();
    puts("native Z80: generated LDI WAIT, rational clock, held/reset phase, terminal miss, EI/HALT IRQ, and terminal unmapped I/O pass");
    return 0;
}
