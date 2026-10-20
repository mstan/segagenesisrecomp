/* Independent architectural expectations plus AOT/floor loop comparison.
 * This is not a claim that every opcode/timing/bus phase is qualified. */
#include "m68k_floor.h"
#include "fixture_a.h"
#include "fixture_b.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
typedef struct {uint8_t bytes[0x2000];unsigned waits,ready,first_reads;unsigned events;uint8_t event_op[16];uint32_t event_addr[16];} Memory;
static Memory ma,mb;
static void event(Memory*m,uint8_t op,uint32_t a){if(a>=0x600&&a<0x800){if(m->events<16){m->event_op[m->events]=op;m->event_addr[m->events]=a;}++m->events;}}
static M68kStepBusResult read8(void*u,uint32_t a,uint8_t*v){Memory*m=u;event(m,1,a);if(a==0x600 && !m->ready)return M68K_BUS_WAIT;if(a==0x5FE)++m->first_reads;if(a>=sizeof m->bytes)return M68K_BUS_FAULT;*v=m->bytes[a];return M68K_BUS_OK;}
static M68kStepBusResult read16(void*u,uint32_t a,uint16_t*v)
{
    Memory*m=u;event(m,1,a);
    if(a==0x600 && !m->ready)return M68K_BUS_WAIT;if(a==0x5FE)++m->first_reads;
    if(a+1>=sizeof m->bytes)return M68K_BUS_FAULT;*v=(uint16_t)(((unsigned)m->bytes[a]<<8)|m->bytes[a+1]);return M68K_BUS_OK;
}
static M68kStepBusResult write8(void*u,uint32_t a,uint8_t v){Memory*m=u;event(m,2,a);if(a>=sizeof m->bytes)return M68K_BUS_FAULT;m->bytes[a]=v;return M68K_BUS_OK;}
static M68kStepBusResult write16(void*u,uint32_t a,uint16_t v){Memory*m=u;event(m,2,a);if(a+1>=sizeof m->bytes)return M68K_BUS_FAULT;m->bytes[a]=(uint8_t)(v>>8);m->bytes[a+1]=(uint8_t)v;return M68K_BUS_OK;}
static int wait_bus(void*u,uint32_t a,int write){Memory*m=u;CHECK(a==0x600 && !write);m->ready=1;++m->waits;return 1;}
static void prepare(M68kStepContext*c,Memory*m)
{
    memset(m,0,sizeof *m);m->ready=1;
    M68kStepBus bus={read8,read16,write8,write16,read16,wait_bus,0,0};m68k_step_init(c,&bus,m);
    c->state.regs.PC=0x1000;c->state.regs.A[7]=0x800;c->state.regs.SR=0x2000;
}
static void load(Memory*m,const char*path){FILE*f=fopen(path,"rb");CHECK(f && fread(m->bytes+0x1000,1,32,f)==32 && !fclose(f));}
static void words(M68kStepContext*c,const uint16_t*values,unsigned count)
{
    c->state.regs.PC=0x1000;c->state.stopped=0;
    for(unsigned n=0;n<count;++n)m68k_step_write16(c,0x1000+n*2u,values[n]);
}
static void step(M68kStepContext*c){CHECK(segacd_m68k_floor_step(c)==M68K_STEP_OK);}
int main(int argc,char**argv)
{
    CHECK(argc==3);M68kStepContext a,b;
    for(unsigned variant=0;variant<2;++variant) {
        prepare(&a,&ma);prepare(&b,&mb);load(&ma,argv[variant+1]);load(&mb,argv[variant+1]);
        const M68kStepModule*module=variant?&fixture_b_module:&fixture_a_module;
        for(unsigned n=0;!a.state.stopped && n<40;++n) {
            CHECK(m68k_step_module_execute(&a,module)==M68K_STEP_OK);step(&b);
            CHECK(!memcmp(&a.state,&b.state,sizeof a.state));CHECK(!memcmp(ma.bytes,mb.bytes,sizeof ma.bytes));
        }
        CHECK(a.state.stopped && a.state.regs.D[1]==(variant?5u:3u) && a.state.regs.A[7]==0x800);
        CHECK(a.state.instructions==(variant?27u:17u) && a.state.cycles==(variant?266u:162u));
    }
    /* MOVEP's sparse bus order, signed displacement, odd-address legality,
     * word upper-half preservation and unchanged flags are architectural
     * expectations, independent of the emitted native operation. */
    for(unsigned size=0;size<2;++size)for(unsigned write=0;write<2;++write)for(unsigned odd=0;odd<2;++odd) {
        prepare(&a,&ma);a.state.regs.SR=0x201F;a.state.regs.D[2]=0xA1B2C3D4;
        a.state.regs.A[3]=0x708+odd;
        unsigned count=size?4u:2u;uint32_t start=0x700+odd;
        for(unsigned n=0;n<8;++n)ma.bytes[0x700+n]=(uint8_t)(0x10+n);
        const uint16_t movep[]={(uint16_t)(0x050Bu|(size?0x40u:0u)|(write?0x80u:0u)),0xFFF8};
        words(&a,movep,2);ma.events=0;step(&a);
        CHECK(ma.events==count && a.state.regs.A[3]==0x708+odd && a.state.regs.SR==0x201F);
        CHECK(a.state.regs.PC==0x1004 && a.charge==(size?24u:16u));
        for(unsigned n=0;n<count;++n)CHECK(ma.event_addr[n]==start+n*2u && ma.event_op[n]==(write?2:1));
        if(write) {
            CHECK(a.state.regs.D[2]==0xA1B2C3D4);
            for(unsigned n=0;n<count;++n)CHECK(ma.bytes[start+n*2u]==(uint8_t)(0xA1B2C3D4u>>((count-1u-n)*8u)));
            for(unsigned n=0;n<8;++n)if(n%2!=odd || n/2>=count)CHECK(ma.bytes[0x700+n]==0x10+n);
        } else CHECK(a.state.regs.D[2]==(size?(odd?0x11131517u:0x10121416u):(odd?0xA1B21113u:0xA1B21012u)));
    }
    prepare(&a,&ma);a.state.regs.A[0]=0x5FE;a.state.regs.D[2]=0xAABBCCDD;
    ma.bytes[0x5FE]=0x12;ma.bytes[0x600]=0x34;ma.bytes[0x602]=0x56;ma.bytes[0x604]=0x78;
    const uint16_t movep_wait[]={0x0548,0};words(&a,movep_wait,2);ma.ready=0;step(&a);
    CHECK(ma.waits==1 && ma.first_reads==1 && a.state.regs.D[2]==0x12345678 && a.state.regs.A[0]==0x5FE);
    prepare(&a,&ma);
    /* CLR reads the old value before writing zero for every memory size. The
     * RMW EA updates postincrement exactly once, including the A7 byte step. */
    const struct {uint16_t opcode;unsigned bytes,increment;} clr_cases[]={
        {0x4218,1,1},{0x4258,2,2},{0x4298,4,4}
    };
    for(unsigned n=0;n<sizeof clr_cases/sizeof *clr_cases;++n) {
        uint32_t addr=0x700;a.state.regs.A[0]=addr;a.state.regs.SR=0x2011;
        ma.bytes[addr]=0xA5;ma.bytes[addr+1]=0x5A;ma.bytes[addr+2]=0xC3;ma.bytes[addr+3]=0x3C;
        words(&a,&clr_cases[n].opcode,1);ma.events=0;step(&a);
        unsigned parts=clr_cases[n].bytes==4?2:1;
        CHECK(ma.events==parts*2 && a.state.regs.A[0]==addr+clr_cases[n].increment);
        for(unsigned p=0;p<parts;++p)CHECK(ma.event_op[p]==1 && ma.event_addr[p]==addr+p*2);
        for(unsigned p=0;p<parts;++p)CHECK(ma.event_op[parts+p]==2 && ma.event_addr[parts+p]==addr+p*2);
        CHECK((a.state.regs.SR&0x1Fu)==0x14);
        for(unsigned p=0;p<clr_cases[n].bytes;++p)CHECK(ma.bytes[addr+p]==0);
    }
    /* Predecrement is charged by the read phase once; displacement and
     * absolute-long extension forms resolve the same data address in both
     * phases without applying address-register changes. */
    a.state.regs.A[0]=0x701;ma.bytes[0x700]=0xA5;const uint16_t clr_pre[]={0x4220};words(&a,clr_pre,1);ma.events=0;step(&a);
    CHECK(ma.events==2 && ma.event_op[0]==1 && ma.event_op[1]==2 && ma.event_addr[0]==0x700 && ma.event_addr[1]==0x700 && a.state.regs.A[0]==0x700 && ma.bytes[0x700]==0);
    a.state.regs.A[0]=0x680;ma.bytes[0x6A0]=0x12;ma.bytes[0x6A1]=0x34;const uint16_t clr_disp[]={0x4268,0x0020};words(&a,clr_disp,2);ma.events=0;step(&a);
    CHECK(ma.events==2 && ma.event_op[0]==1 && ma.event_op[1]==2 && ma.event_addr[0]==0x6A0 && ma.event_addr[1]==0x6A0 && a.state.regs.A[0]==0x680 && ma.bytes[0x6A0]==0 && ma.bytes[0x6A1]==0);
    const uint16_t clr_abs[]={0x4239,0x0000,0x0740};ma.bytes[0x740]=0x5A;words(&a,clr_abs,3);ma.events=0;step(&a);
    CHECK(ma.events==2 && ma.event_op[0]==1 && ma.event_op[1]==2 && ma.event_addr[0]==0x740 && ma.event_addr[1]==0x740 && ma.bytes[0x740]==0);
    a.state.regs.A[7]=0x720;ma.bytes[0x720]=0xA5;ma.events=0;
    const uint16_t clr_a7[]={0x421F};words(&a,clr_a7,1);ma.events=0;step(&a);
    CHECK(ma.events==2 && ma.event_op[0]==1 && ma.event_addr[0]==0x720 && ma.event_op[1]==2 && ma.event_addr[1]==0x720);
    CHECK(a.state.regs.A[7]==0x722 && ma.bytes[0x720]==0);
    /* A read WAIT is retried once before the write; it must not duplicate a
     * completed write or the postincrement. */
    a.state.regs.A[0]=0x600;a.state.regs.SR=0x2010;ma.bytes[0x600]=0x12;ma.bytes[0x601]=0x34;
    const uint16_t clr_wait[]={0x4258};words(&a,clr_wait,1);ma.events=0;ma.ready=0;ma.waits=0;step(&a);
    CHECK(ma.events==3 && ma.event_op[0]==1 && ma.event_op[1]==1 && ma.event_op[2]==2);
    CHECK(ma.event_addr[0]==0x600 && ma.event_addr[1]==0x600 && ma.event_addr[2]==0x600);
    CHECK(ma.waits==1 && a.state.regs.A[0]==0x602 && ma.bytes[0x600]==0 && ma.bytes[0x601]==0);
    const uint16_t immediate[]={0x303C,0x1234};words(&a,immediate,2);step(&a);CHECK(a.state.regs.D[0]==0x1234 && a.state.regs.PC==0x1004 && a.charge==8);
    m68k_step_write16(&a,0x1002,0xBEEF);a.state.regs.PC=0x1000;step(&a);CHECK(a.state.regs.D[0]==0xBEEF && (a.state.regs.SR&15)==8);
    /* A7 byte accesses and the sequential source update in CMPM aliasing. */
    const uint16_t cmpm[]={0xBF0F};a.state.regs.A[7]=0x700;a.state.regs.SR=0x2010;
    m68k_step_write8(&a,0x700,1);m68k_step_write8(&a,0x702,3);words(&a,cmpm,1);step(&a);
    CHECK(a.state.regs.A[7]==0x704 && a.state.regs.SR==0x2010);
    /* A WAIT during the second source word does not replay the first word or
     * the postincrement, even though the instruction has already had effects. */
    const uint16_t copy[]={0x22D8};a.state.regs.A[0]=0x5FE;a.state.regs.A[1]=0x610;ma.waits=0;
    m68k_step_write32(&a,0x5FE,0x12345678);ma.ready=0;words(&a,copy,1);step(&a);
    CHECK(a.state.regs.A[0]==0x602 && a.state.regs.A[1]==0x614 && m68k_step_read32(&a,0x610)==0x12345678 && ma.waits==1 && ma.first_reads==1);
    const uint16_t divzero[]={0x80D8};a.state.regs.A[0]=0x650;a.state.regs.A[7]=0x800;a.state.regs.D[0]=0x12345678;
    m68k_step_write16(&a,0x650,0);m68k_step_write32(&a,5*4,0x1100);words(&a,divzero,1);step(&a);
    CHECK(a.state.exception_vector==5 && a.state.regs.PC==0x1100 && a.state.regs.A[0]==0x652 && a.state.regs.D[0]==0x12345678 && a.charge==42);
    CHECK(m68k_step_read32(&a,a.state.regs.A[7]+2)==0x1002);
    /* MOVEC is illegal on the original 68000; it cannot consume an extension
     * and pretend to be a later processor's control operation. */
    const uint16_t movec[]={0x4E7A,0x4E71};a.state.regs.A[7]=0x800;m68k_step_write32(&a,4*4,0x1100);
    words(&a,movec,2);step(&a);CHECK(a.state.exception_vector==4 && m68k_step_read32(&a,a.state.regs.A[7]+2)==0x1000);
    const uint16_t sr[]={0x46FC,0x2700};prepare(&a,&ma);a.state.regs.USP=0x700;m68k_step_set_sr(&a,0);
    m68k_step_write32(&a,8*4,0x1100);words(&a,sr,2);step(&a);
    CHECK(a.state.exception_vector==8 && a.state.regs.USP==0x700 && a.state.regs.A[7]==0x7FA && m68k_step_read32(&a,0x7FC)==0x1000);
    const uint16_t asl[]={0xE3A0};prepare(&a,&ma);a.state.regs.D[0]=UINT32_MAX;a.state.regs.D[1]=31;words(&a,asl,1);step(&a);
    CHECK(a.state.regs.D[0]==0x80000000u && a.state.regs.SR==0x2019 && a.charge==70);
    for(unsigned direction=0;direction<2;++direction) {
        uint16_t rox=(uint16_t)(direction?0xE3B0:0xE2B0);
        for(unsigned x=0;x<2;++x) {
            a.state.regs.D[0]=0x12345678;a.state.regs.D[1]=0;a.state.regs.SR=(uint16_t)(0x2000|(x?16u:1u));
            words(&a,&rox,1);step(&a);
            CHECK(a.state.regs.D[0]==0x12345678 && a.state.regs.SR==(x?0x2011:0x2000) && a.charge==8);
        }
    }
    const uint16_t oddbranch[]={0x60FF};prepare(&a,&ma);words(&a,oddbranch,1);step(&a);CHECK(a.state.regs.PC==0x1001);
    CHECK(segacd_m68k_floor_step(&a)==M68K_STEP_ADDRESS_ERROR && a.state.fault_address==0x1001);
    puts("Context-bound floor: native loop/stack/cycle parity, live operands, A7/CMPM, partial WAIT, traps/privilege and original-68000 decoding pass");
    return 0;
}
