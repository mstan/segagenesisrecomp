/* Independent synthetic loop, real stack/control flow and competing overlays
 * exercise the complete image -> generated C -> portable runtime path. */
#include "fixture_a.h"
#include "fixture_b.h"
#include "fixture_delta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
typedef struct {uint8_t bytes[0x2000];} Memory;
static M68kStepBusResult read8(void*u,uint32_t a,uint8_t*v){Memory*m=u;if(a>=sizeof m->bytes)return M68K_BUS_FAULT;*v=m->bytes[a];return M68K_BUS_OK;}
static M68kStepBusResult read16(void*u,uint32_t a,uint16_t*v){Memory*m=u;if(a+1>=sizeof m->bytes)return M68K_BUS_FAULT;*v=(uint16_t)(((unsigned)m->bytes[a]<<8)|m->bytes[a+1]);return M68K_BUS_OK;}
static M68kStepBusResult write8(void*u,uint32_t a,uint8_t v){Memory*m=u;if(a>=sizeof m->bytes)return M68K_BUS_FAULT;m->bytes[a]=v;return M68K_BUS_OK;}
static M68kStepBusResult write16(void*u,uint32_t a,uint16_t v){Memory*m=u;if(a+1>=sizeof m->bytes)return M68K_BUS_FAULT;m->bytes[a]=(uint8_t)(v>>8);m->bytes[a+1]=(uint8_t)v;return M68K_BUS_OK;}
static void load(Memory*m,const char*path){FILE*f=fopen(path,"rb");CHECK(f);CHECK(fread(m->bytes+0x1000,1,32,f)==32);CHECK(!fclose(f));}
static void run(M68kStepContext*c,const M68kStepModule*m,unsigned limit)
{for(unsigned n=0;n<limit;++n){CHECK(m68k_step_module_execute(c,m)==M68K_STEP_OK);if(c->state.stopped)return;}CHECK(0);}
static unsigned run_combined(M68kStepContext*c,const M68kStepModule*delta,
                             const M68kStepModule*covered,unsigned limit)
{
    unsigned covered_calls=0;
    for(unsigned n=0;n<limit;++n) {
        M68kStepState before=c->state;
        M68kStepResult result=m68k_step_module_execute(c,delta);
        if(result==M68K_STEP_MISS) {
            /* Module misses must be observationally inert before trying the
             * already-registered exact callback module. */
            CHECK(!memcmp(&before,&c->state,sizeof before));
            result=m68k_step_module_execute(c,covered);
            ++covered_calls;
        }
        CHECK(result==M68K_STEP_OK);
        if(c->state.stopped)return covered_calls;
    }
    CHECK(0);return covered_calls;
}
int main(int argc,char **argv)
{
    CHECK(argc==3);CHECK(m68k_step_module_valid(&fixture_a_module));CHECK(m68k_step_module_valid(&fixture_b_module));
    CHECK(m68k_step_module_valid(&fixture_delta_module));
    CHECK(fixture_a_module.count==7 && fixture_a_module.image_base==0x1000 && fixture_a_module.image_size==32);
    CHECK(fixture_delta_module.count==fixture_a_module.count-1);
    CHECK(fixture_a_module.image_crc32!=fixture_b_module.image_crc32);
    Memory ma={0},mb={0};load(&ma,argv[1]);load(&mb,argv[2]);
    M68kStepBus bus={read8,read16,write8,write16,read16,0,0,0};M68kStepContext a,b;
    m68k_step_init(&a,&bus,&ma);m68k_step_init(&b,&bus,&mb);
    a.state.regs.PC=b.state.regs.PC=0x1000;a.state.regs.A[7]=b.state.regs.A[7]=0x800;a.state.regs.SR=b.state.regs.SR=0x2000;
    M68kStepState before=b.state;
    CHECK(m68k_step_module_execute(&b,&fixture_a_module)==M68K_STEP_MISS && !memcmp(&before,&b.state,sizeof before));
    run(&a,&fixture_a_module,30);run(&b,&fixture_b_module,40);
    CHECK(a.state.regs.D[0]==0 && a.state.regs.D[1]==3 && a.state.regs.A[7]==0x800 && a.state.regs.PC==0x100C && a.state.regs.SR==0x2300);
    CHECK(a.state.instructions==17 && a.state.cycles==162);
    CHECK(b.state.regs.D[0]==0 && b.state.regs.D[1]==5 && b.state.regs.A[7]==0x800 && b.state.regs.SR==0x2400);
    CHECK(b.state.instructions==27 && b.state.cycles==266);
    /* A preexisting native callback supplies exactly the covered MOVEQ while
     * the delta module supplies the rest of the same CFG. The combined native
     * chain must match the complete baseline state and cycle totals. */
    Memory md={0};load(&md,argv[1]);M68kStepContext d;
    m68k_step_init(&d,&bus,&md);
    d.state.regs.PC=0x1000;d.state.regs.A[7]=0x800;d.state.regs.SR=0x2000;
    CHECK(run_combined(&d,&fixture_delta_module,&fixture_a_module,40)==1);
    CHECK(!memcmp(&a.state,&d.state,sizeof a.state));
    CHECK(!memcmp(&ma,&md,sizeof ma));
    /* Same opcode/PC, different extension: reject A's STOP before mutation. */
    b.state.stopped=0;b.state.regs.PC=0x1008;before=b.state;
    CHECK(m68k_step_module_execute(&b,&fixture_a_module)==M68K_STEP_MISS && !memcmp(&before,&b.state,sizeof before));
    CHECK(m68k_step_module_execute(&b,&fixture_b_module)==M68K_STEP_OK && b.state.stopped);
    /* The --all image uses genuine original-68000 lengths, not 68020/010. */
    unsigned found=0;
    for(size_t n=0;n<fixture_b_module.count;++n) {
        const M68kStepEntry*e=&fixture_b_module.entries[n];
        CHECK(e->pc!=0x101E); /* truncated long immediate was not compiled */
        if(e->pc==0x1010 || e->pc==0x1012){CHECK(e->word_count==1);++found;}
        if(e->pc==0x1018)CHECK(e->execute==m68k_step_illegal && e->word_count==1);
    }
    CHECK(found==2);
    M68kStepModule invalid=fixture_a_module;invalid.image_size=2;CHECK(!m68k_step_module_valid(&invalid));
    puts("Native full/delta module execution, exact covered callback state/cycles, CPU isolation, replacement/extension guards and image bounds pass");return 0;
}
