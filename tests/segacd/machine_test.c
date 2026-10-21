#include "machine.h"
#include "fixtures/z80_native/proof_control_step.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);} } while(0)

static SegaCDMachine machine;
static uint8_t bios[SEGACD_BIOS_BYTES];
static M68kStepEntry main_entries[2];
static M68kStepModule main_module;
static const M68kStepModule *main_modules[]={&main_module};
static M68kStepEntry sub_entry;
static M68kStepModule sub_module;
static const M68kStepModule *sub_modules[]={&sub_module};
static M68kStepResult nested_result;
static unsigned audio_events;
static int try_reentry;
static unsigned video_rows;
static void video_observer(void *user,const SegaCDMachine *m,unsigned row)
{
    CHECK(user==&video_rows&&m==&machine);
    CHECK(row==video_rows%224u);
    CHECK(m->width==(unsigned)gvdp_screen_width(&m->vdp));
    ++video_rows;
    CHECK(segacd_machine_step(&machine)==M68K_STEP_REENTERED);
    CHECK(!segacd_machine_video_observer(&machine,NULL,NULL));
}

static void put32(uint8_t*p,uint32_t v)
{p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}

static void first_main_instruction(M68kStepContext *ctx)
{
    m68k_step_write8(ctx,0xA04000u,0x52u);
    ctx->state.regs.PC=0x102u;
}

static void loop_main_instruction(M68kStepContext *ctx)
{
    /* Read the real Genesis controller bus each pass; RIGHT is active-low in
     * the TH-high 3-button protocol. */
    ctx->state.regs.D[0]=m68k_step_read8(ctx,0xA10003u);
    ctx->state.regs.SR=0x2000u;
    ctx->state.regs.PC=0x102u;
}

static void loop_sub_instruction(M68kStepContext *ctx)
{
    ++ctx->state.regs.D[0];ctx->state.regs.PC=0x100u;
}

static void audio_observer(void *user,uint64_t clock,uint8_t port,uint8_t value)
{
    (void)clock;(void)port;(void)value;
    SegaCDMachine *m=user;++audio_events;
    if(try_reentry){try_reentry=0;nested_result=segacd_machine_step(m);}
}

static void setup(int with_main)
{
    memset(&machine,0,sizeof machine);memset(bios,0,sizeof bios);
    put32(bios,0xFFFF1800u);put32(bios+4,0x100u);
    /* VDP vertical interrupt autovector 30 returns to the same test loop. */
    put32(bios+30u*4u,0x102u);
    bios[0x100]=0x4E;bios[0x101]=0x71;bios[0x102]=0x4E;bios[0x103]=0x71;
    main_entries[0]=(M68kStepEntry){0x100u,{0x4E71u},1,8,first_main_instruction};
    main_entries[1]=(M68kStepEntry){0x102u,{0x4E71u},1,4,loop_main_instruction};
    main_module=(M68kStepModule){"synthetic native Genesis consumer",0x100u,4u,0,main_entries,2};
    sub_entry=(M68kStepEntry){0x100u,{0x4E71u},1,4,loop_sub_instruction};
    sub_module=(M68kStepModule){"synthetic native Sub consumer",0x100u,2u,0,&sub_entry,1};
    SegaCDNativeProgram program={with_main?main_modules:NULL,sub_modules,with_main?1u:0u,1,proof_control_step};
    CHECK(segacd_machine_init(&machine,bios,sizeof bios,&program));
    put32(machine.devices.prg,0x7FF00u);put32(machine.devices.prg+4,0x100u);
    machine.devices.prg[0x100]=0x4E;machine.devices.prg[0x101]=0x71;
    machine.audio_write=audio_observer;machine.audio_user=&machine;
    machine.bus.pad_type[0]=0;machine.bus.io_data[0]=0x40;
    segacd_machine_pad(&machine,0,GPAD_RIGHT|GPAD_START);
    machine.vdp.reg[1]|=0x20u; /* V interrupt enabled. */
}

static void test_full_frame_video_irq_and_controls(void)
{
    setup(1);
    video_rows=0;
    CHECK(segacd_machine_video_observer(&machine,video_observer,&video_rows));
    CHECK(segacd_write8(&machine.devices,SEGACD_MAIN,0xA12001u,1u)==SEGACD_OK);
    /* Start the generated HALT fixture with interrupts disabled. A real
     * vertical interrupt must queue without inventing a generated handler. */
    machine.bus.z80_reset_off=1;machine.bus.z80_ram[1]=0x76;
    machine.z80.state.pc=1;
    CHECK(segacd_machine_run_frame(&machine)==M68K_STEP_OK);
    CHECK(machine.frames==1 && machine.height==224 && machine.line>=262);
    CHECK(machine.vdp.vint_pending && machine.z80.irq_pending && machine.z80.state.halted);
    CHECK(machine.coordinator.native_steps[SEGACD_MAIN]>10000);
    /* Both 68000 timelines reach the raster boundary; shared devices never
     * run ahead of the independent Z80 master that caps the coordinator. */
    CHECK(machine.coordinator.time[SEGACD_MAIN]>=machine.coordinator.device_time);
    CHECK(machine.coordinator.time[SEGACD_SUB]>=machine.coordinator.device_time);
    CHECK(machine.coordinator.native_steps[SEGACD_SUB]>10000);
    CHECK(machine.coordinator.device_time<=machine.z80.time);
    CHECK((machine.coordinator.cpu[SEGACD_MAIN].state.regs.D[0]&0x08u)==0);
    CHECK(machine.irq6==0); /* vector 30 was taken and acknowledged */
    CHECK(machine.coordinator.cpu[SEGACD_MAIN].state.regs.A[7]==0xFFFF17FAu);
    CHECK(audio_events==1);
    CHECK(video_rows==224u);
    CHECK(segacd_machine_video_observer(&machine,NULL,NULL));
    segacd_machine_close(&machine);
}

static void test_machine_reentry_from_audio_observer(void)
{
    setup(1);try_reentry=1;nested_result=M68K_STEP_OK;
    CHECK(segacd_machine_step(&machine)==M68K_STEP_OK);
    CHECK(nested_result==M68K_STEP_REENTERED);
    CHECK(machine.coordinator.cpu[SEGACD_MAIN].state.instructions==1);
    CHECK(machine.result==M68K_STEP_OK && machine.z80.result==SEGACD_Z80_OK);
    segacd_machine_close(&machine);
}

static void test_native_miss_is_terminal_without_floor(void)
{
    setup(0);
    CHECK(segacd_machine_step(&machine)==M68K_STEP_MISS);
    uint64_t instructions=machine.coordinator.cpu[SEGACD_MAIN].state.instructions;
    CHECK(segacd_machine_step(&machine)==M68K_STEP_MISS);
    CHECK(machine.coordinator.cpu[SEGACD_MAIN].state.instructions==instructions);
    CHECK(machine.coordinator.cpu[SEGACD_MAIN].state.regs.PC==0x100u);
    segacd_machine_close(&machine);
}

int main(void)
{
    test_full_frame_video_irq_and_controls();
    test_machine_reentry_from_audio_observer();
    test_native_miss_is_terminal_without_floor();
    puts("native machine: frame/raster IRQ, controller bus, Z80 clock ceiling, reentry and strict missing-code stop pass");
    return 0;
}
