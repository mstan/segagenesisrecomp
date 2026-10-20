/* Compile real 68000 instructions through the shared emitter into native C
 * operating on independent CPU/memory contexts. No owned program is used. */
#include "code_generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x); exit(1); } } while (0)
typedef struct { const char *name; uint16_t words[4]; unsigned count; int handled; } Case;
static const Case cases[] = {
    {"add_word", {0x0640,0x0001},2,1},       /* ADDI.W #1,D0 */
    {"add_memory", {0x5258},1,1},           /* ADDQ.W #1,(A0)+ */
    {"indexed_word", {0x3430,0x1800},2,1},  /* MOVE.W (A0,D1.L),D2 */
    {"store_long", {0x2081},1,1},           /* MOVE.L D1,(A0) */
    {"adjust_stack", {0x588F},1,1},         /* ADDQ.L #4,A7 */
    {"moveq", {0x70FF},1,1},               /* MOVEQ #-1,D0 */
    {"rts", {0x4E75},1,0},
    {"stop", {0x4E72,0x2700},2,0},
    {"write_sr", {0x46FC,0x2700},2,0},
    {"divide", {0x80FC,0x0000},2,0},
};

int main(int argc, char **argv)
{
    CHECK(argc==2);FILE *out=fopen(argv[1],"wb");CHECK(out);
    fputs("#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include \"genesis_runtime.h\"\n"
          "typedef struct { M68KState cpu; uint8_t memory[512]; } Context;\n"
          "#define CHECK(x) do { if(!(x)) { fprintf(stderr,\"line %u: %s\\n\",(unsigned)__LINE__,#x); exit(1); } } while(0)\n"
          "static uint8_t read8(Context*c,uint32_t a){CHECK(a<512);return c->memory[a];}\n"
          "static uint16_t read16(Context*c,uint32_t a){return (uint16_t)(((unsigned)read8(c,a)<<8)|read8(c,a+1));}\n"
          "static uint32_t read32(Context*c,uint32_t a){return ((uint32_t)read16(c,a)<<16)|read16(c,a+2);}\n"
          "static void write8(Context*c,uint32_t a,uint8_t v){CHECK(a<512);c->memory[a]=v;}\n"
          "static void write16(Context*c,uint32_t a,uint16_t v){write8(c,a,(uint8_t)(v>>8));write8(c,a+1,(uint8_t)v);}\n"
          "static void write32(Context*c,uint32_t a,uint32_t v){write16(c,a,(uint16_t)(v>>16));write16(c,a+2,(uint16_t)v);}\n"
          "#define g_cpu c->cpu\n"
          "#define m68k_read8(a) read8(c,a)\n#define m68k_read16(a) read16(c,a)\n#define m68k_read32(a) read32(c,a)\n"
          "#define m68k_write8(a,v) write8(c,a,v)\n#define m68k_write16(a,v) write16(c,a,v)\n#define m68k_write32(a,v) write32(c,a,v)\n",out);
    for (unsigned i=0;i<sizeof cases/sizeof *cases;++i) {
        const Case *test=&cases[i];uint8_t image[16]={0};
        for(unsigned n=0;n<test->count;++n){image[2*n]=(uint8_t)(test->words[n]>>8);image[2*n+1]=(uint8_t)test->words[n];}
        GenesisRom rom={0};rom.rom_data=image;rom.rom_size=sizeof image;
        M68KInstr ins;CHECK(m68k_decode(&rom,0,&ins));
        if(test->handled)fprintf(out,"static void %s(Context*c){\n",test->name);
        long before=ftell(out);CHECK(before>=0);
        CHECK(codegen_emit_step_semantics(out,&rom,&ins)==(test->handled!=0));
        if(test->handled)fputs("}\n",out);
        else CHECK(ftell(out)==before);
    }
    fputs("int main(void){\nContext a={0},b={0};\n"
          "a.cpu.D[0]=0xFFFF7FFFu;a.cpu.SR=0x2000;add_word(&a);CHECK(a.cpu.D[0]==0xFFFF8000u);CHECK(a.cpu.SR==0x200Au);\n"
          "b.cpu.D[0]=2;b.cpu.SR=0x2000;add_word(&b);CHECK(b.cpu.D[0]==3);CHECK(b.cpu.SR==0x2000);CHECK(a.cpu.D[0]==0xFFFF8000u);\n"
          "a.cpu.A[0]=0x40;write16(&a,0x40,0x7FFF);add_memory(&a);CHECK(a.cpu.A[0]==0x42);CHECK(read16(&a,0x40)==0x8000);\n"
          "a.cpu.A[0]=0x20;a.cpu.D[1]=0x100;a.cpu.D[2]=0xABCD0000u;write16(&a,0x120,0xBEEF);indexed_word(&a);CHECK(a.cpu.D[2]==0xABCDBEEFu);\n"
          "a.cpu.A[0]=0x50;a.cpu.D[1]=0x12345678;store_long(&a);CHECK(read32(&a,0x50)==0x12345678);CHECK(read32(&b,0x50)==0);\n"
          "a.cpu.A[7]=0x100;a.cpu.SR=0x2701;adjust_stack(&a);CHECK(a.cpu.A[7]==0x104);CHECK(a.cpu.SR==0x2701);\n"
          "moveq(&a);CHECK(a.cpu.D[0]==0xFFFFFFFFu);CHECK((a.cpu.SR&15)==8);\n"
          "puts(\"Native step semantics, independent contexts and explicit control rejection pass\");return 0;}\n",out);
    CHECK(!ferror(out));CHECK(!fclose(out));return 0;
}
