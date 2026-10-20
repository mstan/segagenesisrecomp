/* Compile real 68000 instructions through the shared emitter into native C
 * operating on independent CPU/memory contexts. No owned program is used. */
#include "code_generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x); exit(1); } } while (0)
typedef struct { const char *name; uint16_t words[4]; unsigned count; int handled,operation; } Case;
static const Case cases[] = {
    {"add_word", {0x0640,0x0001},2,1,1},       /* ADDI.W #1,D0 */
    {"add_memory", {0x5258},1,1,1},           /* ADDQ.W #1,(A0)+ */
    {"indexed_word", {0x3430,0x1800},2,1,1},  /* MOVE.W (A0,D1.L),D2 */
    {"store_long", {0x2081},1,1,1},           /* MOVE.L D1,(A0) */
    {"adjust_stack", {0x588F},1,1,1},         /* ADDQ.L #4,A7 */
    {"moveq", {0x70FF},1,1,1},               /* MOVEQ #-1,D0 */
    {"bsr", {0x6100,0x0008},2,0,1},
    {"bne", {0x6602},1,0,1},
    {"dbf", {0x51C8,0xFFFE},2,0,1},
    {"jsr", {0x4E90},1,0,1},
    {"jump", {0x4ED0},1,0,1},
    {"trap", {0x4E40},1,0,1},
    {"rte", {0x4E73},1,0,1},
    {"rts", {0x4E75},1,0,1},
    {"stop", {0x4E72,0x2300},2,0,1},
    {"write_sr", {0x46FC,0x2700},2,0,1},
    {"divide", {0x80FC,0x0000},2,0,1},
    {"divide_one", {0x80FC,0x0001},2,0,1},
    {"signed_divide", {0x81FC,0x0003},2,0,1},
    {"signed_overflow", {0x81FC,0xFFFF},2,0,1},
    {"check", {0x41BC,0x000A},2,0,1},
    {"stack_read", {0x1027},1,1,1},  /* MOVE.B -(A7),D0 */
    {"stack_write", {0x1EC0},1,1,1}, /* MOVE.B D0,(A7)+ */
    {"stack_cmpm", {0xBF0F},1,1,1},  /* CMPM.B (A7)+,(A7)+ */
    {"asl_long", {0xE3A0},1,1,1},    /* ASL.L D1,D0 */
    {"roxr_long", {0xE2B0},1,1,1},   /* ROXR.L D1,D0 */
    {"roxl_long", {0xE3B0},1,1,1},   /* ROXL.L D1,D0 */
    {"clr_byte_postincrement", {0x4218},1,1,1}, /* CLR.B (A0)+ */
    {"clr_word_postincrement", {0x4258},1,1,1}, /* CLR.W (A0)+ */
    {"clr_long_postincrement", {0x4298},1,1,1}, /* CLR.L (A0)+ */
    {"clr_byte_a7_postincrement", {0x421F},1,1,1}, /* CLR.B (A7)+ */
    {"clr_byte_predecrement", {0x4220},1,1,1}, /* CLR.B -(A0) */
    {"clr_word_displacement", {0x4268,0x0028},2,1,1}, /* CLR.W 40(A0) */
    {"clr_byte_absolute_long", {0x4239,0x0000,0x0054},3,1,1}, /* CLR.B $54 */
};

int main(int argc, char **argv)
{
    CHECK(argc==2);FILE *out=fopen(argv[1],"wb");CHECK(out);
    fputs("#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include \"m68k_step.h\"\n"
          "typedef M68kStepContext Context;\ntypedef struct {uint8_t bytes[512];unsigned events;uint8_t op[16];uint32_t addr[16];} Memory;\n"
          "#define CHECK(x) do { if(!(x)) { fprintf(stderr,\"line %u: %s\\n\",(unsigned)__LINE__,#x); exit(1); } } while(0)\n"
          "static void event(Memory*m,uint8_t op,uint32_t a){if(a>=0x30&&a<0x80){if(m->events<16){m->op[m->events]=op;m->addr[m->events]=a;}++m->events;}}\n"
          "static M68kStepBusResult read8(void*u,uint32_t a,uint8_t*v){Memory*m=u;event(m,1,a);if(a>=512)return M68K_BUS_FAULT;*v=m->bytes[a];return M68K_BUS_OK;}\n"
          "static M68kStepBusResult read16(void*u,uint32_t a,uint16_t*v){Memory*m=u;event(m,1,a);if(a+1>=512)return M68K_BUS_FAULT;*v=(uint16_t)(((unsigned)m->bytes[a]<<8)|m->bytes[a+1]);return M68K_BUS_OK;}\n"
          "static M68kStepBusResult write8(void*u,uint32_t a,uint8_t v){Memory*m=u;event(m,2,a);if(a>=512)return M68K_BUS_FAULT;m->bytes[a]=v;return M68K_BUS_OK;}\n"
          "static M68kStepBusResult write16(void*u,uint32_t a,uint16_t v){Memory*m=u;event(m,2,a);if(a+1>=512)return M68K_BUS_FAULT;m->bytes[a]=(uint8_t)(v>>8);m->bytes[a+1]=(uint8_t)v;return M68K_BUS_OK;}\n"
          "#define g_cpu ctx->state.regs\n"
          "#define m68k_brief_index_value m68k_step_brief_index_value\n"
          "#define m68k_read8(a) m68k_step_read8(ctx,a)\n#define m68k_read16(a) m68k_step_read16(ctx,a)\n#define m68k_read32(a) m68k_step_read32(ctx,a)\n"
          "#define m68k_write8(a,v) m68k_step_write8(ctx,a,v)\n#define m68k_write16(a,v) m68k_step_write16(ctx,a,v)\n#define m68k_write32(a,v) m68k_step_write32(ctx,a,v)\n",out);
    for (unsigned i=0;i<sizeof cases/sizeof *cases;++i) {
        const Case *test=&cases[i];uint8_t image[16]={0};
        for(unsigned n=0;n<test->count;++n){image[2*n]=(uint8_t)(test->words[n]>>8);image[2*n+1]=(uint8_t)test->words[n];}
        GenesisRom rom={0};rom.rom_data=image;rom.rom_size=sizeof image;
        M68KInstr ins;CHECK(m68k_decode(&rom,0,&ins));
        FILE *probe=tmpfile();CHECK(probe);
        CHECK(codegen_emit_step_semantics(probe,&rom,&ins)==(test->handled!=0));
        if(!test->handled)CHECK(ftell(probe)==0);
        CHECK(!fclose(probe));
        if(test->operation)fprintf(out,"static void %s(Context*ctx){\n",test->name);
        long before=ftell(out);CHECK(before>=0);
        CHECK(codegen_emit_step_operation(out,&rom,&ins)==(test->operation!=0));
        if(test->operation) {
            fputs("}\n",out);
            fprintf(out,"static const M68kStepEntry e_%s={0,{",test->name);
            for(unsigned w=0;w<ins.word_count;++w)fprintf(out,"%s0x%04X",w?",":"",ins.words[w]);
            fprintf(out,"},%u,4,%s};\n",(unsigned)ins.word_count,test->name);
        } else CHECK(ftell(out)==before);
    }
    fputs("static void run(Context*c,const M68kStepEntry*e){for(unsigned n=0;n<e->word_count;++n)m68k_step_write16(c,e->pc+n*2,e->words[n]);c->state.regs.PC=e->pc;CHECK(m68k_step_execute(c,e)==M68K_STEP_OK);}\n"
          "static void check_shift(Context*c,const M68kStepEntry*e,uint32_t d0,uint32_t d1,uint16_t sr,uint32_t want,uint16_t want_flags){c->state.regs.D[0]=d0;c->state.regs.D[1]=d1;c->state.regs.SR=sr;run(c,e);CHECK(c->state.regs.D[0]==want);CHECK((c->state.regs.SR&0x1Fu)==want_flags);CHECK(c->state.regs.PC==2);CHECK(c->charge==8u+2u*(d1&63u));}\n"
          "static void check_clr_at(Context*c,const M68kStepEntry*e,unsigned size,unsigned areg,unsigned initial,unsigned target,unsigned final){Memory*m=(Memory*)c->user;for(unsigned k=0;k<size;++k)m->bytes[target+k]=(uint8_t)(0xA5u+k);c->state.regs.A[areg]=initial;c->state.regs.SR=0x2011;m->events=0;run(c,e);unsigned parts=size==4?2:1;CHECK(m->events==parts*2);for(unsigned p=0;p<parts;++p)CHECK(m->op[p]==1&&m->addr[p]==target+p*2);for(unsigned p=0;p<parts;++p)CHECK(m->op[parts+p]==2&&m->addr[parts+p]==target+p*2);CHECK((c->state.regs.SR&31)==0x14);CHECK(c->state.regs.A[areg]==final);for(unsigned k=0;k<size;++k)CHECK(m->bytes[target+k]==0);}\n",out);
    fputs("int main(void){\nContext a,b;Memory ma={0},mb={0};M68kStepBus bus={read8,read16,write8,write16,read16,0,0,0};m68k_step_init(&a,&bus,&ma);m68k_step_init(&b,&bus,&mb);\n"
          "a.state.regs.D[0]=0xFFFF7FFFu;a.state.regs.SR=0x2000;run(&a,&e_add_word);CHECK(a.state.regs.D[0]==0xFFFF8000u);CHECK(a.state.regs.SR==0x200Au);\n"
          "b.state.regs.D[0]=2;b.state.regs.SR=0x2000;run(&b,&e_add_word);CHECK(b.state.regs.D[0]==3);CHECK(b.state.regs.SR==0x2000);CHECK(a.state.regs.D[0]==0xFFFF8000u);\n"
          "a.state.regs.A[0]=0x40;m68k_step_write16(&a,0x40,0x7FFF);run(&a,&e_add_memory);CHECK(a.state.regs.A[0]==0x42);CHECK(m68k_step_read16(&a,0x40)==0x8000);\n"
          "a.state.regs.A[0]=0x20;a.state.regs.D[1]=0x100;a.state.regs.D[2]=0xABCD0000u;m68k_step_write16(&a,0x120,0xBEEF);run(&a,&e_indexed_word);CHECK(a.state.regs.D[2]==0xABCDBEEFu);\n"
          "a.state.regs.A[0]=0x50;a.state.regs.D[1]=0x12345678;run(&a,&e_store_long);CHECK(m68k_step_read32(&a,0x50)==0x12345678);CHECK(m68k_step_read32(&b,0x50)==0);\n"
          "a.state.regs.A[7]=0x100;a.state.regs.SR=0x2701;run(&a,&e_adjust_stack);CHECK(a.state.regs.A[7]==0x104);CHECK(a.state.regs.SR==0x2701);\n"
          "run(&a,&e_moveq);CHECK(a.state.regs.D[0]==0xFFFFFFFFu);CHECK((a.state.regs.SR&15)==8);\n"
          "a.state.regs.SR=0x2000;a.state.regs.A[7]=0x100;run(&a,&e_bsr);CHECK(a.state.regs.PC==10);CHECK(a.state.regs.A[7]==0xFC);CHECK(m68k_step_read32(&a,0xFC)==4);run(&a,&e_rts);CHECK(a.state.regs.PC==4 && a.state.regs.A[7]==0x100);\n"
          "a.state.regs.SR=0x2000;run(&a,&e_bne);CHECK(a.state.regs.PC==4);CHECK(a.charge==10);a.state.regs.SR=0x2004;run(&a,&e_bne);CHECK(a.state.regs.PC==2);CHECK(a.charge==8);\n"
          "a.state.regs.D[0]=0xABCD0001;run(&a,&e_dbf);CHECK(a.state.regs.D[0]==0xABCD0000u && a.state.regs.PC==0 && a.charge==10);run(&a,&e_dbf);CHECK(a.state.regs.D[0]==0xABCDFFFFu && a.state.regs.PC==4 && a.charge==14);\n"
          "a.state.regs.A[0]=0x120;a.state.regs.A[7]=0x100;run(&a,&e_jsr);CHECK(a.state.regs.PC==0x120 && a.state.regs.A[7]==0xFC && m68k_step_read32(&a,0xFC)==2);run(&a,&e_rts);CHECK(a.state.regs.PC==2 && a.state.regs.A[7]==0x100);\n"
          "a.state.regs.A[0]=0xFE123456;run(&a,&e_jump);CHECK(a.state.regs.PC==0x123456);\n"
          "a.state.regs.SR=0x2000;a.state.regs.A[7]=0x100;m68k_step_write32(&a,32*4,0x180);run(&a,&e_trap);CHECK(a.state.regs.PC==0x180 && a.state.exception_vector==32 && m68k_step_read32(&a,0xFC)==2);run(&a,&e_rte);CHECK(a.state.regs.PC==2 && a.state.regs.A[7]==0x100);\n"
          "a.state.regs.USP=0x180;run(&a,&e_write_sr);CHECK(a.state.regs.SR==0x2700);\n"
          "run(&a,&e_stop);CHECK(a.state.stopped && a.state.regs.PC==4);m68k_step_write32(&a,28*4,0x180);CHECK(m68k_step_interrupt(&a,4));CHECK(!a.state.stopped && a.state.regs.PC==0x180);run(&a,&e_rte);CHECK(a.state.regs.PC==4);\n"
          "a.state.regs.D[0]=0x10000;a.state.regs.SR=0x2011;run(&a,&e_divide_one);CHECK(a.state.regs.D[0]==0x10000 && (a.state.regs.SR&0x13)==0x12);\n"
          "a.state.regs.D[0]=0xFFFF;a.state.regs.SR=0x2000;run(&a,&e_divide_one);CHECK(a.state.regs.D[0]==0xFFFF && (a.state.regs.SR&0xB)==8);\n"
          "a.state.regs.D[0]=(uint32_t)-7;run(&a,&e_signed_divide);CHECK(a.state.regs.D[0]==0xFFFFFFFEu && (a.state.regs.SR&0xB)==8);\n"
          "a.state.regs.D[0]=0x80000000u;run(&a,&e_signed_overflow);CHECK(a.state.regs.D[0]==0x80000000u && (a.state.regs.SR&2));\n"
          "a.state.regs.A[7]=0x100;a.state.regs.D[0]=0x12345678;m68k_step_write32(&a,5*4,0x180);run(&a,&e_divide);CHECK(a.state.exception_vector==5 && a.state.regs.PC==0x180 && a.state.regs.D[0]==0x12345678 && m68k_step_read32(&a,0xFC)==4 && a.charge==42);\n"
          "a.state.regs.A[7]=0x100;a.state.regs.D[0]=11;m68k_step_write32(&a,6*4,0x180);run(&a,&e_check);CHECK(a.state.exception_vector==6 && a.state.regs.PC==0x180 && !(a.state.regs.SR&8) && a.charge==44);\n"
          "a.state.regs.A[7]=0x100;a.state.regs.D[0]=0xFFFF;run(&a,&e_check);CHECK(a.state.exception_vector==6 && (a.state.regs.SR&8));\n"
          "a.state.regs.D[0]=10;run(&a,&e_check);CHECK(!a.state.exception_vector && a.state.regs.PC==4 && a.charge==14);\n"
          "a.state.regs.A[7]=0x102;a.state.regs.D[0]=0xAABBCC00u;m68k_step_write8(&a,0x100,0x42);run(&a,&e_stack_read);CHECK(a.state.regs.A[7]==0x100 && a.state.regs.D[0]==0xAABBCC42u);\n"
          "a.state.regs.D[0]=0x3E;run(&a,&e_stack_write);CHECK(a.state.regs.A[7]==0x102 && m68k_step_read8(&a,0x100)==0x3E);\n"
          "a.state.regs.A[7]=0x100;a.state.regs.SR=0x2010;m68k_step_write8(&a,0x100,1);m68k_step_write8(&a,0x102,3);run(&a,&e_stack_cmpm);CHECK(a.state.regs.A[7]==0x104 && a.state.regs.SR==0x2010);\n"
          "check_shift(&a,&e_roxl_long,0x80000001u,0,0x200Fu,0x80000001u,0x08);check_shift(&a,&e_roxl_long,0x80000001u,0,0x201Eu,0x80000001u,0x19);\n"
          "check_shift(&a,&e_roxl_long,0x80000001u,1,0x201Eu,0x00000003u,0x11);check_shift(&a,&e_roxl_long,0x80000001u,31,0x201Eu,0xE0000000u,0x08);check_shift(&a,&e_roxl_long,0x80000001u,32,0x201Eu,0xC0000000u,0x19);check_shift(&a,&e_roxl_long,0x80000001u,33,0x201Eu,0x80000001u,0x19);check_shift(&a,&e_roxl_long,0x80000001u,63,0x201Eu,0x70000000u,0x00);\n"
          "check_shift(&a,&e_roxr_long,0x80000001u,0,0x200Fu,0x80000001u,0x08);check_shift(&a,&e_roxr_long,0x80000001u,0,0x201Eu,0x80000001u,0x19);\n"
          "check_shift(&a,&e_roxr_long,0x80000001u,1,0x201Eu,0xC0000000u,0x19);check_shift(&a,&e_roxr_long,0x80000001u,31,0x201Eu,0x00000007u,0x00);check_shift(&a,&e_roxr_long,0x80000001u,32,0x201Eu,0x00000003u,0x11);check_shift(&a,&e_roxr_long,0x80000001u,33,0x201Eu,0x80000001u,0x19);check_shift(&a,&e_roxr_long,0x80000001u,63,0x201Eu,0x0000000Eu,0x00);\n"
          "check_shift(&a,&e_asl_long,0xFFFFFFFFu,0,0x201Eu,0xFFFFFFFFu,0x18);check_shift(&a,&e_asl_long,0xFFFFFFFFu,1,0x201Eu,0xFFFFFFFEu,0x19);check_shift(&a,&e_asl_long,0xFFFFFFFFu,31,0x201Eu,0x80000000u,0x19);check_shift(&a,&e_asl_long,0xFFFFFFFFu,32,0x201Eu,0x00000000u,0x17);check_shift(&a,&e_asl_long,0xFFFFFFFFu,33,0x201Eu,0x00000000u,0x06);check_shift(&a,&e_asl_long,0xFFFFFFFFu,63,0x201Eu,0x00000000u,0x06);check_shift(&a,&e_asl_long,0x00000001u,31,0x201Eu,0x80000000u,0x0A);\n"
          "check_clr_at(&a,&e_clr_byte_postincrement,1,0,0x40,0x40,0x41);check_clr_at(&a,&e_clr_word_postincrement,2,0,0x40,0x40,0x42);check_clr_at(&a,&e_clr_long_postincrement,4,0,0x40,0x40,0x44);check_clr_at(&a,&e_clr_byte_a7_postincrement,1,7,0x70,0x70,0x72);\n"
          "check_clr_at(&a,&e_clr_byte_predecrement,1,0,0x41,0x40,0x40);check_clr_at(&a,&e_clr_word_displacement,2,0,0x20,0x48,0x20);check_clr_at(&a,&e_clr_byte_absolute_long,1,0,0,0x54,0);\n"
          "puts(\"Native arithmetic/control steps, CLR bus order, physical contexts and explicit rejection pass\");return 0;}\n",out);
    CHECK(!ferror(out));CHECK(!fclose(out));return 0;
}
