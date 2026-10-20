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
};

int main(int argc, char **argv)
{
    CHECK(argc==2);FILE *out=fopen(argv[1],"wb");CHECK(out);
    fputs("#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include \"m68k_step.h\"\n"
          "typedef M68kStepContext Context;\ntypedef struct {uint8_t bytes[512];} Memory;\n"
          "#define CHECK(x) do { if(!(x)) { fprintf(stderr,\"line %u: %s\\n\",(unsigned)__LINE__,#x); exit(1); } } while(0)\n"
          "static M68kStepBusResult read8(void*u,uint32_t a,uint8_t*v){Memory*m=u;if(a>=512)return M68K_BUS_FAULT;*v=m->bytes[a];return M68K_BUS_OK;}\n"
          "static M68kStepBusResult read16(void*u,uint32_t a,uint16_t*v){Memory*m=u;if(a+1>=512)return M68K_BUS_FAULT;*v=(uint16_t)(((unsigned)m->bytes[a]<<8)|m->bytes[a+1]);return M68K_BUS_OK;}\n"
          "static M68kStepBusResult write8(void*u,uint32_t a,uint8_t v){Memory*m=u;if(a>=512)return M68K_BUS_FAULT;m->bytes[a]=v;return M68K_BUS_OK;}\n"
          "static M68kStepBusResult write16(void*u,uint32_t a,uint16_t v){Memory*m=u;if(a+1>=512)return M68K_BUS_FAULT;m->bytes[a]=(uint8_t)(v>>8);m->bytes[a+1]=(uint8_t)v;return M68K_BUS_OK;}\n"
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
    fputs("static void run(Context*c,const M68kStepEntry*e){for(unsigned n=0;n<e->word_count;++n)m68k_step_write16(c,e->pc+n*2,e->words[n]);c->state.regs.PC=e->pc;CHECK(m68k_step_execute(c,e)==M68K_STEP_OK);}\n",out);
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
          "puts(\"Native arithmetic/control steps, physical contexts and explicit rejection pass\");return 0;}\n",out);
    CHECK(!ferror(out));CHECK(!fclose(out));return 0;
}
