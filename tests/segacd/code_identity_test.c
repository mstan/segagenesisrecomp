#include "segacd_coordinator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static SegaCD cd;
static SegaCDCoordinator c;
static SegaCDCodeIdentity id;
static uint8_t bios[SEGACD_BIOS_BYTES],ram[0x10000];
static void loop(M68kStepContext *cpu){++cpu->state.regs.D[0];cpu->state.regs.PC=0x100;}
static void direct(M68kStepContext *cpu){cd.prg[0x103]^=1;loop(cpu);}
static void fault(M68kStepContext *cpu){cd.prg[0x103]^=1;m68k_step_read16(cpu,1);}
static const M68kStepEntry entry={0x100,{0x4e71,0x1234},2,4,loop};
static const M68kStepEntry other={0x100,{0x4e71,0x1235},2,4,loop};
static const M68kStepEntry opaque={0x100,{0x4e71,0x1234},2,4,direct};
static const M68kStepEntry bad={0x100,{0x4e71,0x1235},2,4,fault};
static M68kStepResult nested_result;
static void nested(M68kStepContext *cpu);
static void sink(void *u,int16_t l,int16_t r)
{(void)u;(void)l;(void)r;cd.prg[0x103]^=1;}
static void observer(void *u,int entering)
{segacd_coordinator_code_observer(u,entering);}
static void setup(void)
{
    memset(&id,0,sizeof id);memset(bios,0,sizeof bios);
    bios[1]=7;bios[2]=0xff;bios[5]=0;bios[6]=1;
    CHECK(segacd_init(&cd,bios,sizeof bios));
    memcpy(cd.prg,bios,8);cd.prg[0x100]=0x4e;cd.prg[0x101]=0x71;
    cd.prg[0x102]=0x12;cd.prg[0x103]=0x34;
    CHECK(segacd_coordinator_init(&c,&cd,NULL,NULL));
    c.io.pcm_sink=sink;
    c.cpu[1].state.regs.PC=0x100;c.cpu[1].state.regs.SR=0x2700;
    segacd_code_range(&id,0,cd.bios,sizeof cd.bios);
    segacd_code_range(&id,1,cd.prg,sizeof cd.prg);
    segacd_code_range(&id,2,cd.word,sizeof cd.word);
    segacd_code_range(&id,3,ram,sizeof ram);
    segacd_coordinator_code_begin(&c,&id);
}
static void nested(M68kStepContext *cpu)
{
    cd.bios[0x103]^=1;c.cpu[0].state.regs.PC=0x100;
    nested_result=m68k_step_execute_tracked(&c.cpu[0],&entry);loop(cpu);
}
static void versions(void)
{
    setup();M68kStepContext *cpu=&c.cpu[1];
    for(unsigned n=0;n<1000;++n)CHECK(m68k_step_execute_tracked(cpu,&entry)==M68K_STEP_OK);
    CHECK(id.checks==1&&id.hits==999);
    /* An unrelated physical page keeps this guard. A tracked extension write
     * invalidates it, including mismatch caches and variant priority. */
    m68k_step_write16(cpu,0x1000,0xabcd);
    CHECK(m68k_step_execute_tracked(cpu,&entry)==M68K_STEP_OK&&id.checks==1);
    m68k_step_write8(cpu,0x103,0x35);
    CHECK(m68k_step_execute_tracked(cpu,&entry)==M68K_STEP_MISS);
    CHECK(m68k_step_execute_tracked(cpu,&other)==M68K_STEP_OK);
    m68k_step_write16(cpu,0x102,0x1234);
    CHECK(m68k_step_execute_tracked(cpu,&entry)==M68K_STEP_OK);
    CHECK(segacd_coordinator_external_main_write8(&c,0x20103,0x35)==M68K_STEP_OK);
    CHECK(m68k_step_execute_tracked(cpu,&entry)==M68K_STEP_MISS);
    m68k_step_write16(cpu,0x102,0x1234);
    CHECK(m68k_step_execute(cpu,&opaque)==M68K_STEP_OK&&id.suspended==0);
    CHECK(m68k_step_execute_tracked(cpu,&entry)==M68K_STEP_MISS);
    CHECK(m68k_step_execute_tracked(cpu,&other)==M68K_STEP_OK);
    CHECK(m68k_step_execute(cpu,&bad)==M68K_STEP_ADDRESS_ERROR&&id.suspended==0);
    CHECK(!cpu->fault_escape);
    /* Full-state replacement and arbitrary direct/import writes outside the
     * execution scope must never inherit a previously matching identity. */
    segacd_coordinator_code_end(&c);cpu->state.fault=0;cpu->state.regs.PC=0x100;
    cd.prg[0x103]=0x35;
    segacd_coordinator_code_begin(&c,&id);
    CHECK(m68k_step_execute_tracked(cpu,&entry)==M68K_STEP_MISS);
    segacd_advance_io_owned(&cd,384,&c.io,observer,&c,NULL);
    CHECK(m68k_step_execute_tracked(cpu,&entry)==M68K_STEP_OK);
    /* While opaque code owns the C stack, even another CPU must fetch fresh
     * bytes. This covers the ownership-WAIT nesting that cannot defer a fence
     * until the opaque operation returns. */
    memcpy(cd.bios+0x100,cd.prg+0x100,4);segacd_code_fence(&id);
    c.cpu[0].state.regs.PC=0x100;
    CHECK(m68k_step_execute_tracked(&c.cpu[0],&entry)==M68K_STEP_OK);
    M68kStepEntry nesting=entry;nesting.execute=nested;
    CHECK(m68k_step_execute(cpu,&nesting)==M68K_STEP_OK);
    CHECK(nested_result==M68K_STEP_MISS&&!id.suspended);
    segacd_coordinator_code_end(&c);
}
static void aliases(void)
{
    setup();M68kStepContext *cpu=&c.cpu[0];
    M68kStepEntry bank={0x20100,{0x4e71,0x1234},2,4,loop};
    cd.main_control=0;cpu->state.regs.PC=bank.pc;
    CHECK(m68k_step_execute_tracked(cpu,&bank)==M68K_STEP_OK);
    CHECK(segacd_coordinator_external_main_write8(&c,0xA12003,0x40)==M68K_STEP_OK);
    cpu->state.regs.PC=bank.pc;
    CHECK(m68k_step_execute_tracked(cpu,&bank)==M68K_STEP_MISS);
    m68k_step_write8(cpu,0xA12003,0);cpu->state.regs.PC=bank.pc;
    m68k_step_write8(cpu,0xA12001,1);
    /* Ownership WAIT is never certified merely because this physical image
     * matched before. Do not execute a WAIT here without runnable modules. */
    CHECK(cpu->code_span(cpu->user,bank.pc,4)==NULL);
    CHECK(cpu->code_guard(cpu->user,&bank)==0);
    m68k_step_write8(cpu,0xA12001,0);cpu->state.regs.PC=bank.pc;
    m68k_step_write8(&c.cpu[1],0x103,0x35);
    CHECK(m68k_step_execute_tracked(cpu,&bank)==M68K_STEP_MISS);
    /* Both boundary pages participate, with a non-wrapping monotonic token. */
    M68kStepEntry cross={0x1fe,{0x4e71,0x1234},2,4,loop};
    cd.prg[0x1fe]=0x4e;cd.prg[0x1ff]=0x71;cd.prg[0x200]=0x12;cd.prg[0x201]=0x34;
    segacd_code_fence(&id);c.cpu[1].state.regs.PC=cross.pc;
    CHECK(m68k_step_execute_tracked(&c.cpu[1],&cross)==M68K_STEP_OK);
    m68k_step_write8(&c.cpu[1],0x201,0x35);c.cpu[1].state.regs.PC=cross.pc;
    CHECK(m68k_step_execute_tracked(&c.cpu[1],&cross)==M68K_STEP_MISS);
    M68kStepEntry word={0x80100,{0x4e71,0x1234},2,4,loop};
    m68k_step_write8(cpu,0xA12003,2);
    m68k_step_write16(&c.cpu[1],0x80100,0x4e71);m68k_step_write16(&c.cpu[1],0x80102,0x1234);
    c.cpu[1].state.regs.PC=word.pc;
    CHECK(m68k_step_execute_tracked(&c.cpu[1],&word)==M68K_STEP_OK);
    m68k_step_write8(&c.cpu[1],0xFF8003,1);
    CHECK(c.cpu[1].code_span(c.cpu[1].user,word.pc,4)==NULL);
    m68k_step_write8(&c.cpu[0],0x200103,0x35);
    m68k_step_write8(cpu,0xA12003,2);c.cpu[1].state.regs.PC=word.pc;
    CHECK(m68k_step_execute_tracked(&c.cpu[1],&word)==M68K_STEP_MISS);
    m68k_step_write8(&c.cpu[1],0xFF8003,5); /* Main 1M bank1 byte81 aliases physical byte103. */
    CHECK(c.cpu[0].code_span(c.cpu[0].user,0x200080,4)==NULL);
    m68k_step_write8(&c.cpu[0],0x200081,0x34);
    m68k_step_write8(&c.cpu[1],0xFF8003,0);c.cpu[1].state.regs.PC=word.pc;
    CHECK(m68k_step_execute_tracked(&c.cpu[1],&word)==M68K_STEP_OK);
    id.serial=UINT64_MAX;segacd_code_fence(&id);
    CHECK(id.serial==1&&id.fence==1);
    segacd_coordinator_code_end(&c);
}
static unsigned sample_calls;
static void rebind_sink(void *u,int16_t l,int16_t r)
{(void)l;(void)r;++sample_calls;((SegaCDIO *)u)->pcm_sink=NULL;}
static void host_output(void *u,int16_t l,int16_t r)
{(void)l;(void)r;++*(unsigned *)u;}
static void output_effects(void)
{
    setup();unsigned a=0,b=0;
    SegaCDIO qualified={0};qualified.pcm_sink=host_output;qualified.pcm_user=&a;
    c.io.pcm_sink=host_output;c.io.pcm_user=&a;
    CHECK(segacd_coordinator_readonly_io(&c,&qualified));
    qualified.pcm_sink=NULL; /* Qualification is a captured binding. */
    CHECK(m68k_step_execute_tracked(&c.cpu[1],&entry)==M68K_STEP_OK);
    uint64_t fences=id.fences,checks=id.checks;
    segacd_advance_io_owned(&cd,384,&c.io,observer,&c,&c.code_readonly_io);
    CHECK(a==1&&id.fences==fences);
    CHECK(m68k_step_execute_tracked(&c.cpu[1],&entry)==M68K_STEP_OK&&id.checks==checks);
    c.io.pcm_user=&b;
    segacd_advance_io_owned(&cd,384,&c.io,observer,&c,&c.code_readonly_io);
    CHECK(b==1&&id.fences>fences&&!id.suspended);
    c.io.pcm_sink=sink;c.io.pcm_user=&a;
    segacd_advance_io_owned(&cd,384,&c.io,observer,&c,&c.code_readonly_io);
    CHECK(m68k_step_execute_tracked(&c.cpu[1],&entry)==M68K_STEP_MISS);
    CHECK(segacd_coordinator_readonly_io(&c,NULL));
    segacd_coordinator_code_end(&c);
}
static void bindings(void)
{
    setup();static SegaCD reference;reference=cd;
    SegaCDIO reference_io={0};reference_io.pcm_sink=rebind_sink;reference_io.pcm_user=&reference_io;
    sample_calls=0;segacd_advance_io(&reference,384*3,&reference_io);
    unsigned expected=sample_calls;CHECK(expected==3&&!reference_io.pcm_sink);
    c.io.pcm_sink=rebind_sink;c.io.pcm_user=&c.io;sample_calls=0;
    segacd_advance_io_owned(&cd,384*3,&c.io,observer,&c,NULL);
    CHECK(sample_calls==expected&&!c.io.pcm_sink&&!id.suspended);
    CHECK(!memcmp(&cd,&reference,sizeof cd));
    segacd_coordinator_code_end(&c);
}
int main(void)
{versions();aliases();bindings();output_effects();puts("Code identity: CPU/external/opaque/fault/restore/remap/boundary/wrap/IO-rebinding/effect checks passed");return 0;}
