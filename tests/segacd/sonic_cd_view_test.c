#include "sonic_cd_view.h"
#include "sonic_cd_culls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; \
} } while(0)

enum { WORDS=0x40000u, MAIN_BYTES=0x10000u, WORD_BASE=0x200000u,
       MAIN_BASE=0xFF0000u };
static uint8_t fixture[WORDS],main_ram[MAIN_BYTES];
static unsigned captured;
static unsigned sat_captured;
static uint8_t sat_before_store;

static void put_bytes(size_t at,const uint8_t *bytes,size_t n)
{ memcpy(fixture+at,bytes,n); }
static void put_words(size_t at,const uint8_t *bytes,size_t n)
{ put_bytes(at,bytes,n); }
static void put16(uint8_t *p,uint16_t v)
{ p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v; }

static void capture(void *user,const M68kStepContext *cpu)
{
    unsigned *out=(unsigned *)user;
    if(out&&cpu&&cpu->state.regs.A[2]==0xFFFFF800u)++*out;
}
static void capture_sat(void *user,const M68kStepContext *cpu)
{
    (void)user;
    if(cpu) {
        ++sat_captured;
        sat_before_store=main_ram[0xF62C];
        if((uint8_t)cpu->state.regs.D[5]!=3u)sat_captured=99u;
    }
}

static void make_fixture(void)
{
    static const uint8_t draw[]={0x08,0xA8,0,7,0,1,0x10,0x28,0,1,2,0x40,0,0x0C,0x67,0};
    static const uint8_t drawobj[]={0x45,0xF8,0xF8,0,0x7A,0,0x49,0xF8,0xAC,0,0x7E,7,0x4A,0x54,0x67,0};
    static const uint8_t spawn[]={0x70,0,0x10,0x38,0xF7,0x6C,0x30,0x3B,0x12,0x34,0x4E,0xFB,0x56,0x78};
    static const uint8_t despawn[]={0x30,0x28,0,8,0x4A,0x28,0,1};
    memset(fixture,0,sizeof fixture);memset(main_ram,0,sizeof main_ram);
    put_bytes(0x100,draw,sizeof draw);
    put_words(0x100+0x1A,(const uint8_t[]){0x96,0x78,0xF7,0},4);
    put_words(0x100+0x28,(const uint8_t[]){0x0C,0x41,1,0x40},4);
    put_bytes(0x300,drawobj,sizeof drawobj);
    put_words(0x380,(const uint8_t[]){0x11,0xC5,0xF6,0x2C,
                                     0x0C,0x05,0x00,0x50},8);
    put_bytes(0x1000,spawn,sizeof spawn);
    put_words(0x1000+0x16,(const uint8_t[]){0x41,0xF9,0x00,0x20,0x00,0x50},6);
    put_words(0x50,(const uint8_t[]){0x00,0x0C,0x00,0x40,0xFF,0xFF},6);
    put_words(0x1000+0x44,(const uint8_t[]){4,0x46,0,0x80},4);
    put_words(0x1000+0x6E,(const uint8_t[]){4,0x46,0,0x80},4);
    put_words(0x1000+0xBA,(const uint8_t[]){4,0x46,0,0x80},4);
    put_words(0x1000+0xFA,(const uint8_t[]){6,0x46,3,0},4);
    put_words(0x1000+0x126,(const uint8_t[]){6,0x46,2,0x80},4);
    put_words(0x1000+0x154,(const uint8_t[]){4,0x46,3,0},4);
    put_words(0x1000+0xA0,(const uint8_t[]){0xBC,0x78,0xF7,0x6E},4);
    put_bytes(0x1400,despawn,sizeof despawn);
    put_words(0x1400+0x12,(const uint8_t[]){4,0x41,0,0x80},4);
    put_words(0x1400+0x1C,(const uint8_t[]){0x0C,0x40,2,0x80},4);
    /* Synthetic object dispatcher: ID $10 points at a ring state table.
     * An identical unrelated cull verifies that discovery follows the
     * object/state tables instead of selecting the first byte match. */
    put_words(0x2000,(const uint8_t[]){0xD0,0x40,0xD0,0x40,0x43,0xF9,
        0x00,0x20,0x21,0x00,0x22,0x71,0x00,0xFC,0x4E,0x91},16);
    put_words(0x2100+60,(const uint8_t[]){0x00,0x20,0x22,0x00},4);
    put_words(0x2200,(const uint8_t[]){0x70,0x00,0x10,0x28,0x00,0x24,
        0x32,0x3B,0x00,0x06,0x4E,0xFB,0x10,0x02,0x00,0x20,0x00,0xF2},18);
    static const uint8_t ring_cull[]={0x4A,0x28,0x00,0x01,0x6B,0x1E,
        0x30,0x28,0x00,0x32,0x02,0x40,0xFF,0x80,0x32,0x38,0xF7,0x00,
        0x04,0x41,0x00,0x80,0x02,0x41,0xFF,0x80,0x90,0x41,
        0x0C,0x40,0x02,0x80,0x62,0x00};
    put_bytes(0x2300,ring_cull,sizeof ring_cull);
    put_bytes(0x2400,ring_cull,sizeof ring_cull);
    static const uint8_t s1[]={0x30,0x28,0x00,0x08,0x02,0x40,0xFF,0x80,
        0x32,0x38,0xF7,0x00,0x04,0x41,0x00,0x80,0x02,0x41,0xFF,0x80,
        0x90,0x41,0x0C,0x40,0x02,0x80,0x62,0x00,0x00,0x06,
        0x60,0x00,0xDA,0xE0,0x45,0xF9,0x00,0xFF,0x12,0x00,0x70,0x00,
        0x10,0x28,0x00,0x23,0x67,0x06,0x08,0xB2,0x00,0x07,0x00,0x02,
        0x60,0x00,0x01,0xC8};
    static const uint8_t delete_object[]={0x22,0x48,0x72,0x00,0x70,0x0F,
        0x22,0xC1,0x51,0xC8,0xFF,0xFC,0x4E,0x75};
    put_bytes(0x2600,s1,sizeof s1);
    put_bytes(0x2800,delete_object,sizeof delete_object);
}

typedef struct { const uint8_t *code;size_t code_size; } Bus;
static M68kStepBusResult fetch16(void *u,uint32_t a,uint16_t *out)
{
    Bus *b=(Bus *)u;
    if(!b||!out||a<WORD_BASE||a-WORD_BASE+2u>b->code_size)return M68K_BUS_FAULT;
    *out=(uint16_t)(((uint16_t)b->code[a-WORD_BASE]<<8)|b->code[a-WORD_BASE+1]);
    return M68K_BUS_OK;
}
static M68kStepBusResult read16(void *u,uint32_t a,uint16_t *out)
{
    if(!out||a<MAIN_BASE||a-MAIN_BASE+2u>MAIN_BYTES)return M68K_BUS_FAULT;
    (void)u;
    *out=(uint16_t)(((uint16_t)main_ram[a-MAIN_BASE]<<8)|main_ram[a-MAIN_BASE+1]);
    return M68K_BUS_OK;
}
static M68kStepBusResult read8(void *u,uint32_t a,uint8_t *out)
{
    (void)u;
    if(!out||a<MAIN_BASE||a-MAIN_BASE>=MAIN_BYTES)return M68K_BUS_FAULT;
    *out=main_ram[a-MAIN_BASE];return M68K_BUS_OK;
}
static M68kStepBusResult write8(void *u,uint32_t a,uint8_t value)
{
    (void)u;
    if(a<MAIN_BASE||a-MAIN_BASE>=MAIN_BYTES)return M68K_BUS_FAULT;
    main_ram[a-MAIN_BASE]=value;return M68K_BUS_OK;
}

static int module_for_observers(SegaCDView *view,unsigned width)
{
    SegaCDViewSnapshot s={fixture,sizeof fixture,WORD_BASE,main_ram,
                          sizeof main_ram,MAIN_BASE};
    SegaCDViewObservers o={capture,&captured,capture_sat,NULL};
    return segacd_view_configure_observed(view,&s,width,&o);
}

static const M68kStepEntry *entry_opcode(const M68kStepModule *m,uint16_t op,
                                         uint16_t ext)
{
    for(size_t i=0;i<m->count;++i)
        if(m->entries[i].words[0]==op&&m->entries[i].words[1]==ext)
            return &m->entries[i];
    return NULL;
}

static int unit_operators(void)
{
    SegaCDView view;M68kStepContext c;const M68kStepModule *m;
    const M68kStepEntry *e;segacd_view_init(&view);make_fixture();captured=0;
    CHECK(module_for_observers(&view,640));m=segacd_view_module(&view);
    CHECK(m&&m->count==17&&m68k_step_module_valid(m));

    e=entry_opcode(m,0x9678,0xF700);CHECK(e);
    {
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        bus.fetch16=fetch16;bus.read16=read16;m68k_step_init(&c,&bus,&user);
        c.state.regs.PC=e->pc;c.state.regs.D[3]=1000;c.state.regs.SR=0x271F;
        put16(main_ram+0xF700,500);
        CHECK(segacd_view_mark_installed(&view,1));
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK((uint16_t)c.state.regs.D[3]==660&&c.state.regs.PC==e->pc+4);
        CHECK(c.state.cycles==12&&(c.state.regs.SR&0x1Fu)==0);
    }

    e=entry_opcode(m,0x0C41,0x0140);CHECK(e);
    {
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        bus.fetch16=fetch16;bus.read16=read16;m68k_step_init(&c,&bus,&user);
        c.state.regs.PC=e->pc;c.state.regs.D[1]=641;c.state.regs.SR=0x271F;
        CHECK(segacd_view_mark_installed(&view,1));
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK((c.state.regs.SR&0x1Fu)==0x10u&&c.state.cycles==8&&c.state.regs.PC==e->pc+4);
    }

    e=entry_opcode(m,0x0446,0x0080);CHECK(e);
    {
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        bus.fetch16=fetch16;bus.read16=read16;m68k_step_init(&c,&bus,&user);
        c.state.regs.PC=e->pc;c.state.regs.D[6]=1000;c.state.regs.SR=0x271F;
        CHECK(segacd_view_mark_installed(&view,1));
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK((uint16_t)c.state.regs.D[6]==616&&c.state.cycles==8);
    }

    e=entry_opcode(m,0x0646,0x0300);CHECK(e);
    {
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        bus.fetch16=fetch16;bus.read16=read16;m68k_step_init(&c,&bus,&user);
        c.state.regs.PC=e->pc;c.state.regs.D[6]=1000;
        CHECK(segacd_view_mark_installed(&view,1));
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK((uint16_t)c.state.regs.D[6]==2280&&c.state.cycles==8);
    }

    e=entry_opcode(m,0x0646,0x0280);CHECK(e);
    {
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        bus.fetch16=fetch16;bus.read16=read16;m68k_step_init(&c,&bus,&user);
        c.state.regs.PC=e->pc;c.state.regs.D[6]=1000;
        CHECK(segacd_view_mark_installed(&view,1));
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK((uint16_t)c.state.regs.D[6]==1896&&c.state.cycles==8);
    }

    e=entry_opcode(m,0x0446,0x0300);CHECK(e);
    {
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        bus.fetch16=fetch16;bus.read16=read16;m68k_step_init(&c,&bus,&user);
        c.state.regs.PC=e->pc;c.state.regs.D[6]=2000;
        CHECK(segacd_view_mark_installed(&view,1));
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK((uint16_t)c.state.regs.D[6]==720&&c.state.cycles==8);
    }

    e=entry_opcode(m,0x0441,0x0080);CHECK(e);
    {
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        bus.fetch16=fetch16;bus.read16=read16;m68k_step_init(&c,&bus,&user);
        c.state.regs.PC=e->pc;c.state.regs.D[1]=1000;
        CHECK(segacd_view_mark_installed(&view,1));
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK((uint16_t)c.state.regs.D[1]==616&&c.state.cycles==8);
    }

    e=NULL;
    for(size_t i=0;i<m->count;++i)
        if(m->entries[i].pc==WORD_BASE+0x1400u+0x12u)e=&m->entries[i];
    CHECK(e&&e->words[0]==0x0441&&e->words[1]==0x0080);
    {
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        bus.fetch16=fetch16;bus.read16=read16;m68k_step_init(&c,&bus,&user);
        c.state.regs.PC=e->pc;c.state.regs.D[1]=1000;
        CHECK(segacd_view_mark_installed(&view,1));
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK((uint16_t)c.state.regs.D[1]==616&&c.state.cycles==8);
    }

    e=entry_opcode(m,0x0C40,0x0280);CHECK(e);
    {
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        bus.fetch16=fetch16;bus.read16=read16;m68k_step_init(&c,&bus,&user);
        c.state.regs.PC=e->pc;c.state.regs.D[0]=900;
        CHECK(segacd_view_mark_installed(&view,1));
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK((uint16_t)c.state.regs.SR==0x0009&&c.state.cycles==8);
    }

    e=entry_opcode(m,0xBC78,0xF76E);CHECK(e);
    {
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        SegaCDViewSnapshot snap={fixture,sizeof fixture,WORD_BASE,main_ram,
                                 sizeof main_ram,MAIN_BASE};
        bus.fetch16=fetch16;bus.read16=read16;bus.read8=read8;
        main_ram[0xF76C]=2;put16(main_ram+0xF76E,0x0100);
        CHECK(segacd_view_request_spawn_refresh(&view,&snap));
        CHECK(segacd_view_spawn_refresh_pending(&view));
        m68k_step_init(&c,&bus,&user);c.state.regs.PC=e->pc;
        c.state.regs.D[6]=0x0100;c.state.regs.SR=0x271F;
        CHECK(segacd_view_mark_installed(&view,1));
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((c.state.regs.SR&0x000Fu)==0x0008u&&c.state.cycles==12&&
              c.state.regs.PC==e->pc+4&&segacd_view_spawn_refresh_pending(&view));
        /* Backward SUB crosses zero: keep original arithmetic for restoration,
         * scan with $FFFF and no carry so X=0 reaches the source -1 sentinel. */
        const M68kStepEntry *back=NULL,*back_add=NULL,*forward_add=NULL,*forward_sub=NULL;
        for(size_t i=0;i<m->count;++i) {
            if(m->entries[i].pc==WORD_BASE+0x1000u+0xBAu)back=&m->entries[i];
            if(m->entries[i].pc==WORD_BASE+0x1000u+0xFAu)back_add=&m->entries[i];
            if(m->entries[i].pc==WORD_BASE+0x1000u+0x126u)forward_add=&m->entries[i];
            if(m->entries[i].pc==WORD_BASE+0x1000u+0x154u)forward_sub=&m->entries[i];
        }
        CHECK(back&&back_add&&forward_add&&forward_sub);
        c.state.regs.PC=back->pc;c.state.regs.D[6]=0x0100;c.state.cycles=0;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==0xFFFFu&&
              !(c.state.regs.SR&1u)&&c.state.cycles==8);
        c.state.regs.PC=back_add->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==0x0480u);
        put16(main_ram+0xF76E,0x0100);c.state.regs.PC=e->pc;c.state.regs.D[6]=0x0100;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((c.state.regs.SR&0x000Fu)==0&&
              segacd_view_spawn_refresh_pending(&view));
        c.state.regs.D[6]=0x0100;c.state.regs.PC=forward_add->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==0x0480u);
        c.state.regs.PC=forward_sub->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==0xFF80u&&
              !segacd_view_spawn_refresh_pending(&view));

        /* A camera chunk change between the backward and forward passes
         * restarts the native backward path at the new live chunk. */
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK(segacd_view_request_spawn_refresh(&view,&snap));
        CHECK(segacd_view_mark_installed(&view,1));
        c.state.regs.PC=e->pc;c.state.regs.D[6]=0x0100;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        c.state.regs.D[6]=0x0100;c.state.regs.PC=back->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        c.state.regs.PC=back_add->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        c.state.regs.D[6]=0x0180;c.state.regs.PC=e->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((c.state.regs.SR&0x000Fu)==0x0008u&&
              segacd_view_spawn_refresh_pending(&view));
        c.state.regs.D[6]=0x0180;c.state.regs.PC=back->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==0);
        c.state.regs.PC=back_add->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==0x0500u);
        c.state.regs.D[6]=0x0180;c.state.regs.PC=e->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((c.state.regs.SR&0x000Fu)==0);
        c.state.regs.D[6]=0x0180;c.state.regs.PC=forward_add->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==0x0500u);
        c.state.regs.PC=forward_sub->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==0&&
              !segacd_view_spawn_refresh_pending(&view));
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK(segacd_view_request_spawn_refresh(&view,&snap));
        CHECK(segacd_view_mark_installed(&view,1));
        c.state.regs.D[6]=0x0100;c.state.regs.PC=e->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        c.state.regs.D[6]=0x0100;c.state.regs.PC=back->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==0xFFFFu&&
              segacd_view_spawn_refresh_pending(&view));
        /* If code is replaced after the temporary underflow clamp, restore
         * the original SUB result before executing the exact original ADD. */
        uint8_t saved_context=fixture[back_add->pc-WORD_BASE-10u];
        fixture[back_add->pc-WORD_BASE-10u]^=1u;
        c.state.regs.PC=back_add->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        fixture[back_add->pc-WORD_BASE-10u]=saved_context;
        CHECK((uint16_t)c.state.regs.D[6]==0x0280u&&
              !segacd_view_spawn_refresh_pending(&view));
        CHECK(segacd_view_mark_installed(&view,0));
    }

    e=entry_opcode(m,0x45F8,0xF800);CHECK(e);
    {
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        bus.fetch16=fetch16;bus.read16=read16;m68k_step_init(&c,&bus,&user);
        c.state.regs.PC=e->pc;c.state.regs.A[2]=0x12345678u;
        CHECK(segacd_view_mark_installed(&view,1));
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK(c.state.regs.A[2]==0xFFFFF800u&&c.state.cycles==8&&captured==1);
    }

    e=entry_opcode(m,0x11C5,0xF62C);CHECK(e);
    {
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        bus.fetch16=fetch16;bus.read16=read16;bus.read8=read8;bus.write8=write8;
        m68k_step_init(&c,&bus,&user);c.state.regs.PC=e->pc;
        c.state.regs.D[5]=3;c.state.regs.SR=0x271F;main_ram[0xF62C]=0x55;
        sat_captured=0;sat_before_store=0;
        CHECK(segacd_view_mark_installed(&view,1));
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK(sat_captured==1&&sat_before_store==0x55);
        CHECK(main_ram[0xF62C]==3&&c.state.cycles==12&&c.state.regs.PC==e->pc+4);
        CHECK((c.state.regs.SR&0x1Fu)==0x10u);
    }

    /* The adjacent initialization subtracts are intentionally asymmetric. */
    {
        const M68kStepEntry *a=NULL,*b=NULL;
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        for(size_t i=0;i<m->count;++i) {
            if(m->entries[i].pc==WORD_BASE+0x1044u)a=&m->entries[i];
            if(m->entries[i].pc==WORD_BASE+0x106Eu)b=&m->entries[i];
        }
        CHECK(a&&b&&a->words[0]==0x0446&&b->words[0]==0x0446);
        bus.fetch16=fetch16;bus.read16=read16;m68k_step_init(&c,&bus,&user);
        c.state.regs.D[6]=1000;
        CHECK(segacd_view_mark_installed(&view,1));
        c.state.regs.PC=a->pc;CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==616);
        c.state.regs.PC=b->pc;CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK((uint16_t)c.state.regs.D[6]==488); /* camera - 0x100 - C */
    }

    /* Verify paired forward/backward bounds as composed cursor equations. */
    {
        const M68kStepEntry *add300=NULL,*add280=NULL,*sub300=NULL,*ds=NULL,*dc=NULL;
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        for(size_t i=0;i<m->count;++i) {
            const M68kStepEntry *q=&m->entries[i];
            if(q->pc==WORD_BASE+0x10FAu)add300=q;
            if(q->pc==WORD_BASE+0x1126u)add280=q;
            if(q->pc==WORD_BASE+0x1154u)sub300=q;
            if(q->pc==WORD_BASE+0x1412u)ds=q;
            if(q->pc==WORD_BASE+0x141Cu)dc=q;
        }
        CHECK(add300&&add280&&sub300&&ds&&dc);
        bus.fetch16=fetch16;bus.read16=read16;m68k_step_init(&c,&bus,&user);
        CHECK(segacd_view_mark_installed(&view,1));
        c.state.regs.D[6]=1000;c.state.regs.PC=add280->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==1896); /* camera + 0x280 + C */
        c.state.regs.PC=sub300->pc;CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==616); /* camera - 0x80 - C */
        c.state.regs.D[6]=1000;c.state.regs.PC=add300->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==2280);
        c.state.regs.D[6]=1000;c.state.regs.PC=add280->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        c.state.regs.PC=sub300->pc;CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[6]==616);
        c.state.regs.D[1]=1000;c.state.regs.PC=ds->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[1]==616);
        /* The adjusted absolute camera subtraction plus relative compare
         * threshold preserve symmetric hysteresis around the new view. */
        c.state.regs.D[0]=(uint16_t)(0x0280u+2u*256u);c.state.regs.PC=dc->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((c.state.regs.SR&0x1Fu)==0x04u); /* adjusted threshold equality */
        c.state.regs.D[0]=1153;c.state.regs.PC=dc->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((c.state.regs.SR&0x1Fu)==0);
        CHECK(segacd_view_mark_installed(&view,0));
    }
    /* Early ring groups must survive the widened spawn horizon. Their
     * separate inline deletion check uses the same expanded world bounds. */
    {
        const M68kStepEntry *sub=NULL,*cmp=NULL;
        for(size_t i=0;i<m->count;++i) {
            const M68kStepEntry *q=&m->entries[i];
            if(q->pc==WORD_BASE+0x2312u)sub=q;
            if(q->pc==WORD_BASE+0x231Cu)cmp=q;
            CHECK(q->pc!=WORD_BASE+0x2412u&&q->pc!=WORD_BASE+0x241Cu);
        }
        CHECK(sub&&cmp&&sub->words[0]==0x0441&&cmp->words[0]==0x0C40);
        M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
        bus.fetch16=fetch16;m68k_step_init(&c,&bus,&user);
        CHECK(segacd_view_mark_installed(&view,1));
        c.state.regs.D[1]=384;c.state.regs.PC=sub->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((uint16_t)c.state.regs.D[1]==0&&c.state.cycles==8);
        c.state.regs.D[0]=1152-(uint16_t)c.state.regs.D[1];
        c.state.regs.PC=cmp->pc;
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK((c.state.regs.SR&0x0Fu)==0x04u&&c.state.cycles==16);
        CHECK(segacd_view_mark_installed(&view,0));
    }
    return 0;
}

static int short_cleanup_guards(void)
{
    static const uint8_t cull[]={
        0x30,0x28,0x00,0x36,0x02,0x40,0xFF,0x80,
        0x32,0x38,0xF7,0x00,0x04,0x41,0x00,0x80,
        0x02,0x41,0xFF,0x80,0x90,0x41,0x0C,0x40,
        0x02,0x80,0x62,0x02,0x4E,0x75,
        0x4E,0xF9,0x00,0x20,0x28,0x00
    };
    SegaCDViewSnapshot s={fixture,sizeof fixture,WORD_BASE,
        main_ram,sizeof main_ram,MAIN_BASE};
    SegaCDCullDiscovery sites;
    SegaCDView view;
    make_fixture();put_bytes(0x3000,cull,sizeof cull);
    CHECK(segacd_culls_discover(&s,&sites)&&sites.site_count==2u);
    CHECK(sites.sites[1].family==SEGACD_CULL_SHORT_CLEANUP&&
          sites.sites[1].subtract_pc==WORD_BASE+0x300Cu&&
          sites.sites[1].compare_pc==WORD_BASE+0x3016u&&
          sites.sites[1].cleanup_kind==SEGACD_CULL_CLEANUP_DELETE_OBJECT&&
          sites.sites[1].cleanup_pc==WORD_BASE+0x2800u);
    segacd_view_init(&view);
    CHECK(segacd_view_configure(&view,&s,640,NULL,NULL));
    CHECK(segacd_view_module(&view)->count==17u); /* No queue/SAT observers. */
    /* A different full displacement is not this source window, even if its
     * low byte matches. Never widen a different object field by accident. */
    put16(fixture+0x3002,0x0136u);
    CHECK(segacd_culls_discover(&s,&sites)&&sites.site_count==1u);
    put16(fixture+0x3002,0x0036u);
    fixture[0x301B]=4u; /* BHI must reach the exact cleanup entry. */
    CHECK(!segacd_culls_discover(&s,&sites));
    fixture[0x301B]=2u;
    put16(fixture+0x3022,0x2802u); /* Mid-function JMP is not DeleteObject. */
    CHECK(!segacd_culls_discover(&s,&sites));
    return 0;
}

static int fail_closed(void)
{
    static const uint8_t draw_anchor[]={0x08,0xA8,0,7,0,1,0x10,0x28,
        0,1,2,0x40,0,0x0C,0x67,0};
    SegaCDView view;SegaCDViewSnapshot s={fixture,sizeof fixture,WORD_BASE,
        main_ram,sizeof main_ram,MAIN_BASE};
    segacd_view_init(&view);make_fixture();
    CHECK(segacd_view_configure(&view,&s,320,NULL,NULL));
    CHECK(segacd_view_module(&view)&&segacd_view_module(&view)->count==0);
    CHECK(!segacd_view_mark_installed(&view,1));
    CHECK(!segacd_view_configure(&view,&s,319,NULL,NULL));
    CHECK(segacd_view_module(&view)==NULL);
    CHECK(!segacd_view_configure(&view,&s,SEGACD_SONIC_CD_MAX_VIEW_WIDTH+1u,NULL,NULL));
    put16(fixture+0x2210,0x8000);
    CHECK(!segacd_view_configure(&view,&s,640,NULL,NULL));
    CHECK(segacd_view_module(&view)==NULL);
    make_fixture();
    s.word_ram_size=0x20000;
    CHECK(!segacd_view_configure(&view,&s,640,NULL,NULL));
    CHECK(segacd_view_module(&view)==NULL);
    s.word_ram_size=sizeof fixture;
    put_bytes(0x3000,draw_anchor,sizeof draw_anchor);
    CHECK(!segacd_view_configure(&view,&s,640,NULL,NULL));
    CHECK(segacd_view_module(&view)==NULL);
    return 0;
}

static int stale_source_fallback(void)
{
    SegaCDView view;M68kStepContext c;const M68kStepModule *m;
    const M68kStepEntry *e;M68kStepBus bus={0};Bus user={fixture,sizeof fixture};
    uint8_t saved;segacd_view_init(&view);make_fixture();captured=0;
    CHECK(module_for_observers(&view,640));m=segacd_view_module(&view);
    e=entry_opcode(m,0x9678,0xF700);CHECK(e);
    bus.fetch16=fetch16;bus.read16=read16;m68k_step_init(&c,&bus,&user);
    put16(main_ram+0xF700,500);c.state.regs.PC=e->pc;c.state.regs.D[3]=1000;
    /* Change a neighboring source byte inside the saved 32-byte window while
     * keeping the intercepted instruction and its extension unchanged. */
    saved=fixture[e->pc-WORD_BASE-10u];fixture[e->pc-WORD_BASE-10u]^=1u;
    CHECK(segacd_view_mark_installed(&view,1));
    CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
    CHECK(segacd_view_mark_installed(&view,0));
    CHECK((uint16_t)c.state.regs.D[3]==500);
    CHECK(c.state.regs.PC==e->pc+4u&&c.state.cycles==12);
    fixture[e->pc-WORD_BASE-10u]=saved;
    /* A stale source context at the refresh compare cancels the request and
     * leaves the original CMP.W D6,(abs).L architectural result intact. */
    segacd_view_init(&view);make_fixture();
    CHECK(module_for_observers(&view,640));m=segacd_view_module(&view);
    e=entry_opcode(m,0xBC78,0xF76E);CHECK(e);
    {
        SegaCDViewSnapshot snap={fixture,sizeof fixture,WORD_BASE,main_ram,
                                 sizeof main_ram,MAIN_BASE};
        main_ram[0xF76C]=2;put16(main_ram+0xF76E,0x0120);
        CHECK(segacd_view_request_spawn_refresh(&view,&snap));
        saved=fixture[e->pc-WORD_BASE-10u];
        fixture[e->pc-WORD_BASE-10u]^=1u;
        bus.read8=read8;
        m68k_step_init(&c,&bus,&user);c.state.regs.PC=e->pc;
        c.state.regs.D[6]=0x0100;c.state.regs.SR=0x271F;
        CHECK(segacd_view_mark_installed(&view,1));
        CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
        CHECK(segacd_view_mark_installed(&view,0));
        CHECK(!segacd_view_spawn_refresh_pending(&view));
        CHECK((c.state.regs.SR&0x000Fu)==0x0009u&&c.state.cycles==12&&
              c.state.regs.PC==e->pc+4u);
        fixture[e->pc-WORD_BASE-10u]=saved;
    }
    segacd_view_init(&view);
    CHECK(module_for_observers(&view,640));m=segacd_view_module(&view);
    e=entry_opcode(m,0x45F8,0xF800);CHECK(e);
    m68k_step_init(&c,&bus,&user);c.state.regs.PC=e->pc;c.state.regs.A[2]=0x12345678u;
    saved=fixture[e->pc-WORD_BASE-10u];fixture[e->pc-WORD_BASE-10u]^=1u;
    CHECK(segacd_view_mark_installed(&view,1));
    CHECK(m68k_step_module_execute(&c,m)==M68K_STEP_OK);
    CHECK(segacd_view_mark_installed(&view,0));
    CHECK(c.state.regs.A[2]==0xFFFFF800u&&c.state.cycles==8&&captured==0);
    fixture[e->pc-WORD_BASE-10u]=saved;
    return 0;
}

int main(void)
{
    if(fail_closed()||short_cleanup_guards()||stale_source_fallback()||
       unit_operators())return 1;
    puts("Sonic CD guarded view operators passed.");
    return 0;
}
