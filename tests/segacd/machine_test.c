#include "machine.h"
#include "sonic_cd_widescreen.h"
#include "../../runner/video/genesis_dac.h"
#include "fixtures/z80_native/proof_control_step.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);} } while(0)

static SegaCDMachine machine;
extern uint8_t g_ram[65536];
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
    CHECK(!segacd_machine_defer_video(&machine,1));
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

static unsigned fault_mode,floor_returned;
static void fault_main(M68kStepContext *ctx)
{
    m68k_step_write16(ctx,0xff0400,0x1234);++ctx->state.regs.D[3];
    if(fault_mode==0||fault_mode==4)(void)m68k_step_read16(ctx,1);
    else if(fault_mode==1)(void)m68k_step_read16(ctx,0x200000);
    ctx->state.regs.PC=0x100;
}
static void fault_sub(M68kStepContext *ctx)
{
    m68k_step_write16(ctx,0x500,0x5678);++ctx->state.regs.D[4];
    if(fault_mode==5&&ctx->state.regs.D[4]<10){ctx->state.regs.PC=0x100;return;}
    (void)m68k_step_read16(ctx,0xe0000);++ctx->state.regs.D[5];
}
static M68kStepResult translating_floor(M68kStepContext *ctx)
{
    CHECK(m68k_step_execute_live(ctx,fault_main)==M68K_STEP_ADDRESS_ERROR);
    ++floor_returned;return M68K_STEP_UNSUPPORTED;
}
static void test_native_region_fault_parity(void)
{
    SegaCDMachine *reference=malloc(sizeof *reference),*normalized=malloc(sizeof *normalized);
    CHECK(reference&&normalized);
    uint8_t reference_ram[65536];M68kStepResult expected=M68K_STEP_OK;unsigned expected_floor=0;
    for(unsigned mode=0;mode<6;++mode)for(unsigned arm=0;arm<2;++arm) {
        fault_mode=mode;floor_returned=0;setup(mode!=4);
        main_entries[0].execute=fault_main;sub_entry.execute=fault_sub;
        if((mode>=1&&mode<=3)||mode==5)machine.devices.main_control=1;
        if(mode==1)machine.devices.memory_mode=0; /* Real Main WAIT, nested Sub failure. */
        if(mode==2) {
            CHECK(m68k_step_reset(&machine.coordinator.cpu[1])==M68K_STEP_OK);
            machine.coordinator.sub_initialized=machine.coordinator.sub_was_running=1;
            machine.coordinator.cpu[0].state.stopped=1;machine.coordinator.time[1]=20;
        }
        if(mode==3||mode==5){machine.bus.z80_reset_off=1;machine.bus.z80_ram[1]=0x76;machine.z80.state.pc=1;}
        if(mode==5) {
            machine.z80.state.iff1=machine.z80.state.iff2=true;machine.z80.state.im=1;
            machine.z80.state.sp=0x1000;machine.z80.irq_pending=1;
        }
        if(mode==4)CHECK(segacd_coordinator_floor(&machine.coordinator,translating_floor));
        M68kStepResult result;
        if(arm)result=segacd_machine_run_frame(&machine);
        else {do{result=segacd_machine_step(&machine);}while(result==M68K_STEP_OK&&!machine.frames);}
        CHECK(result!=M68K_STEP_OK);
        CHECK(!machine.coordinator.busy[0]&&!machine.coordinator.busy[1]&&!machine.z80.in_step);
        CHECK(!machine.coordinator.cpu[0].fault_escape&&!machine.coordinator.cpu[1].fault_escape);
        if(mode==5)CHECK(machine.z80.state.sp==0xffe&&!machine.z80.state.iff1&&!machine.z80.irq_pending);
        *normalized=machine;
        normalized->coordinator.catalog_index[0]=normalized->coordinator.catalog_index[1]=NULL;
#if defined(M68K_STEP_HAS_EXECUTION_SCOPES) && M68K_STEP_HAS_EXECUTION_SCOPES
        normalized->coordinator.cpu[0].scope=normalized->coordinator.cpu[1].scope=NULL;
#endif
        if(!arm){*reference=*normalized;memcpy(reference_ram,g_ram,sizeof reference_ram);expected=result;expected_floor=floor_returned;}
        else {
            CHECK(result==expected&&floor_returned==expected_floor);
            if(memcmp(reference,normalized,sizeof *normalized)) {
                unsigned shown=0;
                for(size_t i=0;i<sizeof *normalized&&shown<12;++i)
                    if(((uint8_t *)reference)[i]!=((uint8_t *)normalized)[i]) {
                        fprintf(stderr,"mode%u byte%zu old%u new%u (coordinator%zu z80%zu bus%zu)\n",mode,i,
                            ((uint8_t *)reference)[i],((uint8_t *)normalized)[i],
                            offsetof(SegaCDMachine,coordinator),offsetof(SegaCDMachine,z80),offsetof(SegaCDMachine,bus));++shown;
                    }
            }
            CHECK(!memcmp(reference,normalized,sizeof *normalized));
            CHECK(!memcmp(reference_ram,g_ram,sizeof reference_ram));
        }
        segacd_machine_close(&machine);
    }
    free(reference);free(normalized);
    puts("native region faults: exact single-step/frame CPU/device/clock/partial RAM, nested WAIT, STOP peer, Z80 and floor cleanup");
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
    CHECK(segacd_machine_defer_video(&machine,1));
    CHECK(segacd_machine_run_frame(&machine)==M68K_STEP_OK);
    CHECK(video_rows==448u);
    SegaCDMachineVideoStats video_stats;
    CHECK(segacd_machine_video_stats(&machine,&video_stats));
    CHECK(video_stats.captured_rows==240u&&video_stats.composed_rows==0);
    CHECK(segacd_machine_pixels(&machine));
    CHECK(segacd_machine_video_stats(&machine,&video_stats)&&video_stats.composed_rows==240u);
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

static void test_raster_deadline_counter_replacement(void)
{
    setup(1);
    const uint64_t lines[]={0,1,262,UINT64_C(0xffffff),3,UINT64_MAX,0,UINT64_MAX,0};
    for(unsigned i=0;i<sizeof lines/sizeof *lines;++i) {
        machine.line=lines[i];
        for(unsigned repeat=0;repeat<3;++repeat)
            CHECK(machine.coordinator.host_next_event(&machine)==segacd_machine_line_clock(lines[i]));
    }
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

static void test_widescreen_original_frame_and_safe_teardown(void)
{
    setup(1);
    SegaCDNativeProgram program={main_modules,sub_modules,1,1,proof_control_step};
    SegaCDWidescreen *wide=segacd_widescreen_create(&machine,&program);
    CHECK(wide&&segacd_widescreen_state(wide)==SEGACD_WIDESCREEN_NATIVE);
    uint64_t main_steps=machine.coordinator.native_steps[0];
    CHECK(segacd_widescreen_prepare(wide,400));
    CHECK(segacd_widescreen_state(wide)==SEGACD_WIDESCREEN_ORIGINAL_SCENE);
    CHECK(machine.coordinator.native_steps[0]==main_steps);
    CHECK(machine.coordinator.main_remainder==0&&machine.frames==0);
    CHECK(machine.coordinator.modules[0]==main_modules);
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x)
        machine.pixels[y][x]=0xff000000u|(y*320u+x);
    unsigned width,height,pitch;
    const uint32_t *pixels=segacd_widescreen_frame(wide,&width,&height,&pitch);
    CHECK(pixels&&width==400&&height==224&&pitch==1600);
    CHECK(pixels[0]==0xff000000u&&pixels[39]==0xff000000u);
    CHECK(pixels[40]==machine.pixels[0][0]);
    CHECK(pixels[359]==machine.pixels[0][319]&&pixels[360]==0xff000000u);
    CHECK(pixels[223u*400u+100u]==machine.pixels[223][60]);
    CHECK(!segacd_widescreen_prepare(wide,319));
    CHECK(segacd_widescreen_prepare(wide,455));
    pixels=segacd_widescreen_frame(wide,&width,&height,&pitch);
    CHECK(pixels&&width==454&&pitch==454u*sizeof(uint32_t));
    CHECK(pixels[67]==machine.pixels[0][0]);
    CHECK(segacd_widescreen_prepare(wide,320));
    CHECK(segacd_widescreen_state(wide)==SEGACD_WIDESCREEN_NATIVE);
    CHECK(segacd_widescreen_frame(wide,&width,&height,&pitch)==&machine.pixels[0][0]);
    CHECK(width==320&&pitch==GVDP_MAX_WIDTH*sizeof(uint32_t));
    /* A failed observer detach must retain the context. The real machine can
     * still run with that callback and a later safe teardown must succeed. */
    machine.coordinator.busy[0]=1;
    CHECK(!segacd_widescreen_destroy(wide));
    machine.coordinator.busy[0]=0;
    CHECK(segacd_widescreen_prepare(wide,400));
    CHECK(segacd_machine_run_frame(&machine)==M68K_STEP_OK);
    CHECK(machine.frames==1&&!machine.coordinator.floor_steps[0]&&
          !machine.coordinator.floor_steps[1]);
    CHECK(segacd_widescreen_destroy(wide));
    CHECK(segacd_machine_step(&machine)==M68K_STEP_OK);
    segacd_machine_close(&machine);
}

static uint32_t eager_pixels[GVDP_MAX_HEIGHT][GVDP_MAX_WIDTH];
static GVDP eager_vdp;
static uint32_t raster_rng=0x719abc23u;
static uint32_t raster_random(void)
{raster_rng^=raster_rng<<13;raster_rng^=raster_rng>>17;raster_rng^=raster_rng<<5;return raster_rng;}

static void advance_raster_line(void)
{
    machine.coordinator.host_advance(&machine,segacd_machine_line_clock(machine.line));
}

static void test_deferred_raster_contract(void)
{
    uint8_t ext_tiles[2048]={0};
    setup(1);gvdp_set_ws_extra(0);gvdp_set_ws_canvas(0);gvdp_set_bgdiag(0);
    gvdp_set_host_sprites(NULL,NULL);gvdp_set_ext_tiles(NULL);
    for(unsigned i=0;i<sizeof machine.vdp.vram;++i)machine.vdp.vram[i]=(uint8_t)raster_random();
    for(unsigned i=0;i<GVDP_VSRAM_ENTRIES;++i)machine.vdp.vsram[i]=(uint16_t)raster_random();
    machine.vdp.reg[1]=0x40;machine.vdp.reg[2]=0x30;machine.vdp.reg[3]=0x28;
    machine.vdp.reg[4]=7;machine.vdp.reg[5]=0x40;machine.vdp.reg[13]=0x3e;
    machine.vdp.reg[16]=0x11;machine.vdp.reg[11]=7;
    memset(machine.pixels,0x71,sizeof machine.pixels);memcpy(eager_pixels,machine.pixels,sizeof eager_pixels);
    eager_vdp=machine.vdp;
    CHECK(segacd_machine_defer_video(&machine,1));
    SegaCDMachineVideoStats stats;
    CHECK(segacd_machine_video_stats(&machine,&stats)&&stats.deferred&&stats.retained_bytes>0);
    /* Drive the installed host raster callback directly, retaining independent
     * eager row production. This isolates raster ownership from CPU scheduling,
     * which the full-frame tests above already exercise. */
    for(unsigned tick=0;tick<262u*4u;++tick) {
        unsigned line=(unsigned)(machine.line%262u),row=(line+261u)%262u;
        unsigned frame=(unsigned)(machine.line/262u);
        int canvas=frame==0?448:frame==1?0:frame==2?511:0;
        /* An unsupported optional provider retains the eager path, including
         * an old wider row's tail and palette captured on the prior frame. */
        gvdp_set_ext_tiles(frame==3&&row>=50&&row<=75?ext_tiles:NULL);
        gvdp_set_ws_canvas(canvas);gvdp_set_ws_bar_black(frame&1u);gvdp_set_bgdiag(frame==2);
        machine.vdp.reg[1]=(uint8_t)(row%11u?0x40:0);
        machine.vdp.reg[12]=(uint8_t)((frame&1u?0:1)|(row%3u?8:0));
        machine.vdp.reg[7]=(uint8_t)(row&63u);
        memcpy(eager_vdp.reg,machine.vdp.reg,sizeof eager_vdp.reg);
        for(unsigned i=0;i<GVDP_CRAM_ENTRIES;++i)
            machine.vdp.cram[i]=eager_vdp.cram[i]=(uint16_t)raster_random();
        /* Direct storage writes and complete live state replacement must not
         * invalidate already captured inputs or use a stale derived cache. */
        unsigned a=raster_random()&65535u;
        machine.vdp.vram[a]=eager_vdp.vram[a]=(uint8_t)raster_random();
        if(machine.line&&row<GVDP_MAX_HEIGHT) {
            uint8_t indices[GVDP_MAX_WIDTH];
            int width=gvdp_render_scanline(&eager_vdp,(int)row,indices);
            for(int x=0;x<width;++x) {
                unsigned i=indices[x];eager_pixels[row][x]=i<192?
                    genesis_dac_cram_to_argb(eager_vdp.cram[i%64],(int)(i/64)):0xff000000u;
            }
        }
        (void)gvdp_begin_scanline(&eager_vdp,(int)line);
        advance_raster_line();
        CHECK(!memcmp(&machine.vdp,&eager_vdp,sizeof eager_vdp));
        /* Mid-frame reads are sparse enough that old packets are overwritten
         * and narrowed, while still checking partially updated prior frames. */
        if(tick==137||tick==262u*3u+91u||tick==262u*4u-1u) {
            GVDP before=machine.vdp;uint64_t steps=machine.coordinator.native_steps[0];
            CHECK(segacd_machine_pixels(&machine)==&machine.pixels[0][0]);
            CHECK(!memcmp(eager_pixels,machine.pixels,sizeof eager_pixels));
            CHECK(!memcmp(&before,&machine.vdp,sizeof before));
            CHECK(steps==machine.coordinator.native_steps[0]);
        }
    }
    CHECK(segacd_machine_video_stats(&machine,&stats));
    CHECK(stats.captured_rows==GVDP_MAX_HEIGHT*4u-26u&&stats.eager_rows==26u);
    CHECK(stats.composed_rows>0&&stats.composed_rows<stats.captured_rows);
    printf("deferred raster: %llu captured, %llu composed, %zu retained bytes; exact eager pixels/state\n",
        (unsigned long long)stats.captured_rows,(unsigned long long)stats.composed_rows,stats.retained_bytes);
    uint64_t before=stats.captured_rows;
    CHECK(segacd_machine_video_observer(&machine,NULL,NULL));
    CHECK(segacd_machine_video_stats(&machine,&stats)&&!stats.deferred);
    CHECK(!memcmp(eager_pixels,machine.pixels,sizeof eager_pixels));
    for(unsigned i=0;i<262u;++i)advance_raster_line();
    CHECK(segacd_machine_video_stats(&machine,&stats)&&stats.captured_rows==before);
    CHECK(stats.eager_rows==GVDP_MAX_HEIGHT+26u);
    machine.coordinator.busy[0]=1;CHECK(!segacd_machine_defer_video(&machine,1));machine.coordinator.busy[0]=0;
    segacd_machine_close(&machine);
    CHECK(!segacd_machine_pixels(&machine));
    CHECK(!segacd_machine_video_stats(&machine,&stats));
    gvdp_set_ws_canvas(0);gvdp_set_bgdiag(0);gvdp_set_ws_bar_black(0);
}

int main(void)
{
    test_full_frame_video_irq_and_controls();
    test_machine_reentry_from_audio_observer();
    test_native_region_fault_parity();
    test_raster_deadline_counter_replacement();
    test_native_miss_is_terminal_without_floor();
    test_widescreen_original_frame_and_safe_teardown();
    test_deferred_raster_contract();
    puts("native machine: frame/raster IRQ, controller bus, Z80 clock ceiling, reentry and strict missing-code stop pass");
    return 0;
}
